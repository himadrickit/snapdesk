/* ------------------------------------------------------------------ dispatcher */
/* apply one layer to *im (which may be reallocated/resized by geometry tools) */
static void apply_layer_raw(Layer *L, Img *im) {
	switch (L->tool) {
	case T_TUNE: apply_tune(L, im); break;
	case T_DETAILS: apply_details(L, im); break;
	case T_CURVES: apply_curves(L, im); break;
	case T_WB: apply_wb(L, im); break;
	case T_CROP: apply_crop(L, im); break;
	case T_ROTATE: apply_rotate(L, im); break;
	case T_PERSP: apply_persp(L, im); break;
	case T_SELECTIVE: apply_selective(L, im); break;
	case T_BRUSH: apply_brush(L, im); break;
	case T_HEAL: apply_heal(L, im); break;
	case T_PORTRAIT: apply_portrait(L, im); break;
	case T_VIGNETTE: apply_vignette(L, im); break;
	case T_LENS: apply_lens(L, im); break;
	case T_TEXT: apply_text(L, im); break;
	case T_FRAME: apply_frame(L, im); break;
	case T_BW: apply_bw(L, im); break;
	case T_DRAMA: apply_drama(L, im); break;
	case T_TONAL: apply_tonal(L, im); break;
	case T_HDR: apply_hdr(L, im); break;
	case T_GLOW: apply_glow(L, im); break;
	case T_VINTAGE: apply_vintage(L, im); break;
	case T_RETRO: apply_retro(L, im); break;
	case T_GRAIN: apply_grain(L, im); break;
	case T_NOIR: apply_noir(L, im); break;
	case T_GRUNGE: apply_grunge(L, im); break;
	case T_BLOOM: apply_bloom(L, im); break;
	case T_DEHAZE: apply_dehaze(L, im); break;
	case T_MIXER: apply_mixer(L, im); break;
	case T_SPLIT: apply_split(L, im); break;
	case T_DENOISE: apply_denoise(L, im); break;
	case T_CHROMA: apply_chroma(L, im); break;
	case T_STREAK: apply_streak(L, im); break;
	case T_GRAD: apply_grad(L, im); break;
	case T_HALATION: apply_halation(L, im); break;
	case T_DOUBLE: apply_double(L, im); break;
	case T_EXPAND: apply_expand(L, im); break;
	}
}
/* per-layer mask: strokes paint where the effect shows (red overlay in the UI) */
static inline int maskable(int tool) { return tool != T_CROP && tool != T_ROTATE && tool != T_PERSP && tool != T_FRAME && tool != T_EXPAND && tool != T_TEXT; }
static const float *layer_mask(Layer *L, int w, int h) {
	BrushRT *rt = L->mrt;
	if (rt && (rt->w != w || rt->h != h)) {
		brt_free(rt);
		rt = L->mrt = NULL;
	}
	if (!rt) {
		rt = L->mrt = (BrushRT *)xcalloc(1, sizeof(BrushRT));
		rt->w = w;
		rt->h = h;
	}
	brt_raster(rt, L->mstr, L->nmstr, L->mpts);
	return rt->map[0];
}
typedef struct { Img *im; const Img *before; const float *map; int inv, full; } MaskCtx;
static void mask_job(void *v, int y0, int y1) {
	MaskCtx *c = (MaskCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *q = c->before->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, q += 3) {
			float a = c->full ? 1.f : (c->map ? CL01(c->map[(size_t)y * w + x]) : 0.f);
			if (c->inv) a = 1.f - a;
			for (int k = 0; k < 3; k++) p[k] = LERP(q[k], p[k], a);
		}
	}
}
static void apply_layer(Layer *L, Img *im) {
	if (!L->mask_on || !maskable(L->tool)) {
		apply_layer_raw(L, im);
		return;
	}
	Img before = img_clone(im);
	apply_layer_raw(L, im);
	if (im->w == before.w && im->h == before.h) {
		MaskCtx c = {im, &before, layer_mask(L, im->w, im->h), L->mask_inv, 0};
		parallel(mask_job, &c, im->h);
	}
	img_free(&before);
}
/* output size of a layer for a given input size (used for the 100% zoom / export size) */
static void layer_outdims(const Layer *L, int *w, int *h) {
	switch (L->tool) {
	case T_CROP: {
		int x0 = (int)roundf(L->cr[0] * *w), y0 = (int)roundf(L->cr[1] * *h), x1 = (int)roundf(L->cr[2] * *w), y1 = (int)roundf(L->cr[3] * *h);
		x0 = CLAMP(x0, 0, *w - 2);
		y0 = CLAMP(y0, 0, *h - 2);
		x1 = CLAMP(x1, x0 + 2, *w);
		y1 = CLAMP(y1, y0 + 2, *h);
		*w = x1 - x0;
		*h = y1 - y0;
	} break;
	case T_ROTATE:
		if (L->rot & 1) {
			int t = *w;
			*w = *h;
			*h = t;
		}
		break;
	case T_FRAME: {
		int bx, bt, bb;
		frame_geom(L, *w, *h, &bx, &bt, &bb);
		*w += 2 * bx;
		*h += bt + bb;
	} break;
	case T_EXPAND: {
		int l, t, r, b;
		expand_geom(L, *w, *h, &l, &t, &r, &b);
		*w += l + r;
		*h += t + b;
	} break;
	}
}
