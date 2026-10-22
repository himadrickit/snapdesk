
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
