#define main snapdesk_main
#include "../snapdesk.c"
#undef main
int main(int argc, char **argv) {
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
	init_luts(); fonts_init(); pool_init();
	doc.proxy_max = PROXY_DEFAULT;
	FW = 1440; FH = 900; fb = (uint32_t *)xcalloc((size_t)FW * FH, 4);
	if (!doc_load(argc > 1 ? argv[1] : "/home/claude/testimg/big.jpg")) return 1;
	printf("src %dx%d proxy %dx%d\n", doc.w, doc.h, doc.proxy.w, doc.proxy.h);
	for (int t = 0; t < T_COUNT; t++) {
		Layer L; test_setup_layer(&L, t);
		double best = 1e9;
		for (int r = 0; r < 3; r++) {
			Img im = img_clone(&doc.proxy);
			double t0 = now_ms();
			apply_layer(&L, &im);
			best = MIN(best, now_ms() - t0);
			img_free(&im);
		}
		printf("%-18s %8.1f ms\n", TD[t].name, best);
	}
	/* pipeline-side costs */
	double t0 = now_ms();
	for (int i = 0; i < 5; i++) { mips_from_img(&disp, &doc.proxy); }
	printf("mips_from_img      %8.1f ms\n", (now_ms() - t0) / 5);
	t0 = now_ms();
	for (int i = 0; i < 5; i++) compute_histogram(&disp);
	printf("histogram          %8.1f ms\n", (now_ms() - t0) / 5);
	t0 = now_ms();
	for (int i = 0; i < 5; i++) { Img c = img_clone(&doc.proxy); img_free(&c); }
	printf("img_clone          %8.1f ms\n", (now_ms() - t0) / 5);
	return 0;
}
