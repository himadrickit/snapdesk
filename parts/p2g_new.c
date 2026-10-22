/* ------------------------------------------------------------------ Halation */
static void apply_halation(const Layer *L, Img *im) {
	float amt = L->p[HA_AMT] / 100.f, thr = L->p[HA_THR] / 100.f, rad = L->p[HA_RAD] / 100.f;
	if (amt <= 0.f) return;
	static const float TINT[4][3] = {{1.f, 0.10f, 0.03f}, {1.f, 0.32f, 0.04f}, {1.f, 0.28f, 0.42f}, {0.95f, 0.08f, 0.75f}};
	const float *tn = TINT[CLAMP((int)L->p[HA_TINT], 0, 3)];
	Img sm = small_copy(im, 1000);
	extract_highlights(&sm, thr);
	/* keep only the bright energy as a single channel, then spread it widely */
	size_t n = (size_t)sm.w * sm.h;
	float *l = (float *)xmalloc(n * 4);
	for (size_t i = 0; i < n; i++) l[i] = lum3(sm.p[i * 3], sm.p[i * 3 + 1], sm.p[i * 3 + 2]);
	float m = (float)MIN(sm.w, sm.h), s = MAX(2.f, m * (0.012f + 0.07f * rad));
	blur_f(l, sm.w, sm.h, 1, s);
	float *l2 = (float *)xmalloc(n * 4);
	memcpy(l2, l, n * 4);
	blur_f(l2, sm.w, sm.h, 1, s * 2.4f);
	Img acc = img_new(sm.w, sm.h);
	for (size_t i = 0; i < n; i++) {
		float v = 0.6f * l[i] + 0.7f * l2[i];
		for (int k = 0; k < 3; k++) acc.p[i * 3 + k] = v;
	}
	add_light(im, &acc, amt * 0.5f, tn);
	/* halation also warms and slightly lifts the core highlights */
	free(l);
	free(l2);
	img_free(&sm);
	img_free(&acc);
}

/* ------------------------------------------------------------------ Double Exposure */
static char dx_path[1024];
static Img dx_img;
static int dx_ok = 0;
static int dx_load(const char *path) {
	if (dx_ok && !strcmp(dx_path, path)) return 1;
	int w, h, n;
	uint8_t *px = stbi_load(path, &w, &h, &n, 3);
	if (!px) return 0;
	if (dx_ok) img_free(&dx_img);
	int mx = MAX(w, h);
	if (mx > 2400) {
		float s = 2400.f / mx;
		dx_img = resize_u8(px, w, h, MAX(2, (int)(w * s)), MAX(2, (int)(h * s)));
	} else
		dx_img = resize_u8(px, w, h, w, h);
	free(px);
	snprintf(dx_path, sizeof dx_path, "%s", path);
	dx_ok = 1;
	return 1;
}
static float blend_ch(int mode, float a, float b) {
	switch (mode) {
	case 1: return a + b;
	case 2: return 1.f - (1.f - a) * (1.f - b);
	case 3: return a * b;
	case 4: return a < 0.5f ? 2.f * a * b : 1.f - 2.f * (1.f - a) * (1.f - b);
	case 5: return b < 0.5f ? a - (1.f - 2.f * b) * a * (1.f - a) : a + (2.f * b - 1.f) * (sqrtf(a) - a);
	case 6: return MAX(a, b);
	case 7: return MIN(a, b);
	case 8: return fabsf(a - b);
	}
	return b;
}
typedef struct { Img *im; float cx, cy, c, s, inv, opac, bri; int mode; } DxCtx;
static void dx_job(void *v, int y0, int y1) {
	DxCtx *c = (DxCtx *)v;
	int w = c->im->w, h = c->im->h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float px = x + 0.5f - w * 0.5f, py = y + 0.5f - h * 0.5f;
			float rx = (px * c->c + py * c->s) * c->inv + c->cx, ry = (-px * c->s + py * c->c) * c->inv + c->cy;
			float W = (float)dx_img.w, H = (float)dx_img.h;
			float e = MIN(MIN(rx, W - rx), MIN(ry, H - ry));
			float cov = CL01(e / c->inv + 0.5f);
			if (cov <= 0.f) continue;
			float b[3];
			bilin(&dx_img, CLAMP(rx, 0.f, W - 1.f), CLAMP(ry, 0.f, H - 1.f), b);
			float a = c->opac * cov;
			for (int k = 0; k < 3; k++) {
				float t = CL01(blend_ch(c->mode, p[k], CL01(b[k] + c->bri)));
				p[k] = LERP(p[k], t, a);
			}
		}
	}
}
static void apply_double(const Layer *L, Img *im) {
	if (!L->path[0] || !dx_load(L->path)) return;
	float W = (float)dx_img.w, H = (float)dx_img.h;
	float cover = MAX(im->w / W, im->h / H);
	float sc = cover * L->p[DX_SCALE] / 100.f, a = L->p[DX_ROT] * PI_F / 180.f;
	DxCtx c = {im, W * 0.5f, H * 0.5f, cosf(a), sinf(a), 1.f / sc, L->p[DX_OPAC] / 100.f, L->p[DX_BRI] / 200.f, (int)L->p[DX_BLEND]};
	if (c.opac <= 0.f) return;
	parallel(dx_job, &c, im->h);
}

/* ------------------------------------------------------------------ Expand */
static void expand_geom(const Layer *L, int w, int h, int *l, int *t, int *r, int *b) {
	*l = (int)(L->p[EX_L] / 100.f * w);
	*t = (int)(L->p[EX_T] / 100.f * h);
	*r = (int)(L->p[EX_R] / 100.f * w);
	*b = (int)(L->p[EX_B] / 100.f * h);
	static const float AS[6] = {0, 1.f, 0.8f, 1.5f, 16.f / 9.f, 9.f / 16.f};
	int ai = CLAMP((int)L->p[EX_ASP], 0, 5);
	if (ai > 0) { /* grow the short side evenly until the aspect fits */
		float ar = AS[ai];
		int tw = w, th = h;
		if ((float)w / h < ar) tw = (int)(h * ar + 0.5f);
		else th = (int)(w / ar + 0.5f);
		*l = (tw - w) / 2;
		*r = tw - w - *l;
		*t = (th - h) / 2;
		*b = th - h - *t;
	}
}
static int refl(int i, int n) {
	if (n <= 1) return 0;
	int p = 2 * (n - 1);
	i %= p;
	if (i < 0) i += p;
	return i < n ? i : p - i;
}
static void apply_expand(const Layer *L, Img *im) {
	int w = im->w, h = im->h, l, t, r, b;
	expand_geom(L, w, h, &l, &t, &r, &b);
	if (l + t + r + b == 0) return;
	int W = w + l + r, H = h + t + b, fill = (int)L->p[EX_FILL];
	Img o = img_new(W, H);
	static const float SOL[3][3] = {{1, 1, 1}, {0, 0, 0}, {0.5f, 0.5f, 0.5f}};
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float *d = o.p + ((size_t)y * W + x) * 3;
			int sx = x - l, sy = y - t;
			if (fill >= 3) {
				if (sx >= 0 && sy >= 0 && sx < w && sy < h) memcpy(d, im->p + ((size_t)sy * w + sx) * 3, 12);
				else memcpy(d, SOL[fill - 3], 12);
			} else
				memcpy(d, im->p + ((size_t)refl(sy, h) * w + refl(sx, w)) * 3, 12);
		}
	if (fill <= 2 && fill != 1) {
		/* Smart: sharp mirror near the photo, melting into blur away from it. Blur: all blur. */
		Img sm = small_copy(&o, 700);
		float s = MAX(2.f, MIN(sm.w, sm.h) * 0.09f);
		blur_f(sm.p, sm.w, sm.h, 3, s);
		float m = (float)MIN(W, H), fd = m * 0.2f;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int sx = x - l, sy = y - t;
				float dx = sx < 0 ? -sx : (sx >= w ? sx - w + 1 : 0), dy = sy < 0 ? -sy : (sy >= h ? sy - h + 1 : 0);
				float dd = sqrtf(dx * dx + dy * dy);
				if (dd <= 0.f) continue;
				float k = fill == 2 ? 1.f : smoothf(0.f, fd, dd);
				float bl[3];
				bilin(&sm, (x + 0.5f) * sm.w / W, (y + 0.5f) * sm.h / H, bl);
				float *d = o.p + ((size_t)y * W + x) * 3;
				float nz = (hashf(x, y, 77u) - 0.5f) * 0.02f;
				for (int c = 0; c < 3; c++) d[c] = CL01(LERP(d[c], bl[c], k) + nz);
			}
		img_free(&sm);
	} else if (fill == 1) {
		/* Mirror: leave the reflection untouched */
	}
	img_free(im);
	*im = o;
}
