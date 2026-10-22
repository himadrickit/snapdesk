/*
 * snapdesk.c - SnapDesk: a Snapseed-style, non-destructive photo editor for the desktop.
 *
 * One C99 source file. Needs SDL2 (window/input only), stb_image, stb_image_write,
 * stb_truetype (all vendored as headers next to this file) and an embedded font (ui_font.h).
 *
 *   Linux:   gcc -O2 -o snapdesk snapdesk.c $(sdl2-config --cflags --libs) -lm
 *   Windows: gcc -O2 -o snapdesk.exe snapdesk.c -I<SDL2>/include/SDL2 -L<SDL2>/lib \
 *                -mwindows -lSDL2 -lcomdlg32 -lm        (MinGW / MSYS2)
 *
 * Architecture
 *   - The photo is never modified. Edits are a stack of "layers" (tool + parameters).
 *   - Interactive preview runs the stack on a downscaled float "proxy"; Export runs the very
 *     same stack on the full-resolution image. All spatial parameters are resolution independent.
 *   - Everything (UI included) is drawn by a tiny software rasteriser into one framebuffer that
 *     SDL only presents, so the program looks and behaves identically on every OS.
 */
#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define NOMINMAX
#endif
#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_FAILURE_USERMSG
#define STBI_NO_PIC
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "ui_font.h"

/* ------------------------------------------------------------------ utilities */
#define ARRLEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CLAMP(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
#define CL01(x) ((x) < 0.f ? 0.f : (x) > 1.f ? 1.f : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
#define PI_F 3.14159265358979f

static inline float smoothf(float a, float b, float x) {
	float d = b - a;
	if (d == 0.f) return x < a ? 0.f : 1.f;
	float t = (x - a) / d;
	t = CL01(t);
	return t * t * (3.f - 2.f * t);
}

static void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "snapdesk: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}
static void *xmalloc(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}
static void *xcalloc(size_t n, size_t m) {
	void *p = calloc(n ? n : 1, m ? m : 1);
	if (!p) die("out of memory (%zu bytes)", n * m);
	return p;
}
static void *xrealloc(void *p, size_t n) {
	p = realloc(p, n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}

static inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
	uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u + 0x9e3779b9u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	h *= 2246822519u;
	return h ^ (h >> 15);
}
static inline float hashf(uint32_t x, uint32_t y, uint32_t seed) {
	return (float)(hash2(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}
/* smooth value noise on a lattice, result in 0..1 */
static float vnoise(float x, float y, uint32_t seed) {
	float fx = floorf(x), fy = floorf(y);
	int ix = (int)fx, iy = (int)fy;
	float tx = x - fx, ty = y - fy;
	tx = tx * tx * (3.f - 2.f * tx);
	ty = ty * ty * (3.f - 2.f * ty);
	float a = hashf((uint32_t)ix, (uint32_t)iy, seed), b = hashf((uint32_t)ix + 1, (uint32_t)iy, seed);
	float c = hashf((uint32_t)ix, (uint32_t)iy + 1, seed), d = hashf((uint32_t)ix + 1, (uint32_t)iy + 1, seed);
	return LERP(LERP(a, b, tx), LERP(c, d, tx), ty);
}
static float fbm(float x, float y, uint32_t seed, int oct) {
	float s = 0, a = 0.5f, n = 0;
	for (int i = 0; i < oct; i++) {
		s += a * vnoise(x, y, seed + (uint32_t)i * 101u);
		n += a;
		a *= 0.5f;
		x *= 2.03f;
		y *= 2.03f;
	}
	return s / n;
}

/* ------------------------------------------------------------------ thread pool */
typedef void (*RowFn)(void *ctx, int a, int b);
static struct {
	SDL_Thread *th[32];
	int n;
	SDL_mutex *mu;
	SDL_cond *cv, *done;
	RowFn fn;
	void *ctx;
	int total, chunk, busy, gen, quit;
	SDL_atomic_t next;
} POOL;

static void pool_run_chunks(void) {
	for (;;) {
		int a = SDL_AtomicAdd(&POOL.next, POOL.chunk);
		if (a >= POOL.total) break;
		int b = a + POOL.chunk;
		if (b > POOL.total) b = POOL.total;
		POOL.fn(POOL.ctx, a, b);
	}
}
static int pool_worker(void *arg) {
	(void)arg;
	int seen = 0;
	SDL_LockMutex(POOL.mu);
	for (;;) {
		while (!POOL.quit && POOL.gen == seen) SDL_CondWait(POOL.cv, POOL.mu);
		if (POOL.quit) break;
		seen = POOL.gen;
		SDL_UnlockMutex(POOL.mu);
		pool_run_chunks();
		SDL_LockMutex(POOL.mu);
		if (--POOL.busy == 0) SDL_CondSignal(POOL.done);
	}
	SDL_UnlockMutex(POOL.mu);
	return 0;
}
static void pool_init(void) {
	int cpus = SDL_GetCPUCount();
	if (cpus < 1) cpus = 1;
	if (cpus > 32) cpus = 32;
	POOL.mu = SDL_CreateMutex();
	POOL.cv = SDL_CreateCond();
	POOL.done = SDL_CreateCond();
	POOL.n = cpus - 1;
	for (int i = 0; i < POOL.n; i++) POOL.th[i] = SDL_CreateThread(pool_worker, "worker", NULL);
}
static void pool_quit(void) {
	SDL_LockMutex(POOL.mu);
	POOL.quit = 1;
	SDL_CondBroadcast(POOL.cv);
	SDL_UnlockMutex(POOL.mu);
	for (int i = 0; i < POOL.n; i++) SDL_WaitThread(POOL.th[i], NULL);
	POOL.n = 0;
}
/* run fn over [0,n) in chunks on all cores. fn must not call parallel() itself. */
static void parallel(RowFn fn, void *ctx, int n) {
	if (n <= 0) return;
	if (POOL.n == 0 || n < 4) {
		fn(ctx, 0, n);
		return;
	}
	int ways = POOL.n + 1;
	int chunk = n / (ways * 6);
	if (chunk < 1) chunk = 1;
	SDL_LockMutex(POOL.mu);
	POOL.fn = fn;
	POOL.ctx = ctx;
	POOL.total = n;
	POOL.chunk = chunk;
	SDL_AtomicSet(&POOL.next, 0);
	POOL.busy = POOL.n;
	POOL.gen++;
	SDL_CondBroadcast(POOL.cv);
	SDL_UnlockMutex(POOL.mu);
	pool_run_chunks();
	SDL_LockMutex(POOL.mu);
	while (POOL.busy > 0) SDL_CondWait(POOL.done, POOL.mu);
	SDL_UnlockMutex(POOL.mu);
}

/* ------------------------------------------------------------------ float images */
typedef struct {
	int w, h;
	float *p; /* RGB, gamma encoded (sRGB-ish), nominal range 0..1 */
} Img;

static Img img_new(int w, int h) {
	Img i;
	i.w = w;
	i.h = h;
	i.p = (float *)xmalloc((size_t)w * h * 3 * sizeof(float));
	return i;
}
static void img_free(Img *i) {
	free(i->p);
	i->p = NULL;
	i->w = i->h = 0;
}
static Img img_clone(const Img *s) {
	Img i = img_new(s->w, s->h);
	memcpy(i.p, s->p, (size_t)s->w * s->h * 3 * sizeof(float));
	return i;
}
/* replace *dst by a copy of *s, reusing the allocation when the size matches */
static void img_assign(Img *dst, const Img *s) {
	if (dst->p && (dst->w != s->w || dst->h != s->h)) img_free(dst);
	if (!dst->p) *dst = img_new(s->w, s->h);
	memcpy(dst->p, s->p, (size_t)s->w * s->h * 3 * sizeof(float));
}

static inline void bilin(const Img *im, float x, float y, float *o) {
	x -= 0.5f;
	y -= 0.5f;
	float fx0 = floorf(x), fy0 = floorf(y);
	int x0 = (int)fx0, y0 = (int)fy0;
	float fx = x - fx0, fy = y - fy0;
	int x1 = x0 + 1, y1 = y0 + 1;
	x0 = CLAMP(x0, 0, im->w - 1);
	x1 = CLAMP(x1, 0, im->w - 1);
	y0 = CLAMP(y0, 0, im->h - 1);
	y1 = CLAMP(y1, 0, im->h - 1);
	const float *a = im->p + ((size_t)y0 * im->w + x0) * 3, *b = im->p + ((size_t)y0 * im->w + x1) * 3;
	const float *c = im->p + ((size_t)y1 * im->w + x0) * 3, *d = im->p + ((size_t)y1 * im->w + x1) * 3;
	for (int k = 0; k < 3; k++) {
		float t = a[k] + (b[k] - a[k]) * fx, u = c[k] + (d[k] - c[k]) * fx;
		o[k] = t + (u - t) * fy;
	}
}

/* ---- area-average downscale (exact box weights), from u8 or float sources ---- */
typedef struct {
	const uint8_t *s8;
	const float *sf;
	int sw, sh, tw, th;
	int *st, *cnt;
	float *wt;
	int *vst, *vcnt;
	float *vwt;
	float *tmp;
	float *dst;
	int vmax, hmax;
} RsCtx;

static void rs_weights(int si, int to, int **st, int **cnt, float **wt, int *maxc) {
	float s = (float)si / (float)to;
	int mc = (int)ceilf(s) + 2;
	*st = (int *)xmalloc(sizeof(int) * to);
	*cnt = (int *)xmalloc(sizeof(int) * to);
	*wt = (float *)xmalloc(sizeof(float) * to * mc);
	*maxc = mc;
	for (int o = 0; o < to; o++) {
		float a = o * s, b = (o + 1) * s;
		int i0 = (int)floorf(a), i1 = (int)ceilf(b) - 1;
		if (i1 >= si) i1 = si - 1;
		if (i0 < 0) i0 = 0;
		int n = i1 - i0 + 1;
		if (n > mc) n = mc;
		float sum = 0;
		for (int i = 0; i < n; i++) {
			float lo = MAX((float)(i0 + i), a), hi = MIN((float)(i0 + i + 1), b);
			float w = hi - lo;
			if (w < 0) w = 0;
			(*wt)[o * mc + i] = w;
			sum += w;
		}
		if (sum <= 0) {
			(*wt)[o * mc] = 1;
			sum = 1;
			n = 1;
		}
		for (int i = 0; i < n; i++) (*wt)[o * mc + i] /= sum;
		(*st)[o] = i0;
		(*cnt)[o] = n;
	}
}
static void rs_h_job(void *v, int y0, int y1) {
	RsCtx *c = (RsCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *d = c->tmp + (size_t)y * c->tw * 3;
		for (int x = 0; x < c->tw; x++) {
			float r = 0, g = 0, b = 0;
			const float *w = c->wt + (size_t)x * c->hmax;
			for (int i = 0; i < c->cnt[x]; i++) {
				size_t idx = ((size_t)y * c->sw + c->st[x] + i) * 3;
				if (c->s8) {
					r += w[i] * c->s8[idx];
					g += w[i] * c->s8[idx + 1];
					b += w[i] * c->s8[idx + 2];
				} else {
					r += w[i] * c->sf[idx];
					g += w[i] * c->sf[idx + 1];
					b += w[i] * c->sf[idx + 2];
				}
			}
			float k = c->s8 ? (1.f / 255.f) : 1.f;
			d[x * 3] = r * k;
			d[x * 3 + 1] = g * k;
			d[x * 3 + 2] = b * k;
		}
	}
}
static void rs_v_job(void *v, int y0, int y1) {
	RsCtx *c = (RsCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst + (size_t)y * c->tw * 3;
		const float *w = c->vwt + (size_t)y * c->vmax;
		for (int x = 0; x < c->tw * 3; x++) {
			float s = 0;
			for (int i = 0; i < c->vcnt[y]; i++) s += w[i] * c->tmp[((size_t)(c->vst[y] + i) * c->tw) * 3 + x];
			d[x] = s;
		}
	}
}
static Img resize_generic(const uint8_t *s8, const float *sf, int sw, int sh, int tw, int th) {
	Img out = img_new(tw, th);
	if (tw >= sw && th >= sh) { /* no downscale needed: convert / copy (sizes are equal here) */
		size_t n = (size_t)sw * sh * 3;
		if (tw == sw && th == sh) {
			if (s8)
				for (size_t i = 0; i < n; i++) out.p[i] = s8[i] * (1.f / 255.f);
			else
				memcpy(out.p, sf, n * sizeof(float));
			return out;
		}
		/* upscale (rare): bilinear */
		Img src;
		src.w = sw;
		src.h = sh;
		src.p = (float *)xmalloc(n * sizeof(float));
		if (s8)
			for (size_t i = 0; i < n; i++) src.p[i] = s8[i] * (1.f / 255.f);
		else
			memcpy(src.p, sf, n * sizeof(float));
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
				bilin(&src, (x + 0.5f) * sw / tw, (y + 0.5f) * sh / th, out.p + ((size_t)y * tw + x) * 3);
		free(src.p);
		return out;
	}
	if (tw > sw) tw = sw;
	if (th > sh) th = sh;
	RsCtx c;
	memset(&c, 0, sizeof c);
	c.s8 = s8;
	c.sf = sf;
	c.sw = sw;
	c.sh = sh;
	c.tw = out.w;
	c.th = out.h;
	rs_weights(sw, out.w, &c.st, &c.cnt, &c.wt, &c.hmax);
	rs_weights(sh, out.h, &c.vst, &c.vcnt, &c.vwt, &c.vmax);
	c.tmp = (float *)xmalloc((size_t)out.w * sh * 3 * sizeof(float));
	c.dst = out.p;
	parallel(rs_h_job, &c, sh);
	parallel(rs_v_job, &c, out.h);
	free(c.tmp);
	free(c.st);
	free(c.cnt);
	free(c.wt);
	free(c.vst);
	free(c.vcnt);
	free(c.vwt);
	return out;
}
static Img resize_u8(const uint8_t *s, int sw, int sh, int tw, int th) { return resize_generic(s, NULL, sw, sh, tw, th); }
static Img resize_f(const Img *s, int tw, int th) { return resize_generic(NULL, s->p, s->w, s->h, tw, th); }

/* ------------------------------------------------------------------ colour helpers */
static float g2l_t[1025], l2g_t[1025];
static void init_luts(void) {
	for (int i = 0; i <= 1024; i++) {
		float v = i / 1024.f;
		g2l_t[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
		float lin = v * v;
		l2g_t[i] = lin <= 0.0031308f ? 12.92f * lin : 1.055f * powf(lin, 1.f / 2.4f) - 0.055f;
	}
}
static inline float to_lin(float v) {
	v = CL01(v) * 1024.f;
	int i = (int)v;
	if (i >= 1024) return g2l_t[1024];
	float f = v - i;
	return g2l_t[i] + (g2l_t[i + 1] - g2l_t[i]) * f;
}
static inline float to_gam(float l) {
	if (l <= 0.f) return 0.f;
	float t = sqrtf(l) * 1024.f;
	if (t >= 1024.f) return 1.f;
	int i = (int)t;
	float f = t - i;
	return l2g_t[i] + (l2g_t[i + 1] - l2g_t[i]) * f;
}
static inline float lum3(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
/* 4096-entry 1D LUT lookup with interpolation (input clamped to 0..1) */
#define LUTN 4096
static inline float lut_at(const float *t, float v) {
	v = CL01(v) * (LUTN - 1);
	int i = (int)v;
	if (i >= LUTN - 1) return t[LUTN - 1];
	float f = v - i;
	return t[i] + (t[i + 1] - t[i]) * f;
}

/* ------------------------------------------------------------------ blurs & guided filter */
typedef struct {
	const float *src;
	float *dst;
	int w, h, ch, r;
} BoxCtx;

static void box_h_job(void *v, int y0, int y1) {
	BoxCtx *c = (BoxCtx *)v;
	int w = c->w, ch = c->ch, r = c->r;
	double inv = 1.0 / (2 * r + 1);
	for (int y = y0; y < y1; y++) {
		const float *s = c->src + (size_t)y * w * ch;
		float *d = c->dst + (size_t)y * w * ch;
		for (int k = 0; k < ch; k++) {
			double sum = 0;
			for (int i = -r; i <= r; i++) sum += s[CLAMP(i, 0, w - 1) * ch + k];
			for (int x = 0; x < w; x++) {
				d[x * ch + k] = (float)(sum * inv);
				int xa = x + r + 1;
				if (xa >= w) xa = w - 1;
				int xr = x - r;
				if (xr < 0) xr = 0;
				sum += (double)s[xa * ch + k] - (double)s[xr * ch + k];
			}
		}
	}
}
#define VB 16
static void box_v_job(void *v, int b0, int b1) {
	BoxCtx *c = (BoxCtx *)v;
	int w = c->w, h = c->h, ch = c->ch, r = c->r;
	double inv = 1.0 / (2 * r + 1);
	for (int b = b0; b < b1; b++) {
		int x0 = b * VB, x1 = MIN(x0 + VB, w), n = (x1 - x0) * ch;
		double acc[VB * 3];
		for (int k = 0; k < n; k++) acc[k] = 0;
		for (int i = -r; i <= r; i++) {
			const float *s = c->src + ((size_t)CLAMP(i, 0, h - 1) * w + x0) * ch;
			for (int k = 0; k < n; k++) acc[k] += s[k];
		}
		for (int y = 0; y < h; y++) {
			float *d = c->dst + ((size_t)y * w + x0) * ch;
			for (int k = 0; k < n; k++) d[k] = (float)(acc[k] * inv);
			const float *sa = c->src + ((size_t)MIN(y + r + 1, h - 1) * w + x0) * ch;
			const float *sr = c->src + ((size_t)MAX(y - r, 0) * w + x0) * ch;
			for (int k = 0; k < n; k++) acc[k] += (double)sa[k] - (double)sr[k];
		}
	}
}
/* box blur of radius r (window 2r+1), clamped edges. src may equal dst. tmp: w*h*ch floats. */
static void box_blur(const float *src, float *dst, int w, int h, int ch, int r, float *tmp) {
	if (r <= 0) {
		if (src != dst) memcpy(dst, src, (size_t)w * h * ch * sizeof(float));
		return;
	}
	BoxCtx c = {src, tmp, w, h, ch, r};
	parallel(box_h_job, &c, h);
	c.src = tmp;
	c.dst = dst;
	parallel(box_v_job, &c, (w + VB - 1) / VB);
}
/* gaussian-like blur (3 box passes), in place. sigma in pixels. */
static void blur_f(float *buf, int w, int h, int ch, float sigma) {
	if (sigma < 0.6f) return;
	float wi = sqrtf(4.f * sigma * sigma + 1.f);
	int r = (int)((wi - 1.f) * 0.5f + 0.5f);
	if (r < 1) r = 1;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *tmp = (float *)xmalloc((size_t)w * h * ch * sizeof(float));
	for (int i = 0; i < 3; i++) box_blur(buf, buf, w, h, ch, r, tmp);
	free(tmp);
}
/* edge-aware smoothing of a 1-channel plane (self-guided filter, He et al.) */
static void guided_self(const float *I, float *out, int w, int h, int r, float eps) {
	size_t n = (size_t)w * h;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *m = (float *)xmalloc(n * sizeof(float)), *a = (float *)xmalloc(n * sizeof(float));
	float *tmp = (float *)xmalloc(n * sizeof(float));
	box_blur(I, m, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) a[i] = I[i] * I[i];
	box_blur(a, a, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) {
		float var = a[i] - m[i] * m[i];
		if (var < 0) var = 0;
		float aa = var / (var + eps);
		a[i] = aa;
		m[i] = m[i] * (1.f - aa);
	}
	box_blur(a, a, w, h, 1, r, tmp);
	box_blur(m, m, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) out[i] = a[i] * I[i] + m[i];
	free(m);
	free(a);
	free(tmp);
}

typedef struct {
	const Img *im;
	float *L;
} LumCtx;
static void lum_job(void *v, int y0, int y1) {
	LumCtx *c = (LumCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		float *l = c->L + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) l[x] = lum3(p[0], p[1], p[2]);
	}
}
static float *lum_plane(const Img *im) {
	float *L = (float *)xmalloc((size_t)im->w * im->h * sizeof(float));
	LumCtx c = {im, L};
	parallel(lum_job, &c, im->h);
	return L;
}
