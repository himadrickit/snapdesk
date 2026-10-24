
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
		c.cx = L->ax * w;
		c.cy = L->ay * h;
		c.a = L->arx * m;
		c.b = L->ary * m;
		float th = L->arot;
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
