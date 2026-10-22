
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
