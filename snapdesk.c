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

/* ------------------------------------------------------------------ tool registry */
enum {
	T_TUNE, T_DETAILS, T_CURVES, T_WB,
	T_CROP, T_ROTATE, T_PERSP, T_EXPAND,
	T_SELECTIVE, T_BRUSH, T_HEAL, T_PORTRAIT, T_VIGNETTE, T_LENS, T_TEXT, T_FRAME,
	T_BW, T_DRAMA, T_TONAL, T_HDR, T_GLOW, T_VINTAGE, T_RETRO, T_GRAIN, T_NOIR, T_GRUNGE,
	T_BLOOM, T_DEHAZE, T_MIXER, T_SPLIT, T_DENOISE, T_CHROMA, T_STREAK, T_GRAD,
	T_HALATION, T_DOUBLE,
	T_COUNT
};
/* parameter indices per tool */
enum { TU_BRI, TU_CON, TU_SAT, TU_AMB, TU_HIG, TU_SHA, TU_WAR };
enum { DE_STR, DE_SHP };
enum { WB_TEMP, WB_TINT };
enum { CR_ASPECT };
enum { RO_ANGLE };
enum { PE_VERT, PE_HORZ, PE_ROT, PE_SCALE };
enum { BR_MODE, BR_STR, BR_SIZE, BR_HARD, BR_FLOW };
enum { HE_SIZE };
enum { PO_SMOOTH, PO_BRIGHT, PO_WARM };
enum { VG_OUTER, VG_INNER, VG_SIZE, VG_FEATHER, VG_ROUND };
enum { LN_SHAPE, LN_BLUR, LN_TRANS, LN_SIZE, LN_ANGLE, LN_VIG };
enum { TX_SIZE, TX_COLOR, TX_OPAC, TX_STYLE, TX_ROT };
enum { FR_STYLE, FR_WIDTH, FR_COLOR, FR_CORNER };
enum { BW_FILTER, BW_TONE, BW_BRI, BW_CON, BW_GRAIN };
enum { DR_STYLE, DR_STR, DR_SAT };
enum { TC_HIGH, TC_MID, TC_LOW, TC_PSH, TC_PHI };
enum { HD_STYLE, HD_STR, HD_BRI, HD_SAT };
enum { GL_STYLE, GL_GLOW, GL_SAT, GL_WARM };
enum { VI_STYLE, VI_BRI, VI_SAT, VI_STR, VI_VIG };
enum { RE_STYLE, RE_BRI, RE_SAT, RE_STR, RE_SCR, RE_LEAK, RE_SEED };
enum { GR_STYLE, GR_GRAIN, GR_STR, GR_SAT };
enum { NO_STYLE, NO_WASH, NO_GRAIN, NO_VIG, NO_BRI };
enum { GU_STYLE, GU_BRI, GU_CON, GU_TEX, GU_SAT, GU_SEED };
enum { BL_AMT, BL_THR, BL_RAD, BL_WARM };
enum { DH_AMT, DH_SAT };
enum { SP_SHUE, SP_SSTR, SP_MHUE, SP_MSTR, SP_HHUE, SP_HSTR, SP_BAL, SP_BLEND };
enum { DN_LUM, DN_COL, DN_DET };
enum { CA_AMT, CA_EDGE };
enum { ST_AMT, ST_THR, ST_LEN, ST_ANG, ST_TINT };
enum { GD_BRI, GD_CON, GD_SAT, GD_WARM, GD_FEA, GD_ANG };
enum { HA_AMT, HA_THR, HA_RAD, HA_TINT };
enum { DX_BLEND, DX_OPAC, DX_SCALE, DX_ROT, DX_BRI };
enum { EX_FILL, EX_ASP, EX_L, EX_T, EX_R, EX_B };

#define MAXP 8
typedef struct {
	const char *name;
	float mn, mx, def;
	const char *ch; /* '|' separated labels => choice parameter (value = index) */
	int dec;        /* decimals shown */
} PDef;
typedef struct {
	const char *name, *group;
	char key;
	int np;
	PDef p[MAXP];
} ToolDef;
#define SL(n, a, b, d) {n, a, b, d, NULL, 0}
#define SD(n, a, b, d) {n, a, b, d, NULL, 1}
#define CHO(n, list) {n, 0, 0, 0, list, 0}

static const ToolDef TD[T_COUNT] = {
	[T_TUNE] = {"Tune Image", "ADJUST", 'T', 7, {SL("Brightness", -100, 100, 0), SL("Contrast", -100, 100, 0), SL("Saturation", -100, 100, 0), SL("Ambiance", -100, 100, 0), SL("Highlights", -100, 100, 0), SL("Shadows", -100, 100, 0), SL("Warmth", -100, 100, 0)}},
	[T_DETAILS] = {"Details", "ADJUST", 'D', 2, {SL("Structure", -100, 100, 0), SL("Sharpening", 0, 100, 0)}},
	[T_CURVES] = {"Curves", "ADJUST", 'V', 0, {{0}}},
	[T_WB] = {"White Balance", "ADJUST", 'W', 2, {SL("Temperature", -100, 100, 0), SL("Tint", -100, 100, 0)}},
	[T_CROP] = {"Crop", "GEOMETRY", 'C', 1, {CHO("Aspect", "Free|Original|1:1|4:5|3:4|2:3|9:16|5:4|4:3|3:2|16:9")}},
	[T_ROTATE] = {"Rotate", "GEOMETRY", 'R', 1, {SD("Straighten", -45, 45, 0)}},
	[T_PERSP] = {"Perspective", "GEOMETRY", 'P', 4, {SL("Vertical", -100, 100, 0), SL("Horizontal", -100, 100, 0), SD("Rotate", -45, 45, 0), SL("Scale", 50, 150, 100)}},
	[T_SELECTIVE] = {"Selective", "RETOUCH", 'S', 0, {{0}}},
	[T_BRUSH] = {"Brush", "RETOUCH", 'B', 5, {CHO("Mode", "Dodge & Burn|Exposure|Temperature|Saturation|Eraser"), SL("Strength", -100, 100, 40), SL("Size", 1, 100, 25), SL("Hardness", 0, 100, 40), SL("Flow", 5, 100, 60)}},
	[T_HEAL] = {"Healing", "RETOUCH", 'H', 1, {SL("Size", 3, 100, 25)}},
	[T_PORTRAIT] = {"Portrait", "RETOUCH", 'O', 3, {SL("Skin Smoothing", 0, 100, 40), SL("Face Brighten", 0, 100, 0), SL("Skin Warmth", -100, 100, 0)}},
	[T_VIGNETTE] = {"Vignette", "RETOUCH", 'G', 5, {SL("Outer Brightness", -100, 100, -50), SL("Inner Brightness", -100, 100, 0), SL("Size", 0, 100, 50), SL("Feather", 0, 100, 60), SL("Roundness", -100, 100, 0)}},
	[T_LENS] = {"Lens Blur", "RETOUCH", 'L', 6, {CHO("Shape", "Elliptical|Linear"), SL("Blur", 0, 100, 50), SL("Transition", 0, 100, 50), SL("Focus Size", 0, 100, 40), SL("Angle", -90, 90, 0), SL("Vignette", 0, 100, 0)}},
	[T_TEXT] = {"Text", "RETOUCH", 'X', 5, {SL("Size", 1, 100, 30), CHO("Color", "White|Black|Red|Yellow|Teal|Blue|Pink"), SL("Opacity", 0, 100, 100), CHO("Style", "Normal|Outline|Shadow|Plate"), SL("Rotation", -180, 180, 0)}},
	[T_FRAME] = {"Frames", "RETOUCH", 'F', 4, {CHO("Style", "Solid|Polaroid|Double|Inset"), SL("Width", 0, 100, 20), CHO("Color", "White|Black|Gray|Cream|Ink|Sand"), SL("Corners", 0, 100, 0)}},
	[T_BW] = {"Black & White", "FILTERS", '1', 5, {CHO("Filter", "Neutral|Red|Orange|Yellow|Green|Blue"), CHO("Tone", "None|Sepia|Cool|Gold|Selenium"), SL("Brightness", -100, 100, 0), SL("Contrast", -100, 100, 20), SL("Grain", 0, 100, 0)}},
	[T_DRAMA] = {"Drama", "FILTERS", '2', 3, {CHO("Style", "Natural|Strong|Bright|Dark"), SL("Strength", 0, 100, 50), SL("Saturation", -100, 100, 0)}},
	[T_TONAL] = {"Tonal Contrast", "FILTERS", '3', 5, {SL("High Tones", -100, 100, 0), SL("Mid Tones", -100, 100, 30), SL("Low Tones", -100, 100, 0), SL("Protect Shadows", 0, 100, 0), SL("Protect Highlights", 0, 100, 0)}},
	[T_HDR] = {"HDR Scape", "FILTERS", '4', 4, {CHO("Style", "Nature|People|Fine|Strong"), SL("Strength", 0, 100, 50), SL("Brightness", -100, 100, 0), SL("Saturation", -100, 100, 0)}},
	[T_GLOW] = {"Glamour Glow", "FILTERS", '5', 4, {CHO("Style", "Soft|Warm|Cool|Bright"), SL("Glow", 0, 100, 40), SL("Saturation", -100, 100, 0), SL("Warmth", -100, 100, 0)}},
	[T_VINTAGE] = {"Vintage", "FILTERS", '6', 5, {CHO("Style", "1|2|3|4|5|6|7|8|9"), SL("Brightness", -100, 100, 0), SL("Saturation", -100, 100, 0), SL("Style Strength", 0, 100, 70), SL("Vignette", 0, 100, 40)}},
	[T_RETRO] = {"Retrolux", "FILTERS", '7', 7, {CHO("Style", "1|2|3|4|5|6"), SL("Brightness", -100, 100, 0), SL("Saturation", -100, 100, 0), SL("Style Strength", 0, 100, 70), SL("Scratches", 0, 100, 25), SL("Light Leaks", 0, 100, 35), SL("Seed", 0, 99, 7)}},
	[T_GRAIN] = {"Grainy Film", "FILTERS", '8', 4, {CHO("Style", "Neutral|Warm|Cool|Faded|B&W|Punchy"), SL("Grain", 0, 100, 40), SL("Style Strength", 0, 100, 60), SL("Saturation", -100, 100, 0)}},
	[T_NOIR] = {"Noir", "FILTERS", '9', 5, {CHO("Style", "Classic|Silver|Harsh|Faded"), SL("Wash", 0, 100, 20), SL("Grain", 0, 100, 30), SL("Vignette", 0, 100, 30), SL("Brightness", -100, 100, 0)}},
	[T_BLOOM] = {"Bloom", "EFFECTS", 'M', 4, {SL("Amount", 0, 100, 50), SL("Threshold", 0, 100, 55), SL("Radius", 0, 100, 40), SL("Warmth", -100, 100, 0)}},
	[T_DEHAZE] = {"Dehaze", "EFFECTS", 'Y', 2, {SL("Amount", -100, 100, 45), SL("Saturation", -100, 100, 10)}},
	[T_MIXER] = {"Color", "EFFECTS", 'N', 0, {{0}}},
	[T_SPLIT] = {"Color Grade", "EFFECTS", 'U', 8, {SL("Shadow Hue", 0, 360, 215), SL("Shadow Strength", 0, 100, 35), SL("Midtone Hue", 0, 360, 60), SL("Midtone Strength", 0, 100, 0), SL("Highlight Hue", 0, 360, 38), SL("Highlight Strength", 0, 100, 35), SL("Balance", -100, 100, 0), SL("Blending", 0, 100, 50)}},
	[T_DENOISE] = {"Denoise", "EFFECTS", 'Q', 3, {SL("Luminance", 0, 100, 40), SL("Color", 0, 100, 60), SL("Preserve Detail", 0, 100, 50)}},
	[T_CHROMA] = {"Chromatic Aberr.", "EFFECTS", 'A', 2, {SL("Amount", -100, 100, 40), SL("Edge Bias", 0, 100, 60)}},
	[T_STREAK] = {"Light Streaks", "EFFECTS", 'K', 5, {SL("Amount", 0, 100, 50), SL("Threshold", 0, 100, 70), SL("Length", 0, 100, 50), SL("Angle", -90, 90, 0), CHO("Tint", "White|Warm|Cyan|Magenta")}},
	[T_GRAD] = {"Gradient Filter", "EFFECTS", 'I', 6, {SL("Brightness", -100, 100, -40), SL("Contrast", -100, 100, 0), SL("Saturation", -100, 100, 0), SL("Warmth", -100, 100, 0), SL("Feather", 0, 100, 50), SL("Angle", -180, 180, 0)}},
	[T_HALATION] = {"Halation", "EFFECTS", 'Z', 4, {SL("Amount", 0, 100, 55), SL("Threshold", 0, 100, 60), SL("Radius", 0, 100, 35), CHO("Tint", "Red|Orange|Pink|Magenta")}},
	[T_DOUBLE] = {"Double Exposure", "EFFECTS", 'E', 5, {CHO("Blend", "Normal|Add|Screen|Multiply|Overlay|Soft Light|Lighten|Darken|Difference"), SL("Opacity", 0, 100, 55), SL("Scale", 20, 300, 100), SL("Rotation", -180, 180, 0), SL("Brightness", -100, 100, 0)}},
	[T_EXPAND] = {"Expand", "GEOMETRY", 'J', 6, {CHO("Fill", "Smart|Mirror|Blur|White|Black|Gray"), CHO("Aspect", "Custom|1:1|4:5|3:2|16:9|9:16"), SL("Left", 0, 50, 0), SL("Top", 0, 50, 0), SL("Right", 0, 50, 0), SL("Bottom", 0, 50, 0)}},
	[T_GRUNGE] = {"Grunge", "FILTERS", '0', 6, {CHO("Style", "1|2|3|4|5"), SL("Brightness", -100, 100, 0), SL("Contrast", -100, 100, 20), SL("Texture", 0, 100, 55), SL("Saturation", -100, 100, -25), SL("Seed", 0, 99, 3)}},
};

static int choice_count(const char *s) {
	int n = 1;
	for (; *s; s++)
		if (*s == '|') n++;
	return n;
}
static void choice_label(const char *s, int idx, char *buf, int cap) {
	int i = 0;
	while (i < idx && *s) {
		if (*s == '|') i++;
		s++;
	}
	int n = 0;
	while (*s && *s != '|' && n < cap - 1) buf[n++] = *s++;
	buf[n] = 0;
}
static inline int choice_max(const PDef *d) { return choice_count(d->ch) - 1; }

/* ------------------------------------------------------------------ layers (edit stack) */
#define MAXCP 12
#define MAXSEL 8
#define MAXLAYERS 48
typedef struct { float x, y; } Pt;
typedef struct {
	int mode;
	float amt, rad, hard, flow;
	int start, n;
} Stroke;
typedef struct { float x, y, r; } Spot;
typedef struct {
	float x, y, rad;
	float v[4]; /* brightness, contrast, saturation, structure (-100..100) */
} SelPt;
typedef struct BrushRT BrushRT;

typedef struct Layer {
	int tool, visible, touched;
	uint32_t ver; /* changes whenever pixels produced by this layer may change */
	float p[MAXP];
	float cx, cy; /* vignette / lens / text centre (normalised) */
	float cr[4];  /* crop rect x0,y0,x1,y1 (normalised) */
	int rot, flipx, flipy;
	int ncp[4];
	float cp[4][MAXCP][2]; /* curves: RGB, R, G, B */
	int nsel;
	SelPt sel[MAXSEL];
	Pt *pts;
	int npts, cpts;
	Stroke *str;
	int nstr, cstr;
	Spot *spots;
	int nspots, cspots;
	float mix[8][3]; /* colour mixer: hue, saturation, luminance per colour band */
	char text[96];
	char path[1024]; /* double exposure: second photo */
	int mask_on, mask_inv; /* per-layer mask: strokes paint where the effect shows */
	Pt *mpts;
	int nmpts, cmpts;
	Stroke *mstr;
	int nmstr, cmstr;
	BrushRT *mrt;
	BrushRT *rt; /* runtime raster cache, never copied */
} Layer;

static uint32_t g_ver = 1;
static inline void layer_bump(Layer *L) { L->ver = ++g_ver; }

static void curves_reset(Layer *L, int c) {
	L->ncp[c] = 2;
	L->cp[c][0][0] = 0;
	L->cp[c][0][1] = 0;
	L->cp[c][1][0] = 1;
	L->cp[c][1][1] = 1;
}
static void layer_params_default(Layer *L) {
	const ToolDef *d = &TD[L->tool];
	for (int i = 0; i < d->np; i++) L->p[i] = d->p[i].def;
}
static void layer_init(Layer *L, int tool) {
	memset(L, 0, sizeof *L);
	L->tool = tool;
	L->visible = 1;
	L->cx = L->cy = 0.5f;
	if (tool == T_TEXT) L->cy = 0.82f;
	if (tool == T_GRAD) L->cy = 0.38f;
	L->cr[0] = L->cr[1] = 0;
	L->cr[2] = L->cr[3] = 1;
	for (int c = 0; c < 4; c++) curves_reset(L, c);
	layer_params_default(L);
	layer_bump(L);
}
static void brt_free(BrushRT *rt);
static void layer_free(Layer *L) {
	free(L->pts);
	free(L->str);
	free(L->spots);
	free(L->mpts);
	free(L->mstr);
	if (L->rt) brt_free(L->rt);
	if (L->mrt) brt_free(L->mrt);
	memset(L, 0, sizeof *L);
}
static void layer_copy(Layer *dst, const Layer *src) {
	*dst = *src;
	dst->rt = NULL;
	dst->mrt = NULL;
	dst->mpts = NULL;
	dst->mstr = NULL;
	dst->pts = NULL;
	dst->str = NULL;
	dst->spots = NULL;
	if (src->npts) {
		dst->pts = (Pt *)xmalloc(sizeof(Pt) * src->npts);
		memcpy(dst->pts, src->pts, sizeof(Pt) * src->npts);
		dst->cpts = src->npts;
	} else
		dst->cpts = 0;
	if (src->nstr) {
		dst->str = (Stroke *)xmalloc(sizeof(Stroke) * src->nstr);
		memcpy(dst->str, src->str, sizeof(Stroke) * src->nstr);
		dst->cstr = src->nstr;
	} else
		dst->cstr = 0;
	if (src->nmpts) {
		dst->mpts = (Pt *)xmalloc(sizeof(Pt) * src->nmpts);
		memcpy(dst->mpts, src->mpts, sizeof(Pt) * src->nmpts);
	}
	dst->cmpts = src->nmpts;
	if (src->nmstr) {
		dst->mstr = (Stroke *)xmalloc(sizeof(Stroke) * src->nmstr);
		memcpy(dst->mstr, src->mstr, sizeof(Stroke) * src->nmstr);
	}
	dst->cmstr = src->nmstr;
	if (src->nspots) {
		dst->spots = (Spot *)xmalloc(sizeof(Spot) * src->nspots);
		memcpy(dst->spots, src->spots, sizeof(Spot) * src->nspots);
		dst->cspots = src->nspots;
	} else
		dst->cspots = 0;
}
static void layer_add_pt(Layer *L, float x, float y) {
	if (L->npts >= L->cpts) {
		L->cpts = L->cpts ? L->cpts * 2 : 256;
		L->pts = (Pt *)xrealloc(L->pts, sizeof(Pt) * L->cpts);
	}
	L->pts[L->npts].x = x;
	L->pts[L->npts].y = y;
	L->npts++;
}
static Stroke *layer_add_stroke(Layer *L) {
	if (L->nstr >= L->cstr) {
		L->cstr = L->cstr ? L->cstr * 2 : 16;
		L->str = (Stroke *)xrealloc(L->str, sizeof(Stroke) * L->cstr);
	}
	Stroke *s = &L->str[L->nstr++];
	memset(s, 0, sizeof *s);
	s->start = L->npts;
	return s;
}
static void layer_add_mpt(Layer *L, float x, float y) {
	if (L->nmpts >= L->cmpts) {
		L->cmpts = L->cmpts ? L->cmpts * 2 : 256;
		L->mpts = (Pt *)xrealloc(L->mpts, sizeof(Pt) * L->cmpts);
	}
	L->mpts[L->nmpts].x = x;
	L->mpts[L->nmpts].y = y;
	L->nmpts++;
}
static Stroke *layer_add_mstroke(Layer *L) {
	if (L->nmstr >= L->cmstr) {
		L->cmstr = L->cmstr ? L->cmstr * 2 : 16;
		L->mstr = (Stroke *)xrealloc(L->mstr, sizeof(Stroke) * L->cmstr);
	}
	Stroke *s = &L->mstr[L->nmstr++];
	memset(s, 0, sizeof *s);
	s->start = L->nmpts;
	return s;
}
static void layer_add_spot(Layer *L, float x, float y, float r) {
	if (L->nspots >= L->cspots) {
		L->cspots = L->cspots ? L->cspots * 2 : 16;
		L->spots = (Spot *)xrealloc(L->spots, sizeof(Spot) * L->cspots);
	}
	L->spots[L->nspots].x = x;
	L->spots[L->nspots].y = y;
	L->spots[L->nspots].r = r;
	L->nspots++;
}
static int layer_has_data(const Layer *L) {
	switch (L->tool) {
	case T_SELECTIVE: return L->nsel > 0;
	case T_BRUSH: return L->nstr > 0;
	case T_HEAL: return L->nspots > 0;
	case T_TEXT: return L->text[0] != 0;
	case T_DOUBLE: return L->path[0] != 0;
	default: return 1;
	}
}

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

/* ------------------------------------------------------------------ Geometry: crop / rotate / perspective */
static void apply_crop(const Layer *L, Img *im) {
	int w = im->w, h = im->h;
	int x0 = (int)roundf(L->cr[0] * w), y0 = (int)roundf(L->cr[1] * h);
	int x1 = (int)roundf(L->cr[2] * w), y1 = (int)roundf(L->cr[3] * h);
	x0 = CLAMP(x0, 0, w - 2);
	y0 = CLAMP(y0, 0, h - 2);
	x1 = CLAMP(x1, x0 + 2, w);
	y1 = CLAMP(y1, y0 + 2, h);
	if (x0 == 0 && y0 == 0 && x1 == w && y1 == h) return;
	Img o = img_new(x1 - x0, y1 - y0);
	for (int y = y0; y < y1; y++) memcpy(o.p + (size_t)(y - y0) * o.w * 3, im->p + ((size_t)y * w + x0) * 3, (size_t)o.w * 3 * sizeof(float));
	img_free(im);
	*im = o;
}
static Img rot90cw(const Img *s) {
	Img o = img_new(s->h, s->w);
	for (int y = 0; y < s->h; y++)
		for (int x = 0; x < s->w; x++) memcpy(o.p + ((size_t)x * o.w + (s->h - 1 - y)) * 3, s->p + ((size_t)y * s->w + x) * 3, 3 * sizeof(float));
	return o;
}
static void flip_h(Img *im) {
	for (int y = 0; y < im->h; y++) {
		float *r = im->p + (size_t)y * im->w * 3;
		for (int x = 0; x < im->w / 2; x++)
			for (int k = 0; k < 3; k++) {
				float t = r[x * 3 + k];
				r[x * 3 + k] = r[(im->w - 1 - x) * 3 + k];
				r[(im->w - 1 - x) * 3 + k] = t;
			}
	}
}
static void flip_v(Img *im) {
	size_t rb = (size_t)im->w * 3;
	float *t = (float *)xmalloc(rb * sizeof(float));
	for (int y = 0; y < im->h / 2; y++) {
		float *a = im->p + y * rb, *b = im->p + (size_t)(im->h - 1 - y) * rb;
		memcpy(t, a, rb * sizeof(float));
		memcpy(a, b, rb * sizeof(float));
		memcpy(b, t, rb * sizeof(float));
	}
	free(t);
}
typedef struct {
	const Img *in;
	Img *out;
	float a, b, c, d; /* source offset from centre = [a b; c d] * (out offset from centre) */
} WarpCtx;
static void warp_job(void *v, int y0, int y1) {
	WarpCtx *c = (WarpCtx *)v;
	float cx = c->in->w * 0.5f, cy = c->in->h * 0.5f, ox = c->out->w * 0.5f, oy = c->out->h * 0.5f;
	for (int y = y0; y < y1; y++) {
		float *d = c->out->p + (size_t)y * c->out->w * 3;
		for (int x = 0; x < c->out->w; x++, d += 3) {
			float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
			bilin(c->in, cx + c->a * dx + c->b * dy, cy + c->c * dx + c->d * dy, d);
		}
	}
}
static void apply_rotate(const Layer *L, Img *im) {
	for (int i = 0; i < (L->rot & 3); i++) {
		Img t = rot90cw(im);
		img_free(im);
		*im = t;
	}
	if (L->flipx) flip_h(im);
	if (L->flipy) flip_v(im);
	float ang = L->p[RO_ANGLE];
	if (fabsf(ang) < 0.01f) return;
	float th = ang * PI_F / 180.f, cs = cosf(th), sn = sinf(th);
	float w = (float)im->w, h = (float)im->h;
	float s = MIN(w / (w * fabsf(cs) + h * fabsf(sn)), h / (w * fabsf(sn) + h * fabsf(cs)));
	Img o = img_new(im->w, im->h);
	WarpCtx c = {im, &o, s * cs, s * sn, -s * sn, s * cs};
	parallel(warp_job, &c, o.h);
	img_free(im);
	*im = o;
}
typedef struct {
	const Img *in;
	Img *out;
	float kv, kh, cs, sn, fs;
} PersCtx;
static void persp_map(const PersCtx *c, float x, float y, float w, float h, float *sx, float *sy) {
	float m = MAX(w, h) * 0.5f, u = (x - w * 0.5f) / m, v = (y - h * 0.5f) / m;
	float ur = u * c->cs - v * c->sn, vr = u * c->sn + v * c->cs;
	float den = 1.f - c->kv * vr - c->kh * ur;
	if (den < 0.25f) den = 0.25f;
	float f = c->fs / den;
	*sx = w * 0.5f + ur * f * m;
	*sy = h * 0.5f + vr * f * m;
}
static void pers_job(void *v, int y0, int y1) {
	PersCtx *c = (PersCtx *)v;
	float w = (float)c->out->w, h = (float)c->out->h;
	for (int y = y0; y < y1; y++) {
		float *d = c->out->p + (size_t)y * c->out->w * 3;
		for (int x = 0; x < c->out->w; x++, d += 3) {
			float sx, sy;
			persp_map(c, x + 0.5f, y + 0.5f, w, h, &sx, &sy);
			bilin(c->in, sx, sy, d);
		}
	}
}
static void apply_persp(const Layer *L, Img *im) {
	float vt = L->p[PE_VERT], hz = L->p[PE_HORZ], rt = L->p[PE_ROT], sc = L->p[PE_SCALE];
	if (vt == 0.f && hz == 0.f && rt == 0.f && sc == 100.f) return;
	float th = rt * PI_F / 180.f;
	PersCtx c = {im, NULL, vt / 100.f * 0.45f, hz / 100.f * 0.45f, cosf(th), sinf(th), 1.f};
	float w = (float)im->w, h = (float)im->h, fmax_ = 0.f;
	for (int i = 0; i <= 64; i++) {
		float t = i / 64.f, sx, sy;
		float pts[4][2] = {{t * w, 0}, {t * w, h}, {0, t * h}, {w, t * h}};
		for (int k = 0; k < 4; k++) {
			persp_map(&c, pts[k][0], pts[k][1], w, h, &sx, &sy);
			float rx = fabsf(sx - w * 0.5f) / (w * 0.5f), ry = fabsf(sy - h * 0.5f) / (h * 0.5f);
			fmax_ = MAX(fmax_, MAX(rx, ry));
		}
	}
	float autof = 1.f / MAX(1.f, fmax_);
	c.fs = autof * 100.f / MAX(sc, 1.f);
	Img o = img_new(im->w, im->h);
	c.out = &o;
	parallel(pers_job, &c, o.h);
	img_free(im);
	*im = o;
}

/* ------------------------------------------------------------------ Selective (control points) */
typedef struct {
	Img *im;
	const float *Y, *Bl;
	int x0, x1, y0;
	float px, py, R, cr, cg, cb, cL;
	float br, co, sa, st;
} SelCtx;
static void sel_job(void *v, int a, int b) {
	SelCtx *c = (SelCtx *)v;
	int w = c->im->w;
	float inv = 1.f / c->R;
	for (int yy = a; yy < b; yy++) {
		int y = c->y0 + yy;
		float *p = c->im->p + ((size_t)y * w + c->x0) * 3;
		for (int x = c->x0; x < c->x1; x++, p += 3) {
			float dx = x + 0.5f - c->px, dy = y + 0.5f - c->py;
			float d = sqrtf(dx * dx + dy * dy) * inv;
			if (d >= 1.f) continue;
			float sp = 1.f - smoothf(0.f, 1.f, d);
			float dc = fabsf(p[0] - c->cr) + fabsf(p[1] - c->cg) + fabsf(p[2] - c->cb);
			float sim = expf(-dc * dc / (2.f * 0.2f * 0.2f));
			float wg = sp * (0.12f + 0.88f * sim);
			if (wg < 0.002f) continue;
			float r = p[0], g = p[1], bl = p[2];
			if (c->br != 0.f) {
				float k = exp2f(c->br * wg * 0.9f);
				r *= k;
				g *= k;
				bl *= k;
			}
			if (c->co != 0.f) {
				float k = 1.f + c->co * wg * 0.9f;
				r = c->cL + (r - c->cL) * k;
				g = c->cL + (g - c->cL) * k;
				bl = c->cL + (bl - c->cL) * k;
			}
			float L = lum3(r, g, bl);
			if (c->sa != 0.f) {
				float k = 1.f + c->sa * wg;
				r = L + (r - L) * k;
				g = L + (g - L) * k;
				bl = L + (bl - L) * k;
			}
			if (c->st != 0.f && c->Y) {
				size_t i = (size_t)y * w + x;
				float dL = c->st * wg * 1.7f * (c->Y[i] - c->Bl[i]);
				if (L > 0.004f) {
					float k = CLAMP((L + dL) / L, 0.f, 4.f);
					r *= k;
					g *= k;
					bl *= k;
				} else {
					r += dL;
					g += dL;
					bl += dL;
				}
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(bl);
		}
	}
}
static void apply_selective(const Layer *L, Img *im) {
	if (L->nsel == 0) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	float *Y = NULL, *Bl = NULL;
	int needs = 0;
	for (int i = 0; i < L->nsel; i++)
		if (L->sel[i].v[3] != 0.f) needs = 1;
	if (needs) {
		Y = lum_plane(im);
		Bl = (float *)xmalloc((size_t)w * h * sizeof(float));
		memcpy(Bl, Y, (size_t)w * h * sizeof(float));
		blur_f(Bl, w, h, 1, MAX(1.f, m * 0.004f));
	}
	SelCtx c[MAXSEL];
	for (int i = 0; i < L->nsel; i++) { /* sample all centre colours first (from the untouched image) */
		const SelPt *s = &L->sel[i];
		SelCtx *q = &c[i];
		q->px = s->x * w;
		q->py = s->y * h;
		float sr = 0, sg = 0, sb = 0;
		int n = 0;
		for (int dy = -2; dy <= 2; dy++)
			for (int dx = -2; dx <= 2; dx++) {
				int xx = CLAMP((int)q->px + dx, 0, w - 1), yy = CLAMP((int)q->py + dy, 0, h - 1);
				const float *p = im->p + ((size_t)yy * w + xx) * 3;
				sr += p[0];
				sg += p[1];
				sb += p[2];
				n++;
			}
		q->cr = sr / n;
		q->cg = sg / n;
		q->cb = sb / n;
		q->cL = lum3(q->cr, q->cg, q->cb);
	}
	for (int i = 0; i < L->nsel; i++) {
		const SelPt *s = &L->sel[i];
		SelCtx *q = &c[i];
		q->im = im;
		q->Y = Y;
		q->Bl = Bl;
		q->R = MAX(4.f, s->rad * m);
		q->x0 = MAX(0, (int)(q->px - q->R));
		q->x1 = MIN(w, (int)(q->px + q->R) + 1);
		q->y0 = MAX(0, (int)(q->py - q->R));
		int y1 = MIN(h, (int)(q->py + q->R) + 1);
		q->br = s->v[0] / 100.f;
		q->co = s->v[1] / 100.f;
		q->sa = s->v[2] / 100.f;
		q->st = s->v[3] / 100.f;
		if (q->br == 0 && q->co == 0 && q->sa == 0 && q->st == 0) continue;
		if (y1 > q->y0 && q->x1 > q->x0) parallel(sel_job, q, y1 - q->y0);
	}
	free(Y);
	free(Bl);
}

/* ------------------------------------------------------------------ Brush (dodge/burn, exposure, temperature, saturation) */
struct BrushRT {
	int w, h;
	float *map[4];
	int s_idx, p_idx;
	float carry;
};
static void brt_free(BrushRT *rt) {
	for (int i = 0; i < 4; i++) free(rt->map[i]);
	free(rt);
}
static void brt_stamp(BrushRT *rt, const Stroke *s, float cx, float cy, float R) {
	int x0 = MAX(0, (int)(cx - R)), x1 = MIN(rt->w - 1, (int)(cx + R) + 1);
	int y0 = MAX(0, (int)(cy - R)), y1 = MIN(rt->h - 1, (int)(cy + R) + 1);
	float hard = CLAMP(s->hard, 0.f, 0.98f), inv = 1.f / R;
	int erase = s->mode >= 4;
	if (!erase && !rt->map[s->mode]) rt->map[s->mode] = (float *)xcalloc((size_t)rt->w * rt->h, sizeof(float));
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float t = sqrtf(dx * dx + dy * dy) * inv;
			if (t >= 1.f) continue;
			float wgt = 1.f;
			if (t > hard) {
				float u = (t - hard) / (1.f - hard);
				wgt = 1.f - u * u * (3.f - 2.f * u);
			}
			float a = wgt * s->flow;
			size_t i = (size_t)y * rt->w + x;
			if (erase) {
				for (int m = 0; m < 4; m++)
					if (rt->map[m]) rt->map[m][i] *= 1.f - a;
			} else
				rt->map[s->mode][i] += (s->amt - rt->map[s->mode][i]) * a;
		}
}
static void brt_raster(BrushRT *rt, const Stroke *strokes, int nstr, const Pt *pts) {
	float m = (float)MIN(rt->w, rt->h);
	for (int s = rt->s_idx; s < nstr; s++) {
		const Stroke *st = &strokes[s];
		int p0 = (s == rt->s_idx) ? rt->p_idx : 0;
		float R = MAX(1.5f, st->rad * m), spacing = MAX(1.f, R * 0.2f);
		for (int p = p0; p < st->n; p++) {
			Pt a = pts[st->start + p];
			float ax = a.x * rt->w, ay = a.y * rt->h;
			if (p == 0) {
				brt_stamp(rt, st, ax, ay, R);
				rt->carry = 0;
				continue;
			}
			Pt b = pts[st->start + p - 1];
			float bx = b.x * rt->w, by = b.y * rt->h, dx = ax - bx, dy = ay - by;
			float len = sqrtf(dx * dx + dy * dy);
			if (len < 1e-4f) continue;
			float t = spacing - rt->carry;
			while (t <= len) {
				brt_stamp(rt, st, bx + dx * (t / len), by + dy * (t / len), R);
				t += spacing;
			}
			rt->carry = len - (t - spacing);
		}
		rt->s_idx = s;
		rt->p_idx = st->n;
		if (s < nstr - 1) {
			rt->s_idx = s + 1;
			rt->p_idx = 0;
			rt->carry = 0;
		}
	}
}
typedef struct {
	Img *im;
	const float *map;
	int mode;
} BrushCtx;
static void brush_job(void *v, int y0, int y1) {
	BrushCtx *c = (BrushCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *m = c->map + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float A = m[x];
			if (fabsf(A) < 0.003f) continue;
			float r = p[0], g = p[1], b = p[2];
			switch (c->mode) {
			case 0: { /* dodge & burn: gamma curve */
				float e = exp2f(-A * 1.1f);
				r = powf(CL01(r), e);
				g = powf(CL01(g), e);
				b = powf(CL01(b), e);
			} break;
			case 1: { /* exposure in linear light */
				float k = exp2f(A * 1.6f);
				r = to_gam(to_lin(r) * k);
				g = to_gam(to_lin(g) * k);
				b = to_gam(to_lin(b) * k);
			} break;
			case 2:
				r *= 1.f + 0.25f * A;
				b *= 1.f - 0.25f * A;
				break;
			default: {
				float L = lum3(r, g, b), k = 1.f + A;
				r = L + (r - L) * k;
				g = L + (g - L) * k;
				b = L + (b - L) * k;
			}
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_brush(Layer *L, Img *im) {
	if (L->nstr == 0) return;
	BrushRT *rt = L->rt;
	if (rt && (rt->w != im->w || rt->h != im->h)) {
		brt_free(rt);
		rt = L->rt = NULL;
	}
	if (!rt) {
		rt = L->rt = (BrushRT *)xcalloc(1, sizeof(BrushRT));
		rt->w = im->w;
		rt->h = im->h;
	}
	brt_raster(rt, L->str, L->nstr, L->pts);
	for (int m = 0; m < 4; m++) {
		if (!rt->map[m]) continue;
		BrushCtx c = {im, rt->map[m], m};
		parallel(brush_job, &c, im->h);
	}
}

/* ------------------------------------------------------------------ Healing (patch copy with colour match) */
static void heal_spot(Img *im, float cx, float cy, float R) {
	int w = im->w, h = im->h;
	float Rout = R * 1.45f;
	enum { NA = 24 };
	float rr[2] = {R * 1.15f, R * 1.32f};
	float T[2 * NA][3], S[2 * NA][3];
	float offs[2 * NA][2];
	for (int k = 0; k < 2; k++)
		for (int a = 0; a < NA; a++) {
			float an = a * (2.f * PI_F / NA) + k * 0.13f;
			offs[k * NA + a][0] = cosf(an) * rr[k];
			offs[k * NA + a][1] = sinf(an) * rr[k];
		}
	for (int i = 0; i < 2 * NA; i++) bilin(im, cx + offs[i][0], cy + offs[i][1], T[i]);
	float best = 1e30f, bsx = 0, bsy = 0, bmean[3] = {0, 0, 0};
	int found = 0;
	static const float dist[3] = {2.4f, 3.5f, 5.0f};
	for (int di = 0; di < 3; di++)
		for (int ai = 0; ai < 20; ai++) {
			float an = ai * (2.f * PI_F / 20.f) + di * 0.31f;
			float sx = cx + cosf(an) * dist[di] * R, sy = cy + sinf(an) * dist[di] * R;
			if (sx - Rout < 0 || sy - Rout < 0 || sx + Rout >= w || sy + Rout >= h) continue;
			float ssd = 0, mean[3] = {0, 0, 0}, mt[3] = {0, 0, 0};
			for (int i = 0; i < 2 * NA; i++) {
				bilin(im, sx + offs[i][0], sy + offs[i][1], S[i]);
				for (int k = 0; k < 3; k++) {
					mean[k] += S[i][k];
					mt[k] += T[i][k];
				}
			}
			for (int k = 0; k < 3; k++) {
				mean[k] /= 2 * NA;
				mt[k] /= 2 * NA;
			}
			for (int i = 0; i < 2 * NA; i++)
				for (int k = 0; k < 3; k++) {
					float d = (T[i][k] - mt[k]) - (S[i][k] - mean[k]);
					ssd += d * d;
				}
			ssd *= 1.f + 0.04f * di;
			if (ssd < best) {
				best = ssd;
				bsx = sx;
				bsy = sy;
				found = 1;
				for (int k = 0; k < 3; k++) bmean[k] = mt[k] - mean[k];
			}
		}
	int x0 = MAX(0, (int)(cx - R) - 1), x1 = MIN(w - 1, (int)(cx + R) + 1);
	int y0 = MAX(0, (int)(cy - R) - 1), y1 = MIN(h - 1, (int)(cy + R) + 1);
	float mt[3] = {0, 0, 0};
	for (int i = 0; i < 2 * NA; i++)
		for (int k = 0; k < 3; k++) mt[k] += T[i][k] / (2 * NA);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			if (d >= R) continue;
			float a = 1.f - smoothf(R * 0.55f, R, d);
			float s[3];
			float *p = im->p + ((size_t)y * w + x) * 3;
			if (found) {
				bilin(im, bsx + dx, bsy + dy, s);
				for (int k = 0; k < 3; k++) s[k] += bmean[k];
			} else
				for (int k = 0; k < 3; k++) s[k] = mt[k];
			for (int k = 0; k < 3; k++) p[k] = CL01(LERP(p[k], s[k], a));
		}
}
static void apply_heal(const Layer *L, Img *im) {
	float m = (float)MIN(im->w, im->h);
	for (int i = 0; i < L->nspots; i++) heal_spot(im, L->spots[i].x * im->w, L->spots[i].y * im->h, MAX(3.f, L->spots[i].r * m));
}

/* ------------------------------------------------------------------ Vignette */
typedef struct {
	Img *im;
	float cx, cy, og, ig, inner, feather, round, dmax;
} VigCtx;
static void vign_job(void *v, int y0, int y1) {
	VigCtx *c = (VigCtx *)v;
	int w = c->im->w, h = c->im->h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float dx = x + 0.5f - c->cx, dy = y + 0.5f - c->cy;
			float e = sqrtf((dx / (w * 0.5f)) * (dx / (w * 0.5f)) + (dy / (h * 0.5f)) * (dy / (h * 0.5f))) * 0.7071f;
			float ci = sqrtf(dx * dx + dy * dy) / c->dmax;
			float d = LERP(e, ci, c->round);
			float t = smoothf(c->inner, c->inner + c->feather, d);
			float g = LERP(c->ig, c->og, t);
			p[0] = CL01(p[0] * g);
			p[1] = CL01(p[1] * g);
			p[2] = CL01(p[2] * g);
		}
	}
}
static inline float bright_gain(float b) { return b < 0 ? 1.f + b * 0.96f : 1.f + b * 0.9f; }
static void apply_vignette(const Layer *L, Img *im) {
	if (L->p[VG_OUTER] == 0.f && L->p[VG_INNER] == 0.f) return;
	VigCtx c;
	c.im = im;
	c.cx = L->cx * im->w;
	c.cy = L->cy * im->h;
	c.og = bright_gain(L->p[VG_OUTER] / 100.f);
	c.ig = bright_gain(L->p[VG_INNER] / 100.f);
	c.inner = L->p[VG_SIZE] / 100.f * 0.85f;
	c.feather = 0.08f + L->p[VG_FEATHER] / 100.f * 0.9f;
	c.round = (L->p[VG_ROUND] + 100.f) / 200.f;
	c.dmax = 0.5f * sqrtf((float)im->w * im->w + (float)im->h * im->h);
	parallel(vign_job, &c, im->h);
}

/* ------------------------------------------------------------------ Lens blur */
typedef struct {
	float *mask;
	int w, h;
	float cx, cy, a, b, cs, sn, tw;
	int linear;
	const Img *im;
	Img *out;
	float wk[4];
	int lvl;
} LensCtx;
static void lens_mask_job(void *v, int y0, int y1) {
	LensCtx *c = (LensCtx *)v;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->cx, dy = y + 0.5f - c->cy;
			float u = dx * c->cs + dy * c->sn, vv = -dx * c->sn + dy * c->cs, d;
			if (c->linear)
				d = fabsf(vv) / c->b;
			else
				d = sqrtf(u * u / (c->a * c->a) + vv * vv / (c->b * c->b));
			c->mask[(size_t)y * c->w + x] = smoothf(1.f, 1.f + c->tw, d);
		}
}
static void lens_acc_job(void *v, int y0, int y1) {
	LensCtx *c = (LensCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *o = c->out->p + (size_t)y * c->w * 3;
		const float *s = c->im->p + (size_t)y * c->w * 3;
		const float *m = c->mask + (size_t)y * c->w;
		for (int x = 0; x < c->w; x++) {
			float f = m[x] * 3.f;
			float wgt = 1.f - fabsf(f - (float)c->lvl);
			if (wgt <= 0.f) continue;
			o[x * 3] += wgt * s[x * 3];
			o[x * 3 + 1] += wgt * s[x * 3 + 1];
			o[x * 3 + 2] += wgt * s[x * 3 + 2];
		}
	}
}
static void apply_lens(const Layer *L, Img *im) {
	float bl = L->p[LN_BLUR] / 100.f, vg = L->p[LN_VIG] / 100.f;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	if (bl > 0.f) {
		LensCtx c;
		memset(&c, 0, sizeof c);
		c.w = w;
		c.h = h;
		c.cx = L->cx * w;
		c.cy = L->cy * h;
		float rf = (0.08f + L->p[LN_SIZE] / 100.f * 0.55f) * m;
		c.a = rf * 1.35f;
		c.b = rf * 0.85f;
		float th = L->p[LN_ANGLE] * PI_F / 180.f;
		c.cs = cosf(th);
		c.sn = sinf(th);
		c.tw = 0.12f + L->p[LN_TRANS] / 100.f * 1.1f;
		c.linear = (int)L->p[LN_SHAPE] == 1;
		c.mask = (float *)xmalloc((size_t)w * h * sizeof(float));
		parallel(lens_mask_job, &c, h);
		Img out = img_new(w, h), tmp = img_new(w, h);
		memset(out.p, 0, (size_t)w * h * 3 * sizeof(float));
		c.im = im;
		c.out = &out;
		float Rmax = bl * 0.03f * m;
		for (int lvl = 0; lvl <= 3; lvl++) {
			c.lvl = lvl;
			if (lvl == 0) {
				parallel(lens_acc_job, &c, h);
				continue;
			}
			memcpy(tmp.p, im->p, (size_t)w * h * 3 * sizeof(float));
			/* bright pixels bloom a little, like real bokeh */
			size_t n = (size_t)w * h * 3;
			for (size_t i = 0; i < n; i++) tmp.p[i] += 0.6f * MAX(0.f, tmp.p[i] - 0.85f);
			blur_f(tmp.p, w, h, 3, MAX(0.8f, Rmax * lvl / 3.f * 0.7f));
			c.im = &tmp;
			parallel(lens_acc_job, &c, h);
			c.im = im;
		}
		img_free(&tmp);
		free(c.mask);
		img_free(im);
		*im = out;
	}
	if (vg > 0.f) add_vignette(im, -vg * 0.7f, 0.35f);
	img_clamp(im);
}

/* ------------------------------------------------------------------ Portrait (skin smoothing, face brightening) */
typedef struct {
	const Img *im;
	float *mask;
} SkinCtx;
static void skin_job(void *v, int y0, int y1) {
	SkinCtx *c = (SkinCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float r = p[0] * 255.f, g = p[1] * 255.f, b = p[2] * 255.f;
			float Y = 0.299f * r + 0.587f * g + 0.114f * b;
			float cb = 128.f - 0.168736f * r - 0.331264f * g + 0.5f * b;
			float cr = 128.f + 0.5f * r - 0.418688f * g - 0.081312f * b;
			float m = smoothf(70.f, 82.f, cb) * (1.f - smoothf(122.f, 134.f, cb)) * smoothf(126.f, 138.f, cr) * (1.f - smoothf(168.f, 180.f, cr)) * smoothf(30.f, 70.f, Y);
			if (r < g || r < b * 0.95f) m *= 0.3f;
			c->mask[(size_t)y * w + x] = m;
		}
	}
}
typedef struct {
	Img *im;
	const Img *sm;
	const float *mask;
	float amt, bri, warm;
} PortCtx;
static void port_job(void *v, int y0, int y1) {
	PortCtx *c = (PortCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *s = c->sm->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, s += 3) {
			float m = c->mask[(size_t)y * w + x];
			if (m < 0.003f) continue;
			float a = m * c->amt;
			float r = LERP(p[0], s[0] + 0.12f * (p[0] - s[0]), a);
			float g = LERP(p[1], s[1] + 0.12f * (p[1] - s[1]), a);
			float b = LERP(p[2], s[2] + 0.12f * (p[2] - s[2]), a);
			float k = 1.f + c->bri * m * 0.45f;
			r *= k;
			g *= k;
			b *= k;
			r *= 1.f + c->warm * m * 0.12f;
			b *= 1.f - c->warm * m * 0.12f;
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_portrait(const Layer *L, Img *im) {
	float sm = L->p[PO_SMOOTH] / 100.f, br = L->p[PO_BRIGHT] / 100.f, wa = L->p[PO_WARM] / 100.f;
	if (sm == 0.f && br == 0.f && wa == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *mask = (float *)xmalloc(n * sizeof(float));
	SkinCtx sc = {im, mask};
	parallel(skin_job, &sc, h);
	blur_f(mask, w, h, 1, MAX(1.f, m * 0.004f));
	Img smooth = img_new(w, h);
	if (sm > 0.f) {
		float *pl = (float *)xmalloc(n * sizeof(float)), *po = (float *)xmalloc(n * sizeof(float));
		int r = MAX(2, (int)(m * 0.007f));
		float eps = 0.0015f + 0.02f * sm * sm;
		for (int ch = 0; ch < 3; ch++) {
			for (size_t i = 0; i < n; i++) pl[i] = im->p[i * 3 + ch];
			guided_self(pl, po, w, h, r, eps);
			for (size_t i = 0; i < n; i++) smooth.p[i * 3 + ch] = po[i];
		}
		free(pl);
		free(po);
	} else
		memcpy(smooth.p, im->p, n * 3 * sizeof(float));
	PortCtx c = {im, &smooth, mask, MIN(1.f, sm * 1.15f), br, wa};
	parallel(port_job, &c, h);
	img_free(&smooth);
	free(mask);
}

/* ------------------------------------------------------------------ Text */
static const float TEXT_COL[7][3] = {{1, 1, 1}, {0.05f, 0.05f, 0.05f}, {0.93f, 0.2f, 0.2f}, {1, 0.86f, 0.15f}, {0.15f, 0.8f, 0.72f}, {0.25f, 0.5f, 0.95f}, {1, 0.45f, 0.7f}};
static float *text_mask(const char *s, float px, int *ow, int *oh, int pad) {
	float sc = stbtt_ScaleForPixelHeight(&font_med, px);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&font_med, &asc, &desc, &gap);
	float width = 0;
	for (const char *t = s; *t; t++) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&font_med, (unsigned char)*t, &adv, &lsb);
		width += adv * sc;
		if (t[1]) width += stbtt_GetCodepointKernAdvance(&font_med, (unsigned char)*t, (unsigned char)t[1]) * sc;
	}
	int W = (int)ceilf(width) + pad * 2 + 2, H = (int)ceilf((asc - desc) * sc) + pad * 2 + 2;
	float *m = (float *)xcalloc((size_t)W * H, sizeof(float));
	float xp = (float)pad;
	int base = pad + (int)ceilf(asc * sc);
	for (const char *t = s; *t; t++) {
		int cp = (unsigned char)*t, x0, y0, x1, y1, adv, lsb;
		stbtt_GetCodepointBitmapBox(&font_med, cp, sc, sc, &x0, &y0, &x1, &y1);
		int bw = x1 - x0, bh = y1 - y0;
		if (bw > 0 && bh > 0) {
			unsigned char *bm = (unsigned char *)xmalloc((size_t)bw * bh);
			stbtt_MakeCodepointBitmap(&font_med, bm, bw, bh, bw, sc, sc, cp);
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++) {
					int dx = (int)xp + x0 + x, dy = base + y0 + y;
					if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
					float a = bm[y * bw + x] / 255.f;
					float *d = &m[(size_t)dy * W + dx];
					*d = *d + a - *d * a;
				}
			free(bm);
		}
		stbtt_GetCodepointHMetrics(&font_med, cp, &adv, &lsb);
		xp += adv * sc;
		if (t[1]) xp += stbtt_GetCodepointKernAdvance(&font_med, cp, (unsigned char)t[1]) * sc;
	}
	*ow = W;
	*oh = H;
	return m;
}
static inline float mask_at(const float *m, int W, int H, float x, float y) {
	x -= 0.5f;
	y -= 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	float v[4];
	for (int i = 0; i < 4; i++) {
		int xx = x0 + (i & 1), yy = y0 + (i >> 1);
		v[i] = (xx < 0 || yy < 0 || xx >= W || yy >= H) ? 0.f : m[(size_t)yy * W + xx];
	}
	return LERP(LERP(v[0], v[1], fx), LERP(v[2], v[3], fx), fy);
}
static void dilate(float *m, int W, int H, int r) {
	if (r < 1) return;
	float *t = (float *)xmalloc((size_t)W * H * sizeof(float));
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float mx = 0;
			for (int k = -r; k <= r; k++) mx = MAX(mx, m[(size_t)y * W + CLAMP(x + k, 0, W - 1)]);
			t[(size_t)y * W + x] = mx;
		}
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float mx = 0;
			for (int k = -r; k <= r; k++) mx = MAX(mx, t[(size_t)CLAMP(y + k, 0, H - 1) * W + x]);
			m[(size_t)y * W + x] = mx;
		}
	free(t);
}
static void apply_text(const Layer *L, Img *im) {
	if (!L->text[0]) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	float px = MAX(6.f, L->p[TX_SIZE] / 100.f * 0.34f * m);
	int pad = (int)ceilf(px * 0.3f), W, H;
	float *mk = text_mask(L->text, px, &W, &H, pad);
	int style = (int)L->p[TX_STYLE];
	const float *col = TEXT_COL[CLAMP((int)L->p[TX_COLOR], 0, 6)];
	float opac = L->p[TX_OPAC] / 100.f;
	float lumc = lum3(col[0], col[1], col[2]);
	float alt[3] = {lumc > 0.5f ? 0.05f : 1.f, lumc > 0.5f ? 0.05f : 1.f, lumc > 0.5f ? 0.05f : 1.f};
	float *under = NULL;
	if (style == 1) {
		under = (float *)xmalloc((size_t)W * H * sizeof(float));
		memcpy(under, mk, (size_t)W * H * sizeof(float));
		dilate(under, W, H, MAX(1, (int)(px * 0.07f)));
	} else if (style == 2) {
		under = (float *)xmalloc((size_t)W * H * sizeof(float));
		memcpy(under, mk, (size_t)W * H * sizeof(float));
		blur_f(under, W, H, 1, MAX(1.f, px * 0.06f));
		for (size_t i = 0; i < (size_t)W * H; i++) under[i] = CL01(under[i] * 1.6f);
	}
	float th = L->p[TX_ROT] * PI_F / 180.f, cs = cosf(th), sn = sinf(th);
	float cx = L->cx * w, cy = L->cy * h, hw = W * 0.5f, hh = H * 0.5f;
	float ext = (fabsf(cs) * hw + fabsf(sn) * hh) + 2, eyt = (fabsf(sn) * hw + fabsf(cs) * hh) + 2;
	int x0 = MAX(0, (int)(cx - ext)), x1 = MIN(w - 1, (int)(cx + ext)), y0 = MAX(0, (int)(cy - eyt)), y1 = MIN(h - 1, (int)(cy + eyt));
	float plate_r = px * 0.35f;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float u = dx * cs + dy * sn + hw, v = -dx * sn + dy * cs + hh;
			float *p = im->p + ((size_t)y * w + x) * 3;
			if (style == 3) {
				float qx = fabsf(u - hw) - (hw - plate_r * 0.6f), qy = fabsf(v - hh) - (hh - plate_r * 0.6f);
				float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
				float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - plate_r * 0.6f;
				float pa = CL01(0.5f - dd) * opac;
				if (pa > 0) {
					p[0] = LERP(p[0], col[0], pa * 0.92f);
					p[1] = LERP(p[1], col[1], pa * 0.92f);
					p[2] = LERP(p[2], col[2], pa * 0.92f);
				}
				float a = mask_at(mk, W, H, u, v) * opac;
				p[0] = LERP(p[0], alt[0], a);
				p[1] = LERP(p[1], alt[1], a);
				p[2] = LERP(p[2], alt[2], a);
				continue;
			}
			if (under) {
				float a;
				if (style == 2)
					a = mask_at(under, W, H, u - px * 0.05f, v - px * 0.06f) * 0.7f * opac;
				else
					a = mask_at(under, W, H, u, v) * opac;
				p[0] = LERP(p[0], alt[0] * (style == 2 ? 0.f : 1.f), a);
				p[1] = LERP(p[1], alt[1] * (style == 2 ? 0.f : 1.f), a);
				p[2] = LERP(p[2], alt[2] * (style == 2 ? 0.f : 1.f), a);
			}
			float a = mask_at(mk, W, H, u, v) * opac;
			p[0] = LERP(p[0], col[0], a);
			p[1] = LERP(p[1], col[1], a);
			p[2] = LERP(p[2], col[2], a);
		}
	free(mk);
	free(under);
}

/* ------------------------------------------------------------------ Frame */
static const float FRAME_COL[6][3] = {{0.98f, 0.98f, 0.98f}, {0.03f, 0.03f, 0.03f}, {0.5f, 0.5f, 0.5f}, {0.96f, 0.93f, 0.84f}, {0.09f, 0.11f, 0.16f}, {0.78f, 0.7f, 0.55f}};
/* border sizes (left/right, top, bottom) for a frame layer */
static void frame_geom(const Layer *L, int w, int h, int *bx, int *bt, int *bb) {
	int st = (int)L->p[FR_STYLE];
	int b = (int)(L->p[FR_WIDTH] / 100.f * 0.16f * MIN(w, h));
	*bx = *bt = *bb = b;
	if (st == 1) *bb = b * 3; /* polaroid: heavy bottom */
	if (st == 3) *bx = *bt = *bb = 0; /* inset is painted over the photo */
}
static void apply_frame(const Layer *L, Img *im) {
	float wd = L->p[FR_WIDTH] / 100.f, cn = L->p[FR_CORNER] / 100.f;
	if (wd <= 0.f && cn <= 0.f) return;
	int st = (int)L->p[FR_STYLE];
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	int bx, bt, bb;
	frame_geom(L, w, h, &bx, &bt, &bb);
	int bsz = (int)(wd * 0.16f * m);
	const float *col = FRAME_COL[CLAMP((int)L->p[FR_COLOR], 0, 5)];
	float lc = lum3(col[0], col[1], col[2]);
	float line = lc > 0.5f ? 0.12f : 0.9f;
	Img o = img_new(w + 2 * bx, h + bt + bb);
	float rad = cn * 0.12f * m;
	for (int y = 0; y < o.h; y++)
		for (int x = 0; x < o.w; x++) {
			float *d = o.p + ((size_t)y * o.w + x) * 3;
			int sx = x - bx, sy = y - bt;
			float a = 0.f;
			if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
				a = 1.f;
				if (rad > 0.5f) {
					float qx = fabsf(sx + 0.5f - w * 0.5f) - (w * 0.5f - rad), qy = fabsf(sy + 0.5f - h * 0.5f) - (h * 0.5f - rad);
					float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
					float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - rad;
					a = CL01(0.5f - dd);
				}
			}
			float c3[3] = {col[0], col[1], col[2]};
			if (st == 2 && a < 1.f) { /* double: a thin keyline inside the border */
				float ex = MIN(MIN(x, o.w - 1 - x), MIN(y, o.h - 1 - y)) + 0.5f;
				float q = fabsf(ex - bsz * 0.42f) - MAX(1.f, bsz * 0.035f);
				float k = CL01(0.5f - q) * 0.9f;
				for (int i = 0; i < 3; i++) c3[i] = LERP(c3[i], line, k);
			}
			if (st == 3) { /* inset: a border painted on the photo's own edge */
				float ex = MIN(MIN(sx, w - 1 - sx), MIN(sy, h - 1 - sy)) + 0.5f;
				a = 1.f - CL01(bsz + 0.5f - ex);
				if (rad > 0.5f) {
					float qx = fabsf(sx + 0.5f - w * 0.5f) - (w * 0.5f - rad), qy = fabsf(sy + 0.5f - h * 0.5f) - (h * 0.5f - rad);
					float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
					float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - rad;
					a = MIN(a, CL01(0.5f - dd));
				}
			}
			const float *s = im->p + ((size_t)CLAMP(sy, 0, h - 1) * w + CLAMP(sx, 0, w - 1)) * 3;
			for (int k = 0; k < 3; k++) d[k] = LERP(c3[k], s[k], a);
		}
	img_free(im);
	*im = o;
}

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

/* ================================================================== document, pipeline, history, I/O */
#define PROXY_DEFAULT 1600
#define HMAX 200

typedef struct {
	char path[1024];
	uint8_t *rgb; /* source pixels, EXIF-oriented, RGB8 */
	int w, h;
	Img proxy; /* float preview-resolution copy of the source */
	Img proxy_lo; /* half-size copy used while a slider / scrub is being dragged */
	int proxy_max;
	uint32_t proxy_gen;
	int loaded;
} Doc;
static Doc doc;

static Layer stack[MAXLAYERS];
static int nlayers = 0;
static int active = -1; /* layer being edited, -1 = none */

/* ---- display mip chain (RGB packed 0x00RRGGBB) ---- */
#define MAXMIP 9
typedef struct {
	int n, w[MAXMIP], h[MAXMIP];
	uint32_t *px[MAXMIP];
} Mips;
static Mips disp, odisp; /* edited result / original (for compare) */
static float histo[4][256];
static double last_render_ms = 0;
static int render_serial = 0; /* bumps when `out` changes */

typedef struct {
	const Img *im;
	uint32_t *dst;
	uint32_t seed;
} CvtCtx;
static void cvt_job(void *v, int y0, int y1) {
	CvtCtx *c = (CvtCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		uint32_t *d = c->dst + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float dth = hashf(x, y, c->seed) - 0.5f;
			int r = (int)(p[0] * 255.f + 0.5f + dth), g = (int)(p[1] * 255.f + 0.5f + dth), b = (int)(p[2] * 255.f + 0.5f + dth);
			r = CLAMP(r, 0, 255);
			g = CLAMP(g, 0, 255);
			b = CLAMP(b, 0, 255);
			d[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
		}
	}
}
typedef struct {
	const uint32_t *s;
	uint32_t *d;
	int sw, sh, dw, dh;
} MipCtx;
static void mip_job(void *v, int y0, int y1) {
	MipCtx *c = (MipCtx *)v;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->dw; x++) {
			int x0 = MIN(x * 2, c->sw - 1), x1 = MIN(x * 2 + 1, c->sw - 1), ya = MIN(y * 2, c->sh - 1), yb = MIN(y * 2 + 1, c->sh - 1);
			uint32_t q[4] = {c->s[(size_t)ya * c->sw + x0], c->s[(size_t)ya * c->sw + x1], c->s[(size_t)yb * c->sw + x0], c->s[(size_t)yb * c->sw + x1]};
			uint32_t r = 0, g = 0, b = 0;
			for (int i = 0; i < 4; i++) {
				r += (q[i] >> 16) & 255;
				g += (q[i] >> 8) & 255;
				b += q[i] & 255;
			}
			c->d[(size_t)y * c->dw + x] = ((r >> 2) << 16) | ((g >> 2) << 8) | (b >> 2);
		}
}
static void mips_free(Mips *m) {
	for (int i = 0; i < m->n; i++) free(m->px[i]);
	m->n = 0;
}
static void mips_from_img(Mips *m, const Img *im) {
	mips_free(m);
	m->w[0] = im->w;
	m->h[0] = im->h;
	m->px[0] = (uint32_t *)xmalloc((size_t)im->w * im->h * 4);
	CvtCtx c = {im, m->px[0], 1234u};
	parallel(cvt_job, &c, im->h);
	m->n = 1;
	while (m->n < MAXMIP && MIN(m->w[m->n - 1], m->h[m->n - 1]) > 48) {
		int i = m->n;
		m->w[i] = (m->w[i - 1] + 1) / 2;
		m->h[i] = (m->h[i - 1] + 1) / 2;
		m->px[i] = (uint32_t *)xmalloc((size_t)m->w[i] * m->h[i] * 4);
		MipCtx mc = {m->px[i - 1], m->px[i], m->w[i - 1], m->h[i - 1], m->w[i], m->h[i]};
		parallel(mip_job, &mc, m->h[i]);
		m->n++;
	}
}
static void compute_histogram(const Mips *m) {
	memset(histo, 0, sizeof histo);
	if (!m->n) return;
	const uint32_t *p = m->px[MIN(1, m->n - 1)];
	size_t n = (size_t)m->w[MIN(1, m->n - 1)] * m->h[MIN(1, m->n - 1)];
	for (size_t i = 0; i < n; i++) {
		uint32_t c = p[i];
		int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
		histo[0][r]++;
		histo[1][g]++;
		histo[2][b]++;
		histo[3][(int)(0.2126f * r + 0.7152f * g + 0.0722f * b + 0.5f)]++;
	}
}

/* ---- the render pipeline with prefix cache ---- */
static Img pf, out;
static int pf_ok = 0, pf_k = -1, out_ok = 0;
/* drag-time quick preview: the same stack rendered on a half-size proxy, then upscaled */
static Img pfL, outL;
static int pfL_ok = 0, pfL_k = -1, outL_ok = 0, lo_shown = 0;
static uint64_t pfL_sig = 0, outL_sig = 0;
static int interacting_now(void);
static double full_ms = 0; /* cost of the last full-quality render: the quick preview is only used when it is high */
static long proxy_lo_min = 600000; /* only bother with the quick preview above this many preview pixels */
static uint64_t pf_sig = 0, out_sig = 0;
static int need_render = 1;

static inline int is_geom(int tool) { return tool == T_CROP || tool == T_ROTATE || tool == T_PERSP || tool == T_FRAME; }
/* while a layer is being edited: its own crop is shown uncropped, and geometry layers above it are
 * hidden so that on-canvas coordinates stay valid (they come back when the edit is committed) */
static inline int eff_visible(int i) {
	if (!stack[i].visible) return 0;
	if (i == active && stack[i].tool == T_CROP) return 0;
	if (active >= 0 && i > active && is_geom(stack[i].tool)) return 0;
	return 1;
}
static uint64_t stack_sig(int K) {
	uint64_t h = 1469598103934665603ULL;
#define MIX(x) h = (h ^ (uint64_t)(x)) * 1099511628211ULL
	MIX(doc.proxy_gen);
	MIX(K);
	for (int i = 0; i < K; i++) {
		MIX(stack[i].ver);
		MIX(eff_visible(i));
		MIX(stack[i].tool);
	}
#undef MIX
	return h;
}
static void pipeline_invalidate(void) {
	pf_ok = 0;
	out_ok = 0;
	pfL_ok = 0;
	outL_ok = 0;
	need_render = 1;
}
static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

static void pipeline_render_lo(int n, int K) {
	uint64_t sigK = stack_sig(K);
	if (!(pfL_ok && pfL_k == K && pfL_sig == sigK)) {
		if (outL_ok && K == n && outL_sig == sigK)
			img_assign(&pfL, &outL);
		else {
			img_assign(&pfL, &doc.proxy_lo);
			for (int i = 0; i < K; i++)
				if (eff_visible(i)) apply_layer(&stack[i], &pfL);
		}
		pfL_k = K;
		pfL_sig = sigK;
		pfL_ok = 1;
	}
	uint64_t sigN = stack_sig(n);
	if (!(outL_ok && outL_sig == sigN)) {
		img_assign(&outL, &pfL);
		for (int i = K; i < n; i++)
			if (eff_visible(i)) apply_layer(&stack[i], &outL);
		outL_sig = sigN;
		outL_ok = 1;
	}
	/* upscale to exactly the size the full-quality render will have, so the view does not jump */
	int ew = doc.proxy.w, eh = doc.proxy.h;
	for (int i = 0; i < n; i++)
		if (eff_visible(i)) layer_outdims(&stack[i], &ew, &eh);
	Img up = (outL.w == ew && outL.h == eh) ? img_clone(&outL) : resize_f(&outL, ew, eh);
	img_free(&out);
	out = up;
	out_ok = 0; /* `out` is not full quality now */
	lo_shown = 1;
	mips_from_img(&disp, &out);
	compute_histogram(&disp);
	render_serial++;
}
static void pipeline_render(void) {
	if (!doc.loaded) return;
	double t0 = now_ms();
	int n = nlayers;
	int K = active >= 0 ? active : n;
	if (doc.proxy_lo.p && full_ms > 45.0 && interacting_now()) {
		pipeline_render_lo(n, K);
		need_render = 0;
		last_render_ms = now_ms() - t0;
		return;
	}
	lo_shown = 0;
	uint64_t sigK = stack_sig(K);
	if (!(pf_ok && pf_k == K && pf_sig == sigK)) {
		if (out_ok && K == n && out_sig == sigK)
			img_assign(&pf, &out);
		else {
			img_assign(&pf, &doc.proxy);
			for (int i = 0; i < K; i++)
				if (eff_visible(i)) apply_layer(&stack[i], &pf);
		}
		pf_k = K;
		pf_sig = sigK;
		pf_ok = 1;
	}
	uint64_t sigN = stack_sig(n);
	if (!(out_ok && out_sig == sigN)) {
		img_assign(&out, &pf);
		for (int i = K; i < n; i++)
			if (eff_visible(i)) apply_layer(&stack[i], &out);
		out_sig = sigN;
		out_ok = 1;
		mips_from_img(&disp, &out);
		compute_histogram(&disp);
		render_serial++;
	}
	need_render = 0;
	last_render_ms = now_ms() - t0;
	full_ms = last_render_ms;
}

/* final size of the exported image (computed from the stack, no pixels touched) */
static void full_dims(int *w, int *h) {
	*w = doc.w;
	*h = doc.h;
	for (int i = 0; i < nlayers; i++)
		if (stack[i].visible) layer_outdims(&stack[i], w, h);
}

/* ---- history (snapshots of the whole stack; layers are small) ---- */
typedef struct {
	Layer *ls;
	int n;
} Snap;
static Snap hist[HMAX + 1];
static int hn = 0, hpos = -1;
static void snap_take(Snap *s) {
	s->n = nlayers;
	s->ls = nlayers ? (Layer *)xmalloc(sizeof(Layer) * nlayers) : NULL;
	for (int i = 0; i < nlayers; i++) layer_copy(&s->ls[i], &stack[i]);
}
static void snap_free(Snap *s) {
	for (int i = 0; i < s->n; i++) layer_free(&s->ls[i]);
	free(s->ls);
	s->ls = NULL;
	s->n = 0;
}
static void stack_clear(void) {
	for (int i = 0; i < nlayers; i++) layer_free(&stack[i]);
	nlayers = 0;
}
static void snap_restore(const Snap *s) {
	stack_clear();
	for (int i = 0; i < s->n; i++) layer_copy(&stack[i], &s->ls[i]);
	nlayers = s->n;
	need_render = 1;
}
static void hist_reset(void) {
	for (int i = 0; i < hn; i++) snap_free(&hist[i]);
	hn = 0;
	hpos = -1;
}
static void hist_push(void) {
	for (int j = hpos + 1; j < hn; j++) snap_free(&hist[j]);
	hn = hpos + 1;
	if (hn >= HMAX) {
		snap_free(&hist[0]);
		memmove(&hist[0], &hist[1], sizeof(Snap) * (hn - 1));
		hn--;
	}
	snap_take(&hist[hn]);
	hpos = hn;
	hn++;
}
static int hist_can_undo(void) { return hpos > 0; }
static int hist_can_redo(void) { return hpos >= 0 && hpos < hn - 1; }

/* ---- edit sessions (a tool being edited on the active layer) ---- */
static int edit_dirty = 0;
static Layer edit_backup;
static int edit_has_backup = 0, edit_is_new = 0;

static void layer_remove(int i) {
	layer_free(&stack[i]);
	memmove(&stack[i], &stack[i + 1], sizeof(Layer) * (nlayers - i - 1));
	nlayers--;
	memset(&stack[nlayers], 0, sizeof(Layer));
	if (active == i)
		active = -1;
	else if (active > i)
		active--;
	need_render = 1;
}
static void edit_begin(int idx, int is_new) {
	if (edit_has_backup) layer_free(&edit_backup);
	layer_copy(&edit_backup, &stack[idx]);
	edit_has_backup = 1;
	edit_is_new = is_new;
	edit_dirty = 0;
	active = idx;
	need_render = 1;
}
/* leave the tool: commit keeps edits (and records an undo step), !commit rolls them back */
static void edit_end(int commit) {
	if (active < 0) return;
	int i = active, changed = 0;
	if (!commit) {
		if (edit_is_new)
			layer_remove(i);
		else if (edit_has_backup) {
			layer_free(&stack[i]);
			layer_copy(&stack[i], &edit_backup);
			layer_bump(&stack[i]);
		}
	} else if (!layer_has_data(&stack[i]) || (edit_is_new && !stack[i].touched)) {
		changed = edit_dirty && !edit_is_new;
		layer_remove(i);
	} else
		changed = edit_dirty;
	active = -1;
	edit_dirty = 0;
	need_render = 1;
	if (changed) hist_push();
}
static Layer *layer_new(int tool) {
	if (nlayers >= MAXLAYERS) return NULL;
	layer_init(&stack[nlayers], tool);
	return &stack[nlayers++];
}
static void select_tool(int tool) {
	if (active >= 0 && stack[active].tool == tool) return;
	edit_end(1);
	for (int i = nlayers - 1; i >= 0; i--)
		if (stack[i].tool == tool) {
			edit_begin(i, 0);
			return;
		}
	Layer *L = layer_new(tool);
	if (L) {
		edit_begin(nlayers - 1, 1);
		if (TD[tool].group[0] == 'F' || TD[tool].group[0] == 'E') { /* filters / effects show their default look */
			stack[active].touched = 1;
			edit_dirty = 1;
		}
	}
}
static void select_layer(int i) {
	if (i < 0 || i >= nlayers) return;
	if (active == i) return;
	edit_end(1);
	if (i >= nlayers) return;
	edit_begin(i, 0);
}
static void touch(Layer *L) {
	L->touched = 1;
	edit_dirty = 1;
	layer_bump(L);
	need_render = 1;
}

/* ---- looks (preset bundles of layers) ---- */
typedef struct {
	int tool, n;
	struct {
		int idx;
		float v;
	} a[6];
} LookPart;
typedef struct {
	const char *name;
	int np;
	LookPart part[3];
} Look;
static const Look LOOKS[] = {
	{"Pop", 2, {{T_TUNE, 3, {{TU_CON, 18}, {TU_SAT, 28}, {TU_AMB, 22}}}, {T_DETAILS, 1, {{DE_STR, 20}}}}},
	{"Fine Art", 2, {{T_TUNE, 4, {{TU_AMB, 35}, {TU_SHA, 25}, {TU_HIG, -20}, {TU_SAT, -12}}}, {T_DETAILS, 1, {{DE_STR, 30}}}}},
	{"Punch", 2, {{T_TUNE, 3, {{TU_CON, 25}, {TU_SAT, 20}, {TU_AMB, 10}}}, {T_DRAMA, 2, {{DR_STYLE, 0}, {DR_STR, 35}}}}},
	{"Soft Glow", 2, {{T_GLOW, 2, {{GL_GLOW, 55}, {GL_SAT, 8}}}, {T_TUNE, 2, {{TU_BRI, 8}, {TU_WAR, 8}}}}},
	{"Morning", 2, {{T_TUNE, 4, {{TU_BRI, 12}, {TU_WAR, 22}, {TU_SHA, 18}, {TU_CON, -8}}}, {T_GLOW, 2, {{GL_STYLE, 1}, {GL_GLOW, 30}}}}},
	{"Moody", 2, {{T_TUNE, 4, {{TU_BRI, -12}, {TU_CON, 15}, {TU_SAT, -18}, {TU_SHA, -10}}}, {T_VIGNETTE, 1, {{VG_OUTER, -45}}}}},
	{"Faded", 1, {{T_VINTAGE, 3, {{VI_STYLE, 3}, {VI_STR, 60}, {VI_VIG, 20}}}}},
	{"Cinema", 2, {{T_VINTAGE, 3, {{VI_STYLE, 1}, {VI_STR, 85}, {VI_VIG, 25}}}, {T_TUNE, 1, {{TU_CON, 10}}}}},
	{"HDR", 1, {{T_HDR, 2, {{HD_STYLE, 0}, {HD_STR, 60}}}}},
	{"Drama", 1, {{T_DRAMA, 2, {{DR_STYLE, 1}, {DR_STR, 60}}}}},
	{"Retro", 1, {{T_RETRO, 2, {{RE_STYLE, 0}, {RE_STR, 75}}}}},
	{"Film", 1, {{T_GRAIN, 3, {{GR_STYLE, 1}, {GR_GRAIN, 45}, {GR_STR, 65}}}}},
	{"Silver", 1, {{T_BW, 3, {{BW_FILTER, 2}, {BW_CON, 30}, {BW_GRAIN, 10}}}}},
	{"Noir", 1, {{T_NOIR, 3, {{NO_STYLE, 0}, {NO_WASH, 15}, {NO_VIG, 45}}}}},
	{"Portrait", 2, {{T_PORTRAIT, 2, {{PO_SMOOTH, 45}, {PO_BRIGHT, 25}}}, {T_TUNE, 2, {{TU_AMB, 12}, {TU_WAR, 6}}}}},
	{"Dreamy", 2, {{T_BLOOM, 3, {{BL_AMT, 70}, {BL_THR, 45}, {BL_RAD, 55}}}, {T_TUNE, 2, {{TU_WAR, 10}, {TU_SAT, 12}}}}},
	{"Teal & Orange", 2, {{T_SPLIT, 4, {{SP_SHUE, 195}, {SP_SSTR, 55}, {SP_HHUE, 32}, {SP_HSTR, 45}}}, {T_TUNE, 2, {{TU_CON, 14}, {TU_SAT, 8}}}}},
	{"Clear Air", 2, {{T_DEHAZE, 2, {{DH_AMT, 55}, {DH_SAT, 12}}}, {T_DETAILS, 1, {{DE_STR, 20}}}}},
	{"Anamorphic", 3, {{T_STREAK, 3, {{ST_AMT, 60}, {ST_THR, 65}, {ST_TINT, 2}}}, {T_BLOOM, 2, {{BL_AMT, 30}, {BL_THR, 60}}}, {T_VIGNETTE, 1, {{VG_OUTER, -35}}}}},
	{"Cool Matte", 2, {{T_TUNE, 3, {{TU_WAR, -20}, {TU_SAT, -10}, {TU_CON, 8}}}, {T_VINTAGE, 3, {{VI_STYLE, 1}, {VI_STR, 40}, {VI_VIG, 0}}}}},
};
#define NLOOKS ((int)(sizeof(LOOKS) / sizeof(LOOKS[0])))
/* build the layers of a look into out[] (up to 3); returns count */
static int look_build(const Look *lk, Layer *out_l) {
	for (int i = 0; i < lk->np; i++) {
		layer_init(&out_l[i], lk->part[i].tool);
		for (int j = 0; j < lk->part[i].n; j++) out_l[i].p[lk->part[i].a[j].idx] = lk->part[i].a[j].v;
		out_l[i].touched = 1;
	}
	return lk->np;
}
/* ---- custom looks (saved from the current edit stack, stored in the user's pref dir) ---- */
#define MAXCL 16
#define CL_LAYERS 8
typedef struct {
	char name[32];
	int n;
	Layer ls[CL_LAYERS];
} CLook;
static CLook clooks[MAXCL];
static int ncl = 0;
static int lt_serial = -1, lt_next = 0;
static int look_total(void) { return NLOOKS + ncl; }
static const char *look_name(int i) { return i < NLOOKS ? LOOKS[i].name : clooks[i - NLOOKS].name; }
static int look_layers(int i, Layer *o) {
	if (i < NLOOKS) return look_build(&LOOKS[i], o);
	const CLook *c = &clooks[i - NLOOKS];
	for (int k = 0; k < c->n; k++) {
		layer_copy(&o[k], &c->ls[k]);
		o[k].touched = 1;
		o[k].visible = 1;
	}
	return c->n;
}
static int look_saveable(const Layer *L) {
	switch (L->tool) {
	case T_CROP: case T_ROTATE: case T_PERSP: case T_EXPAND: case T_BRUSH: case T_HEAL: case T_SELECTIVE: case T_TEXT: case T_DOUBLE: return 0;
	}
	return !L->mask_on;
}
static char *looks_file(char *buf, size_t cap) {
	char *pd = SDL_GetPrefPath("snapdesk", "snapdesk");
	if (!pd) return NULL;
	snprintf(buf, cap, "%slooks.txt", pd);
	SDL_free(pd);
	return buf;
}
static void looks_save_file(void) {
	char path[1100];
	if (!looks_file(path, sizeof path)) return;
	FILE *f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < ncl; i++) {
		fprintf(f, "LOOK %s\n", clooks[i].name);
		for (int k = 0; k < clooks[i].n; k++) {
			const Layer *L = &clooks[i].ls[k];
			char tn[64];
			snprintf(tn, sizeof tn, "%s", TD[L->tool].name);
			for (char *c = tn; *c; c++)
				if (*c == ' ') *c = '_';
			fprintf(f, "L %s", tn);
			for (int j = 0; j < MAXP; j++) fprintf(f, " %g", L->p[j]);
			fprintf(f, " %g %g\n", L->cx, L->cy);
			if (L->tool == T_MIXER) {
				fprintf(f, "M");
				for (int b = 0; b < 8; b++)
					for (int j = 0; j < 3; j++) fprintf(f, " %g", L->mix[b][j]);
				fprintf(f, "\n");
			}
			for (int ch = 0; ch < 4; ch++)
				if (L->ncp[ch] > 1) {
					fprintf(f, "C %d %d", ch, L->ncp[ch]);
					for (int j = 0; j < L->ncp[ch]; j++) fprintf(f, " %g %g", L->cp[ch][j][0], L->cp[ch][j][1]);
					fprintf(f, "\n");
				}
		}
		fprintf(f, "END\n");
	}
	fclose(f);
}
static void looks_load_file(void) {
	char path[1100], ln[1024];
	ncl = 0;
	if (!looks_file(path, sizeof path)) return;
	FILE *f = fopen(path, "r");
	if (!f) return;
	CLook *cur = NULL;
	while (fgets(ln, sizeof ln, f)) {
		size_t n = strlen(ln);
		while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r')) ln[--n] = 0;
		if (!strncmp(ln, "LOOK ", 5)) {
			if (ncl >= MAXCL) break;
			cur = &clooks[ncl++];
			memset(cur, 0, sizeof *cur);
			snprintf(cur->name, sizeof cur->name, "%.31s", ln + 5);
		} else if (cur && !strncmp(ln, "L ", 2) && cur->n < CL_LAYERS) {
			char tn[64];
			if (sscanf(ln + 2, "%63s", tn) != 1) continue;
			for (char *c = tn; *c; c++)
				if (*c == '_') *c = ' ';
			int t = -1;
			for (int i = 0; i < T_COUNT; i++)
				if (!strcmp(TD[i].name, tn)) t = i;
			if (t < 0) continue;
			Layer *L = &cur->ls[cur->n++];
			layer_init(L, t);
			const char *s = ln + 2 + strlen(tn) + 1;
			for (int j = 0; j < MAXP; j++) {
				int used = 0;
				float v;
				if (sscanf(s, "%f%n", &v, &used) == 1) {
					L->p[j] = v;
					s += used;
				}
			}
			sscanf(s, "%f %f", &L->cx, &L->cy);
		} else if (cur && cur->n && !strncmp(ln, "M ", 2)) {
			Layer *L = &cur->ls[cur->n - 1];
			const char *s = ln + 2;
			for (int b = 0; b < 8; b++)
				for (int j = 0; j < 3; j++) {
					int used = 0;
					float v;
					if (sscanf(s, "%f%n", &v, &used) == 1) {
						L->mix[b][j] = v;
						s += used;
					}
				}
		} else if (cur && cur->n && !strncmp(ln, "C ", 2)) {
			Layer *L = &cur->ls[cur->n - 1];
			int ch, cnt, used = 0;
			if (sscanf(ln + 2, "%d %d%n", &ch, &cnt, &used) == 2 && ch >= 0 && ch < 4 && cnt > 1 && cnt <= MAXCP) {
				const char *s = ln + 2 + used;
				L->ncp[ch] = cnt;
				for (int j = 0; j < cnt; j++) {
					int u2 = 0;
					sscanf(s, "%f %f%n", &L->cp[ch][j][0], &L->cp[ch][j][1], &u2);
					s += u2;
				}
			}
		}
	}
	fclose(f);
	lt_serial = -1;
}
/* returns 1 on success; *skipped counts layers that cannot be stored (geometry, painted data...) */
static int look_save_current(const char *name, int *skipped) {
	CLook c;
	memset(&c, 0, sizeof c);
	snprintf(c.name, sizeof c.name, "%.31s", name);
	*skipped = 0;
	for (int i = 0; i < nlayers && c.n < CL_LAYERS; i++) {
		if (!stack[i].visible) continue;
		if (!look_saveable(&stack[i])) {
			(*skipped)++;
			continue;
		}
		layer_copy(&c.ls[c.n], &stack[i]);
		c.ls[c.n].visible = 1;
		c.n++;
	}
	if (!c.n) return 0;
	int slot = -1;
	for (int i = 0; i < ncl; i++)
		if (!strcmp(clooks[i].name, c.name)) slot = i;
	if (slot < 0) {
		if (ncl >= MAXCL) return -1;
		slot = ncl++;
	}
	clooks[slot] = c;
	looks_save_file();
	lt_serial = -1;
	return 1;
}
static void look_delete_custom(int i) {
	i -= NLOOKS;
	if (i < 0 || i >= ncl) return;
	for (int k = i; k < ncl - 1; k++) clooks[k] = clooks[k + 1];
	ncl--;
	looks_save_file();
	lt_serial = -1;
}
static void look_apply(int i) {
	if (i < 0 || i >= look_total()) return;
	edit_end(1);
	Layer tmp[CL_LAYERS];
	int n = look_layers(i, tmp);
	for (int k = 0; k < n; k++) {
		if (nlayers >= MAXLAYERS) break;
		stack[nlayers++] = tmp[k];
	}
	need_render = 1;
	hist_push();
}

/* ---- loading ---- */
static int exif_orientation(const char *path) {
	FILE *f = stbi__fopen(path, "rb");
	if (!f) return 1;
	unsigned char *b = (unsigned char *)xmalloc(131072);
	size_t n = fread(b, 1, 131072, f);
	fclose(f);
	int res = 1;
	if (n > 12 && b[0] == 0xFF && b[1] == 0xD8) {
		size_t pos = 2;
		while (pos + 4 < n && b[pos] == 0xFF) {
			int mk = b[pos + 1];
			size_t len = ((size_t)b[pos + 2] << 8) | b[pos + 3];
			if (mk == 0xDA) break;
			if (mk == 0xE1 && pos + 10 < n && memcmp(b + pos + 4, "Exif\0\0", 6) == 0) {
				const unsigned char *t = b + pos + 10;
				size_t tl = n - (pos + 10);
				int le = t[0] == 'I';
#define R16(o) (le ? (t[o] | (t[(o) + 1] << 8)) : ((t[o] << 8) | t[(o) + 1]))
#define R32(o) (le ? (uint32_t)(t[o] | (t[(o) + 1] << 8) | (t[(o) + 2] << 16) | ((uint32_t)t[(o) + 3] << 24)) : (uint32_t)(((uint32_t)t[o] << 24) | (t[(o) + 1] << 16) | (t[(o) + 2] << 8) | t[(o) + 3]))
				if (tl > 16) {
					uint32_t ifd = R32(4);
					if (ifd + 2 < tl) {
						int cnt = R16(ifd);
						for (int i = 0; i < cnt && ifd + 2 + (size_t)(i + 1) * 12 <= tl; i++) {
							size_t e = ifd + 2 + (size_t)i * 12;
							if (R16(e) == 0x0112) {
								res = R16(e + 8);
								break;
							}
						}
					}
				}
#undef R16
#undef R32
				break;
			}
			pos += 2 + len;
		}
	}
	free(b);
	return (res >= 1 && res <= 8) ? res : 1;
}
static void orient_u8(uint8_t **rgb, int *w, int *h, int o) {
	if (o <= 1) return;
	int sw = *w, sh = *h, swap = o >= 5;
	int dw = swap ? sh : sw, dh = swap ? sw : sh;
	uint8_t *d = (uint8_t *)xmalloc((size_t)dw * dh * 3), *s = *rgb;
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			int nx, ny;
			switch (o) {
			case 2: nx = sw - 1 - x; ny = y; break;
			case 3: nx = sw - 1 - x; ny = sh - 1 - y; break;
			case 4: nx = x; ny = sh - 1 - y; break;
			case 5: nx = y; ny = x; break;
			case 6: nx = sh - 1 - y; ny = x; break;
			case 7: nx = sh - 1 - y; ny = sw - 1 - x; break;
			default: nx = y; ny = sw - 1 - x; break;
			}
			memcpy(d + ((size_t)ny * dw + nx) * 3, s + ((size_t)y * sw + x) * 3, 3);
		}
	free(s);
	*rgb = d;
	*w = dw;
	*h = dh;
}
static void proxy_build(void) {
	if (!doc.rgb) return;
	img_free(&doc.proxy);
	int tw = doc.w, th = doc.h;
	if (doc.proxy_max > 0 && MAX(tw, th) > doc.proxy_max) {
		float s = (float)doc.proxy_max / (float)MAX(tw, th);
		tw = MAX(1, (int)(doc.w * s + 0.5f));
		th = MAX(1, (int)(doc.h * s + 0.5f));
	}
	doc.proxy = resize_u8(doc.rgb, doc.w, doc.h, tw, th);
	img_free(&doc.proxy_lo);
	if ((long)tw * th > proxy_lo_min) doc.proxy_lo = resize_f(&doc.proxy, MAX(1, (tw + 1) / 2), MAX(1, (th + 1) / 2));
	doc.proxy_gen++;
	mips_from_img(&odisp, &doc.proxy);
	pipeline_invalidate();
}
static char g_err[256];
static int doc_load(const char *path) {
	int w, h, n;
	uint8_t *px = stbi_load(path, &w, &h, &n, 3);
	if (!px) {
		snprintf(g_err, sizeof g_err, "Cannot open: %s", stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
		return 0;
	}
	int o = exif_orientation(path);
	orient_u8(&px, &w, &h, o);
	if (doc.rgb) free(doc.rgb);
	edit_end(0);
	stack_clear();
	hist_reset();
	doc.rgb = px;
	doc.w = w;
	doc.h = h;
	snprintf(doc.path, sizeof doc.path, "%s", path);
	doc.loaded = 1;
	proxy_build();
	hist_push();
	return 1;
}

/* ---- export ---- */
static int ends_with_ci(const char *s, const char *suf) {
	size_t a = strlen(s), b = strlen(suf);
	if (b > a) return 0;
	for (size_t i = 0; i < b; i++)
		if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
	return 1;
}
static int export_image(const char *path, int quality, int scale_pct, char *msg, size_t msgcap) {
	if (!doc.loaded) return 0;
	int sw = doc.w, sh = doc.h;
	Img im;
	if (scale_pct < 100) {
		sw = MAX(1, doc.w * scale_pct / 100);
		sh = MAX(1, doc.h * scale_pct / 100);
	}
	im = resize_u8(doc.rgb, doc.w, doc.h, sw, sh);
	for (int i = 0; i < nlayers; i++) {
		if (!stack[i].visible) continue;
		Layer c;
		layer_copy(&c, &stack[i]);
		apply_layer(&c, &im);
		layer_free(&c);
	}
	uint8_t *rgb = (uint8_t *)xmalloc((size_t)im.w * im.h * 3);
	for (int y = 0; y < im.h; y++)
		for (int x = 0; x < im.w; x++) {
			float d = hashf(x, y, 4321u) - 0.5f;
			const float *p = im.p + ((size_t)y * im.w + x) * 3;
			for (int k = 0; k < 3; k++) {
				int v = (int)(p[k] * 255.f + 0.5f + d);
				rgb[((size_t)y * im.w + x) * 3 + k] = (uint8_t)CLAMP(v, 0, 255);
			}
		}
	int ok;
	if (ends_with_ci(path, ".png"))
		ok = stbi_write_png(path, im.w, im.h, 3, rgb, im.w * 3);
	else if (ends_with_ci(path, ".bmp"))
		ok = stbi_write_bmp(path, im.w, im.h, 3, rgb);
	else if (ends_with_ci(path, ".tga"))
		ok = stbi_write_tga(path, im.w, im.h, 3, rgb);
	else
		ok = stbi_write_jpg(path, im.w, im.h, 3, rgb, CLAMP(quality, 1, 100));
	if (msg) {
		if (ok)
			snprintf(msg, msgcap, "Saved %dx%d  %s", im.w, im.h, path);
		else
			snprintf(msg, msgcap, "Could not write %s", path);
	}
	free(rgb);
	img_free(&im);
	return ok;
}

/* ---- native file dialogs (Win32 common dialog / zenity / kdialog); returns 1 ok, 0 cancelled, -1 unavailable */
static int native_dialog(int save, const char *start, char *outp, int cap) {
#ifdef _WIN32
	wchar_t wf[1024] = {0};
	if (start) MultiByteToWideChar(CP_UTF8, 0, start, -1, wf, 1024);
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof ofn);
	ofn.lStructSize = sizeof ofn;
	ofn.lpstrFile = wf;
	ofn.nMaxFile = 1024;
	ofn.lpstrFilter = save ? L"JPEG (*.jpg)\0*.jpg\0PNG (*.png)\0*.png\0BMP (*.bmp)\0*.bmp\0" : L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.tga;*.gif;*.psd\0All files\0*.*\0";
	ofn.lpstrDefExt = save ? L"jpg" : NULL;
	ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok) return 0;
	WideCharToMultiByte(CP_UTF8, 0, wf, -1, outp, cap, NULL, NULL);
	return 1;
#else
	static int have = -1; /* 0 none, 1 zenity, 2 kdialog */
	if (have < 0) {
		have = 0;
		if (system("command -v zenity >/dev/null 2>&1") == 0)
			have = 1;
		else if (system("command -v kdialog >/dev/null 2>&1") == 0)
			have = 2;
	}
	if (!have) return -1;
	char q[1100];
	size_t k = 0;
	const char *s = start ? start : "";
	for (; *s && k < sizeof q - 6; s++) {
		if (*s == '\'') {
			memcpy(q + k, "'\\''", 4);
			k += 4;
		} else
			q[k++] = *s;
	}
	q[k] = 0;
	char cmd[1400];
	if (have == 1)
		snprintf(cmd, sizeof cmd, "zenity --file-selection %s --filename='%s' 2>/dev/null", save ? "--save --confirm-overwrite" : "", q);
	else
		snprintf(cmd, sizeof cmd, "kdialog %s '%s' 2>/dev/null", save ? "--getsavefilename" : "--getopenfilename", q);
	FILE *p = popen(cmd, "r");
	if (!p) return -1;
	char line[1100] = {0};
	if (!fgets(line, sizeof line, p)) line[0] = 0;
	int rc = pclose(p);
	(void)rc;
	size_t l = strlen(line);
	while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
	if (!l) return 0;
	snprintf(outp, cap, "%s", line);
	return 1;
#endif
}

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
	int x0 = MAX((int)floorf(x), cx0_), y0 = MAX((int)floorf(y), cy0_), x1 = MIN((int)ceilf(x + w), cx1_), y1 = MIN((int)ceilf(y + h), cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float d = sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r);
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void rrect_line(float x, float y, float w, float h, float r, float t, uint32_t c, float alpha) {
	int x0 = MAX((int)floorf(x) - 1, cx0_), y0 = MAX((int)floorf(y) - 1, cy0_), x1 = MIN((int)ceilf(x + w) + 1, cx1_), y1 = MIN((int)ceilf(y + h) + 1, cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float d = fabsf(sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r) + t * 0.5f) - t * 0.5f;
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void disc(float cx, float cy, float r, uint32_t c, float alpha) {
	int x0 = MAX((int)floorf(cx - r - 1), cx0_), y0 = MAX((int)floorf(cy - r - 1), cy0_), x1 = MIN((int)ceilf(cx + r + 1), cx1_), y1 = MIN((int)ceilf(cy + r + 1), cy1_);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
			float cov = CL01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void ring(float cx, float cy, float r, float t, uint32_t c, float alpha) {
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
		if (g->bm) {
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

/* ================================================================== UI state & widgets */
static int mx = 0, my = 0;
static int mdx = 0, mdy = 0;            /* mouse delta since last frame */
static int m_down[3], m_press[3], m_rel[3];
static int m_dbl = 0, wheel = 0, wheel_used = 0;
static int mod_shift = 0, mod_ctrl = 0, mod_alt = 0;
static int act_id = 0;                  /* widget currently being dragged */
static int modal = 0, in_modal_draw = 0;
static int quit_flag = 0, dirty = 1;
static float ui_scale = 1.f;
static int sel_param = 0;               /* parameter picked for keyboard / scrub */
static int sel_pt = 0;                  /* selective: current control point */
static int wb_pick = 0;
static int curve_ch = 0;
static char toast_msg[300];
static uint32_t toast_until = 0;
static uint32_t hud_until = 0;
static char hud_name[48];
static float hud_val = 0;
static int hud_is_choice = 0;
static char hud_text[48];

enum { MODAL_NONE, MODAL_EXPORT, MODAL_OPENPATH, MODAL_HELP, MODAL_SAVELOOK, MODAL_INFO };

static void toast(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(toast_msg, sizeof toast_msg, fmt, ap);
	va_end(ap);
	toast_until = SDL_GetTicks() + 3500;
	dirty = 1;
}

static int mouse_in(int x, int y, int w, int h) {
	if (modal && !in_modal_draw) return 0;
	if (mx < x || my < y || mx >= x + w || my >= y + h) return 0;
	return mx >= cx0_ && my >= cy0_ && mx < cx1_ && my < cy1_;
}

/* ---- text input field (single active field) ---- */
static int tf_id = 0, tf_enter = 0, tf_kind = 0; /* kind: 0 text, 1 number */
static char tf_buf[512];
static void tf_begin(int id, const char *init, int kind) {
	tf_id = id;
	tf_kind = kind;
	tf_enter = 0;
	snprintf(tf_buf, sizeof tf_buf, "%s", init);
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_StartTextInput();
}
static void tf_end(void) {
	tf_id = 0;
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_StopTextInput();
}
static void tf_append(const char *s) {
	size_t n = strlen(tf_buf);
	for (; *s && n < sizeof tf_buf - 1; s++) {
		unsigned char c = (unsigned char)*s;
		if (c < 32) continue;
		if (tf_kind == 1 && !(isdigit(c) || c == '-' || c == '.')) continue;
		tf_buf[n++] = (char)c;
	}
	tf_buf[n] = 0;
}

/* returns 1 if the text changed this frame */
static int text_field(int id, int x, int y, int w, int h, char *dst, int cap, const char *hint) {
	int changed = 0;
	int hov = mouse_in(x, y, w, h);
	if (m_press[0]) {
		if (hov && tf_id != id) tf_begin(id, dst, 0);
		else if (!hov && tf_id == id) tf_end();
	}
	rrect((float)x, (float)y, (float)w, (float)h, 6, C_BG, 1);
	rrect_line((float)x, (float)y, (float)w, (float)h, 6, 1.2f, tf_id == id ? C_ACC : C_LINE, 1);
	const char *show = tf_id == id ? tf_buf : dst;
	Clip c = clip_push(x + 8, y, w - 16, h);
	float tw = text_width(show, 13, 0);
	float ox = tw > w - 20 ? (w - 20 - tw) : 0.f;
	if (!show[0] && tf_id != id) text((float)x + 9, y + h * 0.5f, hint ? hint : "", 13, 0, C_FAINT, 1);
	else text(x + 9 + ox, y + h * 0.5f, show, 13, 0, C_TEXT, 1);
	if (tf_id == id && ((SDL_GetTicks() / 500) & 1) == 0) fill_rect((int)(x + 9 + ox + tw + 1), y + 6, 1, h - 12, C_ACC, 1);
	clip_set(c);
	if (tf_id == id) {
		if (strcmp(dst, tf_buf) != 0) {
			snprintf(dst, cap, "%s", tf_buf);
			changed = 1;
		}
		if (tf_enter) {
			tf_end();
			tf_enter = 0;
			changed |= 2;
		}
	}
	return changed;
}

/* ---- buttons ---- */
enum { BS_NORMAL, BS_ACCENT, BS_GHOST, BS_DANGER };
static int button(int id, int x, int y, int w, int h, const char *label, int style, int on) {
	int hov = mouse_in(x, y, w, h), res = 0;
	if (hov && m_press[0]) act_id = id;
	if (act_id == id && m_rel[0]) {
		if (hov) res = 1;
		act_id = 0;
	}
	uint32_t bg = C_PANEL2, fg = C_TEXT;
	float a = 1.f;
	if (style == BS_ACCENT) {
		bg = C_ACC;
		fg = 0x0b2a27;
	} else if (style == BS_GHOST) {
		bg = C_PANEL3;
		a = 0.f;
	} else if (style == BS_DANGER) {
		bg = C_WARN;
		fg = 0x2a0d09;
	}
	if (on) {
		bg = C_ACC;
		fg = 0x0b2a27;
		a = 1.f;
	}
	if (hov && !on) {
		if (style == BS_GHOST) a = 1.f;
		else bg = style == BS_NORMAL ? C_PANEL3 : bg;
	}
	if (act_id == id && hov) a = 1.f;
	if (a > 0.f) rrect((float)x, (float)y, (float)w, (float)h, 6, bg, a);
	if (style == BS_ACCENT && hov) rrect((float)x, (float)y, (float)w, (float)h, 6, 0xffffff, 0.15f);
	if (label && label[0]) text_c(x + w * 0.5f, y + h * 0.5f, label, 13, 1, fg, 1);
	return res;
}
static int chip_row(int id, int x, int *py, int w, const char *list, float *val, int extra_h) {
	int n = choice_count(list), cur = (int)(*val + 0.5f), changed = 0;
	int cx = x, cy = *py;
	for (int i = 0; i < n; i++) {
		char lb[40];
		choice_label(list, i, lb, sizeof lb);
		int tw = (int)text_width(lb, 12, 1) + 18;
		if (tw < 30) tw = 30;
		if (cx + tw > x + w) {
			cx = x;
			cy += 28;
		}
		int hov = mouse_in(cx, cy, tw, 24);
		int on = i == cur;
		if (hov && m_press[0]) {
			act_id = id + i;
		}
		if (act_id == id + i && m_rel[0]) {
			act_id = 0;
			if (hov && !on) {
				*val = (float)i;
				changed = 1;
				cur = i;
				on = 1;
			}
		}
		rrect((float)cx, (float)cy, (float)tw, 24, 12, on ? C_ACC : (hov ? C_PANEL3 : C_PANEL2), 1);
		text_c(cx + tw * 0.5f, cy + 12, lb, 12, 1, on ? 0x0b2a27 : C_TEXT, 1);
		cx += tw + 5;
	}
	*py = cy + 28 + extra_h;
	return changed;
}

/* slider: returns bit0 = value changed, bit1 = user interacted (pick this param) */
static float sl_start_v, sl_start_x;
static int slider(int id, int x, int y, int w, const char *label, float *v, float mn, float mxv, float def, int dec, int sel) {
	int h = 40, res = 0;
	int hov = mouse_in(x, y, w, h);
	float old = *v;
	int tx = x + 3, tw = w - 6, ty = y + 29;
	int on_track = hov && my >= y + 16;
	int on_val = hov && my < y + 18 && mx > x + w - 70;
	if (hov && m_press[0]) {
		res |= 2;
		if (m_dbl && on_val) {
			char b[32];
			snprintf(b, sizeof b, dec ? "%.1f" : "%.0f", *v);
			tf_begin(id, b, 1);
		} else if (m_dbl) {
			*v = def;
		} else if (on_track) {
			act_id = id;
			sl_start_v = *v;
			sl_start_x = (float)mx;
		}
	}
	if (hov && m_press[2]) {
		*v = def;
		res |= 2;
	}
	if (act_id == id) {
		if (m_down[0]) {
			float t;
			if (mod_shift)
				*v = sl_start_v + ((float)mx - sl_start_x) / tw * (mxv - mn) * 0.2f;
			else {
				t = CL01(((float)mx - tx) / (float)tw);
				*v = mn + t * (mxv - mn);
			}
			res |= 2;
		} else
			act_id = 0;
	}
	if (hov && wheel && !wheel_used && act_id != id) {
		float step = dec ? 0.1f : 1.f;
		if (mxv - mn > 150) step *= 1.f;
		*v += wheel * step * (mod_shift ? 5.f : 1.f);
		wheel_used = 1;
		res |= 2;
	}
	if (tf_id == id && tf_enter) {
		*v = (float)atof(tf_buf);
		tf_end();
		tf_enter = 0;
		res |= 2;
	}
	if (*v < mn) *v = mn;
	if (*v > mxv) *v = mxv;
	if (dec == 0) *v = roundf(*v);
	else *v = roundf(*v * 10.f) / 10.f;
	if (*v != old) res |= 1;
	/* draw */
	uint32_t lc = sel ? C_ACC : C_TEXT;
	if (sel) rrect((float)x - 4, (float)y + 1, 2.5f, (float)h - 4, 1.2f, C_ACC, 1);
	text((float)x + 2, y + 10, label, 13, sel ? 1 : 0, lc, sel ? 1.f : 0.88f);
	if (tf_id == id) {
		int fw = 64;
		rrect((float)(x + w - fw), (float)y + 1, (float)fw, 20, 5, C_BG, 1);
		rrect_line((float)(x + w - fw), (float)y + 1, (float)fw, 20, 5, 1.2f, C_ACC, 1);
		text_r((float)(x + w - 6), y + 11, tf_buf, 13, 0, C_TEXT, 1);
	} else {
		char b[32];
		float vv = *v;
		if (dec) snprintf(b, sizeof b, "%.1f", vv);
		else if (mn < 0 && vv > 0) snprintf(b, sizeof b, "+%d", (int)vv);
		else snprintf(b, sizeof b, "%d", (int)vv);
		text_r((float)(x + w - 2), y + 10, b, 13, 1, (*v != def) ? C_ACC : C_DIM, 1);
	}
	float t = (mxv > mn) ? (*v - mn) / (mxv - mn) : 0.f;
	float t0 = (mn < 0 && mxv > 0) ? (0.f - mn) / (mxv - mn) : 0.f;
	float kx = tx + t * tw;
	rrect((float)tx, (float)ty - 1.5f, (float)tw, 3.f, 1.5f, C_PANEL3, 1);
	float fx0 = MIN(kx, tx + t0 * tw), fx1 = MAX(kx, tx + t0 * tw);
	if (fx1 - fx0 > 0.5f) rrect(fx0, (float)ty - 1.5f, fx1 - fx0, 3.f, 1.5f, sel ? C_ACC : C_ACCD, 1);
	if (mn < 0 && mxv > 0) fill_rect((int)(tx + t0 * tw), ty - 4, 1, 8, C_FAINT, 1);
	float kr = (act_id == id || on_track) ? 7.5f : 6.f;
	disc(kx, (float)ty, kr + 1.5f, 0x000000, 0.25f);
	disc(kx, (float)ty, kr, (act_id == id) ? 0xffffff : C_ACC, 1);
	return res;
}

static uint32_t group_col(const char *g) {
	if (g[0] == 'A') return C_G_ADJ;
	if (g[0] == 'G') return C_G_GEO;
	if (g[0] == 'R') return C_G_RET;
	if (g[0] == 'E') return 0x4fd1c5;
	return C_G_FIL;
}
static const char *fmt_param(const Layer *L, int i, char *buf, int cap) {
	const PDef *d = &TD[L->tool].p[i];
	if (d->ch) {
		choice_label(d->ch, CLAMP((int)L->p[i], 0, choice_max(d)), buf, cap);
		return buf;
	}
	if (d->dec) snprintf(buf, cap, "%.1f", L->p[i]);
	else if (d->mn < 0 && L->p[i] > 0) snprintf(buf, cap, "+%d", (int)L->p[i]);
	else snprintf(buf, cap, "%d", (int)L->p[i]);
	return buf;
}
static void hud_show(const Layer *L, int i) {
	const PDef *d = &TD[L->tool].p[i];
	snprintf(hud_name, sizeof hud_name, "%s", d->name);
	fmt_param(L, i, hud_text, sizeof hud_text);
	hud_until = SDL_GetTicks() + 1100;
	dirty = 1;
}

/* ================================================================== view / canvas geometry */
static int cvx, cvy, cvw, cvh; /* canvas rectangle */
static float zoom = 1.f, vcx = 0, vcy = 0;
static int view_fit = 1, last_ow = 0, last_oh = 0;
static int show_looks = 1, looks_h = 100;
static int compare_hold = 0;

static float fit_zoom(void) {
	if (!out.w || !out.h) return 1.f;
	return MIN((cvw - 40) / (float)out.w, (cvh - 40) / (float)out.h);
}
static void view_do_fit(void) {
	zoom = fit_zoom();
	if (zoom < 0.01f) zoom = 0.01f;
	vcx = out.w * 0.5f;
	vcy = out.h * 0.5f;
}
static void img2scr(float ix, float iy, float *sx, float *sy) {
	*sx = cvx + cvw * 0.5f + (ix - vcx) * zoom;
	*sy = cvy + cvh * 0.5f + (iy - vcy) * zoom;
}
static void scr2img(float sx, float sy, float *ix, float *iy) {
	*ix = vcx + (sx - cvx - cvw * 0.5f) / zoom;
	*iy = vcy + (sy - cvy - cvh * 0.5f) / zoom;
}
static void view_zoom_at(float factor, float sx, float sy) {
	float ix, iy;
	scr2img(sx, sy, &ix, &iy);
	float nz = CLAMP(zoom * factor, fit_zoom() * 0.3f, 48.f);
	zoom = nz;
	vcx = ix - (sx - cvx - cvw * 0.5f) / zoom;
	vcy = iy - (sy - cvy - cvh * 0.5f) / zoom;
	view_fit = 0;
}
static void view_clamp(void) {
	float hw = cvw * 0.5f / zoom, hh = cvh * 0.5f / zoom;
	vcx = CLAMP(vcx, -hw * 0.5f, out.w + hw * 0.5f);
	vcy = CLAMP(vcy, -hh * 0.5f, out.h + hh * 0.5f);
}
static float full_scale(void) { /* proxy px -> full-res px ratio of the displayed image */
	int fw, fh;
	full_dims(&fw, &fh);
	return out.w ? (float)out.w / (float)fw : 1.f;
}

typedef struct {
	const Mips *m;
	int x0, y0, w, h;
	float cx, cy, zoom;
} DrawCtx;
static void canvas_job(void *v, int ya, int yb) {
	DrawCtx *c = (DrawCtx *)v;
	const Mips *m = c->m;
	int lev = 0;
	float s = 1.f / c->zoom;
	while (s >= 1.9f && lev < m->n - 1) {
		s *= 0.5f;
		lev++;
	}
	int W = m->w[lev], H = m->h[lev];
	float lsc = 1.f / (float)(1 << lev);
	const uint32_t *px = m->px[lev];
	int nearest_ = c->zoom >= 5.f;
	for (int y = ya; y < yb; y++) {
		uint32_t *d = &fb[(size_t)(c->y0 + y) * FW + c->x0];
		float iy = (c->cy + (y + 0.5f - c->h * 0.5f) / c->zoom) * lsc;
		if (iy < 0 || iy >= H) continue;
		for (int x = 0; x < c->w; x++) {
			float ix = (c->cx + (x + 0.5f - c->w * 0.5f) / c->zoom) * lsc;
			if (ix < 0 || ix >= W) continue;
			if (nearest_) {
				d[x] = px[(size_t)(int)iy * W + (int)ix];
				continue;
			}
			float fx = ix - 0.5f, fy = iy - 0.5f;
			int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
			int ax = (int)((fx - x0) * 256.f), ay = (int)((fy - y0) * 256.f);
			int x1 = MIN(x0 + 1, W - 1), y1 = MIN(y0 + 1, H - 1);
			x0 = MAX(x0, 0);
			y0 = MAX(y0, 0);
			uint32_t p00 = px[(size_t)y0 * W + x0], p10 = px[(size_t)y0 * W + x1], p01 = px[(size_t)y1 * W + x0], p11 = px[(size_t)y1 * W + x1];
			uint32_t t0 = blend_px(p00, p10, ax), t1 = blend_px(p01, p11, ax);
			d[x] = blend_px(t0, t1, ay);
		}
	}
}
static void draw_mips(const Mips *m, int x0, int y0, int w, int h, float cx, float cy, float z) {
	if (!m->n) return;
	DrawCtx c = {m, x0, y0, w, h, cx, cy, z};
	Clip o = clip_push(x0, y0, w, h);
	/* only rows/cols inside the clip are valid; canvas rect is always inside fb */
	if (x0 >= 0 && y0 >= 0 && x0 + w <= FW && y0 + h <= FH) parallel(canvas_job, &c, h);
	clip_set(o);
}

/* ================================================================== actions */
static char export_path[1024] = "";
static float export_q = 92, export_scale = 100;
static char open_path[1024] = "";
static int export_busy = 0;
#define ID_EXPORT_PATH 9001
#define ID_OPEN_PATH 9002
#define ID_LOOKNAME 9004
static char look_name_buf[32];
#define ID_TEXT_FIELD 9003

static void path_split_name(const char *p, char *dir, int dcap, char *base, int bcap) {
	const char *sl = strrchr(p, '/');
	const char *bs = strrchr(p, '\\');
	if (bs && (!sl || bs > sl)) sl = bs;
	const char *name = sl ? sl + 1 : p;
	int dl = sl ? (int)(sl - p + 1) : 0;
	if (dl >= dcap) dl = dcap - 1;
	memcpy(dir, p, dl);
	dir[dl] = 0;
	snprintf(base, bcap, "%s", name);
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
}
static void default_export_path(void) {
	char dir[1024], base[256];
	path_split_name(doc.path, dir, sizeof dir, base, sizeof base);
	snprintf(export_path, sizeof export_path, "%s%s_edit.jpg", dir, base);
}
static void select_none(void) { edit_end(1); }
static int load_file(const char *p) {
	if (!doc_load(p)) {
		toast("%s", g_err);
		return 0;
	}
	view_fit = 1;
	active = -1;
	sel_param = 0;
	sel_pt = 0;
	wb_pick = 0;
	default_export_path();
	char dir[1024], base[256];
	path_split_name(p, dir, sizeof dir, base, sizeof base);
	toast("Opened %s  (%d x %d)", base, doc.w, doc.h);
	return 1;
}
static void do_open(void) {
	char p[1024];
	snprintf(p, sizeof p, "%s", doc.loaded ? doc.path : "");
	int r = native_dialog(0, p, p, sizeof p);
	if (r == 1)
		load_file(p);
	else if (r < 0) {
		modal = MODAL_OPENPATH;
		snprintf(open_path, sizeof open_path, "%s", doc.loaded ? doc.path : "");
		tf_begin(ID_OPEN_PATH, open_path, 0);
	}
	dirty = 1;
}
static void do_export_dialog(void) {
	if (!doc.loaded) return;
	edit_end(1);
	if (!export_path[0]) default_export_path();
	modal = MODAL_EXPORT;
	tf_end();
	dirty = 1;
}
static void do_undo(void) {
	if (active >= 0 && edit_dirty) {
		layer_free(&stack[active]);
		layer_copy(&stack[active], &edit_backup);
		layer_bump(&stack[active]);
		edit_dirty = 0;
		need_render = 1;
		return;
	}
	edit_end(1);
	if (hist_can_undo()) {
		hpos--;
		snap_restore(&hist[hpos]);
	}
}
static void do_redo(void) {
	edit_end(1);
	if (hist_can_redo()) {
		hpos++;
		snap_restore(&hist[hpos]);
	}
}
static void layer_reset(Layer *L) {
	switch (L->tool) {
	case T_CURVES:
		for (int c = 0; c < 4; c++) curves_reset(L, c);
		break;
	case T_SELECTIVE: L->nsel = 0; sel_pt = 0; break;
	case T_BRUSH:
		L->nstr = 0;
		L->npts = 0;
		if (L->rt) {
			brt_free(L->rt);
			L->rt = NULL;
		}
		break;
	case T_HEAL: L->nspots = 0; break;
	case T_TEXT: L->text[0] = 0; break;
	case T_CROP:
		L->cr[0] = L->cr[1] = 0;
		L->cr[2] = L->cr[3] = 1;
		L->rot = 0;
		L->p[CR_ASPECT] = 0;
		break;
	case T_ROTATE:
		L->rot = 0;
		L->flipx = L->flipy = 0;
		layer_params_default(L);
		break;
	case T_VIGNETTE:
	case T_LENS:
		L->cx = L->cy = 0.5f;
		layer_params_default(L);
		break;
	case T_GRAD:
		L->cx = 0.5f;
		L->cy = 0.38f;
		layer_params_default(L);
		break;
	case T_MIXER: memset(L->mix, 0, sizeof L->mix); break;
	default: layer_params_default(L);
	}
	touch(L);
}
static void do_delete_layer(int i) {
	if (i < 0 || i >= nlayers) return;
	if (active == i) {
		active = -1;
		edit_dirty = 0;
	}
	layer_remove(i);
	hist_push();
}
static void do_move_layer(int i, int dir) {
	int j = i + dir;
	if (i < 0 || j < 0 || i >= nlayers || j >= nlayers) return;
	Layer t = stack[i];
	stack[i] = stack[j];
	stack[j] = t;
	if (active == i) active = j;
	else if (active == j) active = i;
	layer_bump(&stack[i]);
	layer_bump(&stack[j]);
	need_render = 1;
	hist_push();
}

/* ---- auto adjustments computed from the layer's input image (pf) ---- */
static void auto_tune(Layer *L) {
	if (!pf.p) return;
	int bins[256] = {0};
	size_t n = (size_t)pf.w * pf.h, cnt = 0;
	double sum = 0;
	for (size_t i = 0; i < n; i += 7) {
		float Y = CL01(lum3(pf.p[i * 3], pf.p[i * 3 + 1], pf.p[i * 3 + 2]));
		bins[(int)(Y * 255.f)]++;
		sum += Y;
		cnt++;
	}
	float mean = (float)(sum / MAX(cnt, 1)), lo = 0, hi = 1;
	size_t acc = 0;
	for (int i = 0; i < 256; i++) {
		acc += bins[i];
		if (acc >= cnt * 0.01) {
			lo = i / 255.f;
			break;
		}
	}
	acc = 0;
	for (int i = 255; i >= 0; i--) {
		acc += bins[i];
		if (acc >= cnt * 0.01) {
			hi = i / 255.f;
			break;
		}
	}
	L->p[TU_BRI] = roundf(CLAMP((0.47f - mean) * 190.f, -50.f, 50.f));
	L->p[TU_CON] = roundf(CLAMP((0.9f - (hi - lo)) * 90.f, -10.f, 35.f));
	L->p[TU_SHA] = roundf(CLAMP((0.10f - lo) * 220.f, 0.f, 45.f));
	L->p[TU_HIG] = roundf(CLAMP((hi - 0.93f) * -400.f, -45.f, 0.f));
	L->p[TU_AMB] = 18;
	L->p[TU_SAT] = 10;
	touch(L);
}
static void auto_wb(Layer *L) {
	if (!pf.p) return;
	double r = 0, g = 0, b = 0;
	size_t n = (size_t)pf.w * pf.h, cnt = 0;
	for (size_t i = 0; i < n; i += 5) {
		float R = pf.p[i * 3], G = pf.p[i * 3 + 1], B = pf.p[i * 3 + 2];
		float Y = lum3(R, G, B);
		if (Y < 0.08f || Y > 0.95f) continue;
		r += R;
		g += G;
		b += B;
		cnt++;
	}
	if (!cnt) return;
	float t, ti;
	wb_from_sample((float)(r / cnt), (float)(g / cnt), (float)(b / cnt), &t, &ti);
	L->p[WB_TEMP] = roundf(t);
	L->p[WB_TINT] = roundf(ti);
	touch(L);
}

/* ================================================================== looks thumbnails */
#define LT_W 92
#define LT_H 58
static uint32_t *lthumb[64];
static int lt_have[64];
static Img lt_base;
static int looks_scroll = 0;
static void looks_tick(void) {
	if (!out.p || !out_ok || look_total() > 64) return;
	if (lt_serial != render_serial) {
		if (m_down[0]) return;
		/* cover-crop `out` to LT_W:LT_H */
		float ta = (float)LT_W / LT_H, sa = (float)out.w / out.h;
		int cw = out.w, ch = out.h, ox = 0, oy = 0;
		if (sa > ta) {
			cw = (int)(out.h * ta);
			ox = (out.w - cw) / 2;
		} else {
			ch = (int)(out.w / ta);
			oy = (out.h - ch) / 2;
		}
		Img crop = img_new(MAX(cw, 2), MAX(ch, 2));
		for (int y = 0; y < crop.h; y++) memcpy(crop.p + (size_t)y * crop.w * 3, out.p + ((size_t)(y + oy) * out.w + ox) * 3, (size_t)crop.w * 3 * sizeof(float));
		/* render looks on 2x thumbs for better filter behaviour, shown at 1x */
		img_free(&lt_base);
		lt_base = resize_f(&crop, LT_W * 2, LT_H * 2);
		img_free(&crop);
		for (int i = 0; i < 64; i++) lt_have[i] = 0;
		lt_serial = render_serial;
		lt_next = 0;
	}
	for (int k = 0; k < 2 && lt_next < look_total(); k++, lt_next++) {
		int i = lt_next;
		Img t = img_clone(&lt_base);
		Layer ls[CL_LAYERS];
		int n = look_layers(i, ls);
		for (int j = 0; j < n; j++) {
			apply_layer(&ls[j], &t);
			if (i >= NLOOKS) layer_free(&ls[j]);
		}
		if (!lthumb[i]) lthumb[i] = (uint32_t *)xmalloc(LT_W * LT_H * 4);
		for (int y = 0; y < LT_H; y++)
			for (int x = 0; x < LT_W; x++) {
				float rr = 0, gg = 0, bb = 0;
				for (int q = 0; q < 4; q++) {
					const float *p = t.p + ((size_t)(y * 2 + (q >> 1)) * t.w + x * 2 + (q & 1)) * 3;
					rr += p[0];
					gg += p[1];
					bb += p[2];
				}
				lthumb[i][y * LT_W + x] = ((uint32_t)CLAMP((int)(rr * 63.75f), 0, 255) << 16) | ((uint32_t)CLAMP((int)(gg * 63.75f), 0, 255) << 8) | (uint32_t)CLAMP((int)(bb * 63.75f), 0, 255);
			}
		lt_have[i] = 1;
		img_free(&t);
		dirty = 1;
	}
}
static void draw_looks(int x, int y, int w, int h) {
	fill_rect(x, y, w, h, C_PANEL, 1);
	fill_rect(x, y, w, 1, C_LINE, 1);
	text((float)x + 12, y + 14, "LOOKS", 11, 1, C_DIM, 1);
	text((float)x + 12, y + 28, "click to apply", 10, 0, C_FAINT, 1);
	text((float)x + 12, y + 42, "right-click: delete", 9, 0, C_FAINT, 1);
	int tx0 = x + 96, tw = LT_W + 8, total = look_total() * tw;
	int view = w - 96 - 8;
	int maxs = MAX(0, total - view);
	if (mouse_in(x, y, w, h) && wheel && !wheel_used) {
		looks_scroll -= wheel * 60;
		wheel_used = 1;
	}
	looks_scroll = CLAMP(looks_scroll, 0, maxs);
	Clip c = clip_push(tx0, y, view, h);
	for (int i = 0; i < look_total(); i++) {
		int bx = tx0 - looks_scroll + i * tw, by = y + 8;
		if (bx + LT_W < tx0 || bx > tx0 + view) continue;
		int hov = mouse_in(bx, by, LT_W, h - 12);
		if (hov && m_press[0]) act_id = 7000 + i;
		if (act_id == 7000 + i && m_rel[0]) {
			act_id = 0;
			if (hov && doc.loaded) look_apply(i);
		}
		if (hov && m_press[2] && i >= NLOOKS) {
			look_delete_custom(i);
			toast("Look deleted");
			break;
		}
		if (lt_have[i] && lthumb[i]) {
			for (int j = 0; j < LT_H; j++) memcpy(&fb[(size_t)(by + j) * FW + bx], &lthumb[i][j * LT_W], LT_W * 4);
		} else
			fill_rect(bx, by, LT_W, LT_H, C_PANEL2, 1);
		if (hov) rrect_line((float)bx - 1, (float)by - 1, LT_W + 2, LT_H + 2, 3, 2, C_ACC, 1);
		if (i >= NLOOKS) disc((float)bx + 9, (float)by + 9, 3.5f, C_ACC, 1);
		text_c(bx + LT_W * 0.5f, by + LT_H + 11, look_name(i), 12, hov ? 1 : 0, hov ? C_TEXT : C_DIM, 1);
	}
	clip_set(c);
	if (maxs > 0) {
		int sx = tx0, sw = view;
		fill_rect(sx, y + h - 4, sw, 2, C_PANEL2, 1);
		fill_rect(sx + (int)((float)looks_scroll / total * sw), y + h - 4, (int)((float)view / total * sw), 2, C_FAINT, 1);
	}
}

/* ================================================================== histogram */
static void draw_histogram(int x, int y, int w, int h) {
	rrect((float)x, (float)y, (float)w, (float)h, 8, C_BG, 1);
	Clip c = clip_push(x, y, w, h);
	float mxv = 1.f;
	for (int k = 0; k < 4; k++)
		for (int i = 2; i < 254; i++) mxv = MAX(mxv, histo[k][i]);
	for (int i = 0; i < 256; i++) {
		float bx = x + 6 + (w - 12) * i / 255.f;
		int bw = MAX(1, (int)((w - 12) / 256.f + 0.99f));
		for (int k = 0; k < 4; k++) {
			float v = sqrtf(MIN(histo[k][i], mxv) / mxv);
			int bh = (int)(v * (h - 14));
			static const uint32_t col[4] = {0xff5a4d, 0x4dd06a, 0x4d8dff, 0xdddddd};
			if (bh <= 0) continue;
			fill_rect((int)bx, y + h - 6 - bh, bw, bh, col[k], k == 3 ? 0.30f : 0.32f);
		}
	}
	clip_set(c);
	rrect_line((float)x, (float)y, (float)w, (float)h, 8, 1, C_LINE, 1);
}

/* ================================================================== tool panel */
static int panel_scroll = 0, panel_content = 0;
static void hint_text(int x, int *y, int w, const char *s) {
	char line[200];
	int n = 0;
	const char *p = s;
	while (*p) {
		const char *wend = p;
		while (*wend && *wend != ' ') wend++;
		int wl = (int)(wend - p);
		char tmp[200];
		snprintf(tmp, sizeof tmp, "%.*s%s%.*s", n, line, n ? " " : "", wl, p);
		if (n && text_width(tmp, 12, 0) > w) {
			line[n] = 0;
			text((float)x, *y + 8, line, 12, 0, C_DIM, 1);
			*y += 16;
			n = 0;
		}
		if (n) line[n++] = ' ';
		if (n + wl < (int)sizeof line - 2) {
			memcpy(line + n, p, wl);
			n += wl;
		}
		p = wend;
		while (*p == ' ') p++;
	}
	if (n) {
		line[n] = 0;
		text((float)x, *y + 8, line, 12, 0, C_DIM, 1);
		*y += 16;
	}
	*y += 4;
}

static void generic_params(Layer *L, int x, int *py, int w) {
	const ToolDef *d = &TD[L->tool];
	for (int i = 0; i < d->np; i++) {
		const PDef *pd = &d->p[i];
		if (pd->ch) {
			int sel = (sel_param == i);
			text((float)x + 2, *py + 8, pd->name, 13, sel ? 1 : 0, sel ? C_ACC : C_TEXT, sel ? 1.f : 0.88f);
			*py += 20;
			if (chip_row(5000 + i * 32, x, py, w, pd->ch, &L->p[i], 2)) {
				touch(L);
				sel_param = i;
				hud_show(L, i);
			}
			continue;
		}
		int r = slider(4000 + i, x, *py, w, pd->name, &L->p[i], pd->mn, pd->mx, pd->def, pd->dec, sel_param == i);
		if (r & 2) sel_param = i;
		if (r & 1) touch(L);
		*py += 42;
	}
}

/* crop helpers */
static float crop_ratio(const Layer *L) {
	static const float R[] = {0, 0, 1.f, 0.8f, 0.75f, 2.f / 3.f, 9.f / 16.f, 1.25f, 4.f / 3.f, 1.5f, 16.f / 9.f};
	int a = CLAMP((int)L->p[CR_ASPECT], 0, 10);
	float r = a == 0 ? 0.f : a == 1 ? (float)out.w / MAX(out.h, 1) : R[a];
	if (r > 0 && (L->rot & 1)) r = 1.f / r;
	return r;
}
static void crop_conform(Layer *L) {
	float R = crop_ratio(L);
	if (R <= 0) return;
	float W = (float)out.w, H = (float)out.h;
	float x0 = L->cr[0] * W, y0 = L->cr[1] * H, x1 = L->cr[2] * W, y1 = L->cr[3] * H;
	float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, w = x1 - x0, h = y1 - y0;
	float area = w * h;
	float nw = sqrtf(area * R), nh = nw / R;
	if (nw > W) {
		nw = W;
		nh = nw / R;
	}
	if (nh > H) {
		nh = H;
		nw = nh * R;
	}
	cx = CLAMP(cx, nw * 0.5f, W - nw * 0.5f);
	cy = CLAMP(cy, nh * 0.5f, H - nh * 0.5f);
	L->cr[0] = (cx - nw * 0.5f) / W;
	L->cr[2] = (cx + nw * 0.5f) / W;
	L->cr[1] = (cy - nh * 0.5f) / H;
	L->cr[3] = (cy + nh * 0.5f) / H;
}

/* ---- per-layer mask ---- */
static int mask_edit = 0, mask_erase = 0;
static float mask_size = 30.f, mask_hard = 40.f;
static void mask_clear(Layer *L) {
	L->nmpts = L->nmstr = 0;
	if (L->mrt) {
		brt_free(L->mrt);
		L->mrt = NULL;
	}
	touch(L);
}
static void mask_section(Layer *L, int x, int *py, int w) {
	if (!maskable(L->tool)) return;
	*py += 6;
	fill_rect(x, *py, w, 1, C_LINE, 1);
	*py += 10;
	text((float)x, *py + 8, "MASK", 11, 1, C_DIM, 1);
	if (button(4950, x + w - 64, *py - 2, 64, 22, L->mask_on ? "On" : "Off", L->mask_on ? BS_ACCENT : BS_NORMAL, 0)) {
		L->mask_on ^= 1;
		if (!L->mask_on) mask_edit = 0;
		touch(L);
	}
	*py += 28;
	if (!L->mask_on) {
		hint_text(x, py, w, "Limit this effect to part of the photo by painting where it shows.");
		return;
	}
	int bw = (w - 10) / 3;
	if (button(4951, x, *py, bw, 24, mask_edit ? "Painting" : "Paint", mask_edit ? BS_ACCENT : BS_NORMAL, 0)) {
		mask_edit ^= 1;
		mask_erase = 0;
	}
	if (button(4952, x + bw + 5, *py, bw, 24, "Erase", BS_NORMAL, mask_edit && mask_erase)) {
		mask_edit = 1;
		mask_erase = !mask_erase;
	}
	if (button(4953, x + 2 * (bw + 5), *py, bw, 24, "Invert", BS_NORMAL, L->mask_inv)) {
		L->mask_inv ^= 1;
		touch(L);
	}
	*py += 32;
	if (button(4954, x, *py, (w - 5) / 2, 24, "Reveal all", BS_NORMAL, 0)) {
		Stroke *s = layer_add_mstroke(L);
		s->mode = 0;
		s->amt = 1.f;
		s->rad = 2.f;
		s->hard = 0.98f;
		s->flow = 1.f;
		layer_add_mpt(L, 0.5f, 0.5f);
		s->n = 1;
		touch(L);
	}
	if (button(4955, x + (w - 5) / 2 + 5, *py, (w - 5) / 2, 24, "Clear", BS_NORMAL, 0)) mask_clear(L);
	*py += 32;
	int r = slider(4956, x, *py, w, "Brush size", &mask_size, 1, 100, 30, 0, 0);
	(void)r;
	*py += 42;
	slider(4957, x, *py, w, "Brush hardness", &mask_hard, 0, 100, 40, 0, 0);
	*py += 42;
	hint_text(x, py, w, "Red shows where the effect is visible. Paint on the photo; Erase removes it again.");
}
static void panel_tool(int x, int y, int w, int h) {
	Layer *L = &stack[active];
	const ToolDef *d = &TD[L->tool];
	Clip c0 = clip_push(x - 6, y, w + 12, h);
	int content_start = y - panel_scroll;
	int py = content_start + 2;
	/* title row */
	disc(x + 6, py + 12, 5, group_col(d->group), 1);
	text((float)x + 18, py + 12, d->name, 16, 1, C_TEXT, 1);
	int bx = x + w;
	if (button(4900, bx - 56, py + 2, 56, 22, "Reset", BS_NORMAL, 0)) {
		layer_reset(L);
		hud_until = 0;
	}
	bx -= 62;
	if (L->tool == T_TUNE && button(4901, bx - 50, py + 2, 50, 22, "Auto", BS_NORMAL, 0)) auto_tune(L);
	if (L->tool == T_WB && button(4901, bx - 50, py + 2, 50, 22, "Auto", BS_NORMAL, 0)) auto_wb(L);
	py += 34;

	switch (L->tool) {
	case T_CURVES: {
		static const char *chn[4] = {"RGB", "R", "G", "B"};
		static const uint32_t chc[4] = {0xdddddd, 0xff6a5d, 0x58d673, 0x5d9bff};
		int tx = x;
		for (int i = 0; i < 4; i++) {
			int tw = i == 0 ? 54 : 36;
			if (button(4910 + i, tx, py, tw, 24, chn[i], BS_NORMAL, curve_ch == i)) curve_ch = i;
			disc(tx + tw - 9, py + 5, 2.5f, chc[i], 1);
			tx += tw + 5;
		}
		py += 32;
		int S = w;
		int gx = x, gy = py;
		rrect((float)gx, (float)gy, (float)S, (float)S, 8, C_BG, 1);
		/* histogram backdrop */
		{
			float mxv = 1.f;
			int hk = curve_ch == 0 ? 3 : curve_ch - 1;
			for (int i = 2; i < 254; i++) mxv = MAX(mxv, histo[hk][i]);
			for (int i = 0; i < 256; i++) {
				float v = sqrtf(MIN(histo[hk][i], mxv) / mxv);
				int bh = (int)(v * (S - 12));
				fill_rect(gx + 6 + (int)((S - 12) * i / 255.f), gy + S - 6 - bh, MAX(1, (S - 12) / 256 + 1), bh, 0xffffff, 0.10f);
			}
		}
		for (int i = 1; i < 4; i++) {
			fill_rect(gx + 6 + (S - 12) * i / 4, gy + 6, 1, S - 12, C_LINE, 1);
			fill_rect(gx + 6, gy + 6 + (S - 12) * i / 4, S - 12, 1, C_LINE, 1);
		}
		line_aa((float)gx + 6, (float)gy + S - 6, (float)gx + S - 6, (float)gy + 6, 1, C_FAINT, 0.5f);
		float lut[256];
		pchip_lut(L->cp[curve_ch], L->ncp[curve_ch], lut, 256);
		uint32_t cc = chc[curve_ch];
		float ppx = 0, ppy = 0;
		for (int i = 0; i < 256; i += 2) {
			float px_ = gx + 6 + (S - 12) * i / 255.f, py_ = gy + S - 6 - (S - 12) * lut[i];
			if (i) line_aa(ppx, ppy, px_, py_, 2.f, cc, 1);
			ppx = px_;
			ppy = py_;
		}
		int n = L->ncp[curve_ch];
		static int drag_pt = -1;
		float mxn = (mx - (gx + 6)) / (float)(S - 12), myn = 1.f - (my - (gy + 6)) / (float)(S - 12);
		int hov_pt = -1;
		for (int i = 0; i < n; i++) {
			float qx = gx + 6 + (S - 12) * L->cp[curve_ch][i][0], qy = gy + S - 6 - (S - 12) * L->cp[curve_ch][i][1];
			if (mouse_in(gx, gy, S, S) && (mx - qx) * (mx - qx) + (my - qy) * (my - qy) < 100) hov_pt = i;
		}
		if (mouse_in(gx, gy, S, S) && m_press[0]) {
			if (hov_pt >= 0 && m_dbl && hov_pt > 0 && hov_pt < n - 1) {
				for (int k = hov_pt; k < n - 1; k++) {
					L->cp[curve_ch][k][0] = L->cp[curve_ch][k + 1][0];
					L->cp[curve_ch][k][1] = L->cp[curve_ch][k + 1][1];
				}
				L->ncp[curve_ch]--;
				touch(L);
			} else if (hov_pt >= 0) {
				drag_pt = hov_pt;
				act_id = 4950;
			} else if (n < MAXCP) {
				float nx = CL01(mxn);
				int at = 0;
				while (at < n && L->cp[curve_ch][at][0] < nx) at++;
				if (at == 0) at = 1;
				if (at >= n) at = n - 1;
				for (int k = n; k > at; k--) {
					L->cp[curve_ch][k][0] = L->cp[curve_ch][k - 1][0];
					L->cp[curve_ch][k][1] = L->cp[curve_ch][k - 1][1];
				}
				L->cp[curve_ch][at][0] = nx;
				L->cp[curve_ch][at][1] = CL01(myn);
				L->ncp[curve_ch]++;
				drag_pt = at;
				act_id = 4950;
				touch(L);
			}
		}
		if (mouse_in(gx, gy, S, S) && m_press[2] && hov_pt > 0 && hov_pt < n - 1) {
			for (int k = hov_pt; k < n - 1; k++) {
				L->cp[curve_ch][k][0] = L->cp[curve_ch][k + 1][0];
				L->cp[curve_ch][k][1] = L->cp[curve_ch][k + 1][1];
			}
			L->ncp[curve_ch]--;
			touch(L);
		}
		if (act_id == 4950) {
			if (m_down[0] && drag_pt >= 0 && drag_pt < L->ncp[curve_ch]) {
				float lo = drag_pt == 0 ? 0.f : L->cp[curve_ch][drag_pt - 1][0] + 0.01f;
				float hi = drag_pt == L->ncp[curve_ch] - 1 ? 1.f : L->cp[curve_ch][drag_pt + 1][0] - 0.01f;
				float nx = (drag_pt == 0 || drag_pt == L->ncp[curve_ch] - 1) ? L->cp[curve_ch][drag_pt][0] : CLAMP(mxn, lo, hi);
				float ny = CL01(myn);
				if (nx != L->cp[curve_ch][drag_pt][0] || ny != L->cp[curve_ch][drag_pt][1]) {
					L->cp[curve_ch][drag_pt][0] = nx;
					L->cp[curve_ch][drag_pt][1] = ny;
					touch(L);
				}
			} else
				act_id = 0;
		}
		for (int i = 0; i < L->ncp[curve_ch]; i++) {
			float qx = gx + 6 + (S - 12) * L->cp[curve_ch][i][0], qy = gy + S - 6 - (S - 12) * L->cp[curve_ch][i][1];
			disc(qx, qy, (i == hov_pt || (act_id == 4950 && i == drag_pt)) ? 7.f : 5.5f, 0x000000, 0.35f);
			disc(qx, qy, (i == hov_pt || (act_id == 4950 && i == drag_pt)) ? 6.f : 4.5f, i == hov_pt ? 0xffffff : cc, 1);
		}
		rrect_line((float)gx, (float)gy, (float)S, (float)S, 8, 1, C_LINE, 1);
		py += S + 8;
		static const char *pn[4] = {"Contrast", "Lift", "Fade", "Flat"};
		int cxp = x;
		for (int i = 0; i < 4; i++) {
			int tw = (int)text_width(pn[i], 12, 1) + 20;
			if (button(4920 + i, cxp, py, tw, 24, pn[i], BS_NORMAL, 0)) {
				float (*P)[2] = L->cp[curve_ch];
				int np_ = 2;
				if (i == 0) {
					float t[4][2] = {{0, 0}, {0.25f, 0.18f}, {0.75f, 0.83f}, {1, 1}};
					memcpy(P, t, sizeof t);
					np_ = 4;
				} else if (i == 1) {
					float t[3][2] = {{0, 0}, {0.4f, 0.52f}, {1, 1}};
					memcpy(P, t, sizeof t);
					np_ = 3;
				} else if (i == 2) {
					float t[2][2] = {{0, 0.09f}, {1, 0.93f}};
					memcpy(P, t, sizeof t);
				} else {
					float t[3][2] = {{0, 0.1f}, {0.5f, 0.5f}, {1, 0.9f}};
					memcpy(P, t, sizeof t);
					np_ = 3;
				}
				L->ncp[curve_ch] = np_;
				touch(L);
			}
			cxp += tw + 5;
		}
		py += 34;
		hint_text(x, &py, w, "Click the curve to add a point, drag to move, double-click or right-click a point to remove it.");
	} break;

	case T_SELECTIVE: {
		hint_text(x, &py, w, "Click the photo to place a control point (up to 8). Drag a point to move it. Wheel changes its reach. Right-click removes it.");
		if (L->nsel == 0) {
			hint_text(x, &py, w, "No points yet.");
			break;
		}
		sel_pt = CLAMP(sel_pt, 0, L->nsel - 1);
		SelPt *s = &L->sel[sel_pt];
		static const char *nm[4] = {"Brightness", "Contrast", "Saturation", "Structure"};
		for (int i = 0; i < 4; i++) {
			int r = slider(4040 + i, x, py, w, nm[i], &s->v[i], -100, 100, 0, 0, sel_param == i);
			if (r & 2) sel_param = i;
			if (r & 1) touch(L);
			py += 42;
		}
		float rad = s->rad * 100.f;
		int r = slider(4050, x, py, w, "Reach", &rad, 2, 60, 15, 0, sel_param == 4);
		if (r & 2) sel_param = 4;
		if (r & 1) {
			s->rad = rad / 100.f;
			touch(L);
		}
		py += 46;
		char b[40];
		snprintf(b, sizeof b, "Remove point %d", sel_pt + 1);
		if (button(4960, x, py, w, 26, b, BS_NORMAL, 0)) {
			for (int k = sel_pt; k < L->nsel - 1; k++) L->sel[k] = L->sel[k + 1];
			L->nsel--;
			sel_pt = MAX(0, sel_pt - 1);
			touch(L);
		}
		py += 34;
	} break;

	case T_CROP: {
		float before = L->p[CR_ASPECT];
		text((float)x + 2, py + 8, "Aspect ratio", 13, 0, C_TEXT, 1);
		py += 20;
		if (chip_row(5000, x, &py, w, TD[T_CROP].p[0].ch, &L->p[CR_ASPECT], 4)) {
			if (before != L->p[CR_ASPECT]) crop_conform(L);
			touch(L);
		}
		if (button(4930, x, py, 120, 26, "Swap orientation", BS_NORMAL, 0)) {
			L->rot ^= 1;
			crop_conform(L);
			touch(L);
		}
		if (button(4931, x + 126, py, 70, 26, "Full", BS_NORMAL, 0)) {
			L->cr[0] = L->cr[1] = 0;
			L->cr[2] = L->cr[3] = 1;
			crop_conform(L);
			touch(L);
		}
		py += 36;
		int cw = (int)roundf((L->cr[2] - L->cr[0]) * out.w), ch = (int)roundf((L->cr[3] - L->cr[1]) * out.h);
		float fs = full_scale();
		char b[80];
		snprintf(b, sizeof b, "Result: %d x %d px", (int)(cw / fs + 0.5f), (int)(ch / fs + 0.5f));
		text((float)x + 2, py + 8, b, 13, 1, C_ACC, 1);
		py += 24;
		hint_text(x, &py, w, "Drag the corners or edges, drag inside to move. Enter applies the crop.");
	} break;

	case T_ROTATE: {
		int bw = (w - 10) / 2;
		if (button(4940, x, py, bw, 28, "Rotate left", BS_NORMAL, 0)) {
			L->rot = (L->rot + 3) & 3;
			touch(L);
			view_fit = 1;
		}
		if (button(4941, x + bw + 10, py, bw, 28, "Rotate right", BS_NORMAL, 0)) {
			L->rot = (L->rot + 1) & 3;
			touch(L);
			view_fit = 1;
		}
		py += 34;
		if (button(4942, x, py, bw, 28, "Flip horizontal", BS_NORMAL, L->flipx)) {
			L->flipx ^= 1;
			touch(L);
		}
		if (button(4943, x + bw + 10, py, bw, 28, "Flip vertical", BS_NORMAL, L->flipy)) {
			L->flipy ^= 1;
			touch(L);
		}
		py += 40;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag left or right on the photo to straighten.");
	} break;

	case T_WB: {
		generic_params(L, x, &py, w);
		if (button(4944, x, py, w, 28, wb_pick ? "Click a neutral grey on the photo..." : "Pick neutral point", BS_NORMAL, wb_pick)) wb_pick = !wb_pick;
		py += 36;
	} break;

	case T_TEXT: {
		text((float)x + 2, py + 8, "Text", 13, 0, C_TEXT, 1);
		py += 18;
		int ch = text_field(ID_TEXT_FIELD, x, py, w, 30, L->text, sizeof L->text, "Type your text here");
		if (ch & 1) touch(L);
		py += 40;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag the text on the photo to position it.");
	} break;

	case T_BRUSH: {
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Paint on the photo. Wheel changes brush size, Ctrl+wheel zooms. Strength sets how strong new strokes are; use Eraser to remove strokes.");
	} break;
	case T_HEAL:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Click (or drag) over blemishes and dust spots. Wheel changes the size.");
		break;
	case T_VIGNETTE:
	case T_LENS:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag on the photo to move the centre.");
		break;
	case T_GRAD:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag on the photo to place the line (the effect applies above it). Wheel rotates it.");
		break;
	case T_MIXER: {
		static int mix_ch = 0;
		static const uint32_t band[8] = {0xff4a4a, 0xff9a3c, 0xf2d83a, 0x4fd16a, 0x35d0c8, 0x4a86ff, 0x9a64f0, 0xf064c8};
		int cxp = x, cyp = py;
		for (int i = 0; i < 8; i++) {
			char lb[16];
			choice_label(MIX_NAMES, i, lb, sizeof lb);
			int tw = (int)text_width(lb, 12, 1) + 24;
			if (cxp + tw > x + w) {
				cxp = x;
				cyp += 28;
			}
			int hov = mouse_in(cxp, cyp, tw, 24), on = i == mix_ch;
			if (hov && m_press[0]) mix_ch = i;
			rrect((float)cxp, (float)cyp, (float)tw, 24, 12, on ? C_PANEL3 : (hov ? C_PANEL2 : C_PANEL2), 1);
			if (on) rrect_line((float)cxp, (float)cyp, (float)tw, 24, 12, 1.6f, band[i], 1);
			disc(cxp + 11.f, cyp + 12.f, 4.5f, band[i], 1);
			text(cxp + 20.f, cyp + 12.f, lb, 12, 1, C_TEXT, 1);
			cxp += tw + 5;
		}
		py = cyp + 36;
		static const char *nm[3] = {"Hue", "Saturation", "Luminance"};
		for (int k = 0; k < 3; k++) {
			int r = slider(4070 + k, x, py, w, nm[k], &L->mix[mix_ch][k], -100, 100, 0, 0, sel_param == k);
			if (r & 2) sel_param = k;
			if (r & 1) touch(L);
			py += 42;
		}
		hint_text(x, &py, w, "Pick a colour band, then shift its hue, saturation or brightness. Each band blends smoothly into its neighbours.");
	} break;
	case T_DOUBLE: {
		char bn[64], dir[8];
		path_split_name(L->path, dir, sizeof dir, bn, sizeof bn);
		if (button(4960, x, py, w, 30, L->path[0] ? bn : "Choose second photo...", L->path[0] ? BS_NORMAL : BS_ACCENT, 0)) {
			char p[1024];
			snprintf(p, sizeof p, "%s", L->path[0] ? L->path : doc.path);
			int r = native_dialog(0, p, p, sizeof p);
			if (r == 1) {
				snprintf(L->path, sizeof L->path, "%s", p);
				touch(L);
			} else if (r < 0)
				toast("No file dialog here - drop the second photo onto the window");
		}
		py += 38;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Tip: drop an image file onto the window to use it as the second exposure.");
	} break;
	default:
		generic_params(L, x, &py, w);
		if (L->tool != T_PERSP && L->tool != T_PORTRAIT && L->tool != T_FRAME) hint_text(x, &py, w, "Tip: drag up/down on the photo to pick a parameter, left/right to change it.");
		break;
	}
	mask_section(L, x, &py, w);
	panel_content = py - content_start + 8;
	clip_set(c0);
}

/* ================================================================== edit stack */
static int stack_scroll = 0;
static void draw_stack(int x, int y, int w, int h) {
	fill_rect(x, y, w, h, C_PANEL, 1);
	fill_rect(x, y, w, 1, C_LINE, 1);
	text((float)x + 14, y + 16, "EDIT STACK", 11, 1, C_DIM, 1);
	char b[32];
	snprintf(b, sizeof b, "%d", nlayers);
	text((float)x + 14 + text_width("EDIT STACK", 11, 1) + 8, y + 16, b, 11, 1, C_FAINT, 1);
	if (button(4980, x + w - 148, y + 5, 62, 20, "Revert", BS_NORMAL, 0) && nlayers) {
		edit_end(0);
		stack_clear();
		hist_push();
		need_render = 1;
		toast("Reverted all edits  (Ctrl+Z to undo)");
	}
	if (button(4981, x + w - 80, y + 5, 70, 20, "Save Look", BS_NORMAL, 0)) {
		if (!nlayers) toast("Nothing to save - make some edits first");
		else {
			edit_end(1);
			snprintf(look_name_buf, sizeof look_name_buf, "My Look %d", ncl + 1);
			modal = MODAL_SAVELOOK;
			tf_begin(ID_LOOKNAME, look_name_buf, 0);
		}
	}
	int rowh = 30, top = y + 30, vis = h - 34;
	int total = nlayers * rowh;
	if (mouse_in(x, y, w, h) && wheel && !wheel_used) {
		stack_scroll -= wheel * 30;
		wheel_used = 1;
	}
	stack_scroll = CLAMP(stack_scroll, 0, MAX(0, total - vis));
	Clip c = clip_push(x, top, w, vis);
	if (nlayers == 0) text((float)x + 14, top + 18, "No edits yet.", 12, 0, C_FAINT, 1);
	int do_del = -1, do_up = -1, do_dn = -1, do_vis = -1, do_sel = -1;
	for (int k = 0; k < nlayers; k++) {
		int i = nlayers - 1 - k; /* newest on top */
		int ry = top - stack_scroll + k * rowh;
		if (ry + rowh < top || ry > top + vis) continue;
		Layer *L = &stack[i];
		int isact = i == active;
		int hov = mouse_in(x + 6, ry, w - 12, rowh - 2);
		if (isact) rrect((float)x + 6, (float)ry, (float)w - 12, (float)rowh - 2, 6, C_PANEL3, 1);
		else if (hov) rrect((float)x + 6, (float)ry, (float)w - 12, (float)rowh - 2, 6, C_PANEL2, 1);
		if (isact) fill_rect(x + 6, ry + 6, 3, rowh - 14, C_ACC, 1);
		/* eye */
		int ex = x + 24;
		int hoe = mouse_in(ex - 11, ry + 2, 22, rowh - 6);
		icon_eye((float)ex, ry + rowh * 0.5f - 1, 7, L->visible ? C_TEXT : C_FAINT, hoe ? 1.f : 0.85f, L->visible);
		if (hoe && m_press[0]) do_vis = i;
		/* name */
		int nx = x + 42, nw = w - 42 - 78;
		uint32_t gc = group_col(TD[L->tool].group);
		disc((float)nx + 3, ry + rowh * 0.5f - 1, 3.2f, gc, L->visible ? 1.f : 0.4f);
		if (L->mask_on) {
			rrect((float)nx + nw - 16, ry + 7, 16, 14, 4, C_ACC, 0.85f);
			text_c(nx + nw - 8.f, ry + 14.f, "M", 10, 1, 0x0b2a27, 1);
			nw -= 20;
		}
		text_fit((float)nx + 12, ry + rowh * 0.5f - 1, TD[L->tool].name, (float)nw - 12, 13, isact ? 1 : 0, L->visible ? C_TEXT : C_FAINT, 1);
		/* buttons */
		int bx = x + w - 74;
		int hu = mouse_in(bx, ry + 3, 20, rowh - 8), hd = mouse_in(bx + 22, ry + 3, 20, rowh - 8), hx = mouse_in(bx + 46, ry + 3, 24, rowh - 8);
		float cy = ry + rowh * 0.5f - 1;
		tri(bx + 10, cy - 4, bx + 5, cy + 3, bx + 15, cy + 3, C_DIM, hu ? 1.f : 0.5f);
		tri(bx + 32, cy + 4, bx + 27, cy - 3, bx + 37, cy - 3, C_DIM, hd ? 1.f : 0.5f);
		icon_x((float)bx + 58, cy, 4.2f, hx ? C_WARN : C_DIM, hx ? 1.f : 0.6f);
		if (m_press[0]) {
			if (hu) do_up = i;
			else if (hd) do_dn = i;
			else if (hx) do_del = i;
			else if (!hoe && hov) do_sel = i;
		}
	}
	clip_set(c);
	if (do_vis >= 0) {
		stack[do_vis].visible ^= 1;
		layer_bump(&stack[do_vis]);
		need_render = 1;
		hist_push();
	} else if (do_up >= 0)
		do_move_layer(do_up, 1);
	else if (do_dn >= 0)
		do_move_layer(do_dn, -1);
	else if (do_del >= 0)
		do_delete_layer(do_del);
	else if (do_sel >= 0)
		select_layer(do_sel);
}

/* ================================================================== canvas & on-canvas tools */
static int cdrag = 0;           /* 0 none, 1 pan, 2 tool drag, 3 scrub */
static int cd_a = 0;            /* sub-kind / handle mask / point index */
static float cd_x0, cd_y0;      /* image-space anchor at drag start */
static float cd_r[4];           /* crop rect at drag start (px) */
static int scrub_axis = 0;
static float scrub_ax = 0, scrub_ay = 0, scrub_acc = 0;
static int cursor_kind = 0;     /* 0 arrow, 1 crosshair, 2 move, 3 ns, 4 ew, 5 nwse, 6 nesw, 7 hand */
static int hold_space = 0;

static int is_scrub_tool(int t) {
	switch (t) {
	case T_TUNE: case T_DETAILS: case T_WB: case T_ROTATE: case T_PERSP: case T_PORTRAIT: case T_FRAME:
	case T_BW: case T_DRAMA: case T_TONAL: case T_HDR: case T_GLOW: case T_VINTAGE: case T_RETRO: case T_GRAIN: case T_NOIR: case T_GRUNGE:
	case T_BLOOM: case T_DEHAZE: case T_SPLIT: case T_DENOISE: case T_CHROMA: case T_STREAK:
		return 1;
	}
	return 0;
}
/* indexes of non-choice parameters that scrubbing / arrow keys can visit */
static int scrub_list(const Layer *L, int *idx) {
	int n = 0;
	const ToolDef *d = &TD[L->tool];
	for (int i = 0; i < d->np; i++)
		if (!d->p[i].ch) idx[n++] = i;
	return n;
}
static void param_nudge(Layer *L, int pi, float dir, int fine_mode) {
	const PDef *d = &TD[L->tool].p[pi];
	if (d->ch) {
		int n = choice_max(d) + 1;
		L->p[pi] = (float)(((int)L->p[pi] + (dir > 0 ? 1 : n - 1)) % n);
	} else {
		float step = d->dec ? 0.5f : 1.f;
		float range = d->mx - d->mn;
		if (range > 150) step *= 1.f;
		step *= fine_mode ? 5.f : 1.f;
		L->p[pi] = CLAMP(L->p[pi] + dir * step, d->mn, d->mx);
		if (!d->dec) L->p[pi] = roundf(L->p[pi]);
	}
	touch(L);
	hud_show(L, pi);
}
static void param_select_step(Layer *L, int dir) {
	const ToolDef *d = &TD[L->tool];
	if (d->np == 0) return;
	sel_param = (sel_param + dir + d->np) % d->np;
	hud_show(L, sel_param);
}

static float brush_rad(const Layer *L) { return 0.004f + L->p[BR_SIZE] / 100.f * 0.12f; }
static float heal_rad(const Layer *L) { return 0.004f + L->p[HE_SIZE] / 100.f * 0.08f; }

static void dim_outside(float sx0, float sy0, float sx1, float sy1, float a) {
	int x0 = (int)sx0, y0 = (int)sy0, x1 = (int)sx1, y1 = (int)sy1;
	fill_rect(cvx, cvy, cvw, y0 - cvy, 0x000000, a);
	fill_rect(cvx, y1, cvw, cvy + cvh - y1, 0x000000, a);
	fill_rect(cvx, y0, x0 - cvx, y1 - y0, 0x000000, a);
	fill_rect(x1, y0, cvx + cvw - x1, y1 - y0, 0x000000, a);
}

static void crop_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h;
	float r[4] = {L->cr[0] * W, L->cr[1] * H, L->cr[2] * W, L->cr[3] * H};
	float sx0, sy0, sx1, sy1;
	img2scr(r[0], r[1], &sx0, &sy0);
	img2scr(r[2], r[3], &sx1, &sy1);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	const float T = 12.f;
	int mask = 0, inside = 0;
	if (mx >= sx0 - T && mx <= sx1 + T && my >= sy0 - T && my <= sy1 + T) {
		int wv = my >= sy0 - T && my <= sy1 + T, wh = mx >= sx0 - T && mx <= sx1 + T;
		if (fabsf(mx - sx0) <= T && wv) mask |= 1;
		if (fabsf(mx - sx1) <= T && wh == 1 && wv && !(mask & 1)) mask |= 2;
		else if (fabsf(mx - sx1) <= T && wv && (mask & 1) && fabsf(mx - sx1) < fabsf(mx - sx0)) mask = (mask & ~1) | 2;
		if (fabsf(my - sy0) <= T && wh) mask |= 4;
		if (fabsf(my - sy1) <= T && wh && !(mask & 4)) mask |= 8;
		inside = mx > sx0 && mx < sx1 && my > sy0 && my < sy1;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && (mask || inside)) {
		cdrag = 2;
		cd_a = mask ? mask : 0;
		cd_x0 = ix;
		cd_y0 = iy;
		memcpy(cd_r, r, sizeof r);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float R = crop_ratio(L), MINS = 24.f;
			float n[4];
			memcpy(n, cd_r, sizeof n);
			if (cd_a == 0) {
				float dx = ix - cd_x0, dy = iy - cd_y0, w = n[2] - n[0], h = n[3] - n[1];
				n[0] = CLAMP(n[0] + dx, 0.f, W - w);
				n[1] = CLAMP(n[1] + dy, 0.f, H - h);
				n[2] = n[0] + w;
				n[3] = n[1] + h;
			} else {
				if (cd_a & 1) n[0] = CLAMP(ix, 0.f, n[2] - MINS);
				if (cd_a & 2) n[2] = CLAMP(ix, n[0] + MINS, W);
				if (cd_a & 4) n[1] = CLAMP(iy, 0.f, n[3] - MINS);
				if (cd_a & 8) n[3] = CLAMP(iy, n[1] + MINS, H);
				if (R > 0.f) {
					float w = n[2] - n[0], h = n[3] - n[1];
					int hz = cd_a & 3, vt = cd_a & 12;
					if (hz && !vt) {
						float cy = (cd_r[1] + cd_r[3]) * 0.5f, maxh = 2.f * MIN(cy, H - cy);
						h = w / R;
						if (h > maxh) {
							h = maxh;
							w = h * R;
						}
						if (cd_a & 1) n[0] = n[2] - w;
						else n[2] = n[0] + w;
						n[1] = cy - h * 0.5f;
						n[3] = cy + h * 0.5f;
					} else if (vt && !hz) {
						float cx = (cd_r[0] + cd_r[2]) * 0.5f, maxw = 2.f * MIN(cx, W - cx);
						w = h * R;
						if (w > maxw) {
							w = maxw;
							h = w / R;
						}
						if (cd_a & 4) n[1] = n[3] - h;
						else n[3] = n[1] + h;
						n[0] = cx - w * 0.5f;
						n[2] = cx + w * 0.5f;
					} else {
						float ax = (cd_a & 1) ? cd_r[2] : cd_r[0], ay = (cd_a & 4) ? cd_r[3] : cd_r[1];
						if (w / h > R) h = w / R;
						else w = h * R;
						float maxw = (cd_a & 1) ? ax : W - ax, maxh = (cd_a & 4) ? ay : H - ay;
						float s = MIN(1.f, MIN(maxw / w, maxh / h));
						w *= s;
						h *= s;
						n[0] = (cd_a & 1) ? ax - w : ax;
						n[2] = (cd_a & 1) ? ax : ax + w;
						n[1] = (cd_a & 4) ? ay - h : ay;
						n[3] = (cd_a & 4) ? ay : ay + h;
					}
				}
			}
			float nn[4] = {n[0] / W, n[1] / H, n[2] / W, n[3] / H};
			if (memcmp(nn, L->cr, sizeof nn) != 0) {
				memcpy(L->cr, nn, sizeof nn);
				touch(L);
			}
		} else
			cdrag = 0;
	}
	img2scr(L->cr[0] * W, L->cr[1] * H, &sx0, &sy0);
	img2scr(L->cr[2] * W, L->cr[3] * H, &sx1, &sy1);
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	dim_outside(sx0, sy0, sx1, sy1, 0.58f);
	for (int i = 1; i < 3; i++) {
		fill_rect((int)(sx0 + (sx1 - sx0) * i / 3), (int)sy0, 1, (int)(sy1 - sy0), 0xffffff, 0.28f);
		fill_rect((int)sx0, (int)(sy0 + (sy1 - sy0) * i / 3), (int)(sx1 - sx0), 1, 0xffffff, 0.28f);
	}
	frame_rect((int)sx0, (int)sy0, (int)(sx1 - sx0), (int)(sy1 - sy0), 0xffffff, 0.9f);
	float bl = 18;
	float xs[2] = {sx0, sx1}, ys[2] = {sy0, sy1};
	for (int a = 0; a < 2; a++)
		for (int b = 0; b < 2; b++) {
			float dx = a ? -1.f : 1.f, dy = b ? -1.f : 1.f;
			line_aa(xs[a] - dx * 1.5f, ys[b] - dy * 1.5f, xs[a] + dx * bl, ys[b] - dy * 1.5f, 3.f, 0xffffff, 1);
			line_aa(xs[a] - dx * 1.5f, ys[b] - dy * 1.5f, xs[a] - dx * 1.5f, ys[b] + dy * bl, 3.f, 0xffffff, 1);
		}
	clip_set(c);
	/* cursor */
	if (in_canvas || cdrag == 2) {
		int m = cdrag == 2 ? cd_a : mask;
		if (cdrag == 2 && cd_a == 0) cursor_kind = 2;
		else if (m == 1 || m == 2) cursor_kind = 4;
		else if (m == 4 || m == 8) cursor_kind = 3;
		else if (m == 5 || m == 10) cursor_kind = 5;
		else if (m == 6 || m == 9) cursor_kind = 6;
		else if (inside) cursor_kind = 2;
	}
}

static void selective_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	int nearest_ = -1;
	for (int i = 0; i < L->nsel; i++) {
		float sx, sy;
		img2scr(L->sel[i].x * W, L->sel[i].y * H, &sx, &sy);
		if ((mx - sx) * (mx - sx) + (my - sy) * (my - sy) < 16.f * 16.f) nearest_ = i;
	}
	if (in_canvas && cdrag == 0 && !mod_ctrl) {
		if (m_press[2] && nearest_ >= 0) {
			for (int k = nearest_; k < L->nsel - 1; k++) L->sel[k] = L->sel[k + 1];
			L->nsel--;
			sel_pt = CLAMP(sel_pt, 0, MAX(0, L->nsel - 1));
			touch(L);
			m_press[2] = 0;
		} else if (m_press[0]) {
			if (nearest_ >= 0) {
				sel_pt = nearest_;
				cdrag = 2;
			} else if (L->nsel < MAXSEL && ix >= 0 && iy >= 0 && ix < W && iy < H) {
				SelPt *s = &L->sel[L->nsel];
				memset(s, 0, sizeof *s);
				s->x = ix / W;
				s->y = iy / H;
				s->rad = 0.15f;
				sel_pt = L->nsel++;
				cdrag = 2;
				touch(L);
			}
		}
	}
	if (cdrag == 2 && L->nsel > 0) {
		if (m_down[0]) {
			SelPt *s = &L->sel[CLAMP(sel_pt, 0, L->nsel - 1)];
			float nx = CL01(ix / W), ny = CL01(iy / H);
			if (nx != s->x || ny != s->y) {
				s->x = nx;
				s->y = ny;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas && wheel && !mod_ctrl && L->nsel > 0 && !wheel_used) {
		SelPt *s = &L->sel[CLAMP(sel_pt, 0, L->nsel - 1)];
		s->rad = CLAMP(s->rad * powf(1.1f, (float)wheel), 0.02f, 0.6f);
		wheel_used = 1;
		touch(L);
	}
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	for (int i = 0; i < L->nsel; i++) {
		float sx, sy;
		img2scr(L->sel[i].x * W, L->sel[i].y * H, &sx, &sy);
		int s = i == sel_pt;
		if (s) {
			ring(sx, sy, L->sel[i].rad * m * zoom, 1.6f, 0xffffff, 0.55f);
			ring(sx, sy, L->sel[i].rad * m * zoom + 1.5f, 1.f, 0x000000, 0.3f);
		}
		disc(sx, sy, s ? 13.f : 11.f, 0x000000, 0.45f);
		disc(sx, sy, s ? 11.5f : 9.5f, s ? C_ACC : 0xffffff, 1);
		char b[4];
		snprintf(b, sizeof b, "%d", i + 1);
		text_c(sx, sy, b, 12, 1, s ? 0x0b2a27 : 0x222222, 1);
	}
	clip_set(c);
	cursor_kind = nearest_ >= 0 ? 2 : 1;
}

static void brush_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = brush_rad(L);
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		L->p[BR_SIZE] = CLAMP(L->p[BR_SIZE] + wheel * (mod_shift ? 1.f : 3.f), 1.f, 100.f);
		wheel_used = 1;
		touch(L);
		L->touched = L->nstr > 0;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		Stroke *s = layer_add_stroke(L);
		int mode = (int)L->p[BR_MODE];
		s->mode = mode;
		s->amt = mode == 4 ? 0.f : L->p[BR_STR] / 100.f;
		s->rad = rad;
		s->hard = L->p[BR_HARD] / 100.f;
		s->flow = L->p[BR_FLOW] / 100.f * 0.6f;
		layer_add_pt(L, ix / W, iy / H);
		L->str[L->nstr - 1].n = 1;
		cdrag = 2;
		cd_x0 = mx;
		cd_y0 = my;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = mx - cd_x0, dy = my - cd_y0;
			if (dx * dx + dy * dy >= 1.5f * 1.5f && L->nstr > 0) {
				layer_add_pt(L, ix / W, iy / H);
				L->str[L->nstr - 1].n++;
				cd_x0 = (float)mx;
				cd_y0 = (float)my;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		float hard = L->p[BR_HARD] / 100.f;
		if (hard < 0.95f) ring((float)mx, (float)my, r * hard, 1.f, 0xffffff, 0.4f);
		clip_set(c);
		cursor_kind = 8;
	}
}

static void heal_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = heal_rad(L);
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		L->p[HE_SIZE] = CLAMP(L->p[HE_SIZE] + wheel * (mod_shift ? 1.f : 3.f), 3.f, 100.f);
		wheel_used = 1;
		L->touched = L->nspots > 0;
		layer_bump(L);
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && ix >= 0 && iy >= 0 && ix < W && iy < H) {
		layer_add_spot(L, ix / W, iy / H, rad);
		cdrag = 2;
		cd_x0 = ix;
		cd_y0 = iy;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = ix - cd_x0, dy = iy - cd_y0, s = rad * m * 1.3f;
			if (dx * dx + dy * dy >= s * s && ix >= 0 && iy >= 0 && ix < W && iy < H) {
				layer_add_spot(L, ix / W, iy / H, rad);
				cd_x0 = ix;
				cd_y0 = iy;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		line_aa((float)mx - 4, (float)my, (float)mx + 4, (float)my, 1.2f, 0xffffff, 0.8f);
		line_aa((float)mx, (float)my - 4, (float)mx, (float)my + 4, 1.2f, 0xffffff, 0.8f);
		clip_set(c);
		cursor_kind = 8;
	}
}

/* vignette / lens / text: drag the centre */
static void center_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		cdrag = 2;
		cd_x0 = (L->cx - ix / W);
		cd_y0 = (L->cy - iy / H);
		if (L->tool != T_TEXT) cd_x0 = cd_y0 = 0;
		L->cx = CL01(ix / W + cd_x0);
		L->cy = CL01(iy / H + cd_y0);
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float nx = CL01(ix / W + cd_x0), ny = CL01(iy / H + cd_y0);
			if (nx != L->cx || ny != L->cy) {
				L->cx = nx;
				L->cy = ny;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas && wheel && !mod_ctrl && !wheel_used && L->tool == T_LENS) {
		L->p[LN_SIZE] = CLAMP(L->p[LN_SIZE] + wheel * 3.f, 0.f, 100.f);
		wheel_used = 1;
		touch(L);
	}
	if (in_canvas && wheel && !mod_ctrl && !wheel_used && L->tool == T_GRAD) {
		float a = L->p[GD_ANG] + wheel * 5.f;
		if (a > 180.f) a -= 360.f;
		if (a < -180.f) a += 360.f;
		L->p[GD_ANG] = a;
		wheel_used = 1;
		touch(L);
	}
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	float sx, sy;
	img2scr(L->cx * W, L->cy * H, &sx, &sy);
	if (L->tool == T_GRAD) {
		float th = L->p[GD_ANG] * PI_F / 180.f, tx = cosf(th), ty = -sinf(th), nx = sinf(th), ny = cosf(th), L2 = 4000.f;
		float f = (0.004f + L->p[GD_FEA] / 100.f * 0.5f) * m * zoom;
		line_aa(sx - tx * L2, sy - ty * L2, sx + tx * L2, sy + ty * L2, 1.8f, 0xffffff, 0.9f);
		for (int s = -1; s <= 1; s += 2) line_aa(sx + nx * f * s - tx * L2, sy + ny * f * s - ty * L2, sx + nx * f * s + tx * L2, sy + ny * f * s + ty * L2, 1.f, 0xffffff, 0.35f);
		/* small arrow toward the affected side */
		float ax = sx - nx * 26, ay = sy - ny * 26;
		tri(ax - nx * 8, ay - ny * 8, ax + tx * 7, ay + ty * 7, ax - tx * 7, ay - ty * 7, C_ACC, 1);
	}
	if (L->tool == T_VIGNETTE) {
		float inner = L->p[VG_SIZE] / 100.f * 0.85f;
		ellipse_ring(sx, sy, inner * W * 0.5f / 0.7071f * zoom, inner * H * 0.5f / 0.7071f * zoom, 0, 1.4f, 0xffffff, 0.8f, 1);
	} else if (L->tool == T_LENS) {
		float rf = (0.08f + L->p[LN_SIZE] / 100.f * 0.55f) * m * zoom, th = L->p[LN_ANGLE] * PI_F / 180.f;
		if ((int)L->p[LN_SHAPE] == 0)
			ellipse_ring(sx, sy, rf * 1.35f, rf * 0.85f, th, 1.4f, 0xffffff, 0.85f, 1);
		else {
			float nx = -sinf(th), ny = cosf(th), tx = cosf(th), ty = sinf(th), L2 = 3000.f;
			for (int s = -1; s <= 1; s += 2) line_aa(sx + nx * rf * 0.85f * s - tx * L2, sy + ny * rf * 0.85f * s - ty * L2, sx + nx * rf * 0.85f * s + tx * L2, sy + ny * rf * 0.85f * s + ty * L2, 1.4f, 0xffffff, 0.8f);
		}
	} else if (L->tool == T_TEXT) {
		float px = MAX(6.f, L->p[TX_SIZE] / 100.f * 0.34f * m) * zoom;
		float tw = text_width(L->text, 14, 1) / 14.f * px * 1.02f, th = px * 1.25f, a = L->p[TX_ROT] * PI_F / 180.f;
		if (L->text[0]) ellipse_ring(sx, sy, tw * 0.5f * 1.02f, th * 0.5f, a, 1.f, 0xffffff, 0.0f, 0);
		rrect_line(sx - 4, sy - 4, 8, 8, 2, 1.4f, 0xffffff, 0.9f);
	}
	disc(sx, sy, 5.f, 0x000000, 0.4f);
	disc(sx, sy, 4.f, C_ACC, 1);
	clip_set(c);
	if (in_canvas) cursor_kind = 2;
}

static void grid_overlay(int n) {
	if (!out.w) return;
	float x0, y0, x1, y1;
	img2scr(0, 0, &x0, &y0);
	img2scr((float)out.w, (float)out.h, &x1, &y1);
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	for (int i = 1; i < n; i++) {
		fill_rect((int)(x0 + (x1 - x0) * i / n), (int)y0, 1, (int)(y1 - y0), 0xffffff, 0.22f);
		fill_rect((int)x0, (int)(y0 + (y1 - y0) * i / n), (int)(x1 - x0), 1, 0xffffff, 0.22f);
	}
	clip_set(c);
}

static void scrub_tool(Layer *L, int in_canvas) {
	int idx[MAXP];
	int n = scrub_list(L, idx);
	if (wb_pick && L->tool == T_WB) {
		cursor_kind = 1;
		if (in_canvas && m_press[0] && pf.p) {
			float ix, iy;
			scr2img((float)mx, (float)my, &ix, &iy);
			double r = 0, g = 0, b = 0;
			int cnt = 0;
			for (int dy = -2; dy <= 2; dy++)
				for (int dx = -2; dx <= 2; dx++) {
					int xx = CLAMP((int)ix + dx, 0, pf.w - 1), yy = CLAMP((int)iy + dy, 0, pf.h - 1);
					const float *p = pf.p + ((size_t)yy * pf.w + xx) * 3;
					r += p[0];
					g += p[1];
					b += p[2];
					cnt++;
				}
			float t, ti;
			wb_from_sample((float)(r / cnt), (float)(g / cnt), (float)(b / cnt), &t, &ti);
			L->p[WB_TEMP] = roundf(t);
			L->p[WB_TINT] = roundf(ti);
			touch(L);
			wb_pick = 0;
		}
		return;
	}
	if (L->tool == T_ROTATE || L->tool == T_PERSP) grid_overlay(8);
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && n > 0) {
		cdrag = 3;
		scrub_axis = 0;
		scrub_ax = scrub_ay = scrub_acc = 0;
	}
	if (cdrag == 3) {
		if (m_down[0]) {
			scrub_ax += mdx;
			scrub_ay += mdy;
			if (!scrub_axis && fabsf(scrub_ax) + fabsf(scrub_ay) > 7.f) scrub_axis = fabsf(scrub_ax) >= fabsf(scrub_ay) ? 1 : 2;
			if (scrub_axis == 2) {
				const float STEP = 28.f;
				while (scrub_ay >= STEP || scrub_ay <= -STEP) {
					int dir = scrub_ay > 0 ? 1 : -1;
					scrub_ay -= dir * STEP;
					int cur = 0;
					for (int i = 0; i < n; i++)
						if (idx[i] == sel_param) cur = i;
					cur = (cur + dir + n) % n;
					sel_param = idx[cur];
					hud_show(L, sel_param);
				}
			} else if (scrub_axis == 1 && mdx != 0) {
				int pi = sel_param, ok = 0;
				for (int i = 0; i < n; i++)
					if (idx[i] == pi) ok = 1;
				if (!ok) pi = sel_param = idx[0];
				const PDef *d = &TD[L->tool].p[pi];
				scrub_acc += mdx * (d->mx - d->mn) / 380.f * (mod_shift ? 0.2f : 1.f);
				float step = d->dec ? 0.1f : 1.f, whole = truncf(scrub_acc / step) * step;
				if (whole != 0.f) {
					scrub_acc -= whole;
					L->p[pi] = CLAMP(L->p[pi] + whole, d->mn, d->mx);
					L->p[pi] = d->dec ? roundf(L->p[pi] * 10.f) / 10.f : roundf(L->p[pi]);
					touch(L);
				}
				hud_show(L, pi);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 3) cursor_kind = scrub_axis == 2 ? 3 : (n > 0 ? 4 : 0);
}

static int mask_for = -1;
static void mask_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = 0.004f + mask_size / 100.f * 0.12f;
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		mask_size = CLAMP(mask_size + wheel * (mod_shift ? 1.f : 3.f), 1.f, 100.f);
		wheel_used = 1;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		Stroke *s = layer_add_mstroke(L);
		s->mode = mask_erase ? 4 : 0;
		s->amt = 1.f;
		s->rad = rad;
		s->hard = mask_hard / 100.f;
		s->flow = 0.6f;
		layer_add_mpt(L, ix / W, iy / H);
		s->n = 1;
		cdrag = 2;
		cd_x0 = (float)mx;
		cd_y0 = (float)my;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = mx - cd_x0, dy = my - cd_y0;
			if (dx * dx + dy * dy >= 2.25f && L->nmstr > 0) {
				layer_add_mpt(L, ix / W, iy / H);
				L->mstr[L->nmstr - 1].n++;
				cd_x0 = (float)mx;
				cd_y0 = (float)my;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, mask_erase ? 0xffb0a0 : 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		clip_set(c);
		cursor_kind = 8;
	}
}
/* red tint over the areas where a masked effect shows */
static void mask_overlay(Layer *L) {
	BrushRT *rt = L->mrt;
	if (!rt || rt->w != out.w || rt->h != out.h) return;
	const float *mp = rt->map[0];
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	int step = zoom < 1.f ? 1 : 1;
	for (int sy = cvy; sy < cvy + cvh; sy += step)
		for (int sx = cvx; sx < cvx + cvw; sx += step) {
			float ix, iy;
			scr2img(sx + 0.5f, sy + 0.5f, &ix, &iy);
			if (ix < 0 || iy < 0 || ix >= out.w || iy >= out.h) continue;
			float a = mp ? CL01(mp[(size_t)(int)iy * out.w + (int)ix]) : 0.f;
			if (L->mask_inv) a = 1.f - a;
			if (a > 0.01f) put_px(sx, sy, 0xe03a3a, a256(a * 0.5f));
		}
	clip_set(c);
}
static void canvas_tool(int in_canvas) {
	if (active < 0 || active >= nlayers) return;
	Layer *L = &stack[active];
	if (active != mask_for) {
		mask_for = active;
		mask_edit = 0;
	}
	if (mask_edit && L->mask_on && maskable(L->tool)) {
		mask_tool(L, in_canvas);
		mask_overlay(L);
		return;
	}
	if (mask_edit) mask_edit = 0;
	switch (L->tool) {
	case T_CROP: crop_tool(L, in_canvas); break;
	case T_SELECTIVE: selective_tool(L, in_canvas); break;
	case T_BRUSH: brush_tool(L, in_canvas); break;
	case T_HEAL: heal_tool(L, in_canvas); break;
	case T_VIGNETTE: case T_LENS: case T_TEXT: case T_GRAD: center_tool(L, in_canvas); break;
	case T_CURVES: case T_MIXER: break;
	default: scrub_tool(L, in_canvas); break;
	}
}

static void draw_pill(float cx, float cy, const char *a, const char *b) {
	float wa = text_width(a, 14, 0), wb = text_width(b, 20, 1);
	float w = wa + wb + 44;
	rrect(cx - w * 0.5f, cy - 20, w, 40, 20, 0x101214, 0.82f);
	text(cx - w * 0.5f + 20, cy, a, 14, 0, C_DIM, 1);
	text(cx - w * 0.5f + 24 + wa, cy, b, 20, 1, C_ACC, 1);
}

static void draw_canvas(void) {
	Clip cc = clip_push(cvx, cvy, cvw, cvh);
	fill_rect(cvx, cvy, cvw, cvh, 0x101113, 1);
	if (!doc.loaded) {
		float cx = cvx + cvw * 0.5f, cy = cvy + cvh * 0.5f;
		rrect_line(cx - 230, cy - 120, 460, 240, 18, 2, C_LINE, 1);
		text_c(cx, cy - 52, "SnapDesk", 30, 1, C_TEXT, 1);
		text_c(cx, cy - 14, "Drop a photo here, or press Ctrl+O", 14, 0, C_DIM, 1);
		if (button(8001, (int)cx - 70, (int)cy + 20, 140, 38, "Open photo", BS_ACCENT, 0)) do_open();
		text_c(cx, cy + 84, "F1 shows every shortcut", 12, 0, C_FAINT, 1);
		clip_set(cc);
		return;
	}
	if (out.w != last_ow || out.h != last_oh) {
		view_fit = 1;
		last_ow = out.w;
		last_oh = out.h;
	}
	if (view_fit) view_do_fit();
	int in_canvas = mouse_in(cvx, cvy, cvw, cvh) && (cdrag == 0 || cdrag >= 2);
	int space_cmp = compare_hold || hold_space;
	/* pan / zoom */
	int rc_reserved = 0; /* right-click on a selective point deletes it instead of panning */
	if (active >= 0 && stack[active].tool == T_SELECTIVE)
		for (int i = 0; i < stack[active].nsel; i++) {
			float sx, sy;
			img2scr(stack[active].sel[i].x * out.w, stack[active].sel[i].y * out.h, &sx, &sy);
			if ((mx - sx) * (mx - sx) + (my - sy) * (my - sy) < 16.f * 16.f) rc_reserved = 1;
		}
	if (in_canvas && cdrag == 0 && (m_press[1] || (m_press[2] && !rc_reserved) || (m_press[0] && mod_ctrl))) {
		cdrag = 1;
		view_fit = 0;
	}
	if (cdrag == 1) {
		if (m_down[0] || m_down[1] || m_down[2]) {
			vcx -= mdx / zoom;
			vcy -= mdy / zoom;
			view_clamp();
		} else
			cdrag = 0;
		cursor_kind = 7;
	}
	if (in_canvas && wheel && !wheel_used) {
		int size_tool = active >= 0 && (stack[active].tool == T_BRUSH || stack[active].tool == T_HEAL || stack[active].tool == T_SELECTIVE || stack[active].tool == T_LENS || stack[active].tool == T_GRAD);
		if (!size_tool || mod_ctrl) {
			view_zoom_at(powf(1.18f, (float)wheel), (float)mx, (float)my);
			wheel_used = 1;
		}
	}
	/* image */
	const Mips *m = &disp;
	float z = zoom, cx = vcx, cy = vcy;
	if (space_cmp && odisp.n) {
		m = &odisp;
		float ar = (float)odisp.w[0] / odisp.h[0], br = (float)out.w / out.h;
		if (fabsf(ar / br - 1.f) < 0.01f) {
			z = zoom * out.w / odisp.w[0];
			cx = vcx * odisp.w[0] / out.w;
			cy = vcy * odisp.h[0] / out.h;
		} else {
			z = MIN((cvw - 40) / (float)odisp.w[0], (cvh - 40) / (float)odisp.h[0]);
			cx = odisp.w[0] * 0.5f;
			cy = odisp.h[0] * 0.5f;
		}
	}
	{
		float x0, y0, x1, y1;
		float iw = (float)m->w[0], ih = (float)m->h[0];
		x0 = cvx + cvw * 0.5f + (0 - cx) * z;
		y0 = cvy + cvh * 0.5f + (0 - cy) * z;
		x1 = cvx + cvw * 0.5f + (iw - cx) * z;
		y1 = cvy + cvh * 0.5f + (ih - cy) * z;
		if (x1 - x0 < cvw + 30 && y1 - y0 < cvh + 30) soft_shadow((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), 14, 0.55f);
	}
	draw_mips(m, cvx, cvy, cvw, cvh, cx, cy, z);
	if (space_cmp) {
		rrect(cvx + 14.f, cvy + 14.f, 92, 26, 13, 0x101214, 0.8f);
		text_c(cvx + 60.f, cvy + 27.f, "ORIGINAL", 12, 1, C_TEXT, 1);
	}
	/* tool */
	cursor_kind = 0;
	if (!space_cmp) canvas_tool(in_canvas);
	if (in_canvas && cursor_kind == 0 && doc.loaded) cursor_kind = 0;
	/* double-click toggles fit / 100% */
	if (in_canvas && m_dbl && cdrag == 0 && (active < 0 || is_scrub_tool(stack[active].tool))) {
		if (zoom < 0.99f / full_scale() * 0.98f) {
			float ix, iy;
			scr2img((float)mx, (float)my, &ix, &iy);
			float nz = 1.f / full_scale();
			zoom = nz;
			vcx = ix - (mx - cvx - cvw * 0.5f) / zoom;
			vcy = iy - (my - cvy - cvh * 0.5f) / zoom;
			view_fit = 0;
		} else
			view_fit = 1;
	}
	/* HUD pill */
	uint32_t now = SDL_GetTicks();
	if (hud_until > now && !space_cmp) {
		float a = (float)(hud_until - now) / 1100.f;
		(void)a;
		draw_pill(cvx + cvw * 0.5f, cvy + 38.f, hud_name, hud_text);
	}
	/* toast */
	if (toast_until > now) {
		float tw = text_width(toast_msg, 13, 0) + 32;
		float a = CL01((toast_until - now) / 500.f);
		rrect(cvx + 16.f, cvy + cvh - 46.f, tw, 30, 15, 0x101214, 0.9f * a);
		text(cvx + 32.f, cvy + cvh - 31.f, toast_msg, 13, 0, C_TEXT, a);
	}
	/* zoom readout */
	{
		char b[32];
		snprintf(b, sizeof b, "%d%%", (int)(zoom * full_scale() * 100.f + 0.5f));
		text_r((float)(cvx + cvw - 14), (float)(cvy + cvh - 18), b, 12, 1, C_DIM, 1);
		if (last_render_ms > 90.0) {
			snprintf(b, sizeof b, "%d ms", (int)last_render_ms);
			text_r((float)(cvx + cvw - 14), (float)(cvy + cvh - 36), b, 11, 0, C_FAINT, 1);
		}
	}
	clip_set(cc);
}

/* a value is being dragged (slider or on-canvas scrub): render on the half-size proxy meanwhile */
static int g_force_lo = 0; /* benchmark hook */
static int interacting_now(void) { return g_force_lo || (m_down[0] && (act_id != 0 || cdrag == 3)); }
static void pipeline_tick(void) {
	if (lo_shown && !interacting_now()) need_render = 1; /* drag ended: do the full-quality render */
}

/* ================================================================== chrome: top bar, tool rail, right panel, modals */
#define TOP_H 48
#define LEFT_W 292
#define RIGHT_W 312
static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture *g_tex = NULL;
static int rail_scroll = 0;
#define ID_SEARCH 9003
static char tool_q[40];
static float tool_tab = 0;
static int search_enter = 0;
/* Snapseed-style categories: REFINE (tone/colour), FIX (geometry/repair), STYLE (looks/effects) */
static int tool_cat(int t) {
	switch (t) {
	case T_TUNE: case T_DETAILS: case T_CURVES: case T_WB: case T_SELECTIVE: case T_BRUSH: case T_MIXER: case T_SPLIT: case T_DEHAZE: case T_DENOISE: return 1;
	case T_CROP: case T_ROTATE: case T_PERSP: case T_EXPAND: case T_HEAL: case T_PORTRAIT: case T_LENS: case T_CHROMA: return 2;
	}
	return 3;
}
static int tool_shown(int t) {
	int tab = (int)tool_tab;
	if (tab > 0 && tool_cat(t) != tab) return 0;
	if (tool_q[0]) {
		char a[48], b[40];
		snprintf(a, sizeof a, "%s %s", TD[t].name, TD[t].group);
		snprintf(b, sizeof b, "%s", tool_q);
		for (char *c = a; *c; c++) *c = (char)tolower((unsigned char)*c);
		for (char *c = b; *c; c++) *c = (char)tolower((unsigned char)*c);
		if (!strstr(a, b)) return 0;
	}
	return 1;
}
static int pending_export = 0, saved_hpos = 0;
static const int PREVIEW_OPTS[] = {1024, 1600, 2400, 3200, 0};
static int preview_idx = 1;
static uint32_t quit_warn_until = 0;

static void layout(void) {
	int lh = (show_looks && doc.loaded) ? looks_h : 0;
	cvx = LEFT_W;
	cvy = TOP_H;
	cvw = MAX(100, FW - LEFT_W - RIGHT_W);
	cvh = MAX(100, FH - TOP_H - lh);
}
static void cycle_preview(void) {
	preview_idx = (preview_idx + 1) % 5;
	doc.proxy_max = PREVIEW_OPTS[preview_idx];
	if (doc.loaded) {
		proxy_build();
		view_fit = 1;
		pipeline_render();
	}
	if (PREVIEW_OPTS[preview_idx])
		toast("Preview resolution %d px - exports are always full resolution", PREVIEW_OPTS[preview_idx]);
	else
		toast("Preview at full resolution (slower, sharpest zoom)");
}

static void draw_topbar(void) {
	fill_rect(0, 0, FW, TOP_H, C_PANEL, 1);
	fill_rect(0, TOP_H - 1, FW, 1, C_LINE, 1);
	int x = 12, y = 9, h = 30;
	if (button(100, x, y, 66, h, "Open", BS_NORMAL, 0)) do_open();
	x += 72;
	if (button(101, x, y, 78, h, "Export", BS_ACCENT, 0)) do_export_dialog();
	x += 92;
	fill_rect(x - 6, y + 4, 1, h - 8, C_LINE, 1);
	int can_u = hist_can_undo() || (active >= 0 && edit_dirty), can_r = hist_can_redo();
	if (button(102, x, y, 38, h, "", BS_GHOST, 0) && can_u) do_undo();
	icon_undo((float)x + 19, y + 15.f, 8.5f, C_TEXT, can_u ? 1.f : 0.3f, 0);
	x += 42;
	if (button(103, x, y, 38, h, "", BS_GHOST, 0) && can_r) do_redo();
	icon_undo((float)x + 19, y + 15.f, 8.5f, C_TEXT, can_r ? 1.f : 0.3f, 1);
	x += 48;
	{ /* compare: hold */
		int hov = mouse_in(x, y, 98, h);
		if (hov && m_press[0]) act_id = 104;
		compare_hold = (act_id == 104 && m_down[0]);
		if (act_id == 104 && !m_down[0]) act_id = 0;
		rrect((float)x, (float)y, 98, (float)h, 6, compare_hold ? C_ACC : (hov ? C_PANEL3 : C_PANEL2), 1);
		icon_split((float)x + 17, y + 15.f, 6.5f, compare_hold ? 0x0b2a27 : C_TEXT, 1);
		text((float)x + 32, y + 15.f, "Compare", 13, 1, compare_hold ? 0x0b2a27 : C_TEXT, 1);
	}
	x += 108;
	fill_rect(x - 6, y + 4, 1, h - 8, C_LINE, 1);
	if (button(105, x, y, 44, h, "Fit", BS_NORMAL, 0)) view_fit = 1;
	x += 50;
	if (button(106, x, y, 54, h, "100%", BS_NORMAL, 0) && doc.loaded) {
		zoom = 1.f / full_scale();
		view_fit = 0;
	}
	x += 60;
	if (button(107, x, y, 48, h, "Info", BS_NORMAL, 0) && doc.loaded) modal = MODAL_INFO;
	x += 60;
	/* right side */
	int rx = FW - 12;
	if (button(110, rx - 30, y, 30, h, "?", BS_NORMAL, 0)) modal = MODAL_HELP;
	rx -= 40;
	char b[48];
	if (PREVIEW_OPTS[preview_idx]) snprintf(b, sizeof b, "Preview %d px", PREVIEW_OPTS[preview_idx]);
	else snprintf(b, sizeof b, "Preview full");
	int pw = (int)text_width(b, 13, 1) + 24;
	if (button(111, rx - pw, y, pw, h, b, BS_NORMAL, 0)) cycle_preview();
	rx -= pw + 12;
	if (doc.loaded) {
		char dir[1024], base[256];
		path_split_name(doc.path, dir, sizeof dir, base, sizeof base);
		int fw_, fh_;
		full_dims(&fw_, &fh_);
		snprintf(b, sizeof b, "%d x %d", fw_, fh_);
		text_r((float)rx, TOP_H * 0.5f, b, 12, 0, C_DIM, 1);
		rx -= (int)text_width(b, 12, 0) + 14;
		float avail = (float)(rx - x);
		if (avail > 40) {
			float nw = text_width(base, 14, 1);
			text_fit(MAX((float)x, rx - nw), TOP_H * 0.5f, base, avail, 14, 1, C_TEXT, 1);
		}
	}
}

static void draw_rail(void) {
	int x = 0, y = TOP_H, w = LEFT_W, h = FH - TOP_H;
	fill_rect(x, y, w, h, C_PANEL, 1);
	fill_rect(x + w - 1, y, 1, h, C_LINE, 1);
	Clip c = clip_push(x, y, w - 1, h);
	if (mouse_in(x, y, w, h) && wheel && !wheel_used) {
		rail_scroll -= wheel * 36;
		wheel_used = 1;
	}
	int hdr = 66;
	{
		search_enter = text_field(ID_SEARCH, x + 8, y + 6, w - 17, 26, tool_q, sizeof tool_q, "Search tools  (Ctrl+F)") & 2;
		int py = y + 38;
		chip_row(8700, x + 8, &py, w - 16, "All|Refine|Fix|Style", &tool_tab, -4);
	}
	y += hdr;
	h -= hdr;
	clip_set(c);
	c = clip_push(x, y, w - 1, h);
	int rowh = 28, content = 0, shown = 0, firstg = 1;
	int cy = y + 6 - rail_scroll;
	const char *last = "";
	int click_tool = -1, gi = 0, colw = (w - 1 - 12) / 2, first_shown = -1;
	for (int t = 0; t < T_COUNT; t++) {
		const ToolDef *d = &TD[t];
		if (!tool_shown(t)) continue;
		if (!shown) first_shown = t;
		shown++;
		if (strcmp(d->group, last) != 0) {
			if (!firstg && (gi & 1)) cy += rowh;
			last = d->group;
			gi = 0;
			cy += (firstg ? 0 : 8);
			firstg = 0;
			text((float)x + 16, cy + 12, d->group, 11, 1, group_col(d->group), 0.95f);
			fill_rect(x + 16 + (int)text_width(d->group, 11, 1) + 8, cy + 12, w - 50 - (int)text_width(d->group, 11, 1), 1, C_LINE, 1);
			cy += 22;
		}
		int col = gi & 1;
		int rx = x + 6 + col * colw, rw = colw - 3;
		int is_act = active >= 0 && stack[active].tool == t;
		int hov = mouse_in(rx, cy, rw, rowh - 1) && doc.loaded;
		if (is_act) rrect((float)rx, (float)cy, (float)rw, (float)rowh - 2, 7, C_PANEL3, 1);
		else if (hov) rrect((float)rx, (float)cy, (float)rw, (float)rowh - 2, 7, C_PANEL2, 1);
		if (is_act) rrect((float)rx, (float)cy + 5, 3, (float)rowh - 12, 1.5f, C_ACC, 1);
		uint32_t gc = group_col(d->group);
		rrect((float)rx + 10, (float)cy + 4, 18, 18, 5, gc, doc.loaded ? 0.9f : 0.35f);
		char kb[2] = {d->key, 0};
		text_c(rx + 19.f, cy + 12.5f, kb, 12, 1, 0x101214, 1);
		text_fit((float)rx + 36, cy + 12.5f, d->name, (float)rw - 52, 13, is_act ? 1 : 0, doc.loaded ? C_TEXT : C_FAINT, is_act ? 1.f : 0.92f);
		int used = 0;
		for (int i = 0; i < nlayers; i++)
			if (stack[i].tool == t && !(active >= 0 && i == active && edit_is_new && !stack[i].touched)) used = 1;
		if (used) disc((float)rx + rw - 9, cy + 12.5f, 3.2f, C_ACC, 1);
		if (hov && m_press[0]) click_tool = t;
		if (col == 1) cy += rowh;
		gi++;
	}
	if (gi & 1) cy += rowh;
	if (!shown) text((float)x + 16, cy + 18, "No matching tools", 12, 0, C_FAINT, 1);
	content = cy - (y + 6 - rail_scroll) + 12;
	int maxs = MAX(0, content - h);
	rail_scroll = CLAMP(rail_scroll, 0, maxs);
	clip_set(c);
	if (search_enter && first_shown >= 0 && doc.loaded) click_tool = first_shown;
	if (click_tool >= 0) select_tool(click_tool);
}

static void draw_right(void) {
	int x = FW - RIGHT_W, w = RIGHT_W;
	fill_rect(x, TOP_H, w, FH - TOP_H, C_PANEL, 1);
	fill_rect(x, TOP_H, 1, FH - TOP_H, C_LINE, 1);
	draw_histogram(x + 14, TOP_H + 12, w - 28, 84);
	int stack_h = CLAMP((int)(FH * 0.27f), 130, 236);
	int sy = FH - stack_h;
	int ty0 = TOP_H + 108, ty1 = sy - (active >= 0 ? 50 : 0);
	static int last_tool_active = -2;
	if (last_tool_active != active) {
		panel_scroll = 0;
		last_tool_active = active;
		if (active >= 0) {
			sel_param = 0;
			const ToolDef *d = &TD[stack[active].tool];
			for (int i = 0; i < d->np; i++)
				if (!d->p[i].ch) {
					sel_param = i;
					break;
				}
		}
		wb_pick = 0;
		tf_end();
	}
	if (doc.loaded && active >= 0 && active < nlayers) {
		panel_tool(x + 18, ty0, w - 36, ty1 - ty0);
		if (mouse_in(x, ty0, w, ty1 - ty0) && wheel && !wheel_used) {
			panel_scroll -= wheel * 36;
			wheel_used = 1;
		}
		panel_scroll = CLAMP(panel_scroll, 0, MAX(0, panel_content - (ty1 - ty0)));
		int bw = (w - 36 - 8) / 2, by = ty1 + 8;
		if (button(8010, x + 18, by, bw, 34, "Cancel   Esc", BS_NORMAL, 0)) edit_end(0);
		if (button(8011, x + 18 + bw + 8, by, bw, 34, "Apply   Enter", BS_ACCENT, 0)) edit_end(1);
	} else {
		int py = ty0 + 4;
		text((float)x + 18, py + 10, doc.loaded ? "Pick a tool" : "Welcome", 16, 1, C_TEXT, 1);
		py += 30;
		if (doc.loaded) {
			hint_text(x + 18, &py, w - 36, "Click a tool on the left, or press its letter. Every slider is already on screen - no menus to open.");
			py += 6;
			hint_text(x + 18, &py, w - 36, "On the photo: drag up/down to choose a parameter, drag left/right to change it. Up/Down and Left/Right on the keyboard do the same.");
			py += 6;
			hint_text(x + 18, &py, w - 36, "Looks along the bottom apply a ready-made style in one click. Hold Space to compare with the original.");
		} else
			hint_text(x + 18, &py, w - 36, "Open a photo with Ctrl+O, the Open button, or just drop a file onto the window.");
	}
	draw_stack(x, sy, w, stack_h);
}

/* ---- modals ---- */
static void modal_box(int w, int h, const char *title, int *bx, int *by) {
	fill_rect(0, 0, FW, FH, 0x000000, 0.6f);
	*bx = (FW - w) / 2;
	*by = (FH - h) / 2;
	soft_shadow(*bx, *by, w, h, 18, 0.7f);
	rrect((float)*bx, (float)*by, (float)w, (float)h, 14, C_PANEL, 1);
	rrect_line((float)*bx, (float)*by, (float)w, (float)h, 14, 1, C_LINE, 1);
	text((float)*bx + 24, *by + 30.f, title, 20, 1, C_TEXT, 1);
}
static void draw_modal(void) {
	if (!modal) return;
	in_modal_draw = 1;
	int bx, by;
	if (modal == MODAL_EXPORT) {
		modal_box(560, 330, "Export photo", &bx, &by);
		int x = bx + 24, w = 560 - 48, y = by + 64;
		text((float)x, y + 6, "Save as (the format follows the extension: .jpg .png .bmp .tga)", 12, 0, C_DIM, 1);
		y += 22;
		int r = text_field(ID_EXPORT_PATH, x, y, w - 92, 34, export_path, sizeof export_path, "path/to/file.jpg");
		if (button(8020, x + w - 84, y, 84, 34, "Browse...", BS_NORMAL, 0)) {
			char p[1024];
			snprintf(p, sizeof p, "%s", export_path);
			int rr = native_dialog(1, p, p, sizeof p);
			if (rr == 1) snprintf(export_path, sizeof export_path, "%s", p);
			else if (rr < 0) toast("No file dialog available - type the path instead");
		}
		if (tf_id != ID_EXPORT_PATH && !(r & 2) && 0) tf_begin(ID_EXPORT_PATH, export_path, 0);
		y += 52;
		int is_jpg = !(ends_with_ci(export_path, ".png") || ends_with_ci(export_path, ".bmp") || ends_with_ci(export_path, ".tga"));
		if (is_jpg) {
			slider(8021, x, y, w, "JPEG quality", &export_q, 40, 100, 92, 0, 0);
		} else
			text((float)x, y + 14, "Lossless format - quality setting not needed", 13, 0, C_DIM, 1);
		y += 52;
		text((float)x, y + 8, "Size", 13, 0, C_TEXT, 1);
		static const float SC[4] = {100, 75, 50, 25};
		for (int i = 0; i < 4; i++) {
			char lb[16];
			snprintf(lb, sizeof lb, "%d%%", (int)SC[i]);
			if (button(8030 + i, x + 60 + i * 66, y - 4, 60, 28, lb, BS_NORMAL, export_scale == SC[i])) export_scale = SC[i];
		}
		int fw_, fh_;
		full_dims(&fw_, &fh_);
		char b[64];
		snprintf(b, sizeof b, "%d x %d px", (int)(fw_ * export_scale / 100.f), (int)(fh_ * export_scale / 100.f));
		text_r((float)(x + w), y + 10, b, 13, 1, C_ACC, 1);
		y += 50;
		int go = 0;
		if (button(8040, x + w - 200, y, 90, 36, "Cancel", BS_NORMAL, 0)) {
			modal = 0;
			tf_end();
		}
		if (button(8041, x + w - 100, y, 100, 36, "Save", BS_ACCENT, 0)) go = 1;
		if (r & 2) go = 1;
		if (go && export_path[0]) {
			modal = 0;
			tf_end();
			pending_export = 1;
		}
	} else if (modal == MODAL_OPENPATH) {
		modal_box(560, 190, "Open photo", &bx, &by);
		int x = bx + 24, w = 560 - 48, y = by + 64;
		text((float)x, y + 6, "Type or paste the path of an image (jpg, png, bmp, tga, gif, psd)", 12, 0, C_DIM, 1);
		y += 22;
		int r = text_field(ID_OPEN_PATH, x, y, w, 34, open_path, sizeof open_path, "path/to/photo.jpg");
		if (tf_id != ID_OPEN_PATH && !(r & 2)) {
		}
		y += 50;
		int go = 0;
		if (button(8050, x + w - 200, y, 90, 36, "Cancel", BS_NORMAL, 0)) {
			modal = 0;
			tf_end();
		}
		if (button(8051, x + w - 100, y, 100, 36, "Open", BS_ACCENT, 0)) go = 1;
		if (r & 2) go = 1;
		if (go && open_path[0]) {
			modal = 0;
			tf_end();
			load_file(open_path);
		}
	} else if (modal == MODAL_SAVELOOK) {
		modal_box(520, 210, "Save as Look", &bx, &by);
		int x = bx + 24, w = 520 - 48, y = by + 64;
		text((float)x, y + 6, "Stores the visible tone/colour/effect layers of this edit (geometry, brush and masks are left out)", 11, 0, C_DIM, 1);
		y += 22;
		int r = text_field(ID_LOOKNAME, x, y, w, 34, look_name_buf, sizeof look_name_buf, "Look name");
		y += 52;
		int go = 0;
		if (button(8060, x + w - 200, y, 90, 36, "Cancel", BS_NORMAL, 0)) {
			modal = 0;
			tf_end();
		}
		if (button(8061, x + w - 100, y, 100, 36, "Save", BS_ACCENT, 0)) go = 1;
		if (r & 2) go = 1;
		if (go && look_name_buf[0]) {
			int sk = 0, ok = look_save_current(look_name_buf, &sk);
			modal = 0;
			tf_end();
			if (ok == 1) toast(sk ? "Look saved (%d layer(s) can't be stored and were skipped)" : "Look saved - find it at the end of the Looks strip", sk);
			else if (ok == 0) toast("Nothing storable in this edit");
			else toast("Too many saved Looks (max 16) - right-click one to delete it");
		}
	} else if (modal == MODAL_INFO) {
		modal_box(520, 360, "Image details", &bx, &by);
		int x = bx + 24, y = by + 70;
		char dir[1024], base[256], b[160];
		path_split_name(doc.path, dir, sizeof dir, base, sizeof base);
		long fsz = -1;
		FILE *f = fopen(doc.path, "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			fsz = ftell(f);
			fclose(f);
		}
		int fw_, fh_;
		full_dims(&fw_, &fh_);
		const char *K[8], *V[8];
		char v[8][160];
		int n = 0;
		K[n] = "File";
		{
			const char *s1 = strrchr(doc.path, '/'), *s2 = strrchr(doc.path, '\\');
			const char *bn = (s1 > s2 ? s1 : s2) ? (s1 > s2 ? s1 : s2) + 1 : doc.path;
			snprintf(v[n], 160, "%.150s", bn);
		}
		n++;
		K[n] = "Folder";
		snprintf(v[n], 160, "%.150s", dir);
		n++;
		K[n] = "Original";
		snprintf(v[n], 160, "%d x %d px  (%.1f MP)", doc.w, doc.h, doc.w * (double)doc.h / 1e6);
		n++;
		K[n] = "File size";
		if (fsz >= 0) snprintf(v[n], 160, "%.2f MB", fsz / 1048576.0);
		else snprintf(v[n], 160, "-");
		n++;
		K[n] = "Export size";
		snprintf(v[n], 160, "%d x %d px", fw_, fh_);
		n++;
		K[n] = "Preview";
		snprintf(v[n], 160, "%d x %d px  (%.0f ms last render)", out.w, out.h, last_render_ms);
		n++;
		K[n] = "Edits";
		snprintf(v[n], 160, "%d layer(s)", nlayers);
		n++;
		K[n] = "Mean level";
		{
			double s1 = 0, s2 = 0;
			for (int i = 0; i < 256; i++) {
				s1 += histo[3][i] * i;
				s2 += histo[3][i];
			}
			snprintf(v[n], 160, "%.0f / 255 luminance", s2 > 0 ? s1 / s2 : 0.0);
		}
		n++;
		for (int i = 0; i < n; i++) {
			V[i] = v[i];
			text((float)x, y + 10, K[i], 13, 0, C_DIM, 1);
			text_fit((float)x + 110, y + 10, V[i], 360.f, 13, 1, C_TEXT, 1);
			y += 28;
		}
		(void)b;
		if (button(8070, bx + 520 - 124, by + 360 - 54, 100, 34, "Close", BS_ACCENT, 0)) modal = 0;
	} else if (modal == MODAL_HELP) {
		modal_box(760, 620, "Keyboard & mouse", &bx, &by);
		static const char *L1[][2] = {
			{"Ctrl+O", "Open photo (or drop a file on the window)"},
			{"Ctrl+S / Ctrl+E", "Export"},
			{"Ctrl+Z / Ctrl+Y", "Undo / Redo (Ctrl+Shift+Z also redoes)"},
			{"Hold Space", "Show the original (compare)"},
			{"Enter / Esc", "Apply / cancel the tool you are in"},
			{"Tab", "Show or hide the Looks strip"},
			{"Ctrl+0 / Ctrl+1", "Fit to window / 100% (also double-click the photo)"},
			{"Wheel", "Zoom at the cursor (Ctrl+wheel in brush tools)"},
			{"Middle / right drag", "Pan (or Ctrl + left drag)"},
			{"F1", "This help"},
		};
		static const char *L2[][2] = {
			{"Up / Down", "Choose the previous / next parameter"},
			{"Left / Right", "Change it (hold Shift for 5x steps)"},
			{"Drag on photo", "Up/down picks a parameter, left/right changes it"},
			{"Slider", "Drag, wheel, double-click resets, double-click the number to type"},
			{"[ and ]", "Smaller / larger brush, healing and reach"},
		};
		int y = by + 62;
		text((float)bx + 24, y + 6, "GENERAL", 11, 1, C_ACC, 1);
		y += 22;
		for (int i = 0; i < ARRLEN(L1); i++) {
			text((float)bx + 24, y + 9, L1[i][0], 13, 1, C_TEXT, 1);
			text((float)bx + 196, y + 9, L1[i][1], 13, 0, C_DIM, 1);
			y += 24;
		}
		y += 10;
		text((float)bx + 24, y + 6, "EDITING", 11, 1, C_ACC, 1);
		y += 22;
		for (int i = 0; i < ARRLEN(L2); i++) {
			text((float)bx + 24, y + 9, L2[i][0], 13, 1, C_TEXT, 1);
			text((float)bx + 196, y + 9, L2[i][1], 13, 0, C_DIM, 1);
			y += 24;
		}
		y += 10;
		text((float)bx + 24, y + 6, "TOOL KEYS", 11, 1, C_ACC, 1);
		y += 22;
		char line[200] = "";
		int n = 0;
		for (int t = 0; t < T_COUNT; t++) {
			char one[48];
			snprintf(one, sizeof one, "%c %s", TD[t].key, TD[t].name);
			if (n && text_width(line, 12, 0) + text_width(one, 12, 0) > 690) {
				text((float)bx + 24, y + 8, line, 12, 0, C_DIM, 1);
				y += 18;
				line[0] = 0;
				n = 0;
			}
			if (n) strcat(line, "   ");
			strcat(line, one);
			n++;
		}
		if (n) text((float)bx + 24, y + 8, line, 12, 0, C_DIM, 1);
		if (button(8060, bx + 760 - 100, by + 16, 76, 32, "Close", BS_ACCENT, 0)) modal = 0;
	}
	in_modal_draw = 0;
}

static int g_busy = 0;
static void ui_frame(void) {
	clip_reset();
	layout();
	fill_rect(0, 0, FW, FH, C_BG, 1);
	if (doc.loaded && !need_render) looks_tick();
	draw_canvas();
	if (doc.loaded && show_looks) draw_looks(cvx, cvy + cvh, cvw, looks_h);
	draw_rail();
	draw_right();
	draw_topbar();
	draw_modal();
	if (g_busy) {
		fill_rect(0, 0, FW, FH, 0x000000, 0.55f);
		rrect(FW * 0.5f - 130, FH * 0.5f - 28, 260, 56, 14, C_PANEL, 1);
		text_c(FW * 0.5f, FH * 0.5f, "Exporting full resolution...", 14, 1, C_TEXT, 1);
	}
}
static void frame_end(void) {
	m_press[0] = m_press[1] = m_press[2] = 0;
	m_rel[0] = m_rel[1] = m_rel[2] = 0;
	m_dbl = 0;
	wheel = 0;
	wheel_used = 0;
	mdx = mdy = 0;
	tf_enter = 0;
}

/* ================================================================== keyboard */
static void tool_key(int sym) {
	if (!doc.loaded) return;
	int up = toupper(sym);
	for (int t = 0; t < T_COUNT; t++)
		if (TD[t].key == up) {
			select_tool(t);
			return;
		}
}
static void do_zoom_key(float f) { view_zoom_at(f, cvx + cvw * 0.5f, cvy + cvh * 0.5f); }
static void on_key_down(SDL_Keysym ks, int repeat) {
	int sym = ks.sym, mod = ks.mod;
	int ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0, alt = (mod & KMOD_ALT) != 0;
	dirty = 1;
	if (tf_id) {
		if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) tf_enter = 1;
		else if (sym == SDLK_ESCAPE) {
			tf_end();
			if (modal == MODAL_EXPORT || modal == MODAL_OPENPATH || modal == MODAL_SAVELOOK) modal = 0;
		} else if (sym == SDLK_BACKSPACE) {
			size_t n = strlen(tf_buf);
			if (n) tf_buf[n - 1] = 0;
		} else if (ctrl && sym == SDLK_v) {
			char *cb = SDL_GetClipboardText();
			if (cb) {
				tf_append(cb);
				SDL_free(cb);
			}
		} else if (ctrl && sym == SDLK_a)
			tf_buf[0] = 0;
		return;
	}
	if (modal) {
		if (sym == SDLK_ESCAPE || modal == MODAL_HELP || modal == MODAL_INFO) modal = 0;
		if ((sym == SDLK_RETURN) && modal == MODAL_EXPORT && export_path[0]) {
			modal = 0;
			pending_export = 1;
		}
		return;
	}
	if (ctrl) {
		switch (sym) {
		case SDLK_o: do_open(); return;
		case SDLK_f: tf_begin(ID_SEARCH, tool_q, 0); return;
		case SDLK_s: case SDLK_e: do_export_dialog(); return;
		case SDLK_z:
			if (shift) do_redo();
			else do_undo();
			return;
		case SDLK_y: do_redo(); return;
		case SDLK_0: case SDLK_KP_0: view_fit = 1; return;
		case SDLK_1: case SDLK_KP_1:
			if (doc.loaded) {
				zoom = 1.f / full_scale();
				view_fit = 0;
			}
			return;
		case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: do_zoom_key(1.25f); return;
		case SDLK_MINUS: case SDLK_KP_MINUS: do_zoom_key(0.8f); return;
		}
		return;
	}
	if (alt) return;
	switch (sym) {
	case SDLK_F1: modal = MODAL_HELP; return;
	case SDLK_ESCAPE:
		if (active >= 0) edit_end(0);
		return;
	case SDLK_RETURN: case SDLK_KP_ENTER:
		if (active >= 0) edit_end(1);
		return;
	case SDLK_TAB: show_looks = !show_looks; return;
	case SDLK_SPACE:
		hold_space = 1;
		return;
	}
	if (sym == '/' && shift) {
		modal = MODAL_HELP;
		return;
	}
	if (active >= 0 && active < nlayers) {
		Layer *L = &stack[active];
		if (TD[L->tool].np == 0 && (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT)) return;
		switch (sym) {
		case SDLK_UP: param_select_step(L, -1); return;
		case SDLK_DOWN: param_select_step(L, 1); return;
		case SDLK_LEFT: param_nudge(L, sel_param, -1.f, shift); return;
		case SDLK_RIGHT: param_nudge(L, sel_param, 1.f, shift); return;
		case SDLK_LEFTBRACKET: case SDLK_RIGHTBRACKET: {
			float d = sym == SDLK_RIGHTBRACKET ? 3.f : -3.f;
			if (L->tool == T_BRUSH) L->p[BR_SIZE] = CLAMP(L->p[BR_SIZE] + d, 1.f, 100.f);
			else if (L->tool == T_HEAL) L->p[HE_SIZE] = CLAMP(L->p[HE_SIZE] + d, 3.f, 100.f);
			else if (L->tool == T_SELECTIVE && L->nsel) L->sel[CLAMP(sel_pt, 0, L->nsel - 1)].rad = CLAMP(L->sel[CLAMP(sel_pt, 0, L->nsel - 1)].rad * (d > 0 ? 1.1f : 0.91f), 0.02f, 0.6f);
			else break;
			touch(L);
			return;
		}
		case SDLK_DELETE: case SDLK_BACKSPACE:
			if (L->tool == T_SELECTIVE && L->nsel) {
				int i = CLAMP(sel_pt, 0, L->nsel - 1);
				for (int k = i; k < L->nsel - 1; k++) L->sel[k] = L->sel[k + 1];
				L->nsel--;
				sel_pt = MAX(0, sel_pt - 1);
				touch(L);
				return;
			}
			break;
		}
	}
	if (!repeat && sym < 128 && isalnum(sym)) tool_key(sym);
}

/* ================================================================== setup / loop */
static int fb_resize(int w, int h) {
	int nw = MAX(320, (int)ceilf(w / ui_scale)), nh = MAX(240, (int)ceilf(h / ui_scale));
	if (nw == FW && nh == FH && fb) return 0;
	free(fb);
	FW = nw;
	FH = nh;
	fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
	if (g_ren) {
		if (g_tex) SDL_DestroyTexture(g_tex);
		g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, FW, FH);
	}
	return 1;
}
static void present(void) {
	SDL_UpdateTexture(g_tex, NULL, fb, FW * 4);
	SDL_RenderClear(g_ren);
	SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
	SDL_RenderPresent(g_ren);
}
static SDL_Cursor *cursors[9];
static int cur_set = -1;
static void apply_cursor(void) {
	static const int map[9] = {SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_SIZEALL, SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_SIZENWSE, SDL_SYSTEM_CURSOR_SIZENESW, SDL_SYSTEM_CURSOR_HAND, SDL_SYSTEM_CURSOR_CROSSHAIR};
	int k = CLAMP(cursor_kind, 0, 8);
	if (modal || !mouse_in(cvx, cvy, cvw, cvh)) {
		if (!(cdrag && k)) k = 0;
	}
	if (k == cur_set) return;
	if (!cursors[k]) cursors[k] = SDL_CreateSystemCursor((SDL_SystemCursor)map[k]);
	if (cursors[k]) SDL_SetCursor(cursors[k]);
	cur_set = k;
}
static void update_title(void) {
	char t[600];
	if (doc.loaded) {
		char dir[1024], base[256];
		path_split_name(doc.path, dir, sizeof dir, base, sizeof base);
		snprintf(t, sizeof t, "SnapDesk - %s%s", base, hpos != saved_hpos ? " *" : "");
	} else
		snprintf(t, sizeof t, "SnapDesk");
	SDL_SetWindowTitle(g_win, t);
}
static void run_pending_export(void) {
	pending_export = 0;
	g_busy = 1;
	ui_frame();
	present();
	g_busy = 0;
	char path[1100];
	snprintf(path, sizeof path, "%s", export_path);
	if (!strrchr(path, '.') || strrchr(path, '.') < strrchr(path, '/')) strcat(path, ".jpg");
	char msg[1300];
	double t0 = now_ms();
	int ok = export_image(path, (int)export_q, (int)export_scale, msg, sizeof msg);
	if (ok) {
		saved_hpos = hpos;
		toast("%s  (%.1f s)", msg, (now_ms() - t0) / 1000.0);
	} else
		toast("%s", msg);
}

static void handle_event(SDL_Event *e) {
	switch (e->type) {
	case SDL_QUIT:
		if (hpos != saved_hpos && SDL_GetTicks() > quit_warn_until) {
			quit_warn_until = SDL_GetTicks() + 4000;
			toast("Unsaved edits - close the window again to quit anyway");
		} else
			quit_flag = 1;
		break;
	case SDL_WINDOWEVENT:
		if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e->window.event == SDL_WINDOWEVENT_EXPOSED || e->window.event == SDL_WINDOWEVENT_RESTORED) {
			int w, h;
			SDL_GetWindowSize(g_win, &w, &h);
			fb_resize(w, h);
		}
		dirty = 1;
		break;
	case SDL_MOUSEMOTION: {
		int nx = (int)(e->motion.x / ui_scale), ny = (int)(e->motion.y / ui_scale);
		mdx += nx - mx;
		mdy += ny - my;
		mx = nx;
		my = ny;
		dirty = 1;
	} break;
	case SDL_MOUSEBUTTONDOWN: {
		int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_MIDDLE ? 1 : e->button.button == SDL_BUTTON_RIGHT ? 2 : -1;
		if (b < 0) break;
		mx = (int)(e->button.x / ui_scale);
		my = (int)(e->button.y / ui_scale);
		m_down[b] = 1;
		m_press[b] = 1;
		if (b == 0 && e->button.clicks >= 2) m_dbl = 1;
		dirty = 1;
	} break;
	case SDL_MOUSEBUTTONUP: {
		int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_MIDDLE ? 1 : e->button.button == SDL_BUTTON_RIGHT ? 2 : -1;
		if (b < 0) break;
		m_down[b] = 0;
		m_rel[b] = 1;
		dirty = 1;
	} break;
	case SDL_MOUSEWHEEL:
		wheel += e->wheel.y;
		dirty = 1;
		break;
	case SDL_TEXTINPUT:
		if (tf_id) tf_append(e->text.text);
		dirty = 1;
		break;
	case SDL_KEYDOWN: on_key_down(e->key.keysym, e->key.repeat); break;
	case SDL_KEYUP:
		if (e->key.keysym.sym == SDLK_SPACE) hold_space = 0;
		dirty = 1;
		break;
	case SDL_DROPFILE:
		if (doc.loaded && active >= 0 && stack[active].tool == T_DOUBLE) {
			snprintf(stack[active].path, sizeof stack[active].path, "%s", e->drop.file);
			touch(&stack[active]);
		} else
			load_file(e->drop.file);
		SDL_free(e->drop.file);
		dirty = 1;
		break;
	}
	SDL_Keymod km = SDL_GetModState();
	mod_shift = (km & KMOD_SHIFT) != 0;
	mod_ctrl = (km & KMOD_CTRL) != 0;
	mod_alt = (km & KMOD_ALT) != 0;
}

/* ---- headless helpers: --test and --shot ---- */
static void test_setup_layer(Layer *L, int t) {
	layer_init(L, t);
	const ToolDef *d = &TD[t];
	for (int i = 0; i < d->np; i++) {
		const PDef *p = &d->p[i];
		if (p->ch)
			L->p[i] = (float)MIN(1, choice_max(p));
		else if (p->mx > p->def)
			L->p[i] = p->def + (p->mx - p->def) * 0.55f;
		else
			L->p[i] = p->mn + (p->def - p->mn) * 0.4f;
		if (!p->dec) L->p[i] = roundf(L->p[i]);
	}
	switch (t) {
	case T_CURVES: {
		float s[4][2] = {{0, 0}, {0.25f, 0.15f}, {0.75f, 0.86f}, {1, 1}};
		memcpy(L->cp[0], s, sizeof s);
		L->ncp[0] = 4;
		float r[3][2] = {{0, 0.05f}, {0.5f, 0.58f}, {1, 1}};
		memcpy(L->cp[1], r, sizeof r);
		L->ncp[1] = 3;
	} break;
	case T_SELECTIVE:
		L->nsel = 2;
		L->sel[0] = (SelPt){0.3f, 0.4f, 0.2f, {40, 20, 30, 40}};
		L->sel[1] = (SelPt){0.7f, 0.6f, 0.25f, {-40, 10, -30, 0}};
		break;
	case T_BRUSH:
		for (int k = 0; k < 3; k++) {
			Stroke *s = layer_add_stroke(L);
			s->mode = k % 3;
			s->amt = k == 1 ? -0.6f : 0.6f;
			s->rad = 0.05f;
			s->hard = 0.4f;
			s->flow = 0.35f;
			for (int j = 0; j < 20; j++) {
				layer_add_pt(L, 0.2f + 0.05f * j + 0.0f * k, 0.3f + 0.15f * k + 0.04f * sinf(j * 0.7f));
				L->str[L->nstr - 1].n++;
			}
		}
		break;
	case T_HEAL: layer_add_spot(L, 0.5f, 0.5f, 0.03f); layer_add_spot(L, 0.3f, 0.6f, 0.04f); break;
	case T_TEXT: snprintf(L->text, sizeof L->text, "SnapDesk"); break;
	case T_CROP: L->cr[0] = 0.1f; L->cr[1] = 0.15f; L->cr[2] = 0.85f; L->cr[3] = 0.9f; break;
	case T_ROTATE: L->rot = 1; break;
	case T_MIXER:
		L->mix[0][1] = -60; L->mix[0][0] = 30; L->mix[3][0] = -40; L->mix[3][2] = 30; L->mix[5][1] = 70; L->mix[1][2] = 40;
		break;
	}
	L->touched = 1;
}
static int self_test(const char *in, const char *outdir) {
	if (!doc_load(in)) {
		fprintf(stderr, "load failed: %s\n", g_err);
		return 1;
	}
	int bad = 0;
	for (int t = 0; t < T_COUNT; t++) {
		stack_clear();
		Layer *L = layer_new(t);
		test_setup_layer(L, t);
		double t0 = now_ms();
		Img im = img_clone(&doc.proxy);
		apply_layer(L, &im);
		double ms = now_ms() - t0;
		size_t nbad = 0;
		for (size_t i = 0; i < (size_t)im.w * im.h * 3; i++)
			if (!isfinite(im.p[i])) nbad++;
		printf("%-16s %7.1f ms  %dx%d  nonfinite=%zu\n", TD[t].name, ms, im.w, im.h, nbad);
		if (nbad) bad++;
		/* write a small preview */
		int tw = MIN(im.w, 480), th = MAX(1, im.h * tw / im.w);
		Img sm = resize_f(&im, tw, th);
		uint8_t *rgb = (uint8_t *)xmalloc((size_t)tw * th * 3);
		for (int i = 0; i < tw * th * 3; i++) rgb[i] = (uint8_t)CLAMP((int)(sm.p[i] * 255.f + 0.5f), 0, 255);
		char path[1024];
		snprintf(path, sizeof path, "%s/%02d_%s.png", outdir, t, TD[t].name);
		for (char *c = path + strlen(outdir) + 1; *c; c++)
			if (*c == ' ' || *c == '&') *c = '_';
		stbi_write_png(path, tw, th, 3, rgb, tw * 3);
		free(rgb);
		img_free(&sm);
		img_free(&im);
	}
	/* full-resolution export of a multi-layer stack */
	stack_clear();
	int order[] = {T_TUNE, T_DETAILS, T_VIGNETTE, T_GRAIN};
	for (int i = 0; i < 4; i++) {
		Layer *L = layer_new(order[i]);
		test_setup_layer(L, order[i]);
	}
	char path[1100], msg[1300];
	snprintf(path, sizeof path, "%s/export_full.jpg", outdir);
	double t0 = now_ms();
	int ok = export_image(path, 90, 100, msg, sizeof msg);
	printf("full export: %s (%.0f ms) %s\n", ok ? "ok" : "FAILED", now_ms() - t0, msg);
	return bad || !ok;
}
static void shot_args(const char *a, const char *b, const char *c) { (void)a; (void)b; (void)c; }

int main(int argc, char **argv) {
	const char *in_path = NULL, *test_dir = NULL, *shot_path = NULL, *shot_tool = NULL, *shot_set = NULL;
	int shot_w = 1440, shot_h = 900, shot_looks = 1, shot_modal = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--scale") && i + 1 < argc) ui_scale = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--test") && i + 1 < argc) test_dir = argv[++i];
		else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i];
		else if (!strcmp(argv[i], "--tool") && i + 1 < argc) shot_tool = argv[++i];
		else if (!strcmp(argv[i], "--set") && i + 1 < argc) shot_set = argv[++i];
		else if (!strcmp(argv[i], "--size") && i + 2 < argc) {
			shot_w = atoi(argv[++i]);
			shot_h = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--nolooks")) shot_looks = 0;
		else if (!strcmp(argv[i], "--modal") && i + 1 < argc) shot_modal = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printf("SnapDesk - Snapseed-style photo editor\nusage: snapdesk [photo] [--scale N]\n  drag & drop works too, F1 shows all shortcuts\n");
			return 0;
		} else if (argv[i][0] != '-') in_path = argv[i];
	}
	(void)shot_args;
	if (ui_scale < 0.5f) ui_scale = 1.f;
	SDL_SetMainReady();
	int headless = test_dir || shot_path;
	if (headless) {
		if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) die("SDL_Init: %s", SDL_GetError());
	} else {
		SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) die("SDL_Init: %s", SDL_GetError());
	}
	init_luts();
	looks_load_file();
	fonts_init();
	pool_init();
	doc.proxy_max = PROXY_DEFAULT;

	if (test_dir) {
		int rc = in_path ? self_test(in_path, test_dir) : 2;
		pool_quit();
		return rc;
	}
	if (shot_path) {
		FW = shot_w;
		FH = shot_h;
		fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
		show_looks = shot_looks;
		if (in_path) load_file(in_path);
		if (doc.loaded && shot_tool) {
			int st_ = -1;
			for (int t = 0; t < T_COUNT; t++)
				if (strlen(TD[t].name) == strlen(shot_tool) && ends_with_ci(TD[t].name, shot_tool)) st_ = t;
			if (st_ >= 0) select_tool(st_);
			else tool_key(shot_tool[0]);
			if (active >= 0 && shot_set) {
				char *s = strdup(shot_set), *tok = strtok(s, ",");
				while (tok) {
					int idx;
					float v;
					if (sscanf(tok, "%d=%f", &idx, &v) == 2 && idx >= 0 && idx < MAXP) stack[active].p[idx] = v;
					tok = strtok(NULL, ",");
				}
				free(s);
				touch(&stack[active]);
			}
		}
		modal = shot_modal;
		for (int k = 0; k < 6; k++) {
			if (need_render) pipeline_render();
			layout();
			ui_frame();
			frame_end();
			if (doc.loaded && k > 2) {
				for (int j = 0; j < 40 && lt_next < look_total(); j++) looks_tick();
			}
		}
		if (need_render) pipeline_render();
		ui_frame();
		uint8_t *rgb = (uint8_t *)xmalloc((size_t)FW * FH * 3);
		for (int i = 0; i < FW * FH; i++) {
			rgb[i * 3] = (fb[i] >> 16) & 255;
			rgb[i * 3 + 1] = (fb[i] >> 8) & 255;
			rgb[i * 3 + 2] = fb[i] & 255;
		}
		stbi_write_png(shot_path, FW, FH, 3, rgb, FW * 3);
		free(rgb);
		pool_quit();
		return 0;
	}

	/* ---- interactive ---- */
	if (ui_scale == 1.f) {
		float d = 0;
		if (SDL_GetDisplayDPI(0, &d, NULL, NULL) == 0 && d >= 168.f) ui_scale = 2.f;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, (ui_scale == 1.f) ? "nearest" : "linear");
	g_win = SDL_CreateWindow("SnapDesk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(1440 * ui_scale), (int)(900 * ui_scale), SDL_WINDOW_RESIZABLE);
	if (!g_win) die("window: %s", SDL_GetError());
	SDL_SetWindowMinimumSize(g_win, (int)(980 * ui_scale), (int)(600 * ui_scale));
	g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
	if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
	if (!g_ren) die("renderer: %s", SDL_GetError());
	{
		int w, h;
		SDL_GetWindowSize(g_win, &w, &h);
		fb_resize(w, h);
	}
	SDL_StopTextInput();
	if (in_path) load_file(in_path);
	saved_hpos = hpos;
	SDL_Event e;
	char last_title_state = -1;
	while (!quit_flag) {
		int animating = (hud_until > SDL_GetTicks()) || (toast_until > SDL_GetTicks()) || tf_id || (doc.loaded && show_looks && lt_next < look_total() && out_ok) || pending_export;
		int got = animating ? SDL_WaitEventTimeout(&e, 16) : SDL_WaitEvent(&e);
		if (got) {
			handle_event(&e);
			while (SDL_PollEvent(&e)) handle_event(&e);
		} else
			dirty = 1;
		if (quit_flag) break;
		pipeline_tick();
		if (need_render) pipeline_render();
		ui_frame();
		frame_end();
		if (need_render) {
			pipeline_render();
			ui_frame();
			frame_end();
		}
		apply_cursor();
		present();
		if (pending_export) run_pending_export();
		char st = (char)((hpos != saved_hpos) + 2 * doc.loaded);
		if (st != last_title_state) {
			update_title();
			last_title_state = st;
		}
	}
	pool_quit();
	SDL_Quit();
	return 0;
}
