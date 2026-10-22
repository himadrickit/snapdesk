# SnapDesk

A Snapseed-style, non-destructive photo editor for the desktop, written in plain C.
One program, same look and behaviour on Linux and Windows. The UI is drawn by a small software rasteriser;
SDL2 only provides the window and input.

## Build
**Easiest: one file, no extra headers**

    gcc -O2 -o snapdesk snapdesk_single.c $(sdl2-config --cflags --libs) -lm                      # Linux
    gcc -O2 -o snapdesk.exe snapdesk_single.c $(sdl2-config --cflags) -mwindows -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -lm   # MSYS2 MinGW64

`snapdesk.c` is the same program with stb_image / stb_image_write / stb_truetype / ui_font.h kept as separate headers (`make`).
Dependency: SDL2 only (`apt install libsdl2-dev` / `pacman -S mingw-w64-x86_64-SDL2`).
Cross-compiling: `make windows SDL2_MINGW=/path/to/SDL2-2.30.0/x86_64-w64-mingw32` (put SDL2.dll next to the exe).

Run: `./snapdesk [photo.jpg]`, or drop a file on the window. `--scale 2` doubles the UI on HiDPI screens. F1 = all shortcuts.

## Workflow
* Everything is on screen: tools on the left (press its letter), every slider of the current tool on the right.
* On the photo: **drag up/down to choose a parameter, left/right to change it** (Snapseed's gesture). Up/Down/Left/Right keys do the same, Shift = bigger steps.
* Sliders: drag, wheel, double-click resets, double-click the number to type a value.
* **Looks** strip: one-click styles. **Space** (hold) shows the original. Enter applies, Esc cancels.
* **Edit stack** (bottom right): every edit is a layer - click to re-edit it, eye to toggle, arrows to reorder, x to delete.
* Ctrl+Z / Ctrl+Y undo/redo. Ctrl+S exports (JPG/PNG/BMP/TGA, quality, 100/75/50/25%).

## Tools (33)
Adjust: Tune Image (+Auto), Details, Curves (RGB+per channel), White Balance (+picker, Auto)
Geometry: Crop (aspect presets), Rotate/Flip/Straighten, Perspective
Retouch: Selective (control points), Brush (dodge/burn, exposure, temperature, saturation, eraser), Healing, Portrait, Vignette, Lens Blur, Text, Frame
Filters: Black & White, Drama, Tonal Contrast, HDR Scape, Glamour Glow, Vintage, Retrolux, Grainy Film, Noir, Grunge
Effects: Bloom (soft highlight glow), Halation (red film glow, Z), Double Exposure (second photo, 9 blend modes, E), Dehaze, Color (8 hue bands), Color Grade (shadows/midtones/highlights, U), Denoise, Chromatic Aberration, Light Streaks (anamorphic), Gradient Filter (drag the line, wheel rotates it)

## How it works / honest limits
* The original is never touched. The preview runs the edit stack on a downscaled float copy (default 1600 px, top-bar button cycles 1024-full);
  Export re-runs the same stack on the full-resolution image, so results match. Zooming past the preview size magnifies the preview.
* Filters are my own algorithms inspired by Snapseed's tools, not pixel-identical copies. Denoise works on the preview-size copy, so judge it at 100% after export. Healing is patch-based (best for small spots).
  Portrait skin detection is colour-based, not face detection. Input formats: JPEG, PNG, BMP, TGA, GIF, PSD (no RAW/HEIC). EXIF orientation is honoured; metadata is not written back.
* Open/Export dialogs use the native Win32 dialog, or zenity/kdialog on Linux; without them a path box appears.
* Tested: headless self-test of all tools (`snapdesk --test outdir photo.jpg`), an event-injection harness (`make test`, 84 checks, ASan/UBSan clean),
  25 MP export (6 s on 2 cores, ~0.9 GB RAM). The Windows build cross-compiles but was not run on real Windows here.


## Also in this version
- **Expand** (J): grow the canvas by percentage or to 1:1 / 4:5 / 3:2 / 16:9 / 9:16; fill is Smart (mirror that melts into blur), Mirror, Blur or a solid colour. Smart is not true content-aware fill.
- **Frames**: Solid, Polaroid, Double and Inset styles.
- **Mask** on any non-geometry layer (bottom of the tool panel): paint where the effect shows, Erase, Invert, Reveal all, Clear. Red overlay while painting. The edit stack shows an M badge.
- **Save Look** (edit stack header): stores the visible layers as your own Look at the end of the Looks strip; right-click one to delete. Saved to the per-user pref folder (looks.txt). Geometry, brush, heal, selective, text and masked layers are not stored.
- **Revert** (undoable), **Info** (image details), **tool search** (Ctrl+F, Enter opens the first match) and **All / Refine / Fix / Style** tabs in the tool list.
- Drop an image on the window while Double Exposure is open to use it as the second photo.
