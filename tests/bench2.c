#define main snapdesk_main
#include "../snapdesk.c"
#undef main
int main(void) {
	SDL_SetMainReady(); SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
	init_luts(); fonts_init(); pool_init();
	doc.proxy_max = PROXY_DEFAULT;
	FW = 1440; FH = 900; fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
	doc_load("/home/claude/testimg/big.jpg");
	static const int tools[] = {T_TUNE, T_DETAILS, T_BLOOM, T_VINTAGE, T_LENS};
	for (unsigned k = 0; k < sizeof tools / sizeof *tools; k++) {
		select_tool(tools[k]);
		Layer *L = &stack[active];
		for (int lo = 0; lo < 2; lo++) {
			double best = 1e9;
			for (int r = 0; r < 4; r++) {
				L->p[0] += 1; touch(L);
				g_force_lo = lo; full_ms = 1e9;
				double t0 = now_ms(); pipeline_render(); best = MIN(best, now_ms() - t0);
			}
			printf("%-12s %s %7.1f ms\n", TD[tools[k]].name, lo ? "quick" : "full ", best);
		}
		edit_end(0);
	}
	return 0;
}
