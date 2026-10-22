
/* ------------------------------------------------------------------ shared helpers */
static stbtt_fontinfo font_reg, font_med; /* initialised in fonts_init() */

typedef struct {
	Img *im;
	const float *a, *b;
	float k[16];
	const float *lut0, *lut1, *lut2;
} Job;

/* multiply rgb by (Ynew/Yold), keeping hue; gains limited */
static void ratio_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		const float *yo = j->a + (size_t)y * w, *yn = j->b + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float o = yo[x], n = yn[x];
			if (o > 0.004f) {
				float k = n / o;
				k = CLAMP(k, 0.f, 4.f);
				p[0] *= k;
				p[1] *= k;
				p[2] *= k;
			} else {
				float d = n - o;
				p[0] += d;
				p[1] += d;
				p[2] += d;
			}
		}
	}
}
static void apply_ratio(Img *im, const float *Yold, const float *Ynew) {
	Job j = {im, Yold, Ynew, {0}, NULL, NULL, NULL};
	parallel(ratio_job, &j, im->h);
}
static void clamp_job(void *v, int y0, int y1) {
	Img *im = (Img *)v;
	size_t n = (size_t)im->w * 3;
	for (int y = y0; y < y1; y++) {
		float *p = im->p + (size_t)y * n;
		for (size_t i = 0; i < n; i++) p[i] = CL01(p[i]);
	}
}
static void img_clamp(Img *im) { parallel(clamp_job, im, im->h); }

/* saturation scale about luminance; k = 1 neutral */
static void sat_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	float s = j->k[0];
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float L = lum3(p[0], p[1], p[2]);
			p[0] = L + (p[0] - L) * s;
			p[1] = L + (p[1] - L) * s;
			p[2] = L + (p[2] - L) * s;
		}
	}
}
static void img_saturate(Img *im, float s) {
	if (fabsf(s - 1.f) < 1e-4f) return;
	Job j = {im, NULL, NULL, {s}, NULL, NULL, NULL};
	parallel(sat_job, &j, im->h);
}

/* grain: value noise on a lattice tied to image size so preview == export */
static void grain_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	float amt = j->k[0], cell = j->k[1];
	uint32_t seed = (uint32_t)j->k[2];
	float inv = 1.f / cell;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float n = vnoise(x * inv, y * inv, seed) + 0.5f * vnoise(x * inv * 2.1f + 17.f, y * inv * 2.1f + 5.f, seed + 7) - 0.75f;
			float L = lum3(p[0], p[1], p[2]);
			float wgt = 1.f - 0.8f * (2.f * L - 1.f) * (2.f * L - 1.f);
			float d = n * amt * wgt;
			p[0] += d;
			p[1] += d;
			p[2] += d;
		}
	}
}
static void add_grain(Img *im, float amount, uint32_t seed) {
	if (amount <= 0) return;
	float cell = MAX(1.f, MIN(im->w, im->h) / 1100.f);
	Job j = {im, NULL, NULL, {amount * 0.55f, cell, (float)seed}, NULL, NULL, NULL};
	parallel(grain_job, &j, im->h);
}

/* radial darkening/brightening: strength -1..1, size 0..1 (inner radius), used by filters */
static void vig_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w, h = j->im->h;
	float st = j->k[0], size = j->k[1];
	float cx = w * 0.5f, cy = h * 0.5f, dmax = sqrtf(cx * cx + cy * cy);
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float dx = x - cx, dy = y - cy;
			float d = sqrtf(dx * dx + dy * dy) / dmax;
			float t = smoothf(size, 1.05f, d);
			float g = 1.f + st * t * (st < 0 ? 1.f : 0.6f);
			g = MAX(g, 0.f);
			p[0] *= g;
			p[1] *= g;
			p[2] *= g;
		}
	}
}
static void add_vignette(Img *im, float strength, float size) {
	if (fabsf(strength) < 1e-3f) return;
	Job j = {im, NULL, NULL, {strength, size}, NULL, NULL, NULL};
	parallel(vig_job, &j, im->h);
}

/* the S curve used all over: contrast -1..1 about mid-grey */
static inline float scurve(float v, float c) {
	if (c > 0) {
		float s = v * v * (3.f - 2.f * v);
		s = s * s * (3.f - 2.f * s);
		return v + c * 0.9f * (s - v);
	}
	return 0.5f + (v - 0.5f) * (1.f + c * 0.75f);
}

/* ------------------------------------------------------------------ Tune Image */
typedef struct {
	Img *im;
	const float *tone, *hs, *base;
	float amb, sat, warm;
} TuneCtx;
static void tune_a_job(void *v, int y0, int y1) {
	TuneCtx *c = (TuneCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float r = lut_at(c->tone, p[0]), g = lut_at(c->tone, p[1]), b = lut_at(c->tone, p[2]);
			float k = lut_at(c->hs, lum3(r, g, b));
			p[0] = r * k;
			p[1] = g * k;
			p[2] = b * k;
		}
	}
}
static void tune_b_job(void *v, int y0, int y1) {
	TuneCtx *c = (TuneCtx *)v;
	int w = c->im->w;
	float amb = c->amb, sat = c->sat, warm = c->warm;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *bs = c->base ? c->base + (size_t)y * w : NULL;
		for (int x = 0; x < w; x++, p += 3) {
			float r = p[0], g = p[1], b = p[2];
			float L = lum3(r, g, b);
			float sboost = 1.f;
			if (bs) {
				float Lb = bs[x], d = L - Lb;
				float Lb2 = Lb + amb * 0.40f * (0.5f - Lb);
				float L2 = Lb2 + d * (1.f + amb * 1.3f);
				if (L > 0.004f) {
					float k = CLAMP(L2 / L, 0.15f, 4.f);
					r *= k;
					g *= k;
					b *= k;
				} else {
					float dd = L2 - L;
					r += dd;
					g += dd;
					b += dd;
				}
				sboost = 1.f + 0.22f * amb;
				L = lum3(r, g, b);
			}
			if (sat != 0.f || sboost != 1.f) {
				float mx = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b));
				float sx = mx > 0.001f ? (mx - mn) / mx : 0.f;
				float f = sat >= 0.f ? 1.f + sat * (1.f - 0.55f * sx) : 1.f + sat;
				f *= sboost;
				r = L + (r - L) * f;
				g = L + (g - L) * f;
				b = L + (b - L) * f;
			}
			if (warm != 0.f) {
				r *= 1.f + 0.20f * warm;
				b *= 1.f - 0.20f * warm;
				g *= 1.f + 0.02f * warm;
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_tune(const Layer *L, Img *im) {
	float br = L->p[TU_BRI] / 100.f, co = L->p[TU_CON] / 100.f, sa = L->p[TU_SAT] / 100.f;
	float am = L->p[TU_AMB] / 100.f, hi = L->p[TU_HIG] / 100.f, sh = L->p[TU_SHA] / 100.f, wa = L->p[TU_WAR] / 100.f;
	float *tone = (float *)xmalloc(LUTN * sizeof(float)), *hs = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		if (br != 0.f) v = powf(v, exp2f(-br * 1.0f));
		tone[i] = scurve(v, co);
		float l = v;
		float k = 1.f + sh * 1.1f * powf(1.f - l, 3.f) + hi * 0.65f * l * l * l;
		hs[i] = MAX(k, 0.f);
	}
	TuneCtx c = {im, tone, hs, NULL, am, sa, wa};
	parallel(tune_a_job, &c, im->h);
	float *base = NULL;
	if (am != 0.f) {
		float *Y = lum_plane(im);
		base = (float *)xmalloc((size_t)im->w * im->h * sizeof(float));
		int r = MAX(2, (int)(MIN(im->w, im->h) * 0.03f));
		guided_self(Y, base, im->w, im->h, r, 0.02f);
		free(Y);
	}
	c.base = base;
	parallel(tune_b_job, &c, im->h);
	free(base);
	free(tone);
	free(hs);
}

/* ------------------------------------------------------------------ Details (structure, sharpening) */
typedef struct {
	Img *im;
	const float *Y, *b1, *b2, *b3;
	float st, sh;
	float *Ynew;
} DetCtx;
static void det_job(void *v, int y0, int y1) {
	DetCtx *c = (DetCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		for (int x = 0; x < w; x++) {
			size_t i = (size_t)y * w + x;
			float Y = c->Y[i], d = 0.f;
			if (c->b1) {
				float d1 = Y - c->b1[i], d2 = c->b1[i] - c->b2[i];
				float t = 2.f * Y - 1.f;
				float wg = 1.f - 0.6f * t * t;
				d += c->st * (1.5f * d1 + 0.9f * d2) * wg;
			}
			if (c->b3) d += c->sh * 1.8f * (Y - c->b3[i]);
			c->Ynew[i] = Y + d;
		}
	}
}
static void apply_details(const Layer *L, Img *im) {
	float st = L->p[DE_STR] / 100.f, sh = L->p[DE_SHP] / 100.f;
	if (st == 0.f && sh == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *Yn = (float *)xmalloc(n * sizeof(float));
	float *b1 = NULL, *b2 = NULL, *b3 = NULL;
	if (st != 0.f) {
		b1 = (float *)xmalloc(n * sizeof(float));
		b2 = (float *)xmalloc(n * sizeof(float));
		memcpy(b1, Y, n * sizeof(float));
		memcpy(b2, Y, n * sizeof(float));
		blur_f(b1, w, h, 1, MAX(1.f, m * 0.0035f));
		blur_f(b2, w, h, 1, MAX(2.f, m * 0.014f));
	}
	if (sh > 0.f) {
		b3 = (float *)xmalloc(n * sizeof(float));
		memcpy(b3, Y, n * sizeof(float));
		blur_f(b3, w, h, 1, MAX(1.f, m * 0.0012f));
	}
	DetCtx c = {im, Y, b1, b2, b3, st, sh, Yn};
	parallel(det_job, &c, h);
	apply_ratio(im, Y, Yn);
	img_clamp(im);
	free(Y);
	free(Yn);
	free(b1);
	free(b2);
	free(b3);
}

/* ------------------------------------------------------------------ Curves */
static void pchip_lut(const float (*pt)[2], int n, float *out, int N) {
	float d[MAXCP], mm[MAXCP], hh[MAXCP];
	if (n < 2) {
		for (int i = 0; i < N; i++) out[i] = i / (float)(N - 1);
		return;
	}
	for (int k = 0; k < n - 1; k++) {
		hh[k] = MAX(pt[k + 1][0] - pt[k][0], 1e-5f);
		d[k] = (pt[k + 1][1] - pt[k][1]) / hh[k];
	}
	mm[0] = d[0];
	mm[n - 1] = d[n - 2];
	for (int k = 1; k < n - 1; k++) mm[k] = (d[k - 1] * d[k] <= 0.f) ? 0.f : 2.f * d[k - 1] * d[k] / (d[k - 1] + d[k]);
	/* keep end slopes monotone */
	for (int k = 0; k < n - 1; k++) {
		if (d[k] == 0.f) {
			mm[k] = 0;
			mm[k + 1] = 0;
		} else {
			float a = mm[k] / d[k], b = mm[k + 1] / d[k], s = a * a + b * b;
			if (s > 9.f) {
				float t = 3.f / sqrtf(s);
				mm[k] = t * a * d[k];
				mm[k + 1] = t * b * d[k];
			}
		}
	}
	int seg = 0;
	for (int i = 0; i < N; i++) {
		float x = i / (float)(N - 1);
		while (seg < n - 2 && x > pt[seg + 1][0]) seg++;
		float y;
		if (x <= pt[0][0])
			y = pt[0][1];
		else if (x >= pt[n - 1][0])
			y = pt[n - 1][1];
		else {
			float t = (x - pt[seg][0]) / hh[seg], t2 = t * t, t3 = t2 * t;
			y = (2 * t3 - 3 * t2 + 1) * pt[seg][1] + (t3 - 2 * t2 + t) * hh[seg] * mm[seg] + (-2 * t3 + 3 * t2) * pt[seg + 1][1] + (t3 - t2) * hh[seg] * mm[seg + 1];
		}
		out[i] = CL01(y);
	}
}
static void curve_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			p[0] = lut_at(j->lut0, p[0]);
			p[1] = lut_at(j->lut1, p[1]);
			p[2] = lut_at(j->lut2, p[2]);
		}
	}
}
static void curves_luts(const Layer *L, float *lut[3]) {
	float *m = (float *)xmalloc(LUTN * sizeof(float)), *c = (float *)xmalloc(LUTN * sizeof(float));
	pchip_lut(L->cp[0], L->ncp[0], m, LUTN);
	for (int ch = 0; ch < 3; ch++) {
		pchip_lut(L->cp[1 + ch], L->ncp[1 + ch], c, LUTN);
		for (int i = 0; i < LUTN; i++) lut[ch][i] = lut_at(m, c[i]);
	}
	free(m);
	free(c);
}
static void apply_curves(const Layer *L, Img *im) {
	float *l[3];
	for (int i = 0; i < 3; i++) l[i] = (float *)xmalloc(LUTN * sizeof(float));
	curves_luts(L, l);
	Job j = {im, NULL, NULL, {0}, l[0], l[1], l[2]};
	parallel(curve_job, &j, im->h);
	for (int i = 0; i < 3; i++) free(l[i]);
}

/* ------------------------------------------------------------------ White balance */
static void wb_gains(float temp, float tint, float g[3]) {
	float a = temp / 100.f * 0.75f, b = -tint / 100.f * 0.5f;
	g[0] = exp2f(a);
	g[1] = exp2f(b);
	g[2] = exp2f(-a);
	float k = 1.f / lum3(g[0], g[1], g[2]);
	g[0] *= k;
	g[1] *= k;
	g[2] *= k;
}
static void wb_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			p[0] = to_gam(to_lin(p[0]) * j->k[0]);
			p[1] = to_gam(to_lin(p[1]) * j->k[1]);
			p[2] = to_gam(to_lin(p[2]) * j->k[2]);
		}
	}
}
static void apply_wb(const Layer *L, Img *im) {
	if (L->p[WB_TEMP] == 0.f && L->p[WB_TINT] == 0.f) return;
	Job j = {im, NULL, NULL, {0}, NULL, NULL, NULL};
	wb_gains(L->p[WB_TEMP], L->p[WB_TINT], j.k);
	parallel(wb_job, &j, im->h);
}
/* temperature/tint (slider units) that make the given sample neutral */
static void wb_from_sample(float r, float g, float b, float *temp, float *tint) {
	r = MAX(to_lin(r), 1e-4f);
	g = MAX(to_lin(g), 1e-4f);
	b = MAX(to_lin(b), 1e-4f);
	float a = 0.5f * log2f(b / r);
	float bb = 0.5f * (log2f(r) + log2f(b)) - log2f(g);
	*temp = CLAMP(a / 0.75f * 100.f, -100.f, 100.f);
	*tint = CLAMP(-bb / 0.5f * 100.f, -100.f, 100.f);
}
