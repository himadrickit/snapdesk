
/* ================================================================== canvas & on-canvas tools */
static int cdrag = 0;           /* 0 none, 1 pan, 2 tool drag, 3 scrub */
static int cd_a = 0;            /* sub-kind / handle mask / point index */
static float cd_x0, cd_y0;      /* image-space anchor at drag start */
static float cd_r[4];           /* crop rect at drag start (px) */
static int scrub_axis = 0;
static float scrub_ax = 0, scrub_ay = 0, scrub_acc = 0;
static int cursor_kind = 0;     /* 0 arrow, 1 crosshair, 2 move, 3 ns, 4 ew, 5 nwse, 6 nesw, 7 hand */
static int hold_space = 0;

static int is_scrub_tool(int t) {
	switch (t) {
	case T_TUNE: case T_DETAILS: case T_WB: case T_ROTATE: case T_PERSP: case T_PORTRAIT: case T_FRAME:
	case T_BW: case T_DRAMA: case T_TONAL: case T_HDR: case T_GLOW: case T_VINTAGE: case T_RETRO: case T_GRAIN: case T_NOIR: case T_GRUNGE:
	case T_BLOOM: case T_DEHAZE: case T_SPLIT: case T_DENOISE: case T_CHROMA: case T_STREAK:
		return 1;
	}
	return 0;
}
/* indexes of non-choice parameters that scrubbing / arrow keys can visit */
static int scrub_list(const Layer *L, int *idx) {
	int n = 0;
	const ToolDef *d = &TD[L->tool];
	for (int i = 0; i < d->np; i++)
		if (!d->p[i].ch) idx[n++] = i;
	return n;
}
static void param_nudge(Layer *L, int pi, float dir, int fine_mode) {
	const PDef *d = &TD[L->tool].p[pi];
	if (d->ch) {
		int n = choice_max(d) + 1;
		L->p[pi] = (float)(((int)L->p[pi] + (dir > 0 ? 1 : n - 1)) % n);
	} else {
		float step = d->dec ? 0.5f : 1.f;
		float range = d->mx - d->mn;
		if (range > 150) step *= 1.f;
		step *= fine_mode ? 5.f : 1.f;
		L->p[pi] = CLAMP(L->p[pi] + dir * step, d->mn, d->mx);
		if (!d->dec) L->p[pi] = roundf(L->p[pi]);
	}
	touch(L);
	hud_show(L, pi);
}
static void param_select_step(Layer *L, int dir) {
	const ToolDef *d = &TD[L->tool];
	if (d->np == 0) return;
	sel_param = (sel_param + dir + d->np) % d->np;
	hud_show(L, sel_param);
}

static float brush_rad(const Layer *L) { return 0.004f + L->p[BR_SIZE] / 100.f * 0.12f; }
static float heal_rad(const Layer *L) { return 0.004f + L->p[HE_SIZE] / 100.f * 0.08f; }

static void dim_outside(float sx0, float sy0, float sx1, float sy1, float a) {
	int x0 = (int)sx0, y0 = (int)sy0, x1 = (int)sx1, y1 = (int)sy1;
	fill_rect(cvx, cvy, cvw, y0 - cvy, 0x000000, a);
	fill_rect(cvx, y1, cvw, cvy + cvh - y1, 0x000000, a);
	fill_rect(cvx, y0, x0 - cvx, y1 - y0, 0x000000, a);
	fill_rect(x1, y0, cvx + cvw - x1, y1 - y0, 0x000000, a);
}

static void crop_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h;
	float r[4] = {L->cr[0] * W, L->cr[1] * H, L->cr[2] * W, L->cr[3] * H};
	float sx0, sy0, sx1, sy1;
	img2scr(r[0], r[1], &sx0, &sy0);
	img2scr(r[2], r[3], &sx1, &sy1);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	const float T = 12.f;
	int mask = 0, inside = 0;
	if (mx >= sx0 - T && mx <= sx1 + T && my >= sy0 - T && my <= sy1 + T) {
		int wv = my >= sy0 - T && my <= sy1 + T, wh = mx >= sx0 - T && mx <= sx1 + T;
		if (fabsf(mx - sx0) <= T && wv) mask |= 1;
		if (fabsf(mx - sx1) <= T && wh == 1 && wv && !(mask & 1)) mask |= 2;
		else if (fabsf(mx - sx1) <= T && wv && (mask & 1) && fabsf(mx - sx1) < fabsf(mx - sx0)) mask = (mask & ~1) | 2;
		if (fabsf(my - sy0) <= T && wh) mask |= 4;
		if (fabsf(my - sy1) <= T && wh && !(mask & 4)) mask |= 8;
		inside = mx > sx0 && mx < sx1 && my > sy0 && my < sy1;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && (mask || inside)) {
		cdrag = 2;
		cd_a = mask ? mask : 0;
		cd_x0 = ix;
		cd_y0 = iy;
		memcpy(cd_r, r, sizeof r);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float R = crop_ratio(L), MINS = 24.f;
			float n[4];
			memcpy(n, cd_r, sizeof n);
			if (cd_a == 0) {
				float dx = ix - cd_x0, dy = iy - cd_y0, w = n[2] - n[0], h = n[3] - n[1];
				n[0] = CLAMP(n[0] + dx, 0.f, W - w);
				n[1] = CLAMP(n[1] + dy, 0.f, H - h);
				n[2] = n[0] + w;
				n[3] = n[1] + h;
			} else {
				if (cd_a & 1) n[0] = CLAMP(ix, 0.f, n[2] - MINS);
				if (cd_a & 2) n[2] = CLAMP(ix, n[0] + MINS, W);
				if (cd_a & 4) n[1] = CLAMP(iy, 0.f, n[3] - MINS);
				if (cd_a & 8) n[3] = CLAMP(iy, n[1] + MINS, H);
				if (R > 0.f) {
					float w = n[2] - n[0], h = n[3] - n[1];
					int hz = cd_a & 3, vt = cd_a & 12;
					if (hz && !vt) {
						float cy = (cd_r[1] + cd_r[3]) * 0.5f, maxh = 2.f * MIN(cy, H - cy);
						h = w / R;
						if (h > maxh) {
							h = maxh;
							w = h * R;
						}
						if (cd_a & 1) n[0] = n[2] - w;
						else n[2] = n[0] + w;
						n[1] = cy - h * 0.5f;
						n[3] = cy + h * 0.5f;
					} else if (vt && !hz) {
						float cx = (cd_r[0] + cd_r[2]) * 0.5f, maxw = 2.f * MIN(cx, W - cx);
						w = h * R;
						if (w > maxw) {
							w = maxw;
							h = w / R;
						}
						if (cd_a & 4) n[1] = n[3] - h;
						else n[3] = n[1] + h;
						n[0] = cx - w * 0.5f;
						n[2] = cx + w * 0.5f;
					} else {
						float ax = (cd_a & 1) ? cd_r[2] : cd_r[0], ay = (cd_a & 4) ? cd_r[3] : cd_r[1];
						if (w / h > R) h = w / R;
						else w = h * R;
						float maxw = (cd_a & 1) ? ax : W - ax, maxh = (cd_a & 4) ? ay : H - ay;
						float s = MIN(1.f, MIN(maxw / w, maxh / h));
						w *= s;
						h *= s;
						n[0] = (cd_a & 1) ? ax - w : ax;
						n[2] = (cd_a & 1) ? ax : ax + w;
						n[1] = (cd_a & 4) ? ay - h : ay;
						n[3] = (cd_a & 4) ? ay : ay + h;
					}
				}
			}
			float nn[4] = {n[0] / W, n[1] / H, n[2] / W, n[3] / H};
			if (memcmp(nn, L->cr, sizeof nn) != 0) {
				memcpy(L->cr, nn, sizeof nn);
				touch(L);
			}
		} else
			cdrag = 0;
	}
	img2scr(L->cr[0] * W, L->cr[1] * H, &sx0, &sy0);
	img2scr(L->cr[2] * W, L->cr[3] * H, &sx1, &sy1);
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	dim_outside(sx0, sy0, sx1, sy1, 0.58f);
	for (int i = 1; i < 3; i++) {
		fill_rect((int)(sx0 + (sx1 - sx0) * i / 3), (int)sy0, 1, (int)(sy1 - sy0), 0xffffff, 0.28f);
		fill_rect((int)sx0, (int)(sy0 + (sy1 - sy0) * i / 3), (int)(sx1 - sx0), 1, 0xffffff, 0.28f);
	}
	frame_rect((int)sx0, (int)sy0, (int)(sx1 - sx0), (int)(sy1 - sy0), 0xffffff, 0.9f);
	float bl = 18;
	float xs[2] = {sx0, sx1}, ys[2] = {sy0, sy1};
	for (int a = 0; a < 2; a++)
		for (int b = 0; b < 2; b++) {
			float dx = a ? -1.f : 1.f, dy = b ? -1.f : 1.f;
			line_aa(xs[a] - dx * 1.5f, ys[b] - dy * 1.5f, xs[a] + dx * bl, ys[b] - dy * 1.5f, 3.f, 0xffffff, 1);
			line_aa(xs[a] - dx * 1.5f, ys[b] - dy * 1.5f, xs[a] - dx * 1.5f, ys[b] + dy * bl, 3.f, 0xffffff, 1);
		}
	clip_set(c);
	/* cursor */
	if (in_canvas || cdrag == 2) {
		int m = cdrag == 2 ? cd_a : mask;
		if (cdrag == 2 && cd_a == 0) cursor_kind = 2;
		else if (m == 1 || m == 2) cursor_kind = 4;
		else if (m == 4 || m == 8) cursor_kind = 3;
		else if (m == 5 || m == 10) cursor_kind = 5;
		else if (m == 6 || m == 9) cursor_kind = 6;
		else if (inside) cursor_kind = 2;
	}
}

static void selective_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	int nearest_ = -1;
	for (int i = 0; i < L->nsel; i++) {
		float sx, sy;
		img2scr(L->sel[i].x * W, L->sel[i].y * H, &sx, &sy);
		if ((mx - sx) * (mx - sx) + (my - sy) * (my - sy) < 16.f * 16.f) nearest_ = i;
	}
	if (in_canvas && cdrag == 0 && !mod_ctrl) {
		if (m_press[2] && nearest_ >= 0) {
			for (int k = nearest_; k < L->nsel - 1; k++) L->sel[k] = L->sel[k + 1];
			L->nsel--;
			sel_pt = CLAMP(sel_pt, 0, MAX(0, L->nsel - 1));
			touch(L);
			m_press[2] = 0;
		} else if (m_press[0]) {
			if (nearest_ >= 0) {
				sel_pt = nearest_;
				cdrag = 2;
			} else if (L->nsel < MAXSEL && ix >= 0 && iy >= 0 && ix < W && iy < H) {
				SelPt *s = &L->sel[L->nsel];
				memset(s, 0, sizeof *s);
				s->x = ix / W;
				s->y = iy / H;
				s->rad = 0.15f;
				sel_pt = L->nsel++;
				cdrag = 2;
				touch(L);
			}
		}
	}
	if (cdrag == 2 && L->nsel > 0) {
		if (m_down[0]) {
			SelPt *s = &L->sel[CLAMP(sel_pt, 0, L->nsel - 1)];
			float nx = CL01(ix / W), ny = CL01(iy / H);
			if (nx != s->x || ny != s->y) {
				s->x = nx;
				s->y = ny;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas && wheel && !mod_ctrl && L->nsel > 0 && !wheel_used) {
		SelPt *s = &L->sel[CLAMP(sel_pt, 0, L->nsel - 1)];
		s->rad = CLAMP(s->rad * powf(1.1f, (float)wheel), 0.02f, 0.6f);
		wheel_used = 1;
		touch(L);
	}
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	for (int i = 0; i < L->nsel; i++) {
		float sx, sy;
		img2scr(L->sel[i].x * W, L->sel[i].y * H, &sx, &sy);
		int s = i == sel_pt;
		if (s) {
			ring(sx, sy, L->sel[i].rad * m * zoom, 1.6f, 0xffffff, 0.55f);
			ring(sx, sy, L->sel[i].rad * m * zoom + 1.5f, 1.f, 0x000000, 0.3f);
		}
		disc(sx, sy, s ? 13.f : 11.f, 0x000000, 0.45f);
		disc(sx, sy, s ? 11.5f : 9.5f, s ? C_ACC : 0xffffff, 1);
		char b[4];
		snprintf(b, sizeof b, "%d", i + 1);
		text_c(sx, sy, b, 12, 1, s ? 0x0b2a27 : 0x222222, 1);
	}
	clip_set(c);
	cursor_kind = nearest_ >= 0 ? 2 : 1;
}

static void brush_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = brush_rad(L);
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		L->p[BR_SIZE] = CLAMP(L->p[BR_SIZE] + wheel * (mod_shift ? 1.f : 3.f), 1.f, 100.f);
		wheel_used = 1;
		touch(L);
		L->touched = L->nstr > 0;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		Stroke *s = layer_add_stroke(L);
		int mode = (int)L->p[BR_MODE];
		s->mode = mode;
		s->amt = mode == 4 ? 0.f : L->p[BR_STR] / 100.f;
		s->rad = rad;
		s->hard = L->p[BR_HARD] / 100.f;
		s->flow = L->p[BR_FLOW] / 100.f * 0.6f;
		layer_add_pt(L, ix / W, iy / H);
		L->str[L->nstr - 1].n = 1;
		cdrag = 2;
		cd_x0 = mx;
		cd_y0 = my;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = mx - cd_x0, dy = my - cd_y0;
			if (dx * dx + dy * dy >= 1.5f * 1.5f && L->nstr > 0) {
				layer_add_pt(L, ix / W, iy / H);
				L->str[L->nstr - 1].n++;
				cd_x0 = (float)mx;
				cd_y0 = (float)my;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		float hard = L->p[BR_HARD] / 100.f;
		if (hard < 0.95f) ring((float)mx, (float)my, r * hard, 1.f, 0xffffff, 0.4f);
		clip_set(c);
		cursor_kind = 8;
	}
}

static void heal_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = heal_rad(L);
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		L->p[HE_SIZE] = CLAMP(L->p[HE_SIZE] + wheel * (mod_shift ? 1.f : 3.f), 3.f, 100.f);
		wheel_used = 1;
		L->touched = L->nspots > 0;
		layer_bump(L);
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && ix >= 0 && iy >= 0 && ix < W && iy < H) {
		layer_add_spot(L, ix / W, iy / H, rad);
		cdrag = 2;
		cd_x0 = ix;
		cd_y0 = iy;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = ix - cd_x0, dy = iy - cd_y0, s = rad * m * 1.3f;
			if (dx * dx + dy * dy >= s * s && ix >= 0 && iy >= 0 && ix < W && iy < H) {
				layer_add_spot(L, ix / W, iy / H, rad);
				cd_x0 = ix;
				cd_y0 = iy;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		line_aa((float)mx - 4, (float)my, (float)mx + 4, (float)my, 1.2f, 0xffffff, 0.8f);
		line_aa((float)mx, (float)my - 4, (float)mx, (float)my + 4, 1.2f, 0xffffff, 0.8f);
		clip_set(c);
		cursor_kind = 8;
	}
}

/* vignette / lens / text: drag the centre */
static int ctr_drag = 0; /* a centre / position drag is in progress (quick preview applies) */
static void center_tool(Layer *L, int in_canvas) {
	ctr_drag = cdrag == 2;
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		cdrag = 2;
		cd_x0 = (L->cx - ix / W);
		cd_y0 = (L->cy - iy / H);
		if (L->tool != T_TEXT) cd_x0 = cd_y0 = 0;
		L->cx = CL01(ix / W + cd_x0);
		L->cy = CL01(iy / H + cd_y0);
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float nx = CL01(ix / W + cd_x0), ny = CL01(iy / H + cd_y0);
			if (nx != L->cx || ny != L->cy) {
				L->cx = nx;
				L->cy = ny;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas && wheel && !mod_ctrl && !wheel_used && L->tool == T_VIGNETTE) {
		L->p[VG_SIZE] = CLAMP(L->p[VG_SIZE] + wheel * 3.f, 0.f, 100.f);
		wheel_used = 1;
		touch(L);
	}
	if (in_canvas && wheel && !mod_ctrl && !wheel_used && L->tool == T_GRAD) {
		float a = L->p[GD_ANG] + wheel * 5.f;
		if (a > 180.f) a -= 360.f;
		if (a < -180.f) a += 360.f;
		L->p[GD_ANG] = a;
		wheel_used = 1;
		touch(L);
	}
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	float sx, sy;
	img2scr(L->cx * W, L->cy * H, &sx, &sy);
	if (L->tool == T_GRAD) {
		float th = L->p[GD_ANG] * PI_F / 180.f, tx = cosf(th), ty = -sinf(th), nx = sinf(th), ny = cosf(th), L2 = 4000.f;
		float f = (0.004f + L->p[GD_FEA] / 100.f * 0.5f) * m * zoom;
		line_aa(sx - tx * L2, sy - ty * L2, sx + tx * L2, sy + ty * L2, 1.8f, 0xffffff, 0.9f);
		for (int s = -1; s <= 1; s += 2) line_aa(sx + nx * f * s - tx * L2, sy + ny * f * s - ty * L2, sx + nx * f * s + tx * L2, sy + ny * f * s + ty * L2, 1.f, 0xffffff, 0.35f);
		/* small arrow toward the affected side */
		float ax = sx - nx * 26, ay = sy - ny * 26;
		tri(ax - nx * 8, ay - ny * 8, ax + tx * 7, ay + ty * 7, ax - tx * 7, ay - ty * 7, C_ACC, 1);
	}
	if (L->tool == T_VIGNETTE) {
		float inner = L->p[VG_SIZE] / 100.f * 0.85f;
		ellipse_ring(sx, sy, inner * W * 0.5f / 0.7071f * zoom, inner * H * 0.5f / 0.7071f * zoom, 0, 1.4f, 0xffffff, 0.8f, 1);
	} else if (L->tool == T_TEXT) {
		float px = MAX(6.f, L->p[TX_SIZE] / 100.f * 0.34f * m) * zoom;
		float tw = text_width(L->text, 14, 1) / 14.f * px * 1.02f, th = px * 1.25f, a = L->p[TX_ROT] * PI_F / 180.f;
		if (L->text[0]) ellipse_ring(sx, sy, tw * 0.5f * 1.02f, th * 0.5f, a, 1.f, 0xffffff, 0.0f, 0);
		rrect_line(sx - 4, sy - 4, 8, 8, 2, 1.4f, 0xffffff, 0.9f);
	}
	disc(sx, sy, 5.f, 0x000000, 0.4f);
	disc(sx, sy, 4.f, C_ACC, 1);
	clip_set(c);
	if (in_canvas) cursor_kind = 2;
}

static void grid_overlay(int n) {
	if (!out.w) return;
	float x0, y0, x1, y1;
	img2scr(0, 0, &x0, &y0);
	img2scr((float)out.w, (float)out.h, &x1, &y1);
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	for (int i = 1; i < n; i++) {
		fill_rect((int)(x0 + (x1 - x0) * i / n), (int)y0, 1, (int)(y1 - y0), 0xffffff, 0.22f);
		fill_rect((int)x0, (int)(y0 + (y1 - y0) * i / n), (int)(x1 - x0), 1, 0xffffff, 0.22f);
	}
	clip_set(c);
}

static void scrub_tool(Layer *L, int in_canvas) {
	int idx[MAXP];
	int n = scrub_list(L, idx);
	if (wb_pick && L->tool == T_WB) {
		cursor_kind = 1;
		if (in_canvas && m_press[0] && pf.p) {
			float ix, iy;
			scr2img((float)mx, (float)my, &ix, &iy);
			double r = 0, g = 0, b = 0;
			int cnt = 0;
			for (int dy = -2; dy <= 2; dy++)
				for (int dx = -2; dx <= 2; dx++) {
					int xx = CLAMP((int)ix + dx, 0, pf.w - 1), yy = CLAMP((int)iy + dy, 0, pf.h - 1);
					const float *p = pf.p + ((size_t)yy * pf.w + xx) * 3;
					r += p[0];
					g += p[1];
					b += p[2];
					cnt++;
				}
			float t, ti;
			wb_from_sample((float)(r / cnt), (float)(g / cnt), (float)(b / cnt), &t, &ti);
			L->p[WB_TEMP] = roundf(t);
			L->p[WB_TINT] = roundf(ti);
			touch(L);
			wb_pick = 0;
		}
		return;
	}
	if (L->tool == T_ROTATE || L->tool == T_PERSP) grid_overlay(8);
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && n > 0) {
		cdrag = 3;
		scrub_axis = 0;
		scrub_ax = scrub_ay = scrub_acc = 0;
	}
	if (cdrag == 3) {
		if (m_down[0]) {
			scrub_ax += mdx;
			scrub_ay += mdy;
			if (!scrub_axis && fabsf(scrub_ax) + fabsf(scrub_ay) > 7.f) scrub_axis = fabsf(scrub_ax) >= fabsf(scrub_ay) ? 1 : 2;
			if (scrub_axis == 2) {
				const float STEP = 28.f;
				while (scrub_ay >= STEP || scrub_ay <= -STEP) {
					int dir = scrub_ay > 0 ? 1 : -1;
					scrub_ay -= dir * STEP;
					int cur = 0;
					for (int i = 0; i < n; i++)
						if (idx[i] == sel_param) cur = i;
					cur = (cur + dir + n) % n;
					sel_param = idx[cur];
					hud_show(L, sel_param);
				}
			} else if (scrub_axis == 1 && mdx != 0) {
				int pi = sel_param, ok = 0;
				for (int i = 0; i < n; i++)
					if (idx[i] == pi) ok = 1;
				if (!ok) pi = sel_param = idx[0];
				const PDef *d = &TD[L->tool].p[pi];
				scrub_acc += mdx * (d->mx - d->mn) / 380.f * (mod_shift ? 0.2f : 1.f);
				float step = d->dec ? 0.1f : 1.f, whole = truncf(scrub_acc / step) * step;
				if (whole != 0.f) {
					scrub_acc -= whole;
					L->p[pi] = CLAMP(L->p[pi] + whole, d->mn, d->mx);
					L->p[pi] = d->dec ? roundf(L->p[pi] * 10.f) / 10.f : roundf(L->p[pi]);
					touch(L);
				}
				hud_show(L, pi);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 3) cursor_kind = scrub_axis == 2 ? 3 : (n > 0 ? 4 : 0);
}


/* ---- movable / resizable / rotatable oval (Lens Blur focus, or the Area of any effect) ----
 * drag inside = move, edge dots = resize (Shift = circle), top dot = rotate (Shift = 15 degree steps),
 * wheel = pinch in/out, Shift+wheel = rotate, two-finger pinch/twist on touch. Returns 1 if it used the mouse. */
enum { AG_NONE, AG_MOVE, AG_RX, AG_RY, AG_ROT };
static int ag_mode = AG_NONE, ag_hot = AG_NONE;
static float ag_gx, ag_gy;
static void area_clamp(Layer *L) {
	L->arx = CLAMP(L->arx, 0.02f, 1.6f);
	L->ary = CLAMP(L->ary, 0.02f, 1.6f);
	L->ax = CLAMP(L->ax, -0.2f, 1.2f);
	L->ay = CLAMP(L->ay, -0.2f, 1.2f);
	while (L->arot > PI_F) L->arot -= 2.f * PI_F;
	while (L->arot < -PI_F) L->arot += 2.f * PI_F;
}
static int area_tool(Layer *L, int in_canvas, int grab_anywhere) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	int linear = L->tool == T_LENS && (int)L->p[LN_SHAPE] == 1;
	float k = m * zoom, ca = cosf(L->arot), sa = sinf(L->arot);
	float sx, sy;
	img2scr(L->ax * W, L->ay * H, &sx, &sy);
	float exx = ca, exy = sa, eyx = -sa, eyy = ca; /* screen-space axes */
	float rx = L->arx * k, ry = L->ary * k;
	float hx[4][2] = {{sx + exx * rx, sy + exy * rx}, {sx - exx * rx, sy - exy * rx}, {sx + eyx * ry, sy + eyy * ry}, {sx - eyx * ry, sy - eyy * ry}};
	float stem = ry + 30.f, rot_x = sx - eyx * stem, rot_y = sy - eyy * stem;
	int used = 0;
	/* hit-testing */
	int hit = AG_NONE;
	if (in_canvas || cdrag == 2) {
		float R2 = 11.f * 11.f;
		float dxm = (float)mx - sx, dym = (float)my - sy;
		if ((mx - rot_x) * (mx - rot_x) + (my - rot_y) * (my - rot_y) < R2) hit = AG_ROT;
		else if (!linear && ((mx - hx[0][0]) * (mx - hx[0][0]) + (my - hx[0][1]) * (my - hx[0][1]) < R2 || (mx - hx[1][0]) * (mx - hx[1][0]) + (my - hx[1][1]) * (my - hx[1][1]) < R2)) hit = AG_RX;
		else if ((mx - hx[2][0]) * (mx - hx[2][0]) + (my - hx[2][1]) * (my - hx[2][1]) < R2 || (mx - hx[3][0]) * (mx - hx[3][0]) + (my - hx[3][1]) * (my - hx[3][1]) < R2) hit = AG_RY;
		else {
			float u = (dxm * exx + dym * exy) / MAX(rx, 1.f), v = (dxm * eyx + dym * eyy) / MAX(ry, 1.f);
			if (grab_anywhere || (!linear && u * u + v * v <= 1.f) || (linear && fabsf(v) <= 1.f)) hit = AG_MOVE;
		}
	}
	if (cdrag == 0) ag_hot = hit;
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl && hit != AG_NONE) {
		ag_mode = hit;
		cdrag = 2;
		float ix, iy;
		scr2img((float)mx, (float)my, &ix, &iy);
		ag_gx = L->ax - ix / W;
		ag_gy = L->ay - iy / H;
	}
	if (cdrag == 2 && ag_mode != AG_NONE) {
		used = 1;
		if (m_down[0]) {
			float ix, iy;
			scr2img((float)mx, (float)my, &ix, &iy);
			float dx = ix - L->ax * W, dy = iy - L->ay * H;
			switch (ag_mode) {
			case AG_MOVE:
				L->ax = ix / W + ag_gx;
				L->ay = iy / H + ag_gy;
				break;
			case AG_RX: {
				float r = fabsf(dx * ca + dy * sa) / m;
				L->arx = r;
				if (mod_shift) L->ary = r;
			} break;
			case AG_RY: {
				float r = fabsf(-dx * sa + dy * ca) / m;
				L->ary = r;
				if (mod_shift && !linear) L->arx = r;
			} break;
			case AG_ROT: {
				float a = atan2f(dy, dx) + PI_F * 0.5f;
				if (mod_shift) a = roundf(a / (PI_F / 12.f)) * (PI_F / 12.f);
				L->arot = a;
			} break;
			}
			area_clamp(L);
			touch(L);
		} else {
			cdrag = 0;
			ag_mode = AG_NONE;
		}
	}
	/* wheel = pinch, Shift+wheel = twist; touch gestures do the same */
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		if (mod_shift) L->arot += wheel * 5.f * PI_F / 180.f;
		else {
			float f = powf(1.07f, (float)wheel);
			L->arx *= f;
			L->ary *= f;
		}
		area_clamp(L);
		wheel_used = 1;
		touch(L);
	}
	if (in_canvas && (gest_dist != 0.f || gest_theta != 0.f)) {
		float f = 1.f + gest_dist * 3.f;
		L->arx *= f;
		L->ary *= f;
		L->arot += gest_theta;
		area_clamp(L);
		touch(L);
	}
	/* drawing */
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	img2scr(L->ax * W, L->ay * H, &sx, &sy);
	rx = L->arx * k;
	ry = L->ary * k;
	ca = cosf(L->arot);
	sa = sinf(L->arot);
	exx = ca; exy = sa; eyx = -sa; eyy = ca;
	float fea = L->tool == T_LENS ? (0.12f + L->p[LN_TRANS] / 100.f * 1.1f) * 0.5f : (0.03f + 0.9f * L->afea / 100.f);
	hx[0][0] = sx + exx * rx; hx[0][1] = sy + exy * rx; hx[1][0] = sx - exx * rx; hx[1][1] = sy - exy * rx;
	hx[2][0] = sx + eyx * ry; hx[2][1] = sy + eyy * ry; hx[3][0] = sx - eyx * ry; hx[3][1] = sy - eyy * ry;
	rot_x = sx - eyx * (ry + 30.f);
	rot_y = sy - eyy * (ry + 30.f);
	if (!linear) {
		ellipse_ring(sx, sy, rx, ry, L->arot, 1.6f, 0xffffff, 0.9f, 0);
		float fo = 1.f + MIN(fea, 1.5f) * 0.3f, fi = MAX(0.08f, 1.f - MIN(fea, 0.92f));
		ellipse_ring(sx, sy, rx * fo, ry * fo, L->arot, 1.f, 0xffffff, 0.4f, 1);
		ellipse_ring(sx, sy, rx * fi, ry * fi, L->arot, 1.f, 0xffffff, 0.3f, 1);
	} else {
		float Lh = 3000.f;
		for (int s = -1; s <= 1; s += 2) line_aa(sx + eyx * ry * s - exx * Lh, sy + eyy * ry * s - exy * Lh, sx + eyx * ry * s + exx * Lh, sy + eyy * ry * s + exy * Lh, 1.6f, 0xffffff, 0.85f);
		for (int s = -1; s <= 1; s += 2) {
			float o = ry * (1.f + fea * 0.6f) * s;
			line_aa(sx + eyx * o - exx * Lh, sy + eyy * o - exy * Lh, sx + eyx * o + exx * Lh, sy + eyy * o + exy * Lh, 1.f, 0xffffff, 0.3f);
		}
	}
	line_aa(hx[3][0], hx[3][1], rot_x, rot_y, 1.2f, 0xffffff, 0.6f);
	for (int i = 0; i < 4; i++) {
		if (linear && i < 2) continue;
		int hot = (ag_hot == AG_RX && i < 2) || (ag_hot == AG_RY && i >= 2);
		disc(hx[i][0], hx[i][1], hot ? 8.f : 6.5f, 0x000000, 0.45f);
		disc(hx[i][0], hx[i][1], hot ? 6.5f : 5.f, hot ? C_ACC : 0xffffff, 1);
	}
	disc(rot_x, rot_y, ag_hot == AG_ROT ? 8.f : 6.5f, 0x000000, 0.45f);
	disc(rot_x, rot_y, ag_hot == AG_ROT ? 6.5f : 5.f, C_ACC, 1);
	ring(rot_x, rot_y, 8.5f, 1.f, 0xffffff, 0.6f);
	disc(sx, sy, 5.f, 0x000000, 0.4f);
	disc(sx, sy, 4.f, C_ACC, 1);
	clip_set(c);
	if (in_canvas || cdrag == 2) {
		int mode = cdrag == 2 ? ag_mode : ag_hot;
		cursor_kind = mode == AG_ROT ? 7 : (mode == AG_RX ? 4 : (mode == AG_RY ? 3 : (mode == AG_MOVE ? 2 : 0)));
		if (cursor_kind == 0 && !used) return 0;
	}
	return used || ag_hot != AG_NONE || cdrag == 2;
}
static int mask_for = -1;
static void mask_tool(Layer *L, int in_canvas) {
	float W = (float)out.w, H = (float)out.h, m = MIN(W, H);
	float ix, iy;
	scr2img((float)mx, (float)my, &ix, &iy);
	float rad = 0.004f + mask_size / 100.f * 0.12f;
	if (in_canvas && wheel && !mod_ctrl && !wheel_used) {
		mask_size = CLAMP(mask_size + wheel * (mod_shift ? 1.f : 3.f), 1.f, 100.f);
		wheel_used = 1;
	}
	if (in_canvas && cdrag == 0 && m_press[0] && !mod_ctrl) {
		Stroke *s = layer_add_mstroke(L);
		s->mode = mask_erase ? 4 : 0;
		s->amt = 1.f;
		s->rad = rad;
		s->hard = mask_hard / 100.f;
		s->flow = 0.6f;
		layer_add_mpt(L, ix / W, iy / H);
		s->n = 1;
		cdrag = 2;
		cd_x0 = (float)mx;
		cd_y0 = (float)my;
		touch(L);
	}
	if (cdrag == 2) {
		if (m_down[0]) {
			float dx = mx - cd_x0, dy = my - cd_y0;
			if (dx * dx + dy * dy >= 2.25f && L->nmstr > 0) {
				layer_add_mpt(L, ix / W, iy / H);
				L->mstr[L->nmstr - 1].n++;
				cd_x0 = (float)mx;
				cd_y0 = (float)my;
				touch(L);
			}
		} else
			cdrag = 0;
	}
	if (in_canvas || cdrag == 2) {
		Clip c = clip_push(cvx, cvy, cvw, cvh);
		float r = rad * m * zoom;
		ring((float)mx, (float)my, r, 1.4f, mask_erase ? 0xffb0a0 : 0xffffff, 0.9f);
		ring((float)mx, (float)my, r + 1.2f, 1.f, 0x000000, 0.35f);
		clip_set(c);
		cursor_kind = 8;
	}
}
/* red tint over the areas where a masked effect shows */
static void mask_overlay(Layer *L) {
	BrushRT *rt = L->mrt;
	if (!rt || rt->w != out.w || rt->h != out.h) return;
	const float *mp = rt->map[0];
	Clip c = clip_push(cvx, cvy, cvw, cvh);
	int step = zoom < 1.f ? 1 : 1;
	for (int sy = cvy; sy < cvy + cvh; sy += step)
		for (int sx = cvx; sx < cvx + cvw; sx += step) {
			float ix, iy;
			scr2img(sx + 0.5f, sy + 0.5f, &ix, &iy);
			if (ix < 0 || iy < 0 || ix >= out.w || iy >= out.h) continue;
			float a = mp ? CL01(mp[(size_t)(int)iy * out.w + (int)ix]) : 0.f;
			if (L->mask_inv) a = 1.f - a;
			if (a > 0.01f) put_px(sx, sy, 0xe03a3a, a256(a * 0.5f));
		}
	clip_set(c);
}
static void canvas_tool(int in_canvas) {
	if (active < 0 || active >= nlayers) return;
	Layer *L = &stack[active];
	if (active != mask_for) {
		mask_for = active;
		mask_edit = 0;
	}
	if (mask_edit && L->mask_on && maskable(L->tool)) {
		mask_tool(L, in_canvas);
		mask_overlay(L);
		return;
	}
	if (mask_edit) mask_edit = 0;
	switch (L->tool) {
	case T_CROP: crop_tool(L, in_canvas); break;
	case T_SELECTIVE: selective_tool(L, in_canvas); break;
	case T_BRUSH: brush_tool(L, in_canvas); break;
	case T_HEAL: heal_tool(L, in_canvas); break;
	case T_LENS: area_tool(L, in_canvas, 1); break;
	case T_VIGNETTE: case T_TEXT: case T_GRAD: center_tool(L, in_canvas); break;
	case T_CURVES: case T_MIXER: break;
	default:
		if (L->area_on && maskable(L->tool) && area_tool(L, in_canvas, 0)) break;
		scrub_tool(L, in_canvas);
		break;
	}
}

static void draw_pill(float cx, float cy, const char *a, const char *b) {
	float wa = text_width(a, 14, 0), wb = text_width(b, 20, 1);
	float w = wa + wb + 44;
	rrect(cx - w * 0.5f, cy - 20, w, 40, 20, 0x101214, 0.82f);
	text(cx - w * 0.5f + 20, cy, a, 14, 0, C_DIM, 1);
	text(cx - w * 0.5f + 24 + wa, cy, b, 20, 1, C_ACC, 1);
}

static void draw_canvas(void) {
	Clip cc = clip_push(cvx, cvy, cvw, cvh);
	fill_rect(cvx, cvy, cvw, cvh, 0x101113, 1);
	if (!doc.loaded) {
		float cx = cvx + cvw * 0.5f, cy = cvy + cvh * 0.5f;
		rrect_line(cx - 230, cy - 120, 460, 240, 18, 2, C_LINE, 1);
		text_c(cx, cy - 52, "SnapDesk", 30, 1, C_TEXT, 1);
		text_c(cx, cy - 14, "Drop a photo here, or press Ctrl+O", 14, 0, C_DIM, 1);
		if (button(8001, (int)cx - 70, (int)cy + 20, 140, 38, "Open photo", BS_ACCENT, 0)) do_open();
		text_c(cx, cy + 84, "F1 shows every shortcut", 12, 0, C_FAINT, 1);
		clip_set(cc);
		return;
	}
	if (out.w != last_ow || out.h != last_oh) {
		view_fit = 1;
		last_ow = out.w;
		last_oh = out.h;
	}
	if (view_fit) view_do_fit();
	int in_canvas = mouse_in(cvx, cvy, cvw, cvh) && (cdrag == 0 || cdrag >= 2);
	int space_cmp = compare_hold || hold_space;
	/* pan / zoom */
	int rc_reserved = 0; /* right-click on a selective point deletes it instead of panning */
	if (active >= 0 && stack[active].tool == T_SELECTIVE)
		for (int i = 0; i < stack[active].nsel; i++) {
			float sx, sy;
			img2scr(stack[active].sel[i].x * out.w, stack[active].sel[i].y * out.h, &sx, &sy);
			if ((mx - sx) * (mx - sx) + (my - sy) * (my - sy) < 16.f * 16.f) rc_reserved = 1;
		}
	if (in_canvas && cdrag == 0 && (m_press[1] || (m_press[2] && !rc_reserved) || (m_press[0] && mod_ctrl))) {
		cdrag = 1;
		view_fit = 0;
	}
	if (cdrag == 1) {
		if (m_down[0] || m_down[1] || m_down[2]) {
			vcx -= mdx / zoom;
			vcy -= mdy / zoom;
			view_clamp();
		} else
			cdrag = 0;
		cursor_kind = 7;
	}
	if (in_canvas && wheel && !wheel_used) {
		int size_tool = active >= 0 && (stack[active].tool == T_BRUSH || stack[active].tool == T_HEAL || stack[active].tool == T_SELECTIVE || stack[active].tool == T_LENS || stack[active].tool == T_GRAD || stack[active].tool == T_VIGNETTE || (stack[active].area_on && !mask_edit));
		if (!size_tool || mod_ctrl) {
			view_zoom_at(powf(1.18f, (float)wheel), (float)mx, (float)my);
			wheel_used = 1;
		}
	}
	/* image */
	const Mips *m = &disp;
	float z = zoom, cx = vcx, cy = vcy;
	if (!space_cmp && lo_shown && disp.w[0] != out.w) { /* quick preview: small image stretched over the full-size view */
		z = zoom * out.w / disp.w[0];
		cx = vcx * disp.w[0] / out.w;
		cy = vcy * disp.h[0] / out.h;
	}
	if (space_cmp && odisp.n) {
		m = &odisp;
		float ar = (float)odisp.w[0] / odisp.h[0], br = (float)out.w / out.h;
		if (fabsf(ar / br - 1.f) < 0.01f) {
			z = zoom * out.w / odisp.w[0];
			cx = vcx * odisp.w[0] / out.w;
			cy = vcy * odisp.h[0] / out.h;
		} else {
			z = MIN((cvw - 40) / (float)odisp.w[0], (cvh - 40) / (float)odisp.h[0]);
			cx = odisp.w[0] * 0.5f;
			cy = odisp.h[0] * 0.5f;
		}
	}
	{
		float x0, y0, x1, y1;
		float iw = (float)m->w[0], ih = (float)m->h[0];
		x0 = cvx + cvw * 0.5f + (0 - cx) * z;
		y0 = cvy + cvh * 0.5f + (0 - cy) * z;
		x1 = cvx + cvw * 0.5f + (iw - cx) * z;
		y1 = cvy + cvh * 0.5f + (ih - cy) * z;
		if (x1 - x0 < cvw + 30 && y1 - y0 < cvh + 30) soft_shadow((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), 14, 0.55f);
	}
	draw_mips(m, cvx, cvy, cvw, cvh, cx, cy, z);
	if (space_cmp) {
		rrect(cvx + 14.f, cvy + 14.f, 92, 26, 13, 0x101214, 0.8f);
		text_c(cvx + 60.f, cvy + 27.f, "ORIGINAL", 12, 1, C_TEXT, 1);
	}
	/* tool */
	cursor_kind = 0;
	if (!space_cmp) canvas_tool(in_canvas);
	if (in_canvas && cursor_kind == 0 && doc.loaded) cursor_kind = 0;
	/* double-click toggles fit / 100% */
	if (in_canvas && m_dbl && cdrag == 0 && (active < 0 || is_scrub_tool(stack[active].tool))) {
		if (zoom < 0.99f / full_scale() * 0.98f) {
			float ix, iy;
			scr2img((float)mx, (float)my, &ix, &iy);
			float nz = 1.f / full_scale();
			zoom = nz;
			vcx = ix - (mx - cvx - cvw * 0.5f) / zoom;
			vcy = iy - (my - cvy - cvh * 0.5f) / zoom;
			view_fit = 0;
		} else
			view_fit = 1;
	}
	/* HUD pill */
	uint32_t now = SDL_GetTicks();
	if (hud_until > now && !space_cmp) {
		float a = (float)(hud_until - now) / 1100.f;
		(void)a;
		draw_pill(cvx + cvw * 0.5f, cvy + 38.f, hud_name, hud_text);
	}
	/* toast */
	if (toast_until > now) {
		float tw = text_width(toast_msg, 13, 0) + 32;
		float a = CL01((toast_until - now) / 500.f);
		rrect(cvx + 16.f, cvy + cvh - 46.f, tw, 30, 15, 0x101214, 0.9f * a);
		text(cvx + 32.f, cvy + cvh - 31.f, toast_msg, 13, 0, C_TEXT, a);
	}
	/* zoom readout */
	{
		char b[32];
		snprintf(b, sizeof b, "%d%%", (int)(zoom * full_scale() * 100.f + 0.5f));
		text_r((float)(cvx + cvw - 14), (float)(cvy + cvh - 18), b, 12, 1, C_DIM, 1);
		if (last_render_ms > 90.0) {
			snprintf(b, sizeof b, "%d ms", (int)last_render_ms);
			text_r((float)(cvx + cvw - 14), (float)(cvy + cvh - 36), b, 11, 0, C_FAINT, 1);
		}
	}
	clip_set(cc);
}

/* a value is being dragged (slider or on-canvas scrub): render on the half-size proxy meanwhile */
static int g_force_lo = 0; /* benchmark hook */
static int interacting_now(void) { return g_force_lo || (m_down[0] && (act_id != 0 || cdrag == 3 || (cdrag == 2 && (ag_mode != AG_NONE || ctr_drag)))); }
static void pipeline_tick(void) {
	if (lo_shown && !interacting_now()) need_render = 1; /* drag ended: do the full-quality render */
}
