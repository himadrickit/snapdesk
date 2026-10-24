
/* ================================================================== software UI rasteriser */
static uint32_t *fb = NULL;
static int FW = 0, FH = 0;
static int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0; /* clip rect [x0,x1) [y0,y1) */

#define C_BG 0x17181a
#define C_PANEL 0x212325
#define C_PANEL2 0x2a2d30
#define C_PANEL3 0x34383c
#define C_LINE 0x383c40
#define C_TEXT 0xe8eaed
#define C_DIM 0x9aa0a6
#define C_FAINT 0x636a70
#define C_ACC 0x35c4b6
#define C_ACCD 0x23867d
#define C_WARN 0xef6c5d
#define C_G_ADJ 0x4f9dff
#define C_G_GEO 0x55c27a
#define C_G_RET 0xf2a03d
#define C_G_FIL 0xb77cf0

static int ui_dry = 0; /* input-only pass: layout and widget logic run, nothing is drawn (the clip is empty) */
typedef struct { int x0, y0, x1, y1; } Clip;
static Clip clip_get(void) { Clip c = {cx0_, cy0_, cx1_, cy1_}; return c; }
static void clip_set(Clip c) { cx0_ = c.x0; cy0_ = c.y0; cx1_ = c.x1; cy1_ = c.y1; }
static Clip clip_push(int x, int y, int w, int h) {
	Clip o = clip_get();
	cx0_ = MAX(cx0_, MAX(0, x));
	cy0_ = MAX(cy0_, MAX(0, y));
	cx1_ = MIN(cx1_, MIN(FW, x + w));
	cy1_ = MIN(cy1_, MIN(FH, y + h));
	if (cx1_ < cx0_) cx1_ = cx0_;
	if (cy1_ < cy0_) cy1_ = cy0_;
	return o;
}
static void clip_reset(void) { cx0_ = 0; cy0_ = 0; cx1_ = FW; cy1_ = FH; }

static inline uint32_t blend_px(uint32_t d, uint32_t c, int a) { /* a: 0..256 */
	if (a >= 256) return c;
	if (a <= 0) return d;
	uint32_t rb = (((c & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
	uint32_t g = (((c & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
	return rb | g;
}
static inline void put_px(int x, int y, uint32_t c, int a) {
	if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_) return;
	uint32_t *p = &fb[(size_t)y * FW + x];
	*p = blend_px(*p, c, a);
}
static inline int a256(float alpha) { return (int)(CL01(alpha) * 256.f + 0.5f); }

static void fill_rect(int x, int y, int w, int h, uint32_t c, float alpha) {
	if (ui_dry) return;
	int x0 = MAX(x, cx0_), y0 = MAX(y, cy0_), x1 = MIN(x + w, cx1_), y1 = MIN(y + h, cy1_);
	int a = a256(alpha);
	if (a <= 0) return;
	for (int j = y0; j < y1; j++) {
		uint32_t *p = &fb[(size_t)j * FW];
		if (a >= 256)
			for (int i = x0; i < x1; i++) p[i] = c;
		else
			for (int i = x0; i < x1; i++) p[i] = blend_px(p[i], c, a);
	}
}
static void frame_rect(int x, int y, int w, int h, uint32_t c, float alpha) {
	fill_rect(x, y, w, 1, c, alpha);
	fill_rect(x, y + h - 1, w, 1, c, alpha);
	fill_rect(x, y + 1, 1, h - 2, c, alpha);
	fill_rect(x + w - 1, y + 1, 1, h - 2, c, alpha);
}
static float sdf_rr(float px, float py, float cx, float cy, float hw, float hh, float r) {
	float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
	float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
	return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - r;
}
static void rrect(float x, float y, float w, float h, float r, uint32_t c, float alpha) {
	if (ui_dry) return;
	int x0 = MAX((int)floorf(x), cx0_), y0 = MAX((int)floorf(y), cy0_), x1 = MIN((int)ceilf(x + w), cx1_), y1 = MIN((int)ceilf(y + h), cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	int aa = a256(alpha);
	/* rows in the straight band: the interior is fully covered, only the 1-2 edge pixels need the SDF */
	int xs = (int)ceilf(x), xe = (int)floorf(x + w);
	for (int j = y0; j < y1; j++) {
		int sa = x0, sb = x0, have = 0; /* [sa, sb) = span filled without evaluating the SDF */
		if (r >= 1.f && fabsf(j + 0.5f - cy) <= hh - r) {
			sa = MAX(xs, x0);
			sb = MIN(xe, x1);
			have = sb > sa;
		}
		int le = have ? sa : x1; /* SDF pixels: [x0, le) and [re, x1) */
		int re = have ? sb : x1;
		for (int i = x0; i < le; i++) {
			float cov = CL01(0.5f - sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r));
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
		if (have && aa > 0) {
			uint32_t *p = &fb[(size_t)j * FW];
			if (aa >= 256)
				for (int i = sa; i < sb; i++) p[i] = c;
			else
				for (int i = sa; i < sb; i++) p[i] = blend_px(p[i], c, aa);
		}
		for (int i = re; i < x1; i++) {
			float cov = CL01(0.5f - sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r));
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
	}
}
static void rrect_line(float x, float y, float w, float h, float r, float t, uint32_t c, float alpha) {
	if (ui_dry) return;
	int x0 = MAX((int)floorf(x) - 1, cx0_), y0 = MAX((int)floorf(y) - 1, cy0_), x1 = MIN((int)ceilf(x + w) + 1, cx1_), y1 = MIN((int)ceilf(y + h) + 1, cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	int M = (int)ceilf(t) + 3;
	for (int j = y0; j < y1; j++) {
		/* far from the top/bottom edge only the left and right margins can contain the outline */
		int skip = r >= 1.f && fabsf(j + 0.5f - cy) <= hh - r - t - 2.f;
		for (int i = x0; i < x1; i++) {
			if (skip && i >= x0 + M && i < x1 - M) {
				i = x1 - M - 1;
				continue;
			}
			float d = fabsf(sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r) + t * 0.5f) - t * 0.5f;
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
	}
}
static void disc(float cx, float cy, float r, uint32_t c, float alpha) {
	if (ui_dry) return;
	int x0 = MAX((int)floorf(cx - r - 1), cx0_), y0 = MAX((int)floorf(cy - r - 1), cy0_), x1 = MIN((int)ceilf(cx + r + 1), cx1_), y1 = MIN((int)ceilf(cy + r + 1), cy1_);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
			float cov = CL01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void ring(float cx, float cy, float r, float t, uint32_t c, float alpha) {
	if (ui_dry) return;
	float ro = r + t * 0.5f;
	int x0 = MAX((int)floorf(cx - ro - 1), cx0_), y0 = MAX((int)floorf(cy - ro - 1), cy0_), x1 = MIN((int)ceilf(cx + ro + 1), cx1_), y1 = MIN((int)ceilf(cy + ro + 1), cy1_);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
			float d = fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f;
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void ellipse_ring(float cx, float cy, float a, float b, float ang, float t, uint32_t c, float alpha, int dashed) {
	if (ui_dry) return;
	int N = MAX(48, (int)(MAX(a, b) * 0.6f));
	float cs = cosf(ang), sn = sinf(ang), px = 0, py = 0;
	for (int i = 0; i <= N; i++) {
		float u = i * 2.f * PI_F / N;
		float ex = cosf(u) * a, ey = sinf(u) * b;
		float x = cx + ex * cs - ey * sn, y = cy + ex * sn + ey * cs;
		if (i > 0 && (!dashed || (i & 2) == 0)) {
			/* inline segment */
			float dx = x - px, dy = y - py, len2 = dx * dx + dy * dy;
			int bx0 = MAX((int)floorf(MIN(px, x) - t - 1), cx0_), bx1 = MIN((int)ceilf(MAX(px, x) + t + 1), cx1_);
			int by0 = MAX((int)floorf(MIN(py, y) - t - 1), cy0_), by1 = MIN((int)ceilf(MAX(py, y) + t + 1), cy1_);
			for (int j = by0; j < by1; j++)
				for (int k = bx0; k < bx1; k++) {
					float qx = k + 0.5f - px, qy = j + 0.5f - py;
					float h = len2 > 0 ? CL01((qx * dx + qy * dy) / len2) : 0.f;
					float ddx = qx - dx * h, ddy = qy - dy * h;
					float cov = CL01(t * 0.5f - sqrtf(ddx * ddx + ddy * ddy) + 0.5f);
					if (cov > 0.f) put_px(k, j, c, a256(cov * alpha));
				}
		}
		px = x;
		py = y;
	}
}
static void line_aa(float x0, float y0, float x1, float y1, float t, uint32_t c, float alpha) {
	if (ui_dry) return;
	float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
	int bx0 = MAX((int)floorf(MIN(x0, x1) - t - 1), cx0_), bx1 = MIN((int)ceilf(MAX(x0, x1) + t + 1), cx1_);
	int by0 = MAX((int)floorf(MIN(y0, y1) - t - 1), cy0_), by1 = MIN((int)ceilf(MAX(y0, y1) + t + 1), cy1_);
	for (int j = by0; j < by1; j++)
		for (int i = bx0; i < bx1; i++) {
			float qx = i + 0.5f - x0, qy = j + 0.5f - y0;
			float h = len2 > 0 ? CL01((qx * dx + qy * dy) / len2) : 0.f;
			float ddx = qx - dx * h, ddy = qy - dy * h;
			float cov = CL01(t * 0.5f - sqrtf(ddx * ddx + ddy * ddy) + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, float alpha) {
	if (ui_dry) return;
	int bx0 = MAX((int)floorf(MIN(x0, MIN(x1, x2))) - 1, cx0_), bx1 = MIN((int)ceilf(MAX(x0, MAX(x1, x2))) + 1, cx1_);
	int by0 = MAX((int)floorf(MIN(y0, MIN(y1, y2))) - 1, cy0_), by1 = MIN((int)ceilf(MAX(y0, MAX(y1, y2))) + 1, cy1_);
	float ax[3] = {x0, x1, x2}, ay[3] = {y0, y1, y2};
	float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	float sg = area < 0 ? -1.f : 1.f;
	for (int j = by0; j < by1; j++)
		for (int i = bx0; i < bx1; i++) {
			float px = i + 0.5f, py = j + 0.5f, m = 1e9f;
			for (int k = 0; k < 3; k++) {
				int n = (k + 1) % 3;
				float ex = ax[n] - ax[k], ey = ay[n] - ay[k], l = sqrtf(ex * ex + ey * ey) + 1e-6f;
				float d = sg * (ex * (py - ay[k]) - ey * (px - ax[k])) / l;
				m = MIN(m, d);
			}
			float cov = CL01(m + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void soft_shadow(int x, int y, int w, int h, int r, float alpha) {
	if (ui_dry) return;
	for (int i = r; i >= 1; i--) {
		float a = alpha * (1.f - (float)i / (r + 1)) * (1.f - (float)i / (r + 1)) * 0.5f;
		rrect((float)(x - i), (float)(y - i + r / 3), (float)(w + 2 * i), (float)(h + 2 * i), (float)(i + 2), 0x000000, a);
	}
}

/* ---- fonts ---- */
#define NSZ 8
static const int FSZ[NSZ] = {10, 11, 12, 13, 14, 16, 20, 30};
typedef struct {
	uint8_t *bm;
	int w, h, xo, yo, ok;
	float adv;
} Glyph;
static Glyph gcache[2][NSZ][96];
static float fsc[2][NSZ];
static int fasc[2][NSZ];
static stbtt_fontinfo *FI[2];
static void fonts_init(void) {
	if (!stbtt_InitFont(&font_reg, font_regular, 0) || !stbtt_InitFont(&font_med, font_medium, 0)) die("font init failed");
	FI[0] = &font_reg;
	FI[1] = &font_med;
	for (int w = 0; w < 2; w++)
		for (int s = 0; s < NSZ; s++) {
			fsc[w][s] = stbtt_ScaleForPixelHeight(FI[w], (float)FSZ[s]);
			int a, d, g;
			stbtt_GetFontVMetrics(FI[w], &a, &d, &g);
			fasc[w][s] = (int)roundf(a * fsc[w][s]);
		}
}
static int size_idx(int px) {
	int b = 0, bd = 1000;
	for (int i = 0; i < NSZ; i++)
		if (abs(FSZ[i] - px) < bd) {
			bd = abs(FSZ[i] - px);
			b = i;
		}
	return b;
}
static Glyph *get_glyph(int wt, int si, int ch) {
	if (ch < 32 || ch > 126) ch = '?';
	Glyph *g = &gcache[wt][si][ch - 32];
	if (g->ok) return g;
	stbtt_fontinfo *f = FI[wt];
	float sc = fsc[wt][si];
	int x0, y0, x1, y1, adv, lsb;
	stbtt_GetCodepointBitmapBox(f, ch, sc, sc, &x0, &y0, &x1, &y1);
	stbtt_GetCodepointHMetrics(f, ch, &adv, &lsb);
	g->w = x1 - x0;
	g->h = y1 - y0;
	g->xo = x0;
	g->yo = y0;
	g->adv = adv * sc;
	g->ok = 1;
	if (g->w > 0 && g->h > 0) {
		g->bm = (uint8_t *)xmalloc((size_t)g->w * g->h);
		stbtt_MakeCodepointBitmap(f, g->bm, g->w, g->h, g->w, sc, sc, ch);
	}
	return g;
}
static float text_width(const char *s, int px, int wt) {
	int si = size_idx(px);
	float w = 0;
	for (; *s; s++) {
		Glyph *g = get_glyph(wt, si, (unsigned char)*s);
		w += g->adv;
		if (s[1]) w += stbtt_GetCodepointKernAdvance(FI[wt], (unsigned char)*s, (unsigned char)s[1]) * fsc[wt][si];
	}
	return w;
}
/* draws text with its caps vertically centred on cy; returns the advance */
static float text(float x, float cy, const char *s, int px, int wt, uint32_t col, float alpha) {
	int si = size_idx(px);
	float capc = 0.355f * FSZ[si];
	int base = (int)roundf(cy + capc);
	float xp = x;
	for (; *s; s++) {
		Glyph *g = get_glyph(wt, si, (unsigned char)*s);
		if (g->bm && !ui_dry) {
			int gx = (int)floorf(xp + 0.5f) + g->xo, gy = base + g->yo;
			for (int j = 0; j < g->h; j++) {
				int yy = gy + j;
				if (yy < cy0_ || yy >= cy1_) continue;
				for (int i = 0; i < g->w; i++) {
					int xx = gx + i;
					if (xx < cx0_ || xx >= cx1_) continue;
					int a = g->bm[j * g->w + i];
					if (!a) continue;
					uint32_t *p = &fb[(size_t)yy * FW + xx];
					*p = blend_px(*p, col, (int)(a * alpha * 256.f / 255.f));
				}
			}
		}
		xp += g->adv;
		if (s[1]) xp += stbtt_GetCodepointKernAdvance(FI[wt], (unsigned char)*s, (unsigned char)s[1]) * fsc[wt][si];
	}
	return xp - x;
}
static void text_r(float xr, float cy, const char *s, int px, int wt, uint32_t col, float alpha) { text(xr - text_width(s, px, wt), cy, s, px, wt, col, alpha); }
static void text_c(float xc, float cy, const char *s, int px, int wt, uint32_t col, float alpha) { text(xc - text_width(s, px, wt) * 0.5f, cy, s, px, wt, col, alpha); }
/* fits text into maxw by trimming with ".." */
static void text_fit(float x, float cy, const char *s, float maxw, int px, int wt, uint32_t col, float alpha) {
	if (text_width(s, px, wt) <= maxw) {
		text(x, cy, s, px, wt, col, alpha);
		return;
	}
	char buf[160];
	size_t n = strlen(s);
	if (n > sizeof buf - 3) n = sizeof buf - 3;
	while (n > 1) {
		memcpy(buf, s, n);
		buf[n] = '.';
		buf[n + 1] = '.';
		buf[n + 2] = 0;
		if (text_width(buf, px, wt) <= maxw) break;
		n--;
	}
	text(x, cy, buf, px, wt, col, alpha);
}

/* ---- small vector icons ---- */
static void icon_eye(float cx, float cy, float s, uint32_t c, float a, int on) {
	ellipse_ring(cx, cy, s, s * 0.58f, 0, 1.4f, c, a, 0);
	if (on) disc(cx, cy, s * 0.32f, c, a);
	else line_aa(cx - s, cy + s * 0.7f, cx + s, cy - s * 0.7f, 1.4f, c, a);
}
static void icon_x(float cx, float cy, float s, uint32_t c, float a) {
	line_aa(cx - s, cy - s, cx + s, cy + s, 1.6f, c, a);
	line_aa(cx - s, cy + s, cx + s, cy - s, 1.6f, c, a);
}
static void icon_check(float cx, float cy, float s, uint32_t c, float a) {
	line_aa(cx - s, cy, cx - s * 0.25f, cy + s * 0.7f, 2.f, c, a);
	line_aa(cx - s * 0.25f, cy + s * 0.7f, cx + s, cy - s * 0.65f, 2.f, c, a);
}
static void icon_undo(float cx, float cy, float s, uint32_t c, float a, int flip) {
	float f = flip ? -1.f : 1.f, r = s * 0.7f, ccy = cy + s * 0.15f;
	float t0 = -2.7f, t1 = 1.0f, px = 0, py = 0;
	int N = 16;
	for (int i = 0; i <= N; i++) {
		float u = t0 + (t1 - t0) * i / N;
		float x = cx + f * r * cosf(u), y = ccy + r * sinf(u);
		if (i) line_aa(px, py, x, y, 1.7f, c, a);
		px = x;
		py = y;
	}
	float sx = cx + f * r * cosf(t0), sy = ccy + r * sinf(t0);
	float dx = f * sinf(t0), dy = -cosf(t0);
	float tx = sx + dx * s * 0.5f, ty = sy + dy * s * 0.5f, ex = -dy, ey = dx;
	tri(tx, ty, sx + ex * s * 0.42f, sy + ey * s * 0.42f, sx - ex * s * 0.42f, sy - ey * s * 0.42f, c, a);
}
static void icon_split(float cx, float cy, float s, uint32_t c, float a) { /* compare icon: two overlapping squares */
	rrect_line(cx - s, cy - s * 0.7f, s * 1.3f, s * 1.4f, 2, 1.5f, c, a);
	rrect_line(cx - s * 0.3f, cy - s * 0.7f, s * 1.3f, s * 1.4f, 2, 1.5f, c, a);
}
