
/* ================================================================== document, pipeline, history, I/O */
#define PROXY_DEFAULT 1600
#define HMAX 200

typedef struct {
	char path[1024];
	uint8_t *rgb; /* source pixels, EXIF-oriented, RGB8 */
	int w, h;
	Img proxy; /* float preview-resolution copy of the source */
	Img proxy_lo, proxy_lo4; /* half-size and quarter-size copies used while a value is being dragged */
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
static int pfL_ok = 0, pfL_k = -1, outL_ok = 0, lo_shown = 0, pfL_level = 0, lo_level = 1;
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

static void pipeline_render_lo(int n, int K, int level) {
	if (level != pfL_level) {
		pfL_ok = 0;
		outL_ok = 0;
		pfL_level = level;
	}
	const Img *base = level == 2 ? &doc.proxy_lo4 : &doc.proxy_lo;
	uint64_t sigK = stack_sig(K);
	if (!(pfL_ok && pfL_k == K && pfL_sig == sigK)) {
		if (outL_ok && K == n && outL_sig == sigK)
			img_assign(&pfL, &outL);
		else {
			img_assign(&pfL, base);
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
	/* `out` keeps the size the full-quality render has; if that is unchanged the small result is shown
	 * directly (the canvas scales it), otherwise it is upscaled once so the view does not jump */
	int ew = doc.proxy.w, eh = doc.proxy.h;
	for (int i = 0; i < n; i++)
		if (eff_visible(i)) layer_outdims(&stack[i], &ew, &eh);
	if (!(out.p && out.w == ew && out.h == eh)) {
		Img up = resize_f(&outL, ew, eh);
		img_free(&out);
		out = up;
		mips_from_img(&disp, &out);
	} else
		mips_from_img(&disp, &outL);
	out_ok = 0; /* `out` is not up to date now */
	lo_shown = 1;
	compute_histogram(&disp);
	render_serial++;
}
static void pipeline_render(void) {
	if (!doc.loaded) return;
	double t0 = now_ms();
	int n = nlayers;
	int K = active >= 0 ? active : n;
	if (doc.proxy_lo.p && full_ms > 25.0 && interacting_now()) {
		if (!lo_shown) lo_level = (full_ms * 0.25 > 28.0 && doc.proxy_lo4.p) ? 2 : 1; /* heavier tool: smaller preview */
		pipeline_render_lo(n, K, lo_level);
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
	return !L->mask_on && !(L->area_on && L->tool != T_LENS);
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
	img_free(&doc.proxy_lo4);
	if ((long)tw * th > proxy_lo_min) {
		doc.proxy_lo = resize_f(&doc.proxy, MAX(1, (tw + 1) / 2), MAX(1, (th + 1) / 2));
		doc.proxy_lo4 = resize_f(&doc.proxy, MAX(1, (tw + 3) / 4), MAX(1, (th + 3) / 4));
	}
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
