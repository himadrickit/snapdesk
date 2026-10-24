#define main snapdesk_main
#include "../snapdesk.c"
#undef main
static void frame(void) {
	pipeline_tick();
	if (need_render) pipeline_render();
	ui_dry = 1; ui_frame(); ui_dry = 0; frame_end();
	if (need_render) pipeline_render();
	ui_frame();
}
static void move(int x, int y) { mdx += x - mx; mdy += y - my; mx = x; my = y; frame(); }
int main(int argc, char **argv) {
	SDL_SetMainReady(); SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
	init_luts(); fonts_init(); pool_init();
	doc.proxy_max = PROXY_DEFAULT;
	FW = 1918; FH = 1078; fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
	layout();
	doc_load(argc > 1 ? argv[1] : "/home/claude/testimg/big.jpg");
	int tool = argc > 2 ? atoi(argv[2]) : T_LENS;
	for (int pass = 0; pass < 2; pass++) {
		select_tool(tool);
		for (int i = 0; i < 3; i++) frame();
		Layer *L = &stack[active];
		float sx, sy; img2scr(L->ax * out.w, L->ay * out.h, &sx, &sy);
		move((int)sx, (int)sy);
		m_down[0] = 1; m_press[0] = 1; frame();
		double tot = 0, mxm = 0; int n = 40;
		for (int i = 1; i <= n; i++) {
			double t0 = now_ms();
			move((int)sx + i * 6, (int)sy + i * 2);
			double d = now_ms() - t0; tot += d; mxm = MAX(mxm, d);
		}
		m_down[0] = 0; m_rel[0] = 1; frame();
		printf("%s drag frame avg %.1f ms (max %.1f)  lo_level=%d full_ms=%.0f\n", TD[tool].name, tot / n, mxm, lo_level, full_ms);
		edit_end(0);
	}
	double t0 = now_ms(); for (int i = 0; i < 20; i++) { ui_frame(); frame_end(); } printf("idle ui_frame %.1f ms\n", (now_ms() - t0) / 20);
	return 0;
}
