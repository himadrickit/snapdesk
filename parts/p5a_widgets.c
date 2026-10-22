
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
