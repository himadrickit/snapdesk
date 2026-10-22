
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
