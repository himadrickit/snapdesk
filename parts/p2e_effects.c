
/* ================================================================== EFFECTS: Bloom, Dehaze, Color Mixer, Split Toning,
 *                                                       Denoise, Chromatic Aberration, Light Streaks, Gradient Filter */

/* ---- colour space helpers ---- */
static void hue_rgb(float h, float *r, float *g, float *b) {
	h = fmodf(h, 360.f);
	if (h < 0) h += 360.f;
	float x = h / 60.f, f = x - floorf(x);
	switch ((int)x % 6) {
	case 0: *r = 1; *g = f; *b = 0; break;
	case 1: *r = 1 - f; *g = 1; *b = 0; break;
	case 2: *r = 0; *g = 1; *b = f; break;
	case 3: *r = 0; *g = 1 - f; *b = 1; break;
	case 4: *r = f; *g = 0; *b = 1; break;
	default: *r = 1; *g = 0; *b = 1 - f; break;
	}
}
static void rgb2hsv(float r, float g, float b, float *h, float *s, float *v) {
	float mx_ = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b)), d = mx_ - mn;
	*v = mx_;
	*s = mx_ > 1e-6f ? d / mx_ : 0.f;
	if (d < 1e-6f) {
		*h = 0;
		return;
	}
	float hh;
	if (mx_ == r) hh = (g - b) / d + (g < b ? 6.f : 0.f);
	else if (mx_ == g) hh = (b - r) / d + 2.f;
	else hh = (r - g) / d + 4.f;
	*h = hh * 60.f;
}
static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b) {
	float cr, cg, cb;
	hue_rgb(h, &cr, &cg, &cb);
	*r = v * (1.f - s + s * cr);
	*g = v * (1.f - s + s * cg);
	*b = v * (1.f - s + s * cb);
}

/* downscaled copy so that heavy blurs cost the same at preview and export size */
static Img small_copy(const Img *im, int maxdim) {
	int m = MAX(im->w, im->h);
	if (m <= maxdim) return img_clone(im);
	float s = (float)maxdim / m;
	return resize_f(im, MAX(2, (int)(im->w * s + 0.5f)), MAX(2, (int)(im->h * s + 0.5f)));
}
/* soft-knee highlight extraction, result is linear light * mask */
typedef struct { Img *im; float t0, t1; } HlCtx;
static void hl_job(void *v, int y0, int y1) {
	HlCtx *c = (HlCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float m = 0.5f * MAX(p[0], MAX(p[1], p[2])) + 0.5f * lum3(p[0], p[1], p[2]);
			float k = smoothf(c->t0, c->t1, m);
			p[0] = to_lin(p[0]) * k;
			p[1] = to_lin(p[1]) * k;
			p[2] = to_lin(p[2]) * k;
		}
	}
}
static void extract_highlights(Img *im, float thr) {
	float t0 = thr * 0.92f, t1 = MIN(1.05f, t0 + 0.30f);
	HlCtx c = {im, t0, t1};
	parallel(hl_job, &c, im->h);
}
typedef struct { Img *im; const Img *add; float amt, tint[3]; } AddCtx;
static void add_light_job(void *v, int y0, int y1) {
	AddCtx *c = (AddCtx *)v;
	int w = c->im->w, h = c->im->h;
	float sx = (float)c->add->w / w, sy = (float)c->add->h / h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float b[3];
			bilin(c->add, (x + 0.5f) * sx, (y + 0.5f) * sy, b);
			for (int k = 0; k < 3; k++) p[k] = CL01(to_gam(to_lin(p[k]) + c->amt * b[k] * c->tint[k]));
		}
	}
}
static void add_light(Img *im, const Img *light, float amt, const float *tint) {
	AddCtx c = {im, light, amt, {tint[0], tint[1], tint[2]}};
	parallel(add_light_job, &c, im->h);
}

/* ---- Bloom ---- */
static void apply_bloom(const Layer *L, Img *im) {
	float amt = L->p[BL_AMT] / 100.f, thr = L->p[BL_THR] / 100.f, rad = L->p[BL_RAD] / 100.f, wm = L->p[BL_WARM] / 100.f;
	if (amt <= 0.f) return;
	Img sm = small_copy(im, 1100);
	extract_highlights(&sm, thr);
	float m = (float)MIN(sm.w, sm.h);
	float s1 = MAX(1.2f, m * (0.004f + 0.03f * rad));
	static const float SC[3] = {1.f, 2.6f, 6.f}, WT[3] = {0.45f, 0.35f, 0.45f};
	size_t n = (size_t)sm.w * sm.h * 3;
	Img acc = img_new(sm.w, sm.h), t = img_new(sm.w, sm.h);
	memset(acc.p, 0, n * sizeof(float));
	for (int i = 0; i < 3; i++) {
		memcpy(t.p, sm.p, n * sizeof(float));
		blur_f(t.p, t.w, t.h, 3, s1 * SC[i]);
		for (size_t k = 0; k < n; k++) acc.p[k] += WT[i] * t.p[k];
	}
	float tint[3] = {1.f + 0.3f * wm, 1.f, 1.f - 0.3f * wm};
	add_light(im, &acc, amt * 1.0f, tint);
	img_free(&sm);
	img_free(&acc);
	img_free(&t);
}

/* ---- Light streaks (anamorphic) ---- */
typedef struct { const Img *src; Img *dst; float dx, dy, decay; int N; } StrCtx;
static void streak_job(void *v, int y0, int y1) {
	StrCtx *c = (StrCtx *)v;
	int w = c->dst->w;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, d += 3) {
			float r = 0, g = 0, b = 0, ws = 0;
			for (int i = -c->N; i <= c->N; i++) {
				float wt = expf(-fabsf((float)i) * c->decay);
				float s[3];
				bilin(c->src, x + 0.5f + i * c->dx, y + 0.5f + i * c->dy, s);
				r += wt * s[0];
				g += wt * s[1];
				b += wt * s[2];
				ws += wt;
			}
			d[0] = r / ws;
			d[1] = g / ws;
			d[2] = b / ws;
		}
	}
}
static void apply_streak(const Layer *L, Img *im) {
	static const float TINT[4][3] = {{1, 1, 1}, {1.0f, 0.72f, 0.45f}, {0.45f, 0.78f, 1.0f}, {1.0f, 0.45f, 0.9f}};
	float amt = L->p[ST_AMT] / 100.f, thr = L->p[ST_THR] / 100.f, len = L->p[ST_LEN] / 100.f, ang = L->p[ST_ANG] * PI_F / 180.f;
	if (amt <= 0.f || len <= 0.f) return;
	Img sm = small_copy(im, 560);
	extract_highlights(&sm, thr);
	Img out = img_new(sm.w, sm.h);
	int N = 26;
	float half = len * 0.5f * (float)MIN(sm.w, sm.h) * 1.4f;
	StrCtx c = {&sm, &out, cosf(ang) * half / N, sinf(ang) * half / N, 2.6f / N, N};
	parallel(streak_job, &c, out.h);
	/* the streak averages energy away, so scale up (length compensates) */
	size_t n = (size_t)out.w * out.h * 3;
	float gain = 1.1f + 1.6f * len;
	for (size_t i = 0; i < n; i++) out.p[i] *= gain;
	add_light(im, &out, amt * 1.1f, TINT[CLAMP((int)L->p[ST_TINT], 0, 3)]);
	img_free(&sm);
	img_free(&out);
}

/* ---- Dehaze ---- */
typedef struct { Img *im; const float *V; float amt, A, sat; } DhCtx;
static void dehaze_job(void *v, int y0, int y1) {
	DhCtx *c = (DhCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *V = c->V + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float ratio = CL01(V[x] / c->A);
			if (c->amt >= 0.f) {
				float t = MAX(1.f - c->amt * 0.92f * ratio, 0.18f);
				for (int k = 0; k < 3; k++) p[k] = CL01((p[k] - c->A) / t + c->A);
			} else {
				float k2 = -c->amt * 0.55f * (1.f - 0.4f * ratio);
				for (int k = 0; k < 3; k++) p[k] = LERP(p[k], c->A, k2);
			}
		}
	}
}
static void apply_dehaze(const Layer *L, Img *im) {
	float amt = L->p[DH_AMT] / 100.f, sat = L->p[DH_SAT] / 100.f;
	if (amt == 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	if (amt != 0.f) {
		float *mn = (float *)xmalloc(n * sizeof(float)), *V = (float *)xmalloc(n * sizeof(float));
		for (size_t i = 0; i < n; i++) mn[i] = MIN(im->p[i * 3], MIN(im->p[i * 3 + 1], im->p[i * 3 + 2]));
		guided_self(mn, V, w, h, MAX(3, (int)(MIN(w, h) * 0.035f)), 0.01f);
		/* airlight = high percentile of the smoothed dark channel */
		int bins[512] = {0};
		for (size_t i = 0; i < n; i += 3) bins[(int)(CL01(V[i]) * 511.f)]++;
		size_t tot = (n + 2) / 3, acc = 0;
		float A = 0.6f;
		for (int b = 511; b >= 0; b--) {
			acc += bins[b];
			if (acc >= tot / 500 + 1) {
				A = (b + 1) / 512.f;
				break;
			}
		}
		A = CLAMP(A, 0.25f, 0.98f);
		DhCtx c = {im, V, amt, A, sat};
		parallel(dehaze_job, &c, h);
		free(mn);
		free(V);
	}
	/* dehazing flattens colour a little, so the saturation slider is applied on top */
	img_saturate(im, 1.f + sat + (amt > 0.f ? 0.15f * amt : 0.f));
	img_clamp(im);
}

/* ---- Color mixer (8 hue bands: hue / saturation / luminance) ---- */
static const float MIX_CENTER[8] = {0, 30, 60, 120, 180, 225, 275, 320};
static const char *MIX_NAMES = "Red|Orange|Yellow|Green|Aqua|Blue|Purple|Magenta";
typedef struct { Img *im; float dH[360], dS[360], dL[360]; } MixCtx;
static void mix_job(void *v, int y0, int y1) {
	MixCtx *c = (MixCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float h, s, vv;
			rgb2hsv(CL01(p[0]), CL01(p[1]), CL01(p[2]), &h, &s, &vv);
			float sw = smoothf(0.03f, 0.22f, s) * smoothf(0.02f, 0.10f, vv);
			if (sw <= 0.f) continue;
			int hi = (int)h % 360;
			if (hi < 0) hi += 360;
			float nh = h + c->dH[hi] * sw, ns = CL01(s * (1.f + c->dS[hi] * sw));
			float r, g, b;
			hsv2rgb(nh, ns, vv, &r, &g, &b);
			float k = 1.f + c->dL[hi] * sw;
			p[0] = CL01(r * k);
			p[1] = CL01(g * k);
			p[2] = CL01(b * k);
		}
	}
}
static void apply_mixer(const Layer *L, Img *im) {
	int any = 0;
	for (int i = 0; i < 8; i++)
		for (int k = 0; k < 3; k++)
			if (L->mix[i][k] != 0.f) any = 1;
	if (!any) return;
	MixCtx *c = (MixCtx *)xmalloc(sizeof(MixCtx));
	c->im = im;
	for (int hdeg = 0; hdeg < 360; hdeg++) {
		float w[8], sum = 0;
		for (int i = 0; i < 8; i++) {
			float d = fabsf((float)hdeg - MIX_CENTER[i]);
			d = MIN(d, 360.f - d);
			float W = 52.f;
			float cs = d >= W ? 0.f : cosf(d / W * PI_F * 0.5f);
			w[i] = cs * cs;
			sum += w[i];
		}
		float dh = 0, ds = 0, dl = 0;
		if (sum > 1e-6f)
			for (int i = 0; i < 8; i++) {
				float wn = w[i] / sum;
				dh += wn * L->mix[i][0] / 100.f * 35.f;
				ds += wn * L->mix[i][1] / 100.f;
				dl += wn * L->mix[i][2] / 100.f * 0.6f;
			}
		c->dH[hdeg] = dh;
		c->dS[hdeg] = ds;
		c->dL[hdeg] = dl;
	}
	parallel(mix_job, c, im->h);
	free(c);
}

/* ---- Split toning ---- */
typedef struct { Img *im; float cs[3], cm[3], ch[3], ss, sm, sh, bal, blend; } SplitCtx;
static void split_job(void *v, int y0, int y1) {
	SplitCtx *c = (SplitCtx *)v;
	int w = c->im->w;
	float Ls = lum3(c->cs[0], c->cs[1], c->cs[2]), Lh = lum3(c->ch[0], c->ch[1], c->ch[2]), Lm = lum3(c->cm[0], c->cm[1], c->cm[2]);
	float mid = 0.5f + 0.3f * c->bal, ww = 0.1f + 0.44f * c->blend;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float L = lum3(p[0], p[1], p[2]);
			float wh = smoothf(mid - ww, mid + ww, L), wsh = 1.f - wh;
			float t = (L - mid) / (0.18f + 0.3f * c->blend), wm = expf(-t * t);
			wh *= 1.f - 0.5f * wm;
			wsh *= 1.f - 0.5f * wm;
			for (int k = 0; k < 3; k++) p[k] = CL01(p[k] + (c->cs[k] - Ls) * wsh * c->ss * 0.55f + (c->cm[k] - Lm) * wm * c->sm * 0.55f + (c->ch[k] - Lh) * wh * c->sh * 0.55f);
		}
	}
}
static void apply_split(const Layer *L, Img *im) {
	SplitCtx c;
	c.im = im;
	hue_rgb(L->p[SP_SHUE], &c.cs[0], &c.cs[1], &c.cs[2]);
	hue_rgb(L->p[SP_MHUE], &c.cm[0], &c.cm[1], &c.cm[2]);
	hue_rgb(L->p[SP_HHUE], &c.ch[0], &c.ch[1], &c.ch[2]);
	c.sm = L->p[SP_MSTR] / 100.f;
	c.blend = L->p[SP_BLEND] / 100.f;
	c.ss = L->p[SP_SSTR] / 100.f;
	c.sh = L->p[SP_HSTR] / 100.f;
	c.bal = L->p[SP_BAL] / 100.f;
	if (c.ss <= 0.f && c.sh <= 0.f && c.sm <= 0.f) return;
	parallel(split_job, &c, im->h);
}

/* ---- Denoise (edge-aware luminance + guided chroma smoothing) ---- */
static void guided_cross(const float *G, const float *I, float *out, int w, int h, int r, float eps) {
	size_t n = (size_t)w * h;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *mG = (float *)xmalloc(n * 4), *mI = (float *)xmalloc(n * 4), *a = (float *)xmalloc(n * 4), *b = (float *)xmalloc(n * 4), *tmp = (float *)xmalloc(n * 4);
	box_blur(G, mG, w, h, 1, r, tmp);
	box_blur(I, mI, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) a[i] = G[i] * I[i];
	box_blur(a, a, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) b[i] = G[i] * G[i];
	box_blur(b, b, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) {
		float cov = a[i] - mG[i] * mI[i], var = b[i] - mG[i] * mG[i];
		if (var < 0) var = 0;
		float aa = cov / (var + eps);
		a[i] = aa;
		b[i] = mI[i] - aa * mG[i];
	}
	box_blur(a, a, w, h, 1, r, tmp);
	box_blur(b, b, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) out[i] = a[i] * G[i] + b[i];
	free(mG);
	free(mI);
	free(a);
	free(b);
	free(tmp);
}
static void apply_denoise(const Layer *L, Img *im) {
	float lum = L->p[DN_LUM] / 100.f, col = L->p[DN_COL] / 100.f, det = L->p[DN_DET] / 100.f;
	if (lum <= 0.f && col <= 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float m = (float)MIN(w, h);
	float *Y = (float *)xmalloc(n * 4), *cb = (float *)xmalloc(n * 4), *cr = (float *)xmalloc(n * 4), *t = (float *)xmalloc(n * 4);
	for (size_t i = 0; i < n; i++) {
		float r = im->p[i * 3], g = im->p[i * 3 + 1], b = im->p[i * 3 + 2];
		Y[i] = 0.299f * r + 0.587f * g + 0.114f * b;
		cb[i] = b - Y[i];
		cr[i] = r - Y[i];
	}
	if (col > 0.f) {
		int r = MAX(2, (int)(m * (0.004f + 0.012f * col)));
		float eps = 0.0008f + 0.01f * (1.f - col);
		guided_cross(Y, cb, t, w, h, r, eps);
		memcpy(cb, t, n * 4);
		guided_cross(Y, cr, t, w, h, r, eps);
		memcpy(cr, t, n * 4);
	}
	if (lum > 0.f) {
		int r = MAX(1, (int)(m * (0.0015f + 0.0035f * lum)));
		float eps = (0.0004f + 0.012f * lum * lum) * (1.f - det * 0.75f);
		guided_self(Y, t, w, h, r, eps);
		for (size_t i = 0; i < n; i++) Y[i] = LERP(Y[i], t[i], MIN(1.f, lum * 1.15f));
	}
	for (size_t i = 0; i < n; i++) {
		float r = Y[i] + cr[i], b = Y[i] + cb[i];
		float g = (Y[i] - 0.299f * r - 0.114f * b) / 0.587f;
		im->p[i * 3] = CL01(r);
		im->p[i * 3 + 1] = CL01(g);
		im->p[i * 3 + 2] = CL01(b);
	}
	free(Y);
	free(cb);
	free(cr);
	free(t);
}

/* ---- Chromatic aberration (red/blue fringes, positive or negative) ---- */
typedef struct { const Img *src; Img *dst; float k, bias; } CaCtx;
static void ca_job(void *v, int y0, int y1) {
	CaCtx *c = (CaCtx *)v;
	int w = c->dst->w, h = c->dst->h;
	float cx = w * 0.5f, cy = h * 0.5f, rmax2 = cx * cx + cy * cy;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst->p + (size_t)y * w * 3;
		const float *s = c->src->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, d += 3, s += 3) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float r2 = (dx * dx + dy * dy) / rmax2;
			float k = c->k * LERP(1.f, r2, c->bias);
			float a[3], b[3];
			bilin(c->src, cx + dx * (1.f + k), cy + dy * (1.f + k), a);
			bilin(c->src, cx + dx * (1.f - k), cy + dy * (1.f - k), b);
			d[0] = a[0];
			d[1] = s[1];
			d[2] = b[2];
		}
	}
}
static void apply_chroma(const Layer *L, Img *im) {
	float amt = L->p[CA_AMT] / 100.f;
	if (amt == 0.f) return;
	Img out = img_new(im->w, im->h);
	CaCtx c = {im, &out, amt * 0.012f, L->p[CA_EDGE] / 100.f};
	parallel(ca_job, &c, im->h);
	img_free(im);
	*im = out;
}

/* ---- Gradient filter ---- */
typedef struct { Img *im; float cx, cy, nx, ny, f, br, co, sa, wa; } GradCtx;
static void grad_job(void *v, int y0, int y1) {
	GradCtx *c = (GradCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float d = (x + 0.5f - c->cx) * c->nx + (y + 0.5f - c->cy) * c->ny;
			float m = 1.f - smoothf(-c->f, c->f, d);
			if (m < 0.002f) continue;
			float r = p[0], g = p[1], b = p[2];
			if (c->br != 0.f) {
				float k = exp2f(c->br * m * 1.3f);
				r *= k;
				g *= k;
				b *= k;
			}
			if (c->co != 0.f) {
				float k = 1.f + c->co * m * 0.9f;
				r = 0.5f + (r - 0.5f) * k;
				g = 0.5f + (g - 0.5f) * k;
				b = 0.5f + (b - 0.5f) * k;
			}
			if (c->sa != 0.f) {
				float Lm = lum3(r, g, b), k = 1.f + c->sa * m;
				r = Lm + (r - Lm) * k;
				g = Lm + (g - Lm) * k;
				b = Lm + (b - Lm) * k;
			}
			if (c->wa != 0.f) {
				r *= 1.f + 0.22f * c->wa * m;
				b *= 1.f - 0.22f * c->wa * m;
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_grad(const Layer *L, Img *im) {
	GradCtx c;
	c.im = im;
	c.cx = L->cx * im->w;
	c.cy = L->cy * im->h;
	float th = L->p[GD_ANG] * PI_F / 180.f;
	c.nx = sinf(th);
	c.ny = cosf(th);
	c.f = (0.004f + L->p[GD_FEA] / 100.f * 0.5f) * (float)MIN(im->w, im->h);
	c.br = L->p[GD_BRI] / 100.f;
	c.co = L->p[GD_CON] / 100.f;
	c.sa = L->p[GD_SAT] / 100.f;
	c.wa = L->p[GD_WARM] / 100.f;
	if (c.br == 0.f && c.co == 0.f && c.sa == 0.f && c.wa == 0.f) return;
	parallel(grad_job, &c, im->h);
}
