
/* ------------------------------------------------------------------ colour grades (Vintage / Retrolux / Grainy Film / Noir) */
typedef struct {
	float mul[3], fade, con, sat, sh[3], hi[3];
} Grade;
static const Grade VINTAGE[9] = {
	{{1.06f, 1.00f, 0.86f}, 0.07f, 0.12f, 0.85f, {0.02f, 0.00f, -0.02f}, {0.04f, 0.02f, -0.04f}},
	{{0.96f, 1.00f, 1.06f}, 0.03f, 0.22f, 0.95f, {-0.03f, 0.02f, 0.05f}, {0.06f, 0.02f, -0.05f}},
	{{1.12f, 0.97f, 0.74f}, 0.05f, 0.15f, 0.35f, {0.03f, 0.01f, -0.02f}, {0.05f, 0.03f, -0.03f}},
	{{0.95f, 1.03f, 0.93f}, 0.10f, 0.05f, 0.80f, {0.00f, 0.03f, 0.02f}, {0.03f, 0.04f, -0.02f}},
	{{1.03f, 1.00f, 0.94f}, 0.09f, 0.18f, 0.90f, {0.00f, 0.02f, 0.05f}, {0.05f, 0.01f, -0.03f}},
	{{0.94f, 1.02f, 1.10f}, 0.04f, 0.28f, 1.05f, {0.02f, 0.03f, 0.06f}, {0.08f, 0.02f, -0.06f}},
	{{1.10f, 1.00f, 0.82f}, 0.02f, 0.18f, 1.10f, {0.04f, 0.00f, -0.03f}, {0.08f, 0.05f, -0.05f}},
	{{1.05f, 0.97f, 1.00f}, 0.11f, 0.06f, 0.72f, {0.04f, 0.00f, 0.02f}, {0.03f, 0.00f, 0.03f}},
	{{1.04f, 1.00f, 0.93f}, 0.12f, 0.20f, 0.00f, {0.01f, 0.01f, 0.00f}, {0.03f, 0.02f, -0.01f}},
};
static const Grade RETRO[6] = {
	{{1.10f, 0.98f, 0.80f}, 0.10f, 0.25f, 1.15f, {0.05f, 0.00f, -0.04f}, {0.10f, 0.06f, -0.06f}},
	{{0.92f, 1.06f, 1.00f}, 0.09f, 0.20f, 0.90f, {-0.02f, 0.05f, 0.04f}, {0.07f, 0.05f, -0.05f}},
	{{1.00f, 0.94f, 1.12f}, 0.07f, 0.28f, 1.05f, {0.05f, 0.00f, 0.08f}, {0.08f, 0.02f, -0.02f}},
	{{1.08f, 1.02f, 0.90f}, 0.14f, 0.12f, 0.65f, {0.04f, 0.02f, 0.00f}, {0.05f, 0.03f, 0.00f}},
	{{0.98f, 1.00f, 1.04f}, 0.12f, 0.30f, 1.20f, {0.00f, 0.04f, 0.08f}, {0.10f, 0.03f, -0.04f}},
	{{1.12f, 0.92f, 0.86f}, 0.08f, 0.22f, 1.25f, {0.06f, -0.02f, 0.02f}, {0.09f, 0.04f, -0.06f}},
};
static const Grade FILMS[6] = {
	{{1.00f, 1.00f, 1.00f}, 0.00f, 0.10f, 1.00f, {0, 0, 0}, {0, 0, 0}},
	{{1.06f, 1.01f, 0.90f}, 0.02f, 0.12f, 1.05f, {0.02f, 0.00f, -0.02f}, {0.04f, 0.02f, -0.03f}},
	{{0.94f, 1.00f, 1.07f}, 0.02f, 0.12f, 1.00f, {-0.02f, 0.01f, 0.04f}, {0.00f, 0.01f, 0.03f}},
	{{1.02f, 1.00f, 0.96f}, 0.12f, 0.00f, 0.75f, {0.02f, 0.02f, 0.02f}, {0.02f, 0.02f, 0.01f}},
	{{1.00f, 1.00f, 1.00f}, 0.03f, 0.25f, 0.00f, {0, 0, 0}, {0, 0, 0}},
	{{1.02f, 1.00f, 1.02f}, 0.00f, 0.32f, 1.30f, {0, 0, 0.02f}, {0.03f, 0.01f, -0.02f}},
};
typedef struct {
	Img *im;
	const Grade *g;
	float strength;
} GradeCtx;
static void grade_job(void *v, int y0, int y1) {
	GradeCtx *c = (GradeCtx *)v;
	const Grade *g = c->g;
	int w = c->im->w;
	float s = c->strength;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float o[3] = {p[0], p[1], p[2]}, q[3];
			for (int k = 0; k < 3; k++) {
				float t = o[k] * g->mul[k];
				t = g->fade + t * (1.f - g->fade);
				q[k] = scurve(CL01(t), g->con);
			}
			float L = lum3(q[0], q[1], q[2]);
			float ws = (1.f - L) * (1.f - L) * 0.16f, wh = L * L * 0.16f;
			for (int k = 0; k < 3; k++) {
				q[k] = L + (q[k] - L) * g->sat;
				q[k] += g->sh[k] * ws * 6.f + g->hi[k] * wh * 6.f;
				p[k] = CL01(LERP(o[k], q[k], s));
			}
		}
	}
}
static void apply_grade(Img *im, const Grade *g, float strength) {
	if (strength <= 0.f) return;
	GradeCtx c = {im, g, CL01(strength)};
	parallel(grade_job, &c, im->h);
}
static void tone_brightness(Img *im, float br) {
	if (br == 0.f) return;
	float e = exp2f(-br);
	size_t n = (size_t)im->w * im->h * 3;
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) lut[i] = powf(i / (float)(LUTN - 1), e);
	for (size_t i = 0; i < n; i++) im->p[i] = lut_at(lut, im->p[i]);
	free(lut);
}

static void apply_vintage(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[VI_STYLE], 0, 8);
	apply_grade(im, &VINTAGE[st], L->p[VI_STR] / 100.f);
	tone_brightness(im, L->p[VI_BRI] / 100.f * 0.8f);
	img_saturate(im, 1.f + L->p[VI_SAT] / 100.f);
	add_vignette(im, -L->p[VI_VIG] / 100.f * 0.85f, 0.35f);
	add_grain(im, 0.10f * L->p[VI_STR] / 100.f, 31);
	img_clamp(im);
}

/* procedural light leaks and scratches; deterministic in normalised coordinates */
typedef struct {
	Img *im;
	float leak, scr;
	uint32_t seed;
	float lx, ly, lr, col[3], m;
} RetCtx;
static void retro_job(void *v, int y0, int y1) {
	RetCtx *c = (RetCtx *)v;
	int w = c->im->w, h = c->im->h;
	int nscr = (int)(c->scr * 40.f);
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		float fy = (float)y / h;
		for (int x = 0; x < w; x++, p += 3) {
			float fx = (float)x / w;
			if (c->leak > 0.f) {
				float dx = (fx - c->lx) * (float)w / c->m, dy = (fy - c->ly) * (float)h / c->m;
				float d = sqrtf(dx * dx + dy * dy) / c->lr;
				float a = expf(-d * d * 1.6f) * c->leak;
				float n = 0.85f + 0.3f * vnoise(fx * 6.f, fy * 6.f, c->seed + 3);
				a *= n;
				for (int k = 0; k < 3; k++) p[k] = 1.f - (1.f - p[k]) * (1.f - CL01(c->col[k] * a));
			}
			for (int s = 0; s < nscr; s++) {
				float sx = hashf(s, 1, c->seed), y0s = hashf(s, 2, c->seed) * 1.2f - 0.1f, len = 0.1f + hashf(s, 3, c->seed) * 0.5f;
				sx += (fy - y0s) * (hashf(s, 4, c->seed) - 0.5f) * 0.02f;
				float dxp = fabsf(fx - sx) * w / c->m;
				if (fy < y0s || fy > y0s + len || dxp > 0.0018f) continue;
				float a = (1.f - dxp / 0.0018f) * 0.45f * (0.5f + 0.5f * vnoise(fy * 90.f, (float)s, c->seed));
				float t = (hashf(s, 5, c->seed) > 0.4f) ? 1.f : 0.f;
				for (int k = 0; k < 3; k++) p[k] = LERP(p[k], t, a);
			}
		}
	}
}
static void apply_retro(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[RE_STYLE], 0, 5);
	uint32_t seed = (uint32_t)L->p[RE_SEED];
	apply_grade(im, &RETRO[st], L->p[RE_STR] / 100.f);
	tone_brightness(im, L->p[RE_BRI] / 100.f * 0.8f);
	img_saturate(im, 1.f + L->p[RE_SAT] / 100.f);
	RetCtx c;
	memset(&c, 0, sizeof c);
	c.im = im;
	c.leak = L->p[RE_LEAK] / 100.f * 0.9f;
	c.scr = L->p[RE_SCR] / 100.f;
	c.seed = seed * 7919u + 13u;
	c.m = (float)MIN(im->w, im->h);
	int side = (int)(hash2(seed, 9, 5) % 4);
	c.lx = (side == 0) ? -0.05f : (side == 1) ? 1.05f : 0.2f + 0.6f * hashf(seed, 10, 1);
	c.ly = (side == 2) ? -0.05f : (side == 3) ? 1.05f : 0.2f + 0.6f * hashf(seed, 11, 1);
	c.lr = 0.35f + 0.35f * hashf(seed, 12, 1);
	static const float LC[5][3] = {{1.0f, 0.45f, 0.1f}, {1.0f, 0.2f, 0.3f}, {1.0f, 0.75f, 0.2f}, {0.9f, 0.3f, 0.6f}, {1.0f, 0.55f, 0.2f}};
	const float *lc = LC[hash2(seed, 13, 2) % 5];
	c.col[0] = lc[0];
	c.col[1] = lc[1];
	c.col[2] = lc[2];
	if (c.leak > 0.f || c.scr > 0.f) parallel(retro_job, &c, im->h);
	add_vignette(im, -0.35f * L->p[RE_STR] / 100.f, 0.4f);
	add_grain(im, 0.12f, seed + 5);
	img_clamp(im);
}
static void apply_grain(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[GR_STYLE], 0, 5);
	apply_grade(im, &FILMS[st], L->p[GR_STR] / 100.f);
	img_saturate(im, 1.f + L->p[GR_SAT] / 100.f);
	add_grain(im, L->p[GR_GRAIN] / 100.f * 1.3f, 77);
	img_clamp(im);
}

/* ------------------------------------------------------------------ Black & White / Noir */
static const float BW_W[6][3] = {{0.30f, 0.59f, 0.11f}, {0.80f, 0.27f, -0.07f}, {0.62f, 0.45f, -0.07f}, {0.45f, 0.58f, -0.03f}, {0.10f, 0.80f, 0.10f}, {-0.05f, 0.20f, 0.85f}};
static const float BW_T[5][2][3] = {
	{{1, 1, 1}, {1, 1, 1}},
	{{0.95f, 0.82f, 0.65f}, {1.05f, 0.97f, 0.80f}},
	{{0.85f, 0.95f, 1.10f}, {0.96f, 1.00f, 1.06f}},
	{{1.00f, 0.90f, 0.70f}, {1.08f, 0.98f, 0.72f}},
	{{0.93f, 0.88f, 1.05f}, {1.00f, 0.98f, 0.98f}},
};
typedef struct {
	Img *im;
	const float *w;
	const float (*tone)[3];
	const float *lut;
	float wash;
} BwCtx;
static void bw_job(void *v, int y0, int y1) {
	BwCtx *c = (BwCtx *)v;
	int W = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * W * 3;
		for (int x = 0; x < W; x++, p += 3) {
			float g = CL01(c->w[0] * p[0] + c->w[1] * p[1] + c->w[2] * p[2]);
			g = lut_at(c->lut, g);
			for (int k = 0; k < 3; k++) p[k] = CL01(g * LERP(c->tone[0][k], c->tone[1][k], g));
		}
	}
}
static void apply_bw(const Layer *L, Img *im) {
	int f = CLAMP((int)L->p[BW_FILTER], 0, 5), t = CLAMP((int)L->p[BW_TONE], 0, 4);
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	float br = L->p[BW_BRI] / 100.f, co = L->p[BW_CON] / 100.f;
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		if (br != 0.f) v = powf(v, exp2f(-br));
		lut[i] = scurve(v, co);
	}
	BwCtx c = {im, BW_W[f], BW_T[t], lut, 0};
	parallel(bw_job, &c, im->h);
	free(lut);
	add_grain(im, L->p[BW_GRAIN] / 100.f * 1.2f, 99);
	img_clamp(im);
}
static void apply_noir(const Layer *L, Img *im) {
	static const float W3[3] = {0.30f, 0.59f, 0.11f};
	static const float STY[4][3] = {{0.45f, 0.00f, 0.0f}, {0.15f, 0.12f, 0.15f}, {0.85f, -0.03f, 0.04f}, {0.20f, 0.18f, 0.2f}}; /* contrast, lift, gamma bias */
	int st = CLAMP((int)L->p[NO_STYLE], 0, 3);
	float wash = L->p[NO_WASH] / 100.f, br = L->p[NO_BRI] / 100.f;
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		v = powf(v, exp2f(-br + STY[st][2] * 2.f));
		v = scurve(v, STY[st][0]);
		v = wash * 0.28f + v * (1.f - wash * 0.38f);
		lut[i] = CL01(v);
	}
	BwCtx c = {im, W3, BW_T[0], lut, 0};
	parallel(bw_job, &c, im->h);
	free(lut);
	add_vignette(im, -L->p[NO_VIG] / 100.f * 0.95f, 0.3f);
	add_grain(im, L->p[NO_GRAIN] / 100.f * 1.3f, 123);
	img_clamp(im);
}

/* ------------------------------------------------------------------ local-contrast filters: Drama, Tonal Contrast, HDR Scape */
static void apply_drama(const Layer *L, Img *im) {
	static const float SG[4][4] = {{1.5f, 0.20f, 0.00f, 0.10f}, {2.6f, 0.32f, 0.00f, 0.28f}, {1.9f, 0.25f, 0.07f, 0.06f}, {2.2f, 0.25f, -0.08f, 0.28f}};
	int st = CLAMP((int)L->p[DR_STYLE], 0, 3);
	float f = L->p[DR_STR] / 100.f, sat = L->p[DR_SAT] / 100.f;
	if (f <= 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *base = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, base, w, h, MAX(3, (int)(MIN(w, h) * 0.035f)), 0.03f);
	for (size_t i = 0; i < n; i++) {
		float b = base[i], d = Y[i] - b;
		float b2 = 0.5f + (b - 0.5f) * (1.f - SG[st][1] * f) + SG[st][2] * f;
		float v = b2 + d * (1.f + SG[st][0] * f);
		Yn[i] = scurve(CL01(v), SG[st][3] * f);
	}
	apply_ratio(im, Y, Yn);
	img_saturate(im, (1.f - 0.28f * f) * (sat >= 0 ? 1.f + sat : 1.f + sat));
	img_clamp(im);
	free(Y);
	free(base);
	free(Yn);
}
static void apply_tonal(const Layer *L, Img *im) {
	float hi = L->p[TC_HIGH] / 100.f, mi = L->p[TC_MID] / 100.f, lo = L->p[TC_LOW] / 100.f;
	float psh = L->p[TC_PSH] / 100.f, phi = L->p[TC_PHI] / 100.f;
	if (hi == 0 && mi == 0 && lo == 0) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *base = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, base, w, h, MAX(2, (int)(MIN(w, h) * 0.018f)), 0.008f);
	for (size_t i = 0; i < n; i++) {
		float b = base[i], d = Y[i] - b;
		float wH = smoothf(0.5f, 0.95f, b), wL = 1.f - smoothf(0.05f, 0.5f, b), wM = 1.f - fabsf(2.f * b - 1.f);
		wM = wM * wM * (3.f - 2.f * wM);
		float g = 2.4f * (hi * wH + mi * wM + lo * wL);
		float prot = 1.f - psh * (1.f - smoothf(0.0f, 0.3f, Y[i])) - phi * smoothf(0.7f, 1.f, Y[i]);
		Yn[i] = Y[i] + d * g * MAX(prot, 0.f);
	}
	apply_ratio(im, Y, Yn);
	img_clamp(im);
	free(Y);
	free(base);
	free(Yn);
}
static void apply_hdr(const Layer *L, Img *im) {
	static const float ST[4][4] = {{1.6f, 1.2f, 0.18f, 0.35f}, {1.1f, 0.8f, -0.05f, 0.30f}, {1.5f, 1.7f, 0.10f, 0.30f}, {2.6f, 1.6f, 0.22f, 0.45f}};
	int st = CLAMP((int)L->p[HD_STYLE], 0, 3);
	float f = L->p[HD_STR] / 100.f, br = L->p[HD_BRI] / 100.f, sat = L->p[HD_SAT] / 100.f;
	if (f <= 0.f && br == 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *b1 = (float *)xmalloc(n * sizeof(float)), *b2 = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, b1, w, h, MAX(4, (int)(m * 0.07f)), 0.03f);
	guided_self(Y, b2, w, h, MAX(2, (int)(m * 0.012f)), 0.006f);
	for (size_t i = 0; i < n; i++) {
		float base = b1[i] + (0.5f - b1[i]) * ST[st][3] * f + br * 0.25f;
		float mid = (b2[i] - b1[i]) * (1.f + ST[st][0] * f);
		float fine = (Y[i] - b2[i]) * (1.f + ST[st][1] * f);
		Yn[i] = CL01(base + mid + fine);
	}
	apply_ratio(im, Y, Yn);
	img_saturate(im, (1.f + ST[st][2] * f) * (sat >= 0 ? 1.f + sat : 1.f + sat));
	img_clamp(im);
	free(Y);
	free(b1);
	free(b2);
	free(Yn);
}

/* ------------------------------------------------------------------ Glamour glow */
typedef struct {
	Img *im;
	const Img *bl;
	float glow, k0, k1, k2;
} GlowCtx;
static void glow_job(void *v, int y0, int y1) {
	GlowCtx *c = (GlowCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *b = c->bl->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, b += 3) {
			float g[3] = {b[0], b[1], b[2]};
			g[0] *= c->k0;
			g[1] *= c->k1;
			g[2] *= c->k2;
			for (int k = 0; k < 3; k++) {
				float gl = CL01(g[k] * (1.f + 0.5f * c->glow));
				float s = 1.f - (1.f - p[k]) * (1.f - gl * c->glow * 0.6f);
				p[k] = LERP(s, g[k], 0.12f * c->glow);
			}
		}
	}
}
static void apply_glow(const Layer *L, Img *im) {
	static const float TN[4][3] = {{1, 1, 1}, {1.08f, 1.0f, 0.88f}, {0.92f, 0.99f, 1.08f}, {1.04f, 1.04f, 1.04f}};
	int st = CLAMP((int)L->p[GL_STYLE], 0, 3);
	float gl = L->p[GL_GLOW] / 100.f * (st == 3 ? 1.35f : 1.f), sat = L->p[GL_SAT] / 100.f, wa = L->p[GL_WARM] / 100.f;
	if (gl > 0.f) {
		Img bl = img_clone(im);
		blur_f(bl.p, bl.w, bl.h, 3, MAX(1.5f, MIN(im->w, im->h) * 0.018f));
		GlowCtx c = {im, &bl, gl, TN[st][0], TN[st][1], TN[st][2]};
		parallel(glow_job, &c, im->h);
		img_free(&bl);
	}
	img_saturate(im, 1.f + sat);
	if (wa != 0.f) {
		size_t n = (size_t)im->w * im->h;
		for (size_t i = 0; i < n; i++) {
			im->p[i * 3] *= 1.f + 0.18f * wa;
			im->p[i * 3 + 2] *= 1.f - 0.18f * wa;
		}
	}
	img_clamp(im);
}

/* ------------------------------------------------------------------ Grunge */
typedef struct {
	Img *im;
	float tex, m, tint[3];
	uint32_t seed;
	int style;
} GrungeCtx;
static void grunge_job(void *v, int y0, int y1) {
	GrungeCtx *c = (GrungeCtx *)v;
	int w = c->im->w;
	float m = c->m;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float fx = x / m, fy = y / m;
			float t = 0.45f * fbm(fx * 4.f, fy * 4.f, c->seed, 4) + 0.35f * fbm(fx * 16.f + 9.f, fy * 16.f, c->seed + 11, 3) + 0.2f * fbm(fx * 90.f, fy * 90.f, c->seed + 23, 2);
			float cr = vnoise(fx * 3.f + 40.f, fy * 3.f, c->seed + 5);
			float crack = (cr > 0.48f && cr < 0.5f) ? 1.f : 0.f;
			t = CL01((t - 0.5f) * 2.2f + 0.5f - crack * 0.4f);
			float d = (t - 0.5f) * c->tex * 1.5f;
			for (int k = 0; k < 3; k++) {
				float v0 = p[k];
				float soft = v0 + d * (v0 < 0.5f ? 2.f * v0 + 0.3f : 2.f * (1.f - v0) + 0.3f);
				p[k] = CL01(soft * LERP(1.f, c->tint[k], c->tex * 0.8f));
			}
		}
	}
}
static void apply_grunge(const Layer *L, Img *im) {
	static const float TINT[5][3] = {{0.95f, 0.92f, 0.84f}, {0.86f, 0.95f, 1.0f}, {0.92f, 1.0f, 0.88f}, {1.0f, 0.86f, 0.78f}, {0.9f, 0.9f, 0.92f}};
	int st = CLAMP((int)L->p[GU_STYLE], 0, 4);
	GrungeCtx c;
	memset(&c, 0, sizeof c);
	c.im = im;
	c.tex = L->p[GU_TEX] / 100.f;
	c.m = (float)MIN(im->w, im->h);
	c.seed = (uint32_t)L->p[GU_SEED] * 31u + (uint32_t)st * 7u + 1u;
	for (int k = 0; k < 3; k++) c.tint[k] = TINT[st][k];
	tone_brightness(im, L->p[GU_BRI] / 100.f * 0.8f);
	size_t n = (size_t)im->w * im->h * 3;
	float co = L->p[GU_CON] / 100.f;
	if (co != 0.f)
		for (size_t i = 0; i < n; i++) im->p[i] = scurve(CL01(im->p[i]), co);
	img_saturate(im, 1.f + L->p[GU_SAT] / 100.f);
	if (c.tex > 0.f) parallel(grunge_job, &c, im->h);
	add_vignette(im, -0.5f * c.tex, 0.4f);
	img_clamp(im);
}

