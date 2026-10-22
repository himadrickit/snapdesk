# SnapDesk - one C file, builds with gcc (Linux) or MinGW-w64 (Windows)
#
#   Linux  :  make                      (needs libsdl2-dev)
#   Windows:  make                      (MSYS2 MinGW64 shell, pacman -S mingw-w64-x86_64-SDL2)
#   Cross  :  make windows SDL2_MINGW=/path/to/SDL2-2.30.0/x86_64-w64-mingw32
CC      ?= gcc
CFLAGS  ?= -O3 -fno-math-errno -std=gnu99 -Wall -Wno-unused-function
UNAME_S := $(shell uname -s 2>/dev/null)

ifneq (,$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S)))
  EXE    = .exe
  SDLC   = $(shell sdl2-config --cflags)
  SDLL   = -mwindows -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -lm
else
  EXE    =
  SDLC   = $(shell sdl2-config --cflags)
  SDLL   = $(shell sdl2-config --libs) -lm
endif

snapdesk$(EXE): snapdesk.c ui_font.h stb_image.h stb_image_write.h stb_truetype.h
	$(CC) $(CFLAGS) -o $@ snapdesk.c $(SDLC) $(SDLL)

# cross compile from Linux
windows: snapdesk.c
	x86_64-w64-mingw32-gcc $(CFLAGS) -o snapdesk.exe snapdesk.c -I$(SDL2_MINGW)/include/SDL2 -L$(SDL2_MINGW)/lib \
	  -mwindows -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -lm -static-libgcc

test: snapdesk.c
	$(CC) -std=gnu99 -O1 -g -Wno-unused-function -fsanitize=address,undefined -o tests/harness tests/harness.c $(SDLC) $(SDLL)
	ASAN_OPTIONS=detect_leaks=0 ./tests/harness

clean:
	rm -f snapdesk snapdesk.exe tests/harness
.PHONY: windows test clean
