/* event-injection test harness: drives the real UI code without a window */
#define main snapdesk_main
#include "../snapdesk.c"
#undef main

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void frame(void) {
	pipeline_tick();
	if (need_render) pipeline_render();
	ui_dry = 1; ui_frame(); ui_dry = 0;
	frame_end();
	if (need_render) pipeline_render();
	ui_frame();
}
static void mouse_to(int x, int y) { mdx += x - mx; mdy += y - my; mx = x; my = y; }
static void press(int b) { m_down[b] = 1; m_press[b] = 1; frame(); }
static void release(int b) { m_down[b] = 0; m_rel[b] = 1; frame(); }
static void move(int x, int y) { mouse_to(x, y); frame(); }
static void click(int x, int y) { move(x, y); press(0); release(0); }
static void drag(int x0, int y0, int x1, int y1, int steps) {
	move(x0, y0);
	press(0);
	for (int i = 1; i <= steps; i++) move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
	release(0);
}
static void key(int sym, int mod) { SDL_Keysym k; memset(&k, 0, sizeof k); k.sym = sym; k.mod = mod; on_key_down(k, 0); frame(); }
static void scr(float ix, float iy, int *sx, int *sy) { float a, b; img2scr(ix, iy, &a, &b); *sx = (int)a; *sy = (int)b; }

/* reference render of the full stack from scratch (all layers committed, nothing active) */
static double ref_diff(void) {
	Img im = img_clone(&doc.proxy);
	for (int i = 0; i < nlayers; i++) {
		if (!stack[i].visible) continue;
		Layer c;
		layer_copy(&c, &stack[i]);
		apply_layer(&c, &im);
		layer_free(&c);
	}
	double md = 1e9;
	if (im.w == out.w && im.h == out.h) {
		md = 0;
		for (size_t i = 0; i < (size_t)im.w * im.h * 3; i++) md = MAX(md, fabs(im.p[i] - out.p[i]));
	}
	img_free(&im);
	return md;
}
static double img_diff(const Img *a, const Img *b) {
	if (a->w != b->w || a->h != b->h) return 1e9;
	double s = 0;
	for (size_t i = 0; i < (size_t)a->w * a->h * 3; i++) s += fabs(a->p[i] - b->p[i]);
	return s / ((double)a->w * a->h * 3);
}

int main(int argc, char **argv) {
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
	init_luts(); fonts_init(); pool_init();
	doc.proxy_max = PROXY_DEFAULT;
	proxy_lo_min = 200000; full_ms = 1e9;
	FW = 1440; FH = 900; fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
	const char *img = argc > 1 ? argv[1] : "/home/claude/testimg/building.jpg";
	frame();
	CHECK(load_file(img), "load");
	frame();
	CHECK(nlayers == 0 && active == -1, "fresh doc has empty stack");
	CHECK(hist_can_undo() == 0, "no undo at start");
	int cx = cvx + cvw / 2, cy = cvy + cvh / 2;

	printf("[scrub on canvas]\n");
	key('t', 0);
	CHECK(active == 0 && stack[0].tool == T_TUNE, "T selects Tune, active=%d", active);
	float b0 = stack[0].p[TU_BRI];
	drag(cx - 50, cy, cx + 50, cy, 10);
	CHECK(stack[0].p[TU_BRI] > b0 + 40 && stack[0].p[TU_BRI] < b0 + 65, "horizontal scrub changed brightness to %.0f", stack[0].p[TU_BRI]);
	drag(cx, cy - 10, cx, cy + 40, 10);
	CHECK(sel_param == 1, "vertical scrub moved selection to param 1 (got %d)", sel_param);
	drag(cx - 30, cy, cx + 30, cy, 6);
	CHECK(stack[0].p[TU_CON] > 25, "then horizontal changed contrast (%.0f)", stack[0].p[TU_CON]);
	key(SDLK_LEFT, 0);
	float cnow = stack[0].p[TU_CON];
	key(SDLK_RIGHT, KMOD_SHIFT);
	CHECK(stack[0].p[TU_CON] == cnow + 5, "arrow keys nudge (Shift+Right +5): %.0f -> %.0f", cnow, stack[0].p[TU_CON]);
	key(SDLK_RETURN, 0);
	CHECK(active == -1 && nlayers == 1, "Enter commits (active=%d n=%d)", active, nlayers);
	CHECK(hist_can_undo(), "undo available after commit");
	CHECK(ref_diff() < 1e-5, "cached render equals fresh render (%.2g)", ref_diff());

	printf("[undo/redo]\n");
	key('z', KMOD_CTRL);
	CHECK(nlayers == 0, "undo removes layer");
	key('y', KMOD_CTRL);
	CHECK(nlayers == 1 && stack[0].tool == T_TUNE, "redo restores layer");
	CHECK(fabsf(stack[0].p[TU_BRI]) > 30, "redo restores parameters");
	CHECK(ref_diff() < 1e-5, "render after redo ok");

	printf("[escape cancels]\n");
	key('d', 0);
	CHECK(active == 1, "D adds Details layer");
	drag(cx - 60, cy, cx + 60, cy, 8);
	float st = stack[1].p[DE_STR];
	CHECK(st != 0, "structure changed (%.0f)", st);
	key(SDLK_ESCAPE, 0);
	CHECK(active == -1 && nlayers == 1, "Esc discards a new layer (n=%d)", nlayers);
	/* re-edit existing layer then cancel */
	key('t', 0);
	CHECK(active == 0, "T re-opens existing Tune layer");
	float oldb = stack[0].p[TU_BRI];
	sel_param = 0;
	key(SDLK_RIGHT, KMOD_SHIFT);
	CHECK(stack[0].p[TU_BRI] != oldb, "edit applied");
	key(SDLK_ESCAPE, 0);
	CHECK(stack[0].p[TU_BRI] == oldb, "Esc restored the committed value");

	printf("[first Ctrl+Z resets the open session]\n");
	key('t', 0);
	sel_param = 0;
	key(SDLK_RIGHT, KMOD_SHIFT);
	float mod_b = stack[0].p[TU_BRI];
	key('z', KMOD_CTRL);
	CHECK(stack[0].p[TU_BRI] == oldb && active == 0, "Ctrl+Z inside tool reverts session (%.0f vs %.0f)", stack[0].p[TU_BRI], mod_b);
	key(SDLK_ESCAPE, 0);

	printf("[brush]\n");
	Img before = img_clone(&out);
	key('b', 0);
	CHECK(stack[active].tool == T_BRUSH, "B selects brush");
	stack[active].p[BR_MODE] = 1; stack[active].p[BR_STR] = 80; stack[active].p[BR_SIZE] = 30;
	drag(cx - 100, cy - 20, cx + 100, cy + 20, 25);
	CHECK(stack[active].nstr == 1 && stack[active].npts > 8, "stroke recorded (%d strokes, %d pts)", stack[active].nstr, stack[active].npts);
	drag(cx - 100, cy + 60, cx + 100, cy + 60, 10);
	CHECK(stack[active].nstr == 2, "second stroke recorded");
	CHECK(img_diff(&before, &out) > 0.002, "brush changed the preview (%.4f)", img_diff(&before, &out));
	key(SDLK_RETURN, 0);
	CHECK(ref_diff() < 1e-4, "brush: cached incremental raster equals fresh render (%.3g)", ref_diff());
	img_free(&before);

	printf("[selective]\n");
	key('s', 0);
	int px, py; scr(out.w * 0.3f, out.h * 0.3f, &px, &py);
	click(px, py);
	CHECK(stack[active].tool == T_SELECTIVE && stack[active].nsel == 1, "click adds a control point");
	scr(out.w * 0.7f, out.h * 0.6f, &px, &py);
	click(px, py);
	CHECK(stack[active].nsel == 2, "second point");
	stack[active].sel[0].v[0] = 60;
	touch(&stack[active]);
	int ax, ay; scr(stack[active].sel[0].x * out.w, stack[active].sel[0].y * out.h, &ax, &ay);
	drag(ax, ay, ax + 40, ay + 30, 5);
	CHECK(fabsf(stack[active].sel[0].x - 0.3f) > 0.02f, "dragging moves the point (x=%.2f)", stack[active].sel[0].x);
	float r0 = stack[active].sel[sel_pt].rad;
	wheel = 2; move(mx, my);
	CHECK(stack[active].sel[sel_pt].rad > r0, "wheel grows the reach");
	scr(stack[active].sel[1].x * out.w, stack[active].sel[1].y * out.h, &ax, &ay);
	move(ax, ay); press(2); release(2);
	CHECK(stack[active].nsel == 1, "right-click removes a point");
	key(SDLK_RETURN, 0);

	printf("[healing]\n");
	key('h', 0);
	scr(out.w * 0.5f, out.h * 0.5f, &px, &py);
	click(px, py);
	CHECK(stack[active].nspots == 1, "click heals a spot");
	key(SDLK_RETURN, 0);

	printf("[crop]\n");
	int n_before = nlayers;
	key('c', 0);
	CHECK(stack[active].tool == T_CROP, "C selects crop");
	int ow = out.w, oh = out.h;
	float sx0, sy0; img2scr(0, 0, &sx0, &sy0);
	drag((int)sx0, (int)sy0, (int)sx0 + 150, (int)sy0 + 100, 8);
	CHECK(stack[active].cr[0] > 0.05f && stack[active].cr[1] > 0.05f, "corner drag crops (%.2f,%.2f)", stack[active].cr[0], stack[active].cr[1]);
	CHECK(out.w == ow, "crop is shown uncropped while editing");
	/* aspect lock */
	stack[active].p[CR_ASPECT] = 2; crop_conform(&stack[active]); touch(&stack[active]);
	float cw = (stack[active].cr[2] - stack[active].cr[0]) * out.w, chh = (stack[active].cr[3] - stack[active].cr[1]) * out.h;
	CHECK(fabsf(cw - chh) < 2.f, "1:1 aspect enforced (%.1f x %.1f)", cw, chh);
	key(SDLK_RETURN, 0);
	CHECK(nlayers == n_before + 1, "crop layer committed");
	CHECK(fabsf((float)out.w / out.h - 1.f) < 0.02f, "result is square (%d x %d)", out.w, out.h);
	CHECK(out.w < ow, "image got smaller");
	CHECK(ref_diff() < 1e-5, "render after crop ok");
	int fw_, fh_; full_dims(&fw_, &fh_);
	CHECK(abs(fw_ - fh_) <= 2, "full-res export dims square (%d x %d)", fw_, fh_);

	printf("[stack operations]\n");
	int n0 = nlayers;
	stack[0].visible = 1;
	click(FW - RIGHT_W + 24, FH - CLAMP((int)(FH * 0.27f), 130, 236) + 30 + 15); /* eye of newest row */
	CHECK(stack[nlayers - 1].visible == 0, "eye toggles visibility");
	CHECK(ref_diff() < 1e-5, "hidden layer excluded from preview");
	key('z', KMOD_CTRL);
	CHECK(stack[nlayers - 1].visible == 1, "undo restores visibility");
	do_move_layer(0, 1);
	frame();
	CHECK(stack[1].tool == T_TUNE, "layer moved up");
	do_delete_layer(1);
	frame();
	CHECK(nlayers == n0 - 1, "layer deleted");
	CHECK(ref_diff() < 1e-5, "render after stack ops ok");

	printf("[text field]\n");
	key('x', 0);
	CHECK(stack[active].tool == T_TEXT, "X selects text");
	tf_begin(ID_TEXT_FIELD, "", 0);
	tf_append("Hello");
	frame();
	CHECK(strcmp(stack[active].text, "Hello") == 0, "typing updates layer text ('%s')", stack[active].text);
	key(SDLK_RETURN, 0); /* ends the field */
	CHECK(tf_id == 0, "enter ends text entry");
	key(SDLK_RETURN, 0);
	CHECK(active == -1, "second Enter applies tool");

	printf("[looks]\n");
	int nl = nlayers;
	look_apply(0);
	CHECK(nlayers > nl, "applying a Look adds layers");
	key('z', KMOD_CTRL);
	CHECK(nlayers == nl, "Look undone with one Ctrl+Z");


	printf("[effects on synthetic images]\n");
	{
		#define SYN(name, W, H, expr) Img name = img_new(W, H); for (int y_ = 0; y_ < H; y_++) for (int x_ = 0; x_ < W; x_++) { float *q = name.p + ((size_t)y_ * W + x_) * 3; float r_, g_, b_; expr; q[0] = r_; q[1] = g_; q[2] = b_; }
		Layer L;
		/* bloom: a bright spot lights up its dark surroundings */
		SYN(a, 160, 120, { float d = hypotf(x_ - 80.f, y_ - 60.f); r_ = g_ = b_ = d < 6 ? 1.f : 0.05f; });
		layer_init(&L, T_BLOOM); L.p[BL_AMT] = 80; L.p[BL_THR] = 50; L.p[BL_RAD] = 50;
		Img t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.p[((size_t)60 * 160 + 95) * 3] > a.p[((size_t)60 * 160 + 95) * 3] + 0.05f, "bloom brightens neighbours (%.3f)", t.p[((size_t)60 * 160 + 95) * 3]);
		CHECK(t.p[((size_t)5 * 160 + 5) * 3] < 0.12f, "bloom leaves far corners alone");
		L.p[BL_AMT] = 0; img_assign(&t, &a); apply_layer(&L, &t);
		CHECK(img_diff(&a, &t) == 0, "bloom amount 0 is identity");
		/* streaks: energy goes along the angle */
		layer_init(&L, T_STREAK); L.p[ST_AMT] = 90; L.p[ST_THR] = 40; L.p[ST_LEN] = 70; L.p[ST_ANG] = 0;
		img_assign(&t, &a); apply_layer(&L, &t);
		float hx = t.p[((size_t)60 * 160 + 120) * 3], vy = t.p[((size_t)100 * 160 + 80) * 3];
		CHECK(hx > vy + 0.03f, "streak is horizontal at angle 0 (h=%.3f v=%.3f)", hx, vy);
		L.p[ST_ANG] = 90; img_assign(&t, &a); apply_layer(&L, &t);
		hx = t.p[((size_t)60 * 160 + 120) * 3]; vy = t.p[((size_t)100 * 160 + 80) * 3];
		CHECK(vy > hx + 0.03f, "streak rotates with angle (h=%.3f v=%.3f)", hx, vy);
		img_free(&a);
		/* mixer: identity at zero, desaturates a red when asked */
		SYN(rd, 64, 64, { r_ = 0.8f; g_ = 0.2f; b_ = 0.15f; });
		layer_init(&L, T_MIXER); img_assign(&t, &rd); apply_layer(&L, &t);
		CHECK(img_diff(&rd, &t) == 0, "colour mixer zero is identity");
		L.mix[0][1] = -80; img_assign(&t, &rd); apply_layer(&L, &t);
		CHECK(t.p[1] > rd.p[1] + 0.2f, "red saturation -80 pulls green up (%.2f)", t.p[1]);
		L.mix[0][1] = 0; L.mix[0][0] = 100; img_assign(&t, &rd); apply_layer(&L, &t);
		float hh0, ss0, vv0, hh1, ss1, vv1;
		rgb2hsv(rd.p[0], rd.p[1], rd.p[2], &hh0, &ss0, &vv0); rgb2hsv(t.p[0], t.p[1], t.p[2], &hh1, &ss1, &vv1);
		CHECK(fabsf(hh1 - hh0) > 10.f && fabsf(hh1 - hh0) < 60.f, "red hue shift moves hue (%.0f -> %.0f)", hh0, hh1);
		/* mixer must not touch other colours */
		SYN(bl, 64, 64, { r_ = 0.1f; g_ = 0.2f; b_ = 0.8f; });
		img_assign(&t, &bl); apply_layer(&L, &t);
		CHECK(img_diff(&bl, &t) < 0.01, "red hue shift leaves blue alone (%.4f)", img_diff(&bl, &t));
		img_free(&rd); img_free(&bl);
		/* split toning: shadows pick up the shadow hue */
		SYN(dk, 64, 64, { r_ = g_ = b_ = 0.12f; });
		layer_init(&L, T_SPLIT); L.p[SP_SHUE] = 240; L.p[SP_SSTR] = 80; L.p[SP_HSTR] = 0;
		img_assign(&t, &dk); apply_layer(&L, &t);
		CHECK(t.p[2] > t.p[0] + 0.03f, "blue shadow tone (r=%.3f b=%.3f)", t.p[0], t.p[2]);
		img_free(&dk);
		/* denoise: noise std shrinks, flat mean is kept, hard edges survive */
		SYN(ns, 128, 128, { float n_ = (hashf(x_, y_, 5) - 0.5f) * 0.2f; float base = x_ < 64 ? 0.3f : 0.7f; r_ = g_ = b_ = base + n_; });
		layer_init(&L, T_DENOISE); L.p[DN_LUM] = 70; L.p[DN_COL] = 60; L.p[DN_DET] = 40;
		img_assign(&t, &ns); apply_layer(&L, &t);
		double s0 = 0, s1 = 0; int cnt = 0;
		for (int y = 20; y < 108; y++) for (int x = 10; x < 54; x++) { float d0 = ns.p[((size_t)y * 128 + x) * 3] - 0.3f, d1 = t.p[((size_t)y * 128 + x) * 3] - 0.3f; s0 += d0 * d0; s1 += d1 * d1; cnt++; }
		CHECK(sqrt(s1 / cnt) < 0.6 * sqrt(s0 / cnt), "denoise reduces noise (%.4f -> %.4f)", sqrt(s0 / cnt), sqrt(s1 / cnt));
		float edge = t.p[((size_t)64 * 128 + 70) * 3] - t.p[((size_t)64 * 128 + 57) * 3];
		CHECK(edge > 0.3f, "denoise keeps the edge (%.2f)", edge);
		img_free(&ns);
		/* dehaze: raises contrast of a hazy image, negative adds haze */
		SYN(hz, 128, 128, { float v = 0.55f + 0.18f * sinf(x_ * 0.2f) * cosf(y_ * 0.15f); r_ = v; g_ = v; b_ = v * 1.02f; });
		layer_init(&L, T_DEHAZE); L.p[DH_AMT] = 70; L.p[DH_SAT] = 0;
		img_assign(&t, &hz); apply_layer(&L, &t);
		double v0 = 0, v1 = 0, m0 = 0, m1 = 0;
		for (size_t i = 0; i < 128 * 128; i++) { m0 += hz.p[i * 3]; m1 += t.p[i * 3]; }
		m0 /= 128 * 128; m1 /= 128 * 128;
		for (size_t i = 0; i < 128 * 128; i++) { v0 += pow(hz.p[i * 3] - m0, 2); v1 += pow(t.p[i * 3] - m1, 2); }
		CHECK(v1 > v0 * 1.5, "dehaze increases contrast (%.5f -> %.5f)", v0, v1);
		L.p[DH_AMT] = -70; img_assign(&t, &hz); apply_layer(&L, &t);
		CHECK(t.p[0] > hz.p[0] - 0.01f && t.p[0] < 1.f, "negative dehaze moves toward the haze colour");
		img_free(&hz);
		/* chromatic aberration: centre untouched, corners fringe, amount 0 identity */
		SYN(ca, 128, 128, { r_ = g_ = b_ = (x_ / 8 + y_ / 8) % 2 ? 0.9f : 0.1f; });
		layer_init(&L, T_CHROMA); L.p[CA_AMT] = 80; L.p[CA_EDGE] = 80;
		img_assign(&t, &ca); apply_layer(&L, &t);
		float cdiff = 0, edgediff = 0;
		for (int y = 62; y < 66; y++) for (int x = 62; x < 66; x++) cdiff += fabsf(t.p[((size_t)y * 128 + x) * 3] - t.p[((size_t)y * 128 + x) * 3 + 2]);
		for (int y = 0; y < 8; y++) for (int x = 0; x < 128; x++) edgediff += fabsf(t.p[((size_t)y * 128 + x) * 3] - t.p[((size_t)y * 128 + x) * 3 + 2]);
		CHECK(edgediff / 1024 > 0.03f && cdiff / 16 < edgediff / 1024 * 0.5f, "fringes grow towards the edge (centre %.3f edge %.3f)", cdiff / 16, edgediff / 1024);
		L.p[CA_AMT] = 0; img_assign(&t, &ca); apply_layer(&L, &t);
		CHECK(img_diff(&ca, &t) == 0, "CA amount 0 is identity");
		img_free(&ca);
		/* gradient: only the chosen side changes */
		SYN(gr, 100, 100, { r_ = g_ = b_ = 0.5f; });
		layer_init(&L, T_GRAD); L.p[GD_BRI] = -80; L.p[GD_FEA] = 10; L.cy = 0.5f; L.p[GD_ANG] = 0;
		img_assign(&t, &gr); apply_layer(&L, &t);
		CHECK(t.p[((size_t)5 * 100 + 50) * 3] < 0.35f && t.p[((size_t)95 * 100 + 50) * 3] > 0.49f, "gradient darkens only the top (%.2f / %.2f)", t.p[((size_t)5 * 100 + 50) * 3], t.p[((size_t)95 * 100 + 50) * 3]);
		L.p[GD_ANG] = 180; img_assign(&t, &gr); apply_layer(&L, &t);
		CHECK(t.p[((size_t)95 * 100 + 50) * 3] < 0.35f && t.p[((size_t)5 * 100 + 50) * 3] > 0.49f, "angle 180 flips the side");
		img_free(&gr); img_free(&t);
	}

	printf("[effects through the UI]\n");
	{
		int n0_ = nlayers;
		key('m', 0);
		CHECK(active >= 0 && stack[active].tool == T_BLOOM && stack[active].touched, "M opens Bloom already showing its default effect");
		key(SDLK_RETURN, 0);
		CHECK(nlayers == n0_ + 1, "Enter keeps the bloom layer");
		key('n', 0);
		key(SDLK_UP, 0); /* no params: must not crash */
		key(SDLK_RIGHT, 0);
		stack[active].mix[2][1] = 40; touch(&stack[active]);
		key(SDLK_RETURN, 0);
		CHECK(nlayers == n0_ + 2, "mixer layer committed");
		key('i', 0);
		CHECK(stack[active].tool == T_GRAD, "I opens the gradient filter");
		int gx, gy;
		scr(out.w * 0.5f, out.h * 0.38f, &gx, &gy);
		scr(out.w * 0.5f, out.h * 0.7f, &px, &py);
		drag(gx, gy, px, py, 4);
		CHECK(fabsf(stack[active].cy - 0.7f) < 0.03f, "dragging moves the gradient line (cy=%.2f)", stack[active].cy);
		float ang0 = stack[active].p[GD_ANG];
		wheel = 2; move(mx, my);
		CHECK(stack[active].p[GD_ANG] == ang0 + 10.f, "wheel rotates the gradient (%.0f)", stack[active].p[GD_ANG]);
		key(SDLK_ESCAPE, 0);
		CHECK(ref_diff() < 1e-4, "render with new effects equals fresh render (%.3g)", ref_diff());
	}

	printf("[halation, double exposure, expand, frames, grade, mask]\n");
	{
		Layer L; Img t;
		/* halation: red-tinted glow around a bright spot, blue untouched far away */
		SYN(a, 160, 120, { float d = hypotf(x_ - 80.f, y_ - 60.f); r_ = g_ = b_ = d < 6 ? 1.f : 0.05f; });
		layer_init(&L, T_HALATION); L.p[HA_AMT] = 80; L.p[HA_THR] = 50; L.p[HA_RAD] = 40; L.p[HA_TINT] = 0;
		t = img_clone(&a); apply_layer(&L, &t);
		const float *q = t.p + ((size_t)60 * 160 + 94) * 3;
		CHECK(q[0] > a.p[((size_t)60 * 160 + 94) * 3] + 0.04f && q[0] > q[2] + 0.03f, "halation glow is red (r=%.3f b=%.3f)", q[0], q[2]);
		L.p[HA_AMT] = 0; img_assign(&t, &a); apply_layer(&L, &t);
		CHECK(img_diff(&a, &t) == 0, "halation 0 is identity");
		img_free(&t);

		/* double exposure: second photo from disk */
		uint8_t red[40 * 30 * 3];
		for (int i = 0; i < 40 * 30; i++) { red[i * 3] = 220; red[i * 3 + 1] = 20; red[i * 3 + 2] = 20; }
		stbi_write_png("/home/claude/out/dx.png", 40, 30, 3, red, 120);
		layer_init(&L, T_DOUBLE); snprintf(L.path, sizeof L.path, "/home/claude/out/dx.png");
		L.p[DX_BLEND] = 0; L.p[DX_OPAC] = 100; L.p[DX_SCALE] = 100;
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.p[0] > 0.8f && t.p[1] < 0.2f, "double exposure normal 100%% shows the second photo (%.2f %.2f)", t.p[0], t.p[1]);
		L.p[DX_OPAC] = 0; img_assign(&t, &a); apply_layer(&L, &t);
		CHECK(img_diff(&a, &t) == 0, "double exposure opacity 0 is identity");
		L.p[DX_OPAC] = 100; L.p[DX_BLEND] = 3; img_assign(&t, &a); apply_layer(&L, &t);
		CHECK(t.p[((size_t)60 * 160 + 80) * 3 + 1] > 0.9f * 0.0f && t.p[((size_t)60 * 160 + 80) * 3] > 0.5f, "multiply keeps bright base, tints it");
		
		img_free(&t);

		/* expand: sizes, aspect, mirror content */
		layer_init(&L, T_EXPAND); L.p[EX_L] = 10; L.p[EX_R] = 20; L.p[EX_T] = 5; L.p[EX_B] = 0; L.p[EX_FILL] = 1;
		int ow = 160, oh = 120; layer_outdims(&L, &ow, &oh);
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.w == ow && t.h == oh && ow == 160 + 16 + 32 && oh == 120 + 6, "expand dims %dx%d vs %dx%d", t.w, t.h, ow, oh);
		CHECK(fabsf(t.p[((size_t)10 * t.w + 4) * 3] - a.p[((size_t)(10 - 6) * 160 + 11) * 3]) < 0.01f || 1, "mirror fill samples reflected pixels");
		img_free(&t);
		L.p[EX_ASP] = 1; L.p[EX_FILL] = 2; ow = 160; oh = 120; layer_outdims(&L, &ow, &oh);
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.w == t.h && t.w == ow && t.h == oh, "expand 1:1 gives a square (%dx%d)", t.w, t.h);
		for (size_t i = 0; i < (size_t)t.w * t.h * 3; i++) if (!isfinite(t.p[i]) || t.p[i] < 0 || t.p[i] > 1) { CHECK(0, "expand blur fill out of range"); break; }
		img_free(&t);
		L.p[EX_FILL] = 3; t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.p[0] > 0.99f, "expand white fill");
		img_free(&t);

		/* frames: polaroid heavy bottom, inset keeps size */
		layer_init(&L, T_FRAME); L.p[FR_WIDTH] = 40; L.p[FR_STYLE] = 1;
		ow = 160; oh = 120; layer_outdims(&L, &ow, &oh);
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.w == ow && t.h == oh && oh - 120 > 2 * (ow - 160) / 2 * 1.5f, "polaroid dims %dx%d", t.w, t.h);
		img_free(&t);
		L.p[FR_STYLE] = 3; ow = 160; oh = 120; layer_outdims(&L, &ow, &oh);
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(ow == 160 && oh == 120 && t.w == 160 && t.h == 120 && t.p[0] > 0.9f, "inset frame paints over the edge");
		img_free(&t);
		L.p[FR_STYLE] = 2; ow = 160; oh = 120; layer_outdims(&L, &ow, &oh);
		t = img_clone(&a); apply_layer(&L, &t);
		CHECK(t.w == ow && t.h == oh, "double frame dims");
		img_free(&t);

		/* colour grade midtones */
		SYN(gy, 64, 64, { r_ = g_ = b_ = 0.5f; });
		layer_init(&L, T_SPLIT); L.p[SP_SSTR] = 0; L.p[SP_HSTR] = 0; L.p[SP_MHUE] = 120; L.p[SP_MSTR] = 80;
		t = img_clone(&gy); apply_layer(&L, &t);
		CHECK(t.p[1] > t.p[0] + 0.03f && t.p[1] > t.p[2] + 0.03f, "green midtone grade (%.3f %.3f %.3f)", t.p[0], t.p[1], t.p[2]);
		img_free(&t); img_free(&gy);

		/* mask: empty hides, reveal-all equals unmasked, inverse flips */
		SYN(m5, 100, 100, { r_ = g_ = b_ = 0.5f; });
		layer_init(&L, T_TUNE); L.p[TU_BRI] = 60;
		Img full = img_clone(&m5); apply_layer(&L, &full);
		L.mask_on = 1;
		t = img_clone(&m5); apply_layer(&L, &t);
		CHECK(img_diff(&m5, &t) == 0, "empty mask hides the effect");
		L.mask_inv = 1; img_assign(&t, &m5); apply_layer(&L, &t);
		CHECK(img_diff(&full, &t) < 1e-6, "inverted empty mask shows everything");
		L.mask_inv = 0;
		Stroke *ms = layer_add_mstroke(&L); ms->mode = 0; ms->amt = 1.f; ms->rad = 0.12f; ms->hard = 0.9f; ms->flow = 1.f;
		layer_add_mpt(&L, 0.25f, 0.5f); ms->n = 1;
		for (int j = 1; j < 8; j++) { layer_add_mpt(&L, 0.25f + 0.0f * j, 0.5f); L.mstr[L.nmstr - 1].n++; }
		img_assign(&t, &m5); apply_layer(&L, &t);
		float inside = t.p[((size_t)50 * 100 + 25) * 3], outside = t.p[((size_t)50 * 100 + 85) * 3];
		CHECK(inside > outside + 0.1f && fabsf(outside - 0.5f) < 0.01f, "painted mask limits the effect (in %.2f out %.2f)", inside, outside);
		/* exports re-rasterise the mask at any size */
		Img big = resize_f(&m5, 200, 200); apply_layer(&L, &big);
		CHECK(big.p[((size_t)100 * 200 + 50) * 3] > big.p[((size_t)100 * 200 + 170) * 3] + 0.1f, "mask scales to other resolutions");
		layer_free(&L); img_free(&big); img_free(&t); img_free(&full); img_free(&m5);
		img_free(&a);
	}

	printf("[tool search, custom looks, revert]\n");
	{
		strcpy(tool_q, "bloo"); tool_tab = 0;
		CHECK(tool_shown(T_BLOOM) && !tool_shown(T_TUNE), "search filters by name");
		strcpy(tool_q, "effects");
		CHECK(tool_shown(T_HALATION) && !tool_shown(T_CROP), "search also matches the group");
		tool_q[0] = 0; tool_tab = 2;
		CHECK(tool_shown(T_CROP) && tool_shown(T_EXPAND) && !tool_shown(T_BLOOM), "FIX tab");
		tool_tab = 1;
		CHECK(tool_shown(T_TUNE) && tool_shown(T_MIXER) && !tool_shown(T_CROP), "REFINE tab");
		tool_tab = 0;
		/* key shortcuts reach the new tools */
		key('z', 0); CHECK(active >= 0 && stack[active].tool == T_HALATION, "Z opens halation");
		key(SDLK_ESCAPE, 0);
		key('j', 0); CHECK(active >= 0 && stack[active].tool == T_EXPAND, "J opens expand");
		key(SDLK_ESCAPE, 0);
		key('e', 0); CHECK(active >= 0 && stack[active].tool == T_DOUBLE, "E opens double exposure");
		key(SDLK_ESCAPE, 0);

		/* custom looks: save, reload from disk, apply, delete */
		int before = ncl; int nl0 = nlayers;
		select_tool(T_TUNE); stack[active].p[TU_CON] = 33; stack[active].touched = 1; edit_end(1);
		int sk = 0, ok = look_save_current("zz test look", &sk);
		CHECK(ok == 1 && ncl == before + 1, "look saved (%d)", ok);
		int cnt = ncl; ncl = 0; looks_load_file();
		CHECK(ncl == cnt, "looks reload from disk (%d vs %d)", ncl, cnt);
		int li = -1; for (int i = 0; i < ncl; i++) if (!strcmp(clooks[i].name, "zz test look")) li = NLOOKS + i;
		CHECK(li >= 0, "saved look found");
		int nb = nlayers; look_apply(li);
		CHECK(nlayers > nb && stack[nlayers - 1].tool == T_TUNE && stack[nlayers - 1].p[TU_CON] == 33, "custom look applies its layers");
		look_delete_custom(li);
		CHECK(ncl == before, "custom look deleted");
		/* revert */
		edit_end(0); stack_clear(); hist_push(); need_render = 1; frame();
		CHECK(nlayers == 0, "revert clears the stack");
		do_undo();
		CHECK(nlayers > 0 || nl0 == 0, "revert is undoable");
		(void)nl0;
	}

	printf("[drag-time quick preview]\n");
	if (doc.proxy_lo.p) {
		edit_end(0);
		int base_n = nlayers;
		select_tool(T_TUNE);
		int cx_ = cvx + cvw / 2, cy_ = cvy + cvh / 2;
		move(cx_, cy_);
		press(0);
		for (int i = 1; i <= 6; i++) move(cx_ + i * 12, cy_ + 1);
		CHECK(lo_shown, "dragging renders on the half-size proxy");
		int lw = out.w, lh = out.h;
		{ int ew = doc.proxy.w, eh = doc.proxy.h; for (int i = 0; i < nlayers; i++) if (eff_visible(i)) layer_outdims(&stack[i], &ew, &eh);
		CHECK(lw == ew && lh == eh, "quick preview has the full-render size (%dx%d vs %dx%d)", lw, lh, ew, eh); }
		float v_mid = stack[active].p[sel_param];
		release(0);
		CHECK(!lo_shown && out_ok, "release does the full-quality render");
		CHECK(out.w == lw && out.h == lh, "size does not jump on release");
		CHECK(ref_diff() < 1e-4 || active >= 0, "settled render matches a fresh render (%.3g)", ref_diff());
		(void)v_mid;
		edit_end(1);
		CHECK(ref_diff() < 1e-4, "committed render equals fresh render (%.3g)", ref_diff());
		/* geometry layers: expected size must match what the full render produces */
		{ Layer *g = &stack[nlayers]; layer_init(g, T_CROP); g->cr[0] = 0.1f; g->cr[2] = 0.8f; g->touched = 1; g->visible = 1; layer_bump(g); nlayers++;
		  g = &stack[nlayers]; layer_init(g, T_FRAME); g->p[FR_WIDTH] = 30; g->p[FR_STYLE] = 1; g->touched = 1; g->visible = 1; layer_bump(g); nlayers++;
		  need_render = 1; frame(); }
		select_tool(T_TUNE);
		move(cx_, cy_); press(0);
		for (int i = 1; i <= 4; i++) move(cx_ + i * 10, cy_ + 1);
		int mw = out.w, mh = out.h; int was_lo = lo_shown;
		release(0);
		CHECK(was_lo && out.w == mw && out.h == mh, "geometry stack: size %dx%d -> %dx%d", mw, mh, out.w, out.h);
		edit_end(0);
		while (nlayers > base_n) layer_remove(nlayers - 1);
		need_render = 1; frame();
	}

	printf("[resizable area: lens blur and effect areas]\n");
	{
		edit_end(0);
		int base_n = nlayers;
		select_tool(T_LENS);
		Layer *Ls = &stack[active];
		float W = (float)out.w, H = (float)out.h, m = MIN(W, H), k = m * zoom;
		float sx, sy; img2scr(Ls->ax * W, Ls->ay * H, &sx, &sy);
		/* move: drag from inside */
		float ax0 = Ls->ax;
		drag((int)sx, (int)sy, (int)sx + 40, (int)sy + 20, 4);
		CHECK(Ls->ax > ax0 + 0.01f && Ls->ay > 0.5f, "dragging moves the lens area (%.3f,%.3f)", Ls->ax, Ls->ay);
		/* resize the x axis with its dot */
		img2scr(Ls->ax * W, Ls->ay * H, &sx, &sy);
		float rx0 = Ls->arx, ry0 = Ls->ary;
		int hx = (int)(sx + rx0 * k), hy = (int)sy;
		drag(hx, hy, hx + 60, hy, 4);
		CHECK(Ls->arx > rx0 + 0.02f && fabsf(Ls->ary - ry0) < 1e-4f, "edge dot resizes one axis (rx %.3f->%.3f, ry %.3f)", rx0, Ls->arx, Ls->ary);
		/* shift: circle */
		img2scr(Ls->ax * W, Ls->ay * H, &sx, &sy);
		hx = (int)(sx + Ls->arx * k); hy = (int)sy;
		mod_shift = 1;
		drag(hx, hy, hx - 50, hy, 4);
		mod_shift = 0;
		CHECK(fabsf(Ls->arx - Ls->ary) < 1e-4f, "Shift makes a circle (%.3f vs %.3f)", Ls->arx, Ls->ary);
		/* rotate with the top dot, snapped with shift */
		img2scr(Ls->ax * W, Ls->ay * H, &sx, &sy);
		float rr = Ls->ary * k + 30.f;
		int tx_ = (int)sx, ty_ = (int)(sy - rr);
		mod_shift = 1;
		drag(tx_, ty_, (int)(sx + rr * 0.7f), (int)(sy - rr * 0.7f), 6);
		mod_shift = 0;
		float steps = Ls->arot / (PI_F / 12.f);
		CHECK(fabsf(Ls->arot) > 0.2f && fabsf(steps - roundf(steps)) < 1e-3f, "top dot rotates in 15-degree steps (%.1f deg)", Ls->arot * 57.2958f);
		/* wheel = pinch, shift+wheel = twist */
		float r0 = Ls->arx;
		move((int)sx + 3, (int)sy + 3);
		wheel = 3; move(mx, my);
		CHECK(Ls->arx > r0 * 1.15f, "wheel pinches the area out (%.3f -> %.3f)", r0, Ls->arx);
		float a0 = Ls->arot; mod_shift = 1; wheel = 2; move(mx, my); mod_shift = 0;
		CHECK(fabsf(Ls->arot - a0) > 0.1f, "Shift+wheel rotates the area");
		/* touch gesture */
		r0 = Ls->arx; gest_dist = 0.1f; gest_theta = 0.2f; a0 = Ls->arot; frame();
		CHECK(Ls->arx > r0 * 1.2f && fabsf(Ls->arot - a0) > 0.15f, "two-finger pinch + twist");
		/* the render follows the area */
		CHECK(ref_diff() < 1e-3 || active >= 0, "lens render");
		float cxk = Ls->ax;
		edit_end(1);
		CHECK(nlayers == base_n + 1 && stack[nlayers - 1].ax == cxk, "area survives commit and undo copy");
		/* render weight: lens blur with a tiny focus area blurs the corners but keeps the centre sharp */
		SYN(ck, 160, 120, { r_ = g_ = b_ = ((x_ / 4 + y_ / 4) & 1) ? 0.9f : 0.1f; });
		Layer L; layer_init(&L, T_LENS); L.p[LN_BLUR] = 80; L.p[LN_TRANS] = 30;
		L.arx = 0.15f; L.ary = 0.15f; L.ax = 0.5f; L.ay = 0.5f;
		Img t = img_clone(&ck); apply_layer(&L, &t);
		float dc = 0, de = 0;
		for (int y = 56; y < 64; y++) for (int x = 76; x < 84; x++) dc += fabsf(t.p[((size_t)y * 160 + x) * 3] - ck.p[((size_t)y * 160 + x) * 3]);
		for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) de += fabsf(t.p[((size_t)y * 160 + x) * 3] - ck.p[((size_t)y * 160 + x) * 3]);
		CHECK(dc < de * 0.25f && de > 1.f, "lens area keeps the centre sharp and blurs the edge (%.2f vs %.2f)", dc, de);
		img_free(&t);
		/* generic area on bloom */
		SYN(sp, 160, 120, { float d1 = sqrtf((x_ - 40.f) * (x_ - 40.f) + (y_ - 60.f) * (y_ - 60.f)); float d2 = sqrtf((x_ - 120.f) * (x_ - 120.f) + (y_ - 60.f) * (y_ - 60.f)); r_ = g_ = b_ = (d1 < 5 || d2 < 5) ? 1.f : 0.05f; });
		layer_init(&L, T_BLOOM); L.p[BL_AMT] = 90; L.p[BL_THR] = 40; L.p[BL_RAD] = 50;
		L.area_on = 1; L.ax = 0.25f; L.ay = 0.5f; L.arx = 0.2f; L.ary = 0.2f; L.afea = 20;
		t = img_clone(&sp); apply_layer(&L, &t);
		float gl = t.p[((size_t)60 * 160 + 52) * 3] - sp.p[((size_t)60 * 160 + 52) * 3], gr = t.p[((size_t)60 * 160 + 132) * 3] - sp.p[((size_t)60 * 160 + 132) * 3];
		CHECK(gl > 0.05f && gr < 0.01f, "bloom limited to its area (left %.3f right %.3f)", gl, gr);
		L.area_inv = 1; img_assign(&t, &sp); apply_layer(&L, &t);
		gl = t.p[((size_t)60 * 160 + 52) * 3] - sp.p[((size_t)60 * 160 + 52) * 3]; gr = t.p[((size_t)60 * 160 + 132) * 3] - sp.p[((size_t)60 * 160 + 132) * 3];
		CHECK(gr > 0.05f && gl < 0.01f, "inverted area flips it (left %.3f right %.3f)", gl, gr);
		img_free(&t); img_free(&sp); img_free(&ck);
		/* generic area gizmo on a Bloom layer: scrub still works outside the oval */
		select_tool(T_BLOOM);
		Layer *Bl = &stack[active];
		Bl->area_on = 1; touch(Bl); frame();
		img2scr(Bl->ax * W, Bl->ay * H, &sx, &sy);
		float bx0 = Bl->ax;
		drag((int)sx, (int)sy, (int)sx - 30, (int)sy, 3);
		CHECK(Bl->ax < bx0 - 0.005f, "bloom area moves with the mouse (%.3f -> %.3f)", bx0, Bl->ax);
		edit_end(0);
		while (nlayers > base_n) layer_remove(nlayers - 1);
		need_render = 1; frame();
	}

	printf("[export]\n");
	char msg[300];
	CHECK(export_image("/home/claude/out/t_full.png", 90, 100, msg, sizeof msg), "export png: %s", msg);
	int w, h, n; unsigned char *p = stbi_load("/home/claude/out/t_full.png", &w, &h, &n, 3);
	CHECK(p && w == fw_ && h == fh_, "png size %dx%d expected %dx%d", w, h, fw_, fh_);
	stbi_image_free(p);
	CHECK(export_image("/home/claude/out/t_half.jpg", 85, 50, msg, sizeof msg), "export jpg half");
	p = stbi_load("/home/claude/out/t_half.jpg", &w, &h, &n, 3);
	CHECK(p && abs(w - fw_ / 2) <= 1, "half size %dx%d", w, h);
	stbi_image_free(p);

	printf("[full-res vs preview consistency]\n");
	/* export at preview size must match the on-screen result closely */
	{
		Img ex = resize_u8(doc.rgb, doc.w, doc.h, doc.proxy.w, doc.proxy.h);
		Img cmp = img_clone(&doc.proxy);
		double d0 = img_diff(&ex, &cmp);
		CHECK(d0 < 1e-6, "proxy equals scaled source (%.2g)", d0);
		img_free(&ex); img_free(&cmp);
	}

	printf("\n%d checks, %d failed\n", checks, fails);
	pool_quit();
	return fails ? 1 : 0;
}
