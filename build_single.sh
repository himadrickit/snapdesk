#!/bin/sh
# assemble parts/*.c into snapdesk.c
cd "$(dirname "$0")"
cat parts/p1_core.c parts/p2a_layers.c parts/p2b_basic.c parts/p2c_retouch.c parts/p2d_filters.c parts/p2e_effects.c parts/p2g_new.c parts/p2f_dispatch.c parts/p3_pipeline.c parts/p4_raster.c parts/p5a_widgets.c parts/p5b_panels.c parts/p5c_canvas.c parts/p6_main.c > snapdesk.c
