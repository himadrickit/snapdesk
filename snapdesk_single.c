/*
 * snapdesk.c - SnapDesk: a Snapseed-style, non-destructive photo editor for the desktop.
 *
 * One C99 source file. Needs SDL2 (window/input only), stb_image, stb_image_write,
 * stb_truetype (all vendored as headers next to this file) and an embedded font (ui_font.h).
 *
 *   Linux:   gcc -O2 -o snapdesk snapdesk.c $(sdl2-config --cflags --libs) -lm
 *   Windows: gcc -O2 -o snapdesk.exe snapdesk.c -I<SDL2>/include/SDL2 -L<SDL2>/lib \
 *                -mwindows -lSDL2 -lcomdlg32 -lm        (MinGW / MSYS2)
 *
 * Architecture
 *   - The photo is never modified. Edits are a stack of "layers" (tool + parameters).
 *   - Interactive preview runs the stack on a downscaled float "proxy"; Export runs the very
 *     same stack on the full-resolution image. All spatial parameters are resolution independent.
 *   - Everything (UI included) is drawn by a tiny software rasteriser into one framebuffer that
 *     SDL only presents, so the program looks and behaves identically on every OS.
 */
#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define NOMINMAX
#endif
#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_FAILURE_USERMSG
#define STBI_NO_PIC
/* stb_image - v2.30 - public domain image loader - http://nothings.org/stb
                                  no warranty implied; use at your own risk

   Do this:
      #define STB_IMAGE_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.

   // i.e. it should look like this:
   #include ...
   #include ...
   #include ...
   #define STB_IMAGE_IMPLEMENTATION
   #include "stb_image.h"

   You can #define STBI_ASSERT(x) before the #include to avoid using assert.h.
   And #define STBI_MALLOC, STBI_REALLOC, and STBI_FREE to avoid using malloc,realloc,free


   QUICK NOTES:
      Primarily of interest to game developers and other people who can
          avoid problematic images and only need the trivial interface

      JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
      PNG 1/2/4/8/16-bit-per-channel

      TGA (not sure what subset, if a subset)
      BMP non-1bpp, non-RLE
      PSD (composited view only, no extra channels, 8/16 bit-per-channel)

      GIF (*comp always reports as 4-channel)
      HDR (radiance rgbE format)
      PIC (Softimage PIC)
      PNM (PPM and PGM binary only)

      Animated GIF still needs a proper API, but here's one way to do it:
          http://gist.github.com/urraka/685d9a6340b26b830d49

      - decode from memory or through FILE (define STBI_NO_STDIO to remove code)
      - decode from arbitrary I/O callbacks
      - SIMD acceleration on x86/x64 (SSE2) and ARM (NEON)

   Full documentation under "DOCUMENTATION" below.


LICENSE

  See end of file for license information.

RECENT REVISION HISTORY:

      2.30  (2024-05-31) avoid erroneous gcc warning
      2.29  (2023-05-xx) optimizations
      2.28  (2023-01-29) many error fixes, security errors, just tons of stuff
      2.27  (2021-07-11) document stbi_info better, 16-bit PNM support, bug fixes
      2.26  (2020-07-13) many minor fixes
      2.25  (2020-02-02) fix warnings
      2.24  (2020-02-02) fix warnings; thread-local failure_reason and flip_vertically
      2.23  (2019-08-11) fix clang static analysis warning
      2.22  (2019-03-04) gif fixes, fix warnings
      2.21  (2019-02-25) fix typo in comment
      2.20  (2019-02-07) support utf8 filenames in Windows; fix warnings and platform ifdefs
      2.19  (2018-02-11) fix warning
      2.18  (2018-01-30) fix warnings
      2.17  (2018-01-29) bugfix, 1-bit BMP, 16-bitness query, fix warnings
      2.16  (2017-07-23) all functions have 16-bit variants; optimizations; bugfixes
      2.15  (2017-03-18) fix png-1,2,4; all Imagenet JPGs; no runtime SSE detection on GCC
      2.14  (2017-03-03) remove deprecated STBI_JPEG_OLD; fixes for Imagenet JPGs
      2.13  (2016-12-04) experimental 16-bit API, only for PNG so far; fixes
      2.12  (2016-04-02) fix typo in 2.11 PSD fix that caused crashes
      2.11  (2016-04-02) 16-bit PNGS; enable SSE2 in non-gcc x64
                         RGB-format JPEG; remove white matting in PSD;
                         allocate large structures on the stack;
                         correct channel count for PNG & BMP
      2.10  (2016-01-22) avoid warning introduced in 2.09
      2.09  (2016-01-16) 16-bit TGA; comments in PNM files; STBI_REALLOC_SIZED

   See end of file for full revision history.


 ============================    Contributors    =========================

 Image formats                          Extensions, features
    Sean Barrett (jpeg, png, bmp)          Jetro Lauha (stbi_info)
    Nicolas Schulz (hdr, psd)              Martin "SpartanJ" Golini (stbi_info)
    Jonathan Dummer (tga)                  James "moose2000" Brown (iPhone PNG)
    Jean-Marc Lienher (gif)                Ben "Disch" Wenger (io callbacks)
    Tom Seddon (pic)                       Omar Cornut (1/2/4-bit PNG)
    Thatcher Ulrich (psd)                  Nicolas Guillemot (vertical flip)
    Ken Miller (pgm, ppm)                  Richard Mitton (16-bit PSD)
    github:urraka (animated gif)           Junggon Kim (PNM comments)
    Christopher Forseth (animated gif)     Daniel Gibson (16-bit TGA)
                                           socks-the-fox (16-bit PNG)
                                           Jeremy Sawicki (handle all ImageNet JPGs)
 Optimizations & bugfixes                  Mikhail Morozov (1-bit BMP)
    Fabian "ryg" Giesen                    Anael Seghezzi (is-16-bit query)
    Arseny Kapoulkine                      Simon Breuss (16-bit PNM)
    John-Mark Allen
    Carmelo J Fdez-Aguera

 Bug & warning fixes
    Marc LeBlanc            David Woo          Guillaume George     Martins Mozeiko
    Christpher Lloyd        Jerry Jansson      Joseph Thomson       Blazej Dariusz Roszkowski
    Phil Jordan                                Dave Moore           Roy Eltham
    Hayaki Saito            Nathan Reed        Won Chun
    Luke Graham             Johan Duparc       Nick Verigakis       the Horde3D community
    Thomas Ruf              Ronny Chevalier                         github:rlyeh
    Janez Zemva             John Bartholomew   Michal Cichon        github:romigrou
    Jonathan Blow           Ken Hamada         Tero Hanninen        github:svdijk
    Eugene Golushkov        Laurent Gomila     Cort Stratton        github:snagar
    Aruelien Pocheville     Sergio Gonzalez    Thibault Reuille     github:Zelex
    Cass Everitt            Ryamond Barbiero                        github:grim210
    Paul Du Bois            Engin Manap        Aldo Culquicondor    github:sammyhw
    Philipp Wiesemann       Dale Weiler        Oriol Ferrer Mesia   github:phprus
    Josh Tobin              Neil Bickford      Matthew Gregan       github:poppolopoppo
    Julian Raschke          Gregory Mullen     Christian Floisand   github:darealshinji
    Baldur Karlsson         Kevin Schmidt      JR Smith             github:Michaelangel007
                            Brad Weinberger    Matvey Cherevko      github:mosra
    Luca Sas                Alexander Veselov  Zack Middleton       [reserved]
    Ryan C. Gordon          [reserved]                              [reserved]
                     DO NOT ADD YOUR NAME HERE

                     Jacko Dirks

  To add your name to the credits, pick a random blank space in the middle and fill it.
  80% of merge conflicts on stb PRs are due to people adding their name at the end
  of the credits.
*/

#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

// DOCUMENTATION
//
// Limitations:
//    - no 12-bit-per-channel JPEG
//    - no JPEGs with arithmetic coding
//    - GIF always returns *comp=4
//
// Basic usage (see HDR discussion below for HDR usage):
//    int x,y,n;
//    unsigned char *data = stbi_load(filename, &x, &y, &n, 0);
//    // ... process data if not NULL ...
//    // ... x = width, y = height, n = # 8-bit components per pixel ...
//    // ... replace '0' with '1'..'4' to force that many components per pixel
//    // ... but 'n' will always be the number that it would have been if you said 0
//    stbi_image_free(data);
//
// Standard parameters:
//    int *x                 -- outputs image width in pixels
//    int *y                 -- outputs image height in pixels
//    int *channels_in_file  -- outputs # of image components in image file
//    int desired_channels   -- if non-zero, # of image components requested in result
//
// The return value from an image loader is an 'unsigned char *' which points
// to the pixel data, or NULL on an allocation failure or if the image is
// corrupt or invalid. The pixel data consists of *y scanlines of *x pixels,
// with each pixel consisting of N interleaved 8-bit components; the first
// pixel pointed to is top-left-most in the image. There is no padding between
// image scanlines or between pixels, regardless of format. The number of
// components N is 'desired_channels' if desired_channels is non-zero, or
// *channels_in_file otherwise. If desired_channels is non-zero,
// *channels_in_file has the number of components that _would_ have been
// output otherwise. E.g. if you set desired_channels to 4, you will always
// get RGBA output, but you can check *channels_in_file to see if it's trivially
// opaque because e.g. there were only 3 channels in the source image.
//
// An output image with N components has the following components interleaved
// in this order in each pixel:
//
//     N=#comp     components
//       1           grey
//       2           grey, alpha
//       3           red, green, blue
//       4           red, green, blue, alpha
//
// If image loading fails for any reason, the return value will be NULL,
// and *x, *y, *channels_in_file will be unchanged. The function
// stbi_failure_reason() can be queried for an extremely brief, end-user
// unfriendly explanation of why the load failed. Define STBI_NO_FAILURE_STRINGS
// to avoid compiling these strings at all, and STBI_FAILURE_USERMSG to get slightly
// more user-friendly ones.
//
// Paletted PNG, BMP, GIF, and PIC images are automatically depalettized.
//
// To query the width, height and component count of an image without having to
// decode the full file, you can use the stbi_info family of functions:
//
//   int x,y,n,ok;
//   ok = stbi_info(filename, &x, &y, &n);
//   // returns ok=1 and sets x, y, n if image is a supported format,
//   // 0 otherwise.
//
// Note that stb_image pervasively uses ints in its public API for sizes,
// including sizes of memory buffers. This is now part of the API and thus
// hard to change without causing breakage. As a result, the various image
// loaders all have certain limits on image size; these differ somewhat
// by format but generally boil down to either just under 2GB or just under
// 1GB. When the decoded image would be larger than this, stb_image decoding
// will fail.
//
// Additionally, stb_image will reject image files that have any of their
// dimensions set to a larger value than the configurable STBI_MAX_DIMENSIONS,
// which defaults to 2**24 = 16777216 pixels. Due to the above memory limit,
// the only way to have an image with such dimensions load correctly
// is for it to have a rather extreme aspect ratio. Either way, the
// assumption here is that such larger images are likely to be malformed
// or malicious. If you do need to load an image with individual dimensions
// larger than that, and it still fits in the overall size limit, you can
// #define STBI_MAX_DIMENSIONS on your own to be something larger.
//
// ===========================================================================
//
// UNICODE:
//
//   If compiling for Windows and you wish to use Unicode filenames, compile
//   with
//       #define STBI_WINDOWS_UTF8
//   and pass utf8-encoded filenames. Call stbi_convert_wchar_to_utf8 to convert
//   Windows wchar_t filenames to utf8.
//
// ===========================================================================
//
// Philosophy
//
// stb libraries are designed with the following priorities:
//
//    1. easy to use
//    2. easy to maintain
//    3. good performance
//
// Sometimes I let "good performance" creep up in priority over "easy to maintain",
// and for best performance I may provide less-easy-to-use APIs that give higher
// performance, in addition to the easy-to-use ones. Nevertheless, it's important
// to keep in mind that from the standpoint of you, a client of this library,
// all you care about is #1 and #3, and stb libraries DO NOT emphasize #3 above all.
//
// Some secondary priorities arise directly from the first two, some of which
// provide more explicit reasons why performance can't be emphasized.
//
//    - Portable ("ease of use")
//    - Small source code footprint ("easy to maintain")
//    - No dependencies ("ease of use")
//
// ===========================================================================
//
// I/O callbacks
//
// I/O callbacks allow you to read from arbitrary sources, like packaged
// files or some other source. Data read from callbacks are processed
// through a small internal buffer (currently 128 bytes) to try to reduce
// overhead.
//
// The three functions you must define are "read" (reads some bytes of data),
// "skip" (skips some bytes of data), "eof" (reports if the stream is at the end).
//
// ===========================================================================
//
// SIMD support
//
// The JPEG decoder will try to automatically use SIMD kernels on x86 when
// supported by the compiler. For ARM Neon support, you must explicitly
// request it.
//
// (The old do-it-yourself SIMD API is no longer supported in the current
// code.)
//
// On x86, SSE2 will automatically be used when available based on a run-time
// test; if not, the generic C versions are used as a fall-back. On ARM targets,
// the typical path is to have separate builds for NEON and non-NEON devices
// (at least this is true for iOS and Android). Therefore, the NEON support is
// toggled by a build flag: define STBI_NEON to get NEON loops.
//
// If for some reason you do not want to use any of SIMD code, or if
// you have issues compiling it, you can disable it entirely by
// defining STBI_NO_SIMD.
//
// ===========================================================================
//
// HDR image support   (disable by defining STBI_NO_HDR)
//
// stb_image supports loading HDR images in general, and currently the Radiance
// .HDR file format specifically. You can still load any file through the existing
// interface; if you attempt to load an HDR file, it will be automatically remapped
// to LDR, assuming gamma 2.2 and an arbitrary scale factor defaulting to 1;
// both of these constants can be reconfigured through this interface:
//
//     stbi_hdr_to_ldr_gamma(2.2f);
//     stbi_hdr_to_ldr_scale(1.0f);
//
// (note, do not use _inverse_ constants; stbi_image will invert them
// appropriately).
//
// Additionally, there is a new, parallel interface for loading files as
// (linear) floats to preserve the full dynamic range:
//
//    float *data = stbi_loadf(filename, &x, &y, &n, 0);
//
// If you load LDR images through this interface, those images will
// be promoted to floating point values, run through the inverse of
// constants corresponding to the above:
//
//     stbi_ldr_to_hdr_scale(1.0f);
//     stbi_ldr_to_hdr_gamma(2.2f);
//
// Finally, given a filename (or an open file or memory block--see header
// file for details) containing image data, you can query for the "most
// appropriate" interface to use (that is, whether the image is HDR or
// not), using:
//
//     stbi_is_hdr(char *filename);
//
// ===========================================================================
//
// iPhone PNG support:
//
// We optionally support converting iPhone-formatted PNGs (which store
// premultiplied BGRA) back to RGB, even though they're internally encoded
// differently. To enable this conversion, call
// stbi_convert_iphone_png_to_rgb(1).
//
// Call stbi_set_unpremultiply_on_load(1) as well to force a divide per
// pixel to remove any premultiplied alpha *only* if the image file explicitly
// says there's premultiplied data (currently only happens in iPhone images,
// and only if iPhone convert-to-rgb processing is on).
//
// ===========================================================================
//
// ADDITIONAL CONFIGURATION
//
//  - You can suppress implementation of any of the decoders to reduce
//    your code footprint by #defining one or more of the following
//    symbols before creating the implementation.
//
//        STBI_NO_JPEG
//        STBI_NO_PNG
//        STBI_NO_BMP
//        STBI_NO_PSD
//        STBI_NO_TGA
//        STBI_NO_GIF
//        STBI_NO_HDR
//        STBI_NO_PIC
//        STBI_NO_PNM   (.ppm and .pgm)
//
//  - You can request *only* certain decoders and suppress all other ones
//    (this will be more forward-compatible, as addition of new decoders
//    doesn't require you to disable them explicitly):
//
//        STBI_ONLY_JPEG
//        STBI_ONLY_PNG
//        STBI_ONLY_BMP
//        STBI_ONLY_PSD
//        STBI_ONLY_TGA
//        STBI_ONLY_GIF
//        STBI_ONLY_HDR
//        STBI_ONLY_PIC
//        STBI_ONLY_PNM   (.ppm and .pgm)
//
//   - If you use STBI_NO_PNG (or _ONLY_ without PNG), and you still
//     want the zlib decoder to be available, #define STBI_SUPPORT_ZLIB
//
//  - If you define STBI_MAX_DIMENSIONS, stb_image will reject images greater
//    than that size (in either width or height) without further processing.
//    This is to let programs in the wild set an upper bound to prevent
//    denial-of-service attacks on untrusted data, as one could generate a
//    valid image of gigantic dimensions and force stb_image to allocate a
//    huge block of memory and spend disproportionate time decoding it. By
//    default this is set to (1 << 24), which is 16777216, but that's still
//    very big.

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif // STBI_NO_STDIO

#define STBI_VERSION 1

enum
{
   STBI_default = 0, // only used for desired_channels

   STBI_grey       = 1,
   STBI_grey_alpha = 2,
   STBI_rgb        = 3,
   STBI_rgb_alpha  = 4
};

#include <stdlib.h>
typedef unsigned char stbi_uc;
typedef unsigned short stbi_us;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBIDEF
#ifdef STB_IMAGE_STATIC
#define STBIDEF static
#else
#define STBIDEF extern
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
// PRIMARY API - works on images of any type
//

//
// load image by filename, open file, or memory buffer
//

typedef struct
{
   int      (*read)  (void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
   void     (*skip)  (void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
   int      (*eof)   (void *user);                       // returns nonzero if we are at end of file/data
} stbi_io_callbacks;

////////////////////////////////////
//
// 8-bits-per-channel interface
//

STBIDEF stbi_uc *stbi_load_from_memory   (stbi_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk  , void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_uc *stbi_load            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
// for stbi_load_from_file, file pointer is left pointing immediately after image
#endif

#ifndef STBI_NO_GIF
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
#endif

#ifdef STBI_WINDOWS_UTF8
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input);
#endif

////////////////////////////////////
//
// 16-bits-per-channel interface
//

STBIDEF stbi_us *stbi_load_16_from_memory   (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_us *stbi_load_16          (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_from_file_16(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
#endif

////////////////////////////////////
//
// float-per-channel interface
//
#ifndef STBI_NO_LINEAR
   STBIDEF float *stbi_loadf_from_memory     (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_callbacks  (stbi_io_callbacks const *clbk, void *user, int *x, int *y,  int *channels_in_file, int desired_channels);

   #ifndef STBI_NO_STDIO
   STBIDEF float *stbi_loadf            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
   #endif
#endif

#ifndef STBI_NO_HDR
   STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma);
   STBIDEF void   stbi_hdr_to_ldr_scale(float scale);
#endif // STBI_NO_HDR

#ifndef STBI_NO_LINEAR
   STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma);
   STBIDEF void   stbi_ldr_to_hdr_scale(float scale);
#endif // STBI_NO_LINEAR

// stbi_is_hdr is always defined, but always returns false if STBI_NO_HDR
STBIDEF int    stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user);
STBIDEF int    stbi_is_hdr_from_memory(stbi_uc const *buffer, int len);
#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename);
STBIDEF int      stbi_is_hdr_from_file(FILE *f);
#endif // STBI_NO_STDIO


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *stbi_failure_reason  (void);

// free the loaded image -- this is just free()
STBIDEF void     stbi_image_free      (void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int      stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len);
STBIDEF int      stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *clbk, void *user);

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_info               (char const *filename,     int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_file     (FILE *f,                  int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit          (char const *filename);
STBIDEF int      stbi_is_16_bit_from_file(FILE *f);
#endif



// for image formats that explicitly notate that they have premultiplied alpha,
// we just return the colors as stored in the file. set this flag to force
// unpremultiplication. results are undefined if the unpremultiply overflow.
STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply);

// indicate whether we should process iphone images back to canonical format,
// or just pass them through "as-is"
STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert);

// flip the image vertically, so the first pixel in the output array is the bottom left
STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

// as above, but only applies to images loaded on the thread that calls the function
// this function is only available if your compiler supports thread-local variables;
// calling it will fail to link if your compiler doesn't
STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply);
STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert);
STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip);

// ZLIB client - used by PNG, available for other purposes

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen);
STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header);
STBIDEF char *stbi_zlib_decode_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

STBIDEF char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);


#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // STBI_INCLUDE_STB_IMAGE_H

#ifdef STB_IMAGE_IMPLEMENTATION

#if defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_PNG) || defined(STBI_ONLY_BMP) \
  || defined(STBI_ONLY_TGA) || defined(STBI_ONLY_GIF) || defined(STBI_ONLY_PSD) \
  || defined(STBI_ONLY_HDR) || defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNM) \
  || defined(STBI_ONLY_ZLIB)
   #ifndef STBI_ONLY_JPEG
   #define STBI_NO_JPEG
   #endif
   #ifndef STBI_ONLY_PNG
   #define STBI_NO_PNG
   #endif
   #ifndef STBI_ONLY_BMP
   #define STBI_NO_BMP
   #endif
   #ifndef STBI_ONLY_PSD
   #define STBI_NO_PSD
   #endif
   #ifndef STBI_ONLY_TGA
   #define STBI_NO_TGA
   #endif
   #ifndef STBI_ONLY_GIF
   #define STBI_NO_GIF
   #endif
   #ifndef STBI_ONLY_HDR
   #define STBI_NO_HDR
   #endif
   #ifndef STBI_ONLY_PIC
   #define STBI_NO_PIC
   #endif
   #ifndef STBI_ONLY_PNM
   #define STBI_NO_PNM
   #endif
#endif

#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif


#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR)
#include <math.h>  // ldexp, pow
#endif

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif

#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
#define STBI_EXTERN extern "C"
#else
#define STBI_EXTERN extern
#endif


#ifndef _MSC_VER
   #ifdef __cplusplus
   #define stbi_inline inline
   #else
   #define stbi_inline
   #endif
#else
   #define stbi_inline __forceinline
#endif

#ifndef STBI_NO_THREAD_LOCALS
   #if defined(__cplusplus) &&  __cplusplus >= 201103L
      #define STBI_THREAD_LOCAL       thread_local
   #elif defined(__GNUC__) && __GNUC__ < 5
      #define STBI_THREAD_LOCAL       __thread
   #elif defined(_MSC_VER)
      #define STBI_THREAD_LOCAL       __declspec(thread)
   #elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBI_THREAD_LOCAL       _Thread_local
   #endif

   #ifndef STBI_THREAD_LOCAL
      #if defined(__GNUC__)
        #define STBI_THREAD_LOCAL       __thread
      #endif
   #endif
#endif

#if defined(_MSC_VER) || defined(__SYMBIAN32__)
typedef unsigned short stbi__uint16;
typedef   signed short stbi__int16;
typedef unsigned int   stbi__uint32;
typedef   signed int   stbi__int32;
#else
#include <stdint.h>
typedef uint16_t stbi__uint16;
typedef int16_t  stbi__int16;
typedef uint32_t stbi__uint32;
typedef int32_t  stbi__int32;
#endif

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbi__uint32)==4 ? 1 : -1];

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef _MSC_VER
#define STBI_HAS_LROTL
#endif

#ifdef STBI_HAS_LROTL
   #define stbi_lrot(x,y)  _lrotl(x,y)
#else
   #define stbi_lrot(x,y)  (((x) << (y)) | ((x) >> (-(y) & 31)))
#endif

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
// gcc doesn't support sse2 intrinsics unless you compile with -msse2,
// which in turn means it gets to use SSE2 everywhere. This is unfortunate,
// but previous attempts to provide the SSE2 functions with runtime
// detection caused numerous issues. The way architecture extensions are
// exposed in GCC/Clang is, sadly, not really suited for one-file libs.
// New behavior: if compiled with -msse2, we use SSE2 without any
// detection; if not, we don't use it at all.
#define STBI_NO_SIMD
#endif

#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)
// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET
//
// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the
// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.
// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not
// simultaneously enabling "-mstackrealign".
//
// See https://github.com/nothings/stb/issues/81 for more information.
//
// So default to no SSE2 on 32-bit MinGW. If you've read this far and added
// -mstackrealign to your build settings, feel free to #define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>

#ifdef _MSC_VER

#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuid
static int stbi__cpuid3(void)
{
   int info[4];
   __cpuid(info,1);
   return info[3];
}
#else
static int stbi__cpuid3(void)
{
   int res;
   __asm {
      mov  eax,1
      cpuid
      mov  res,edx
   }
   return res;
}
#endif

#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   int info3 = stbi__cpuid3();
   return ((info3 >> 26) & 1) != 0;
}
#endif

#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   // If we're even attempting to compile this on GCC/Clang, that means
   // -msse2 is on, which means the compiler is allowed to use SSE2
   // instructions at will, and so are we.
   return 1;
}
#endif

#endif
#endif

// ARM NEON
#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif

#ifdef STBI_NEON
#include <arm_neon.h>
#ifdef _MSC_VER
#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name
#else
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif
#endif

#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif

#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif

///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
typedef struct
{
   stbi__uint32 img_x, img_y;
   int img_n, img_out_n;

   stbi_io_callbacks io;
   void *io_user_data;

   int read_from_callbacks;
   int buflen;
   stbi_uc buffer_start[128];
   int callback_already_read;

   stbi_uc *img_buffer, *img_buffer_end;
   stbi_uc *img_buffer_original, *img_buffer_original_end;
} stbi__context;


static void stbi__refill_buffer(stbi__context *s);

// initialize a memory-decode context
static void stbi__start_mem(stbi__context *s, stbi_uc const *buffer, int len)
{
   s->io.read = NULL;
   s->read_from_callbacks = 0;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = (stbi_uc *) buffer;
   s->img_buffer_end = s->img_buffer_original_end = (stbi_uc *) buffer+len;
}

// initialize a callback-based context
static void stbi__start_callbacks(stbi__context *s, stbi_io_callbacks *c, void *user)
{
   s->io = *c;
   s->io_user_data = user;
   s->buflen = sizeof(s->buffer_start);
   s->read_from_callbacks = 1;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = s->buffer_start;
   stbi__refill_buffer(s);
   s->img_buffer_original_end = s->img_buffer_end;
}

#ifndef STBI_NO_STDIO

static int stbi__stdio_read(void *user, char *data, int size)
{
   return (int) fread(data,1,size,(FILE*) user);
}

static void stbi__stdio_skip(void *user, int n)
{
   int ch;
   fseek((FILE*) user, n, SEEK_CUR);
   ch = fgetc((FILE*) user);  /* have to read a byte to reset feof()'s flag */
   if (ch != EOF) {
      ungetc(ch, (FILE *) user);  /* push byte back onto stream if valid. */
   }
}

static int stbi__stdio_eof(void *user)
{
   return feof((FILE*) user) || ferror((FILE *) user);
}

static stbi_io_callbacks stbi__stdio_callbacks =
{
   stbi__stdio_read,
   stbi__stdio_skip,
   stbi__stdio_eof,
};

static void stbi__start_file(stbi__context *s, FILE *f)
{
   stbi__start_callbacks(s, &stbi__stdio_callbacks, (void *) f);
}

//static void stop_file(stbi__context *s) { }

#endif // !STBI_NO_STDIO

static void stbi__rewind(stbi__context *s)
{
   // conceptually rewind SHOULD rewind to the beginning of the stream,
   // but we just rewind to the beginning of the initial buffer, because
   // we only use it after doing 'test', which only ever looks at at most 92 bytes
   s->img_buffer = s->img_buffer_original;
   s->img_buffer_end = s->img_buffer_original_end;
}

enum
{
   STBI_ORDER_RGB,
   STBI_ORDER_BGR
};

typedef struct
{
   int bits_per_channel;
   int num_channels;
   int channel_order;
} stbi__result_info;

#ifndef STBI_NO_JPEG
static int      stbi__jpeg_test(stbi__context *s);
static void    *stbi__jpeg_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__jpeg_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PNG
static int      stbi__png_test(stbi__context *s);
static void    *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__png_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__png_is16(stbi__context *s);
#endif

#ifndef STBI_NO_BMP
static int      stbi__bmp_test(stbi__context *s);
static void    *stbi__bmp_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__bmp_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_TGA
static int      stbi__tga_test(stbi__context *s);
static void    *stbi__tga_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__tga_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PSD
static int      stbi__psd_test(stbi__context *s);
static void    *stbi__psd_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc);
static int      stbi__psd_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__psd_is16(stbi__context *s);
#endif

#ifndef STBI_NO_HDR
static int      stbi__hdr_test(stbi__context *s);
static float   *stbi__hdr_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__hdr_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PIC
static int      stbi__pic_test(stbi__context *s);
static void    *stbi__pic_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__pic_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_GIF
static int      stbi__gif_test(stbi__context *s);
static void    *stbi__gif_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static void    *stbi__load_gif_main(stbi__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
static int      stbi__gif_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PNM
static int      stbi__pnm_test(stbi__context *s);
static void    *stbi__pnm_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__pnm_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__pnm_is16(stbi__context *s);
#endif

static
#ifdef STBI_THREAD_LOCAL
STBI_THREAD_LOCAL
#endif
const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
{
   return stbi__g_failure_reason;
}

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
   stbi__g_failure_reason = str;
   return 0;
}
#endif

static void *stbi__malloc(size_t size)
{
    return STBI_MALLOC(size);
}

// stb_image uses ints pervasively, including for offset calculations.
// therefore the largest decoded image size we can support with the
// current code, even on 64-bit targets, is INT_MAX. this is not a
// significant limitation for the intended use case.
//
// we do, however, need to make sure our size calculations don't
// overflow. hence a few helper functions for size calculations that
// multiply integers together, making sure that they're non-negative
// and no overflow occurs.

// return 1 if the sum is valid, 0 on overflow.
// negative terms are considered invalid.
static int stbi__addsizes_valid(int a, int b)
{
   if (b < 0) return 0;
   // now 0 <= b <= INT_MAX, hence also
   // 0 <= INT_MAX - b <= INTMAX.
   // And "a + b <= INT_MAX" (which might overflow) is the
   // same as a <= INT_MAX - b (no overflow)
   return a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int stbi__mul2sizes_valid(int a, int b)
{
   if (a < 0 || b < 0) return 0;
   if (b == 0) return 1; // mul-by-0 is always safe
   // portable way to check for no overflows in a*b
   return a <= INT_MAX/b;
}

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbi__mad2sizes_valid(int a, int b, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a*b, add);
}
#endif

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__addsizes_valid(a*b*c, add);
}

// returns 1 if "a*b*c*d + add" has no negative terms/factors and doesn't overflow
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static int stbi__mad4sizes_valid(int a, int b, int c, int d, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__mul2sizes_valid(a*b*c, d) && stbi__addsizes_valid(a*b*c*d, add);
}
#endif

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// mallocs with size overflow checking
static void *stbi__malloc_mad2(int a, int b, int add)
{
   if (!stbi__mad2sizes_valid(a, b, add)) return NULL;
   return stbi__malloc(a*b + add);
}
#endif

static void *stbi__malloc_mad3(int a, int b, int c, int add)
{
   if (!stbi__mad3sizes_valid(a, b, c, add)) return NULL;
   return stbi__malloc(a*b*c + add);
}

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static void *stbi__malloc_mad4(int a, int b, int c, int d, int add)
{
   if (!stbi__mad4sizes_valid(a, b, c, d, add)) return NULL;
   return stbi__malloc(a*b*c*d + add);
}
#endif

// returns 1 if the sum of two signed ints is valid (between -2^31 and 2^31-1 inclusive), 0 on overflow.
static int stbi__addints_valid(int a, int b)
{
   if ((a >= 0) != (b >= 0)) return 1; // a and b have different signs, so no overflow
   if (a < 0 && b < 0) return a >= INT_MIN - b; // same as a + b >= INT_MIN; INT_MIN - b cannot overflow since b < 0.
   return a <= INT_MAX - b;
}

// returns 1 if the product of two ints fits in a signed short, 0 on overflow.
static int stbi__mul2shorts_valid(int a, int b)
{
   if (b == 0 || b == -1) return 1; // multiplication by 0 is always 0; check for -1 so SHRT_MIN/b doesn't overflow
   if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX/b; // product is positive, so similar to mul2sizes_valid
   if (b < 0) return a <= SHRT_MIN / b; // same as a * b >= SHRT_MIN
   return a >= SHRT_MIN / b;
}

// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   #define stbi__err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
   #define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))

STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
   STBI_FREE(retval_from_stbi_load);
}

#ifndef STBI_NO_LINEAR
static float   *stbi__ldr_to_hdr(stbi_uc *data, int x, int y, int comp);
#endif

#ifndef STBI_NO_HDR
static stbi_uc *stbi__hdr_to_ldr(float   *data, int x, int y, int comp);
#endif

static int stbi__vertically_flip_on_load_global = 0;

STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_global = flag_true_if_should_flip;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__vertically_flip_on_load  stbi__vertically_flip_on_load_global
#else
static STBI_THREAD_LOCAL int stbi__vertically_flip_on_load_local, stbi__vertically_flip_on_load_set;

STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_local = flag_true_if_should_flip;
   stbi__vertically_flip_on_load_set = 1;
}

#define stbi__vertically_flip_on_load  (stbi__vertically_flip_on_load_set       \
                                         ? stbi__vertically_flip_on_load_local  \
                                         : stbi__vertically_flip_on_load_global)
#endif // STBI_THREAD_LOCAL

static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
   ri->channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
   ri->num_channels = 0;

   // test the formats with a very explicit header first (at least a FOURCC
   // or distinctive magic number first)
   #ifndef STBI_NO_PNG
   if (stbi__png_test(s))  return stbi__png_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_BMP
   if (stbi__bmp_test(s))  return stbi__bmp_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_GIF
   if (stbi__gif_test(s))  return stbi__gif_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_PSD
   if (stbi__psd_test(s))  return stbi__psd_load(s,x,y,comp,req_comp, ri, bpc);
   #else
   STBI_NOTUSED(bpc);
   #endif
   #ifndef STBI_NO_PIC
   if (stbi__pic_test(s))  return stbi__pic_load(s,x,y,comp,req_comp, ri);
   #endif

   // then the formats that can end up attempting to load with just 1 or 2
   // bytes matching expectations; these are prone to false positives, so
   // try them later
   #ifndef STBI_NO_JPEG
   if (stbi__jpeg_test(s)) return stbi__jpeg_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_PNM
   if (stbi__pnm_test(s))  return stbi__pnm_load(s,x,y,comp,req_comp, ri);
   #endif

   #ifndef STBI_NO_HDR
   if (stbi__hdr_test(s)) {
      float *hdr = stbi__hdr_load(s, x,y,comp,req_comp, ri);
      return stbi__hdr_to_ldr(hdr, *x, *y, req_comp ? req_comp : *comp);
   }
   #endif

   #ifndef STBI_NO_TGA
   // test tga last because it's a crappy test!
   if (stbi__tga_test(s))
      return stbi__tga_load(s,x,y,comp,req_comp, ri);
   #endif

   return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

static stbi_uc *stbi__convert_16_to_8(stbi__uint16 *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   stbi_uc *reduced;

   reduced = (stbi_uc *) stbi__malloc(img_len);
   if (reduced == NULL) return stbi__errpuc("outofmem", "Out of memory");

   for (i = 0; i < img_len; ++i)
      reduced[i] = (stbi_uc)((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

   STBI_FREE(orig);
   return reduced;
}

static stbi__uint16 *stbi__convert_8_to_16(stbi_uc *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   stbi__uint16 *enlarged;

   enlarged = (stbi__uint16 *) stbi__malloc(img_len*2);
   if (enlarged == NULL) return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");

   for (i = 0; i < img_len; ++i)
      enlarged[i] = (stbi__uint16)((orig[i] << 8) + orig[i]); // replicate to high and low byte, maps 0->0, 255->0xffff

   STBI_FREE(orig);
   return enlarged;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   stbi_uc temp[2048];
   stbi_uc *bytes = (stbi_uc *)image;

   for (row = 0; row < (h>>1); row++) {
      stbi_uc *row0 = bytes + row*bytes_per_row;
      stbi_uc *row1 = bytes + (h - row - 1)*bytes_per_row;
      // swap row0 with row1
      size_t bytes_left = bytes_per_row;
      while (bytes_left) {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

#ifndef STBI_NO_GIF
static void stbi__vertical_flip_slices(void *image, int w, int h, int z, int bytes_per_pixel)
{
   int slice;
   int slice_size = w * h * bytes_per_pixel;

   stbi_uc *bytes = (stbi_uc *)image;
   for (slice = 0; slice < z; ++slice) {
      stbi__vertical_flip(bytes, w, h, bytes_per_pixel);
      bytes += slice_size;
   }
}
#endif

static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   stbi__result_info ri;
   void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 8) {
      result = stbi__convert_16_to_8((stbi__uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 8;
   }

   // @TODO: move stbi__convert_format to here

   if (stbi__vertically_flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi_uc));
   }

   return (unsigned char *) result;
}

static stbi__uint16 *stbi__load_and_postprocess_16bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   stbi__result_info ri;
   void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 16);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 16) {
      result = stbi__convert_8_to_16((stbi_uc *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 16;
   }

   // @TODO: move stbi__convert_format16 to here
   // @TODO: special case RGB-to-Y (and RGBA-to-YA) for 8-bit-to-16-bit case to keep more precision

   if (stbi__vertically_flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi__uint16));
   }

   return (stbi__uint16 *) result;
}

#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
static void stbi__float_postprocess(float *result, int *x, int *y, int *comp, int req_comp)
{
   if (stbi__vertically_flip_on_load && result != NULL) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(float));
   }
}
#endif

#ifndef STBI_NO_STDIO

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBI_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBI_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);
#endif

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
	return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
}
#endif

static FILE *stbi__fopen(char const *filename, char const *mode)
{
   FILE *f;
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
	if (0 != _wfopen_s(&f, wFilename, wMode))
		f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
   f = fopen(filename, mode);
#endif
   return f;
}


STBIDEF stbi_uc *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   unsigned char *result;
   if (!f) return stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}

STBIDEF stbi_uc *stbi_load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   unsigned char *result;
   stbi__context s;
   stbi__start_file(&s,f);
   result = stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
   if (result) {
      // need to 'unget' all the characters in the IO buffer
      fseek(f, - (int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
   }
   return result;
}

STBIDEF stbi__uint16 *stbi_load_from_file_16(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__uint16 *result;
   stbi__context s;
   stbi__start_file(&s,f);
   result = stbi__load_and_postprocess_16bit(&s,x,y,comp,req_comp);
   if (result) {
      // need to 'unget' all the characters in the IO buffer
      fseek(f, - (int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
   }
   return result;
}

STBIDEF stbi_us *stbi_load_16(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   stbi__uint16 *result;
   if (!f) return (stbi_us *) stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file_16(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}


#endif //!STBI_NO_STDIO

STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
}

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *)clbk, user);
   return stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
}

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

#ifndef STBI_NO_GIF
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   unsigned char *result;
   stbi__context s;
   stbi__start_mem(&s,buffer,len);

   result = (unsigned char*) stbi__load_gif_main(&s, delays, x, y, z, comp, req_comp);
   if (stbi__vertically_flip_on_load) {
      stbi__vertical_flip_slices( result, *x, *y, *z, *comp );
   }

   return result;
}
#endif

#ifndef STBI_NO_LINEAR
static float *stbi__loadf_main(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   unsigned char *data;
   #ifndef STBI_NO_HDR
   if (stbi__hdr_test(s)) {
      stbi__result_info ri;
      float *hdr_data = stbi__hdr_load(s,x,y,comp,req_comp, &ri);
      if (hdr_data)
         stbi__float_postprocess(hdr_data,x,y,comp,req_comp);
      return hdr_data;
   }
   #endif
   data = stbi__load_and_postprocess_8bit(s, x, y, comp, req_comp);
   if (data)
      return stbi__ldr_to_hdr(data, *x, *y, req_comp ? req_comp : *comp);
   return stbi__errpf("unknown image type", "Image not of any known type, or corrupt");
}

STBIDEF float *stbi_loadf_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}

#ifndef STBI_NO_STDIO
STBIDEF float *stbi_loadf(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   float *result;
   FILE *f = stbi__fopen(filename, "rb");
   if (!f) return stbi__errpf("can't fopen", "Unable to open file");
   result = stbi_loadf_from_file(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}

STBIDEF float *stbi_loadf_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_file(&s,f);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}
#endif // !STBI_NO_STDIO

#endif // !STBI_NO_LINEAR

// these is-hdr-or-not is defined independent of whether STBI_NO_LINEAR is
// defined, for API simplicity; if STBI_NO_LINEAR is defined, it always
// reports false!

STBIDEF int stbi_is_hdr_from_memory(stbi_uc const *buffer, int len)
{
   #ifndef STBI_NO_HDR
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__hdr_test(&s);
   #else
   STBI_NOTUSED(buffer);
   STBI_NOTUSED(len);
   return 0;
   #endif
}

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename)
{
   FILE *f = stbi__fopen(filename, "rb");
   int result=0;
   if (f) {
      result = stbi_is_hdr_from_file(f);
      fclose(f);
   }
   return result;
}

STBIDEF int stbi_is_hdr_from_file(FILE *f)
{
   #ifndef STBI_NO_HDR
   long pos = ftell(f);
   int res;
   stbi__context s;
   stbi__start_file(&s,f);
   res = stbi__hdr_test(&s);
   fseek(f, pos, SEEK_SET);
   return res;
   #else
   STBI_NOTUSED(f);
   return 0;
   #endif
}
#endif // !STBI_NO_STDIO

STBIDEF int      stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user)
{
   #ifndef STBI_NO_HDR
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__hdr_test(&s);
   #else
   STBI_NOTUSED(clbk);
   STBI_NOTUSED(user);
   return 0;
   #endif
}

#ifndef STBI_NO_LINEAR
static float stbi__l2h_gamma=2.2f, stbi__l2h_scale=1.0f;

STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma) { stbi__l2h_gamma = gamma; }
STBIDEF void   stbi_ldr_to_hdr_scale(float scale) { stbi__l2h_scale = scale; }
#endif

static float stbi__h2l_gamma_i=1.0f/2.2f, stbi__h2l_scale_i=1.0f;

STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma) { stbi__h2l_gamma_i = 1/gamma; }
STBIDEF void   stbi_hdr_to_ldr_scale(float scale) { stbi__h2l_scale_i = 1/scale; }


//////////////////////////////////////////////////////////////////////////////
//
// Common code used by all image loaders
//

enum
{
   STBI__SCAN_load=0,
   STBI__SCAN_type,
   STBI__SCAN_header
};

static void stbi__refill_buffer(stbi__context *s)
{
   int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);
   s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
   if (n == 0) {
      // at end of file, treat same as if from memory, but need to handle case
      // where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
      s->read_from_callbacks = 0;
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start+1;
      *s->img_buffer = 0;
   } else {
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start + n;
   }
}

stbi_inline static stbi_uc stbi__get8(stbi__context *s)
{
   if (s->img_buffer < s->img_buffer_end)
      return *s->img_buffer++;
   if (s->read_from_callbacks) {
      stbi__refill_buffer(s);
      return *s->img_buffer++;
   }
   return 0;
}

#if defined(STBI_NO_JPEG) && defined(STBI_NO_HDR) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
stbi_inline static int stbi__at_eof(stbi__context *s)
{
   if (s->io.read) {
      if (!(s->io.eof)(s->io_user_data)) return 0;
      // if feof() is true, check if buffer = end
      // special case: we've only got the special 0 character at the end
      if (s->read_from_callbacks == 0) return 1;
   }

   return s->img_buffer >= s->img_buffer_end;
}
#endif

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC)
// nothing
#else
static void stbi__skip(stbi__context *s, int n)
{
   if (n == 0) return;  // already there!
   if (n < 0) {
      s->img_buffer = s->img_buffer_end;
      return;
   }
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         s->img_buffer = s->img_buffer_end;
         (s->io.skip)(s->io_user_data, n - blen);
         return;
      }
   }
   s->img_buffer += n;
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_TGA) && defined(STBI_NO_HDR) && defined(STBI_NO_PNM)
// nothing
#else
static int stbi__getn(stbi__context *s, stbi_uc *buffer, int n)
{
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         int res, count;

         memcpy(buffer, s->img_buffer, blen);

         count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);
         res = (count == (n-blen));
         s->img_buffer = s->img_buffer_end;
         return res;
      }
   }

   if (s->img_buffer+n <= s->img_buffer_end) {
      memcpy(buffer, s->img_buffer, n);
      s->img_buffer += n;
      return 1;
   } else
      return 0;
}
#endif

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)
// nothing
#else
static int stbi__get16be(stbi__context *s)
{
   int z = stbi__get8(s);
   return (z << 8) + stbi__get8(s);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)
// nothing
#else
static stbi__uint32 stbi__get32be(stbi__context *s)
{
   stbi__uint32 z = stbi__get16be(s);
   return (z << 16) + stbi__get16be(s);
}
#endif

#if defined(STBI_NO_BMP) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF)
// nothing
#else
static int stbi__get16le(stbi__context *s)
{
   int z = stbi__get8(s);
   return z + (stbi__get8(s) << 8);
}
#endif

#ifndef STBI_NO_BMP
static stbi__uint32 stbi__get32le(stbi__context *s)
{
   stbi__uint32 z = stbi__get16le(s);
   z += (stbi__uint32)stbi__get16le(s) << 16;
   return z;
}
#endif

#define STBI__BYTECAST(x)  ((stbi_uc) ((x) & 255))  // truncate int to byte without warnings

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
//////////////////////////////////////////////////////////////////////////////
//
//  generic converter from built-in img_n to req_comp
//    individual types do this automatically as much as possible (e.g. jpeg
//    does all cases internally since it needs to colorspace convert anyway,
//    and it never has alpha, so very few cases ). png can automatically
//    interleave an alpha=255 channel, but falls back to this for other cases
//
//  assume data buffer is malloced, so malloc a new one and free that one
//  only failure mode is malloc failing

static stbi_uc stbi__compute_y(int r, int g, int b)
{
   return (stbi_uc) (((r*77) + (g*150) +  (29*b)) >> 8);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
static unsigned char *stbi__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   unsigned char *good;

   if (req_comp == img_n) return data;
   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

   good = (unsigned char *) stbi__malloc_mad3(req_comp, x, y, 0);
   if (good == NULL) {
      STBI_FREE(data);
      return stbi__errpuc("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      unsigned char *src  = data + j * x * img_n   ;
      unsigned char *dest = good + j * x * req_comp;

      #define STBI__COMBO(a,b)  ((a)*8+(b))
      #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBI__COMBO(img_n, req_comp)) {
         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
         STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
         STBI__CASE(3,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(3,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
         STBI__CASE(4,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(4,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
         default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good); return stbi__errpuc("unsupported", "Unsupported format conversion");
      }
      #undef STBI__CASE
   }

   STBI_FREE(data);
   return good;
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)
// nothing
#else
static stbi__uint16 stbi__compute_y_16(int r, int g, int b)
{
   return (stbi__uint16) (((r*77) + (g*150) +  (29*b)) >> 8);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)
// nothing
#else
static stbi__uint16 *stbi__convert_format16(stbi__uint16 *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   stbi__uint16 *good;

   if (req_comp == img_n) return data;
   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

   good = (stbi__uint16 *) stbi__malloc(req_comp * x * y * 2);
   if (good == NULL) {
      STBI_FREE(data);
      return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      stbi__uint16 *src  = data + j * x * img_n   ;
      stbi__uint16 *dest = good + j * x * req_comp;

      #define STBI__COMBO(a,b)  ((a)*8+(b))
      #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBI__COMBO(img_n, req_comp)) {
         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;
         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;
         STBI__CASE(2,1) { dest[0]=src[0];                                                     } break;
         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;
         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;
         STBI__CASE(3,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
         STBI__CASE(3,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1] = 0xffff; } break;
         STBI__CASE(4,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
         STBI__CASE(4,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;
         default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good); return (stbi__uint16*) stbi__errpuc("unsupported", "Unsupported format conversion");
      }
      #undef STBI__CASE
   }

   STBI_FREE(data);
   return good;
}
#endif

#ifndef STBI_NO_LINEAR
static float   *stbi__ldr_to_hdr(stbi_uc *data, int x, int y, int comp)
{
   int i,k,n;
   float *output;
   if (!data) return NULL;
   output = (float *) stbi__malloc_mad4(x, y, comp, sizeof(float), 0);
   if (output == NULL) { STBI_FREE(data); return stbi__errpf("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   for (i=0; i < x*y; ++i) {
      for (k=0; k < n; ++k) {
         output[i*comp + k] = (float) (pow(data[i*comp+k]/255.0f, stbi__l2h_gamma) * stbi__l2h_scale);
      }
   }
   if (n < comp) {
      for (i=0; i < x*y; ++i) {
         output[i*comp + n] = data[i*comp + n]/255.0f;
      }
   }
   STBI_FREE(data);
   return output;
}
#endif

#ifndef STBI_NO_HDR
#define stbi__float2int(x)   ((int) (x))
static stbi_uc *stbi__hdr_to_ldr(float   *data, int x, int y, int comp)
{
   int i,k,n;
   stbi_uc *output;
   if (!data) return NULL;
   output = (stbi_uc *) stbi__malloc_mad3(x, y, comp, 0);
   if (output == NULL) { STBI_FREE(data); return stbi__errpuc("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   for (i=0; i < x*y; ++i) {
      for (k=0; k < n; ++k) {
         float z = (float) pow(data[i*comp+k]*stbi__h2l_scale_i, stbi__h2l_gamma_i) * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (stbi_uc) stbi__float2int(z);
      }
      if (k < comp) {
         float z = data[i*comp+k] * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (stbi_uc) stbi__float2int(z);
      }
   }
   STBI_FREE(data);
   return output;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  "baseline" JPEG/JFIF decoder
//
//    simple implementation
//      - doesn't support delayed output of y-dimension
//      - simple interface (only one output format: 8-bit interleaved RGB)
//      - doesn't try to recover corrupt jpegs
//      - doesn't allow partial loading, loading multiple at once
//      - still fast on x86 (copying globals into locals doesn't help x86)
//      - allocates lots of intermediate memory (full size of all components)
//        - non-interleaved case requires this anyway
//        - allows good upsampling (see next)
//    high-quality
//      - upsampled channels are bilinearly interpolated, even across blocks
//      - quality integer IDCT derived from IJG's 'slow'
//    performance
//      - fast huffman; reasonable integer IDCT
//      - some SIMD kernels for common paths on targets with SSE2/NEON
//      - uses a lot of intermediate memory, could cache poorly

#ifndef STBI_NO_JPEG

// huffman decoding acceleration
#define FAST_BITS   9  // larger handles more cases; smaller stomps less cache

typedef struct
{
   stbi_uc  fast[1 << FAST_BITS];
   // weirdly, repacking this into AoS is a 10% speed loss, instead of a win
   stbi__uint16 code[256];
   stbi_uc  values[256];
   stbi_uc  size[257];
   unsigned int maxcode[18];
   int    delta[17];   // old 'firstsymbol' - old 'firstcode'
} stbi__huffman;

typedef struct
{
   stbi__context *s;
   stbi__huffman huff_dc[4];
   stbi__huffman huff_ac[4];
   stbi__uint16 dequant[4][64];
   stbi__int16 fast_ac[4][1 << FAST_BITS];

// sizes for components, interleaved MCUs
   int img_h_max, img_v_max;
   int img_mcu_x, img_mcu_y;
   int img_mcu_w, img_mcu_h;

// definition of jpeg image component
   struct
   {
      int id;
      int h,v;
      int tq;
      int hd,ha;
      int dc_pred;

      int x,y,w2,h2;
      stbi_uc *data;
      void *raw_data, *raw_coeff;
      stbi_uc *linebuf;
      short   *coeff;   // progressive only
      int      coeff_w, coeff_h; // number of 8x8 coefficient blocks
   } img_comp[4];

   stbi__uint32   code_buffer; // jpeg entropy-coded buffer
   int            code_bits;   // number of valid bits
   unsigned char  marker;      // marker seen while filling entropy buffer
   int            nomore;      // flag if we saw a marker so must stop

   int            progressive;
   int            spec_start;
   int            spec_end;
   int            succ_high;
   int            succ_low;
   int            eob_run;
   int            jfif;
   int            app14_color_transform; // Adobe APP14 tag
   int            rgb;

   int scan_n, order[4];
   int restart_interval, todo;

// kernels
   void (*idct_block_kernel)(stbi_uc *out, int out_stride, short data[64]);
   void (*YCbCr_to_RGB_kernel)(stbi_uc *out, const stbi_uc *y, const stbi_uc *pcb, const stbi_uc *pcr, int count, int step);
   stbi_uc *(*resample_row_hv_2_kernel)(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs);
} stbi__jpeg;

static int stbi__build_huffman(stbi__huffman *h, int *count)
{
   int i,j,k=0;
   unsigned int code;
   // build size list for each symbol (from JPEG spec)
   for (i=0; i < 16; ++i) {
      for (j=0; j < count[i]; ++j) {
         h->size[k++] = (stbi_uc) (i+1);
         if(k >= 257) return stbi__err("bad size list","Corrupt JPEG");
      }
   }
   h->size[k] = 0;

   // compute actual symbols (from jpeg spec)
   code = 0;
   k = 0;
   for(j=1; j <= 16; ++j) {
      // compute delta to add to code to compute symbol id
      h->delta[j] = k - code;
      if (h->size[k] == j) {
         while (h->size[k] == j)
            h->code[k++] = (stbi__uint16) (code++);
         if (code-1 >= (1u << j)) return stbi__err("bad code lengths","Corrupt JPEG");
      }
      // compute largest code + 1 for this size, preshifted as needed later
      h->maxcode[j] = code << (16-j);
      code <<= 1;
   }
   h->maxcode[j] = 0xffffffff;

   // build non-spec acceleration table; 255 is flag for not-accelerated
   memset(h->fast, 255, 1 << FAST_BITS);
   for (i=0; i < k; ++i) {
      int s = h->size[i];
      if (s <= FAST_BITS) {
         int c = h->code[i] << (FAST_BITS-s);
         int m = 1 << (FAST_BITS-s);
         for (j=0; j < m; ++j) {
            h->fast[c+j] = (stbi_uc) i;
         }
      }
   }
   return 1;
}

// build a table that decodes both magnitude and value of small ACs in
// one go.
static void stbi__build_fast_ac(stbi__int16 *fast_ac, stbi__huffman *h)
{
   int i;
   for (i=0; i < (1 << FAST_BITS); ++i) {
      stbi_uc fast = h->fast[i];
      fast_ac[i] = 0;
      if (fast < 255) {
         int rs = h->values[fast];
         int run = (rs >> 4) & 15;
         int magbits = rs & 15;
         int len = h->size[fast];

         if (magbits && len + magbits <= FAST_BITS) {
            // magnitude code followed by receive_extend code
            int k = ((i << len) & ((1 << FAST_BITS) - 1)) >> (FAST_BITS - magbits);
            int m = 1 << (magbits - 1);
            if (k < m) k += (~0U << magbits) + 1;
            // if the result is small enough, we can fit it in fast_ac table
            if (k >= -128 && k <= 127)
               fast_ac[i] = (stbi__int16) ((k * 256) + (run * 16) + (len + magbits));
         }
      }
   }
}

static void stbi__grow_buffer_unsafe(stbi__jpeg *j)
{
   do {
      unsigned int b = j->nomore ? 0 : stbi__get8(j->s);
      if (b == 0xff) {
         int c = stbi__get8(j->s);
         while (c == 0xff) c = stbi__get8(j->s); // consume fill bytes
         if (c != 0) {
            j->marker = (unsigned char) c;
            j->nomore = 1;
            return;
         }
      }
      j->code_buffer |= b << (24 - j->code_bits);
      j->code_bits += 8;
   } while (j->code_bits <= 24);
}

// (1 << n) - 1
static const stbi__uint32 stbi__bmask[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

// decode a jpeg huffman value from the bitstream
stbi_inline static int stbi__jpeg_huff_decode(stbi__jpeg *j, stbi__huffman *h)
{
   unsigned int temp;
   int c,k;

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);

   // look at the top FAST_BITS and determine what symbol ID it is,
   // if the code is <= FAST_BITS
   c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
   k = h->fast[c];
   if (k < 255) {
      int s = h->size[k];
      if (s > j->code_bits)
         return -1;
      j->code_buffer <<= s;
      j->code_bits -= s;
      return h->values[k];
   }

   // naive test is to shift the code_buffer down so k bits are
   // valid, then test against maxcode. To speed this up, we've
   // preshifted maxcode left so that it has (16-k) 0s at the
   // end; in other words, regardless of the number of bits, it
   // wants to be compared against something shifted to have 16;
   // that way we don't need to shift inside the loop.
   temp = j->code_buffer >> 16;
   for (k=FAST_BITS+1 ; ; ++k)
      if (temp < h->maxcode[k])
         break;
   if (k == 17) {
      // error! code not found
      j->code_bits -= 16;
      return -1;
   }

   if (k > j->code_bits)
      return -1;

   // convert the huffman code to the symbol id
   c = ((j->code_buffer >> (32 - k)) & stbi__bmask[k]) + h->delta[k];
   if(c < 0 || c >= 256) // symbol id out of bounds!
       return -1;
   STBI_ASSERT((((j->code_buffer) >> (32 - h->size[c])) & stbi__bmask[h->size[c]]) == h->code[c]);

   // convert the id to a symbol
   j->code_bits -= k;
   j->code_buffer <<= k;
   return h->values[c];
}

// bias[n] = (-1<<n) + 1
static const int stbi__jbias[16] = {0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767};

// combined JPEG 'receive' and JPEG 'extend', since baseline
// always extends everything it receives.
stbi_inline static int stbi__extend_receive(stbi__jpeg *j, int n)
{
   unsigned int k;
   int sgn;
   if (j->code_bits < n) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing

   sgn = j->code_buffer >> 31; // sign bit always in MSB; 0 if MSB clear (positive), 1 if MSB set (negative)
   k = stbi_lrot(j->code_buffer, n);
   j->code_buffer = k & ~stbi__bmask[n];
   k &= stbi__bmask[n];
   j->code_bits -= n;
   return k + (stbi__jbias[n] & (sgn - 1));
}

// get some unsigned bits
stbi_inline static int stbi__jpeg_get_bits(stbi__jpeg *j, int n)
{
   unsigned int k;
   if (j->code_bits < n) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = stbi_lrot(j->code_buffer, n);
   j->code_buffer = k & ~stbi__bmask[n];
   k &= stbi__bmask[n];
   j->code_bits -= n;
   return k;
}

stbi_inline static int stbi__jpeg_get_bit(stbi__jpeg *j)
{
   unsigned int k;
   if (j->code_bits < 1) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < 1) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = j->code_buffer;
   j->code_buffer <<= 1;
   --j->code_bits;
   return k & 0x80000000;
}

// given a value that's at position X in the zigzag stream,
// where does it appear in the 8x8 matrix coded as row-major?
static const stbi_uc stbi__jpeg_dezigzag[64+15] =
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63,
   // let corrupt input sample past end
   63, 63, 63, 63, 63, 63, 63, 63,
   63, 63, 63, 63, 63, 63, 63
};

// decode one 64-entry block--
static int stbi__jpeg_decode_block(stbi__jpeg *j, short data[64], stbi__huffman *hdc, stbi__huffman *hac, stbi__int16 *fac, int b, stbi__uint16 *dequant)
{
   int diff,dc,k;
   int t;

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
   t = stbi__jpeg_huff_decode(j, hdc);
   if (t < 0 || t > 15) return stbi__err("bad huffman code","Corrupt JPEG");

   // 0 all the ac values now so we can do it 32-bits at a time
   memset(data,0,64*sizeof(data[0]));

   diff = t ? stbi__extend_receive(j, t) : 0;
   if (!stbi__addints_valid(j->img_comp[b].dc_pred, diff)) return stbi__err("bad delta","Corrupt JPEG");
   dc = j->img_comp[b].dc_pred + diff;
   j->img_comp[b].dc_pred = dc;
   if (!stbi__mul2shorts_valid(dc, dequant[0])) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
   data[0] = (short) (dc * dequant[0]);

   // decode AC components, see JPEG spec
   k = 1;
   do {
      unsigned int zig;
      int c,r,s;
      if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
      c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
      r = fac[c];
      if (r) { // fast-AC path
         k += (r >> 4) & 15; // run
         s = r & 15; // combined length
         if (s > j->code_bits) return stbi__err("bad huffman code", "Combined length longer than code bits available");
         j->code_buffer <<= s;
         j->code_bits -= s;
         // decode into unzigzag'd location
         zig = stbi__jpeg_dezigzag[k++];
         data[zig] = (short) ((r >> 8) * dequant[zig]);
      } else {
         int rs = stbi__jpeg_huff_decode(j, hac);
         if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
         s = rs & 15;
         r = rs >> 4;
         if (s == 0) {
            if (rs != 0xf0) break; // end block
            k += 16;
         } else {
            k += r;
            // decode into unzigzag'd location
            zig = stbi__jpeg_dezigzag[k++];
            data[zig] = (short) (stbi__extend_receive(j,s) * dequant[zig]);
         }
      }
   } while (k < 64);
   return 1;
}

static int stbi__jpeg_decode_block_prog_dc(stbi__jpeg *j, short data[64], stbi__huffman *hdc, int b)
{
   int diff,dc;
   int t;
   if (j->spec_end != 0) return stbi__err("can't merge dc and ac", "Corrupt JPEG");

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);

   if (j->succ_high == 0) {
      // first scan for DC coefficient, must be first
      memset(data,0,64*sizeof(data[0])); // 0 all the ac values now
      t = stbi__jpeg_huff_decode(j, hdc);
      if (t < 0 || t > 15) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
      diff = t ? stbi__extend_receive(j, t) : 0;

      if (!stbi__addints_valid(j->img_comp[b].dc_pred, diff)) return stbi__err("bad delta", "Corrupt JPEG");
      dc = j->img_comp[b].dc_pred + diff;
      j->img_comp[b].dc_pred = dc;
      if (!stbi__mul2shorts_valid(dc, 1 << j->succ_low)) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
      data[0] = (short) (dc * (1 << j->succ_low));
   } else {
      // refinement scan for DC coefficient
      if (stbi__jpeg_get_bit(j))
         data[0] += (short) (1 << j->succ_low);
   }
   return 1;
}

// @OPTIMIZE: store non-zigzagged during the decode passes,
// and only de-zigzag when dequantizing
static int stbi__jpeg_decode_block_prog_ac(stbi__jpeg *j, short data[64], stbi__huffman *hac, stbi__int16 *fac)
{
   int k;
   if (j->spec_start == 0) return stbi__err("can't merge dc and ac", "Corrupt JPEG");

   if (j->succ_high == 0) {
      int shift = j->succ_low;

      if (j->eob_run) {
         --j->eob_run;
         return 1;
      }

      k = j->spec_start;
      do {
         unsigned int zig;
         int c,r,s;
         if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
         c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
         r = fac[c];
         if (r) { // fast-AC path
            k += (r >> 4) & 15; // run
            s = r & 15; // combined length
            if (s > j->code_bits) return stbi__err("bad huffman code", "Combined length longer than code bits available");
            j->code_buffer <<= s;
            j->code_bits -= s;
            zig = stbi__jpeg_dezigzag[k++];
            data[zig] = (short) ((r >> 8) * (1 << shift));
         } else {
            int rs = stbi__jpeg_huff_decode(j, hac);
            if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r);
                  if (r)
                     j->eob_run += stbi__jpeg_get_bits(j, r);
                  --j->eob_run;
                  break;
               }
               k += 16;
            } else {
               k += r;
               zig = stbi__jpeg_dezigzag[k++];
               data[zig] = (short) (stbi__extend_receive(j,s) * (1 << shift));
            }
         }
      } while (k <= j->spec_end);
   } else {
      // refinement scan for these AC coefficients

      short bit = (short) (1 << j->succ_low);

      if (j->eob_run) {
         --j->eob_run;
         for (k = j->spec_start; k <= j->spec_end; ++k) {
            short *p = &data[stbi__jpeg_dezigzag[k]];
            if (*p != 0)
               if (stbi__jpeg_get_bit(j))
                  if ((*p & bit)==0) {
                     if (*p > 0)
                        *p += bit;
                     else
                        *p -= bit;
                  }
         }
      } else {
         k = j->spec_start;
         do {
            int r,s;
            int rs = stbi__jpeg_huff_decode(j, hac); // @OPTIMIZE see if we can use the fast path here, advance-by-r is so slow, eh
            if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r) - 1;
                  if (r)
                     j->eob_run += stbi__jpeg_get_bits(j, r);
                  r = 64; // force end of block
               } else {
                  // r=15 s=0 should write 16 0s, so we just do
                  // a run of 15 0s and then write s (which is 0),
                  // so we don't have to do anything special here
               }
            } else {
               if (s != 1) return stbi__err("bad huffman code", "Corrupt JPEG");
               // sign bit
               if (stbi__jpeg_get_bit(j))
                  s = bit;
               else
                  s = -bit;
            }

            // advance by r
            while (k <= j->spec_end) {
               short *p = &data[stbi__jpeg_dezigzag[k++]];
               if (*p != 0) {
                  if (stbi__jpeg_get_bit(j))
                     if ((*p & bit)==0) {
                        if (*p > 0)
                           *p += bit;
                        else
                           *p -= bit;
                     }
               } else {
                  if (r == 0) {
                     *p = (short) s;
                     break;
                  }
                  --r;
               }
            }
         } while (k <= j->spec_end);
      }
   }
   return 1;
}

// take a -128..127 value and stbi__clamp it and convert to 0..255
stbi_inline static stbi_uc stbi__clamp(int x)
{
   // trick to use a single test to catch both cases
   if ((unsigned int) x > 255) {
      if (x < 0) return 0;
      if (x > 255) return 255;
   }
   return (stbi_uc) x;
}

#define stbi__f2f(x)  ((int) (((x) * 4096 + 0.5)))
#define stbi__fsh(x)  ((x) * 4096)

// derived from jidctint -- DCT_ISLOW
#define STBI__IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7) \
   int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
   p2 = s2;                                    \
   p3 = s6;                                    \
   p1 = (p2+p3) * stbi__f2f(0.5411961f);       \
   t2 = p1 + p3*stbi__f2f(-1.847759065f);      \
   t3 = p1 + p2*stbi__f2f( 0.765366865f);      \
   p2 = s0;                                    \
   p3 = s4;                                    \
   t0 = stbi__fsh(p2+p3);                      \
   t1 = stbi__fsh(p2-p3);                      \
   x0 = t0+t3;                                 \
   x3 = t0-t3;                                 \
   x1 = t1+t2;                                 \
   x2 = t1-t2;                                 \
   t0 = s7;                                    \
   t1 = s5;                                    \
   t2 = s3;                                    \
   t3 = s1;                                    \
   p3 = t0+t2;                                 \
   p4 = t1+t3;                                 \
   p1 = t0+t3;                                 \
   p2 = t1+t2;                                 \
   p5 = (p3+p4)*stbi__f2f( 1.175875602f);      \
   t0 = t0*stbi__f2f( 0.298631336f);           \
   t1 = t1*stbi__f2f( 2.053119869f);           \
   t2 = t2*stbi__f2f( 3.072711026f);           \
   t3 = t3*stbi__f2f( 1.501321110f);           \
   p1 = p5 + p1*stbi__f2f(-0.899976223f);      \
   p2 = p5 + p2*stbi__f2f(-2.562915447f);      \
   p3 = p3*stbi__f2f(-1.961570560f);           \
   p4 = p4*stbi__f2f(-0.390180644f);           \
   t3 += p1+p4;                                \
   t2 += p2+p3;                                \
   t1 += p2+p4;                                \
   t0 += p1+p3;

static void stbi__idct_block(stbi_uc *out, int out_stride, short data[64])
{
   int i,val[64],*v=val;
   stbi_uc *o;
   short *d = data;

   // columns
   for (i=0; i < 8; ++i,++d, ++v) {
      // if all zeroes, shortcut -- this avoids dequantizing 0s and IDCTing
      if (d[ 8]==0 && d[16]==0 && d[24]==0 && d[32]==0
           && d[40]==0 && d[48]==0 && d[56]==0) {
         //    no shortcut                 0     seconds
         //    (1|2|3|4|5|6|7)==0          0     seconds
         //    all separate               -0.047 seconds
         //    1 && 2|3 && 4|5 && 6|7:    -0.047 seconds
         int dcterm = d[0]*4;
         v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dcterm;
      } else {
         STBI__IDCT_1D(d[ 0],d[ 8],d[16],d[24],d[32],d[40],d[48],d[56])
         // constants scaled things up by 1<<12; let's bring them back
         // down, but keep 2 extra bits of precision
         x0 += 512; x1 += 512; x2 += 512; x3 += 512;
         v[ 0] = (x0+t3) >> 10;
         v[56] = (x0-t3) >> 10;
         v[ 8] = (x1+t2) >> 10;
         v[48] = (x1-t2) >> 10;
         v[16] = (x2+t1) >> 10;
         v[40] = (x2-t1) >> 10;
         v[24] = (x3+t0) >> 10;
         v[32] = (x3-t0) >> 10;
      }
   }

   for (i=0, v=val, o=out; i < 8; ++i,v+=8,o+=out_stride) {
      // no fast case since the first 1D IDCT spread components out
      STBI__IDCT_1D(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7])
      // constants scaled things up by 1<<12, plus we had 1<<2 from first
      // loop, plus horizontal and vertical each scale by sqrt(8) so together
      // we've got an extra 1<<3, so 1<<17 total we need to remove.
      // so we want to round that, which means adding 0.5 * 1<<17,
      // aka 65536. Also, we'll end up with -128 to 127 that we want
      // to encode as 0..255 by adding 128, so we'll add that before the shift
      x0 += 65536 + (128<<17);
      x1 += 65536 + (128<<17);
      x2 += 65536 + (128<<17);
      x3 += 65536 + (128<<17);
      // tried computing the shifts into temps, or'ing the temps to see
      // if any were out of range, but that was slower
      o[0] = stbi__clamp((x0+t3) >> 17);
      o[7] = stbi__clamp((x0-t3) >> 17);
      o[1] = stbi__clamp((x1+t2) >> 17);
      o[6] = stbi__clamp((x1-t2) >> 17);
      o[2] = stbi__clamp((x2+t1) >> 17);
      o[5] = stbi__clamp((x2-t1) >> 17);
      o[3] = stbi__clamp((x3+t0) >> 17);
      o[4] = stbi__clamp((x3-t0) >> 17);
   }
}

#ifdef STBI_SSE2
// sse2 integer IDCT. not the fastest possible implementation but it
// produces bit-identical results to the generic C version so it's
// fully "transparent".
static void stbi__idct_simd(stbi_uc *out, int out_stride, short data[64])
{
   // This is constructed to match our regular (generic) integer IDCT exactly.
   __m128i row0, row1, row2, row3, row4, row5, row6, row7;
   __m128i tmp;

   // dot product constant: even elems=x, odd elems=y
   #define dct_const(x,y)  _mm_setr_epi16((x),(y),(x),(y),(x),(y),(x),(y))

   // out(0) = c0[even]*x + c0[odd]*y   (c0, x, y 16-bit, out 32-bit)
   // out(1) = c1[even]*x + c1[odd]*y
   #define dct_rot(out0,out1, x,y,c0,c1) \
      __m128i c0##lo = _mm_unpacklo_epi16((x),(y)); \
      __m128i c0##hi = _mm_unpackhi_epi16((x),(y)); \
      __m128i out0##_l = _mm_madd_epi16(c0##lo, c0); \
      __m128i out0##_h = _mm_madd_epi16(c0##hi, c0); \
      __m128i out1##_l = _mm_madd_epi16(c0##lo, c1); \
      __m128i out1##_h = _mm_madd_epi16(c0##hi, c1)

   // out = in << 12  (in 16-bit, out 32-bit)
   #define dct_widen(out, in) \
      __m128i out##_l = _mm_srai_epi32(_mm_unpacklo_epi16(_mm_setzero_si128(), (in)), 4); \
      __m128i out##_h = _mm_srai_epi32(_mm_unpackhi_epi16(_mm_setzero_si128(), (in)), 4)

   // wide add
   #define dct_wadd(out, a, b) \
      __m128i out##_l = _mm_add_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_add_epi32(a##_h, b##_h)

   // wide sub
   #define dct_wsub(out, a, b) \
      __m128i out##_l = _mm_sub_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_sub_epi32(a##_h, b##_h)

   // butterfly a/b, add bias, then shift by "s" and pack
   #define dct_bfly32o(out0, out1, a,b,bias,s) \
      { \
         __m128i abiased_l = _mm_add_epi32(a##_l, bias); \
         __m128i abiased_h = _mm_add_epi32(a##_h, bias); \
         dct_wadd(sum, abiased, b); \
         dct_wsub(dif, abiased, b); \
         out0 = _mm_packs_epi32(_mm_srai_epi32(sum_l, s), _mm_srai_epi32(sum_h, s)); \
         out1 = _mm_packs_epi32(_mm_srai_epi32(dif_l, s), _mm_srai_epi32(dif_h, s)); \
      }

   // 8-bit interleave step (for transposes)
   #define dct_interleave8(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi8(a, b); \
      b = _mm_unpackhi_epi8(tmp, b)

   // 16-bit interleave step (for transposes)
   #define dct_interleave16(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi16(a, b); \
      b = _mm_unpackhi_epi16(tmp, b)

   #define dct_pass(bias,shift) \
      { \
         /* even part */ \
         dct_rot(t2e,t3e, row2,row6, rot0_0,rot0_1); \
         __m128i sum04 = _mm_add_epi16(row0, row4); \
         __m128i dif04 = _mm_sub_epi16(row0, row4); \
         dct_widen(t0e, sum04); \
         dct_widen(t1e, dif04); \
         dct_wadd(x0, t0e, t3e); \
         dct_wsub(x3, t0e, t3e); \
         dct_wadd(x1, t1e, t2e); \
         dct_wsub(x2, t1e, t2e); \
         /* odd part */ \
         dct_rot(y0o,y2o, row7,row3, rot2_0,rot2_1); \
         dct_rot(y1o,y3o, row5,row1, rot3_0,rot3_1); \
         __m128i sum17 = _mm_add_epi16(row1, row7); \
         __m128i sum35 = _mm_add_epi16(row3, row5); \
         dct_rot(y4o,y5o, sum17,sum35, rot1_0,rot1_1); \
         dct_wadd(x4, y0o, y4o); \
         dct_wadd(x5, y1o, y5o); \
         dct_wadd(x6, y2o, y5o); \
         dct_wadd(x7, y3o, y4o); \
         dct_bfly32o(row0,row7, x0,x7,bias,shift); \
         dct_bfly32o(row1,row6, x1,x6,bias,shift); \
         dct_bfly32o(row2,row5, x2,x5,bias,shift); \
         dct_bfly32o(row3,row4, x3,x4,bias,shift); \
      }

   __m128i rot0_0 = dct_const(stbi__f2f(0.5411961f), stbi__f2f(0.5411961f) + stbi__f2f(-1.847759065f));
   __m128i rot0_1 = dct_const(stbi__f2f(0.5411961f) + stbi__f2f( 0.765366865f), stbi__f2f(0.5411961f));
   __m128i rot1_0 = dct_const(stbi__f2f(1.175875602f) + stbi__f2f(-0.899976223f), stbi__f2f(1.175875602f));
   __m128i rot1_1 = dct_const(stbi__f2f(1.175875602f), stbi__f2f(1.175875602f) + stbi__f2f(-2.562915447f));
   __m128i rot2_0 = dct_const(stbi__f2f(-1.961570560f) + stbi__f2f( 0.298631336f), stbi__f2f(-1.961570560f));
   __m128i rot2_1 = dct_const(stbi__f2f(-1.961570560f), stbi__f2f(-1.961570560f) + stbi__f2f( 3.072711026f));
   __m128i rot3_0 = dct_const(stbi__f2f(-0.390180644f) + stbi__f2f( 2.053119869f), stbi__f2f(-0.390180644f));
   __m128i rot3_1 = dct_const(stbi__f2f(-0.390180644f), stbi__f2f(-0.390180644f) + stbi__f2f( 1.501321110f));

   // rounding biases in column/row passes, see stbi__idct_block for explanation.
   __m128i bias_0 = _mm_set1_epi32(512);
   __m128i bias_1 = _mm_set1_epi32(65536 + (128<<17));

   // load
   row0 = _mm_load_si128((const __m128i *) (data + 0*8));
   row1 = _mm_load_si128((const __m128i *) (data + 1*8));
   row2 = _mm_load_si128((const __m128i *) (data + 2*8));
   row3 = _mm_load_si128((const __m128i *) (data + 3*8));
   row4 = _mm_load_si128((const __m128i *) (data + 4*8));
   row5 = _mm_load_si128((const __m128i *) (data + 5*8));
   row6 = _mm_load_si128((const __m128i *) (data + 6*8));
   row7 = _mm_load_si128((const __m128i *) (data + 7*8));

   // column pass
   dct_pass(bias_0, 10);

   {
      // 16bit 8x8 transpose pass 1
      dct_interleave16(row0, row4);
      dct_interleave16(row1, row5);
      dct_interleave16(row2, row6);
      dct_interleave16(row3, row7);

      // transpose pass 2
      dct_interleave16(row0, row2);
      dct_interleave16(row1, row3);
      dct_interleave16(row4, row6);
      dct_interleave16(row5, row7);

      // transpose pass 3
      dct_interleave16(row0, row1);
      dct_interleave16(row2, row3);
      dct_interleave16(row4, row5);
      dct_interleave16(row6, row7);
   }

   // row pass
   dct_pass(bias_1, 17);

   {
      // pack
      __m128i p0 = _mm_packus_epi16(row0, row1); // a0a1a2a3...a7b0b1b2b3...b7
      __m128i p1 = _mm_packus_epi16(row2, row3);
      __m128i p2 = _mm_packus_epi16(row4, row5);
      __m128i p3 = _mm_packus_epi16(row6, row7);

      // 8bit 8x8 transpose pass 1
      dct_interleave8(p0, p2); // a0e0a1e1...
      dct_interleave8(p1, p3); // c0g0c1g1...

      // transpose pass 2
      dct_interleave8(p0, p1); // a0c0e0g0...
      dct_interleave8(p2, p3); // b0d0f0h0...

      // transpose pass 3
      dct_interleave8(p0, p2); // a0b0c0d0...
      dct_interleave8(p1, p3); // a4b4c4d4...

      // store
      _mm_storel_epi64((__m128i *) out, p0); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p0, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p2); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p2, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p1); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p1, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p3); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p3, 0x4e));
   }

#undef dct_const
#undef dct_rot
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_interleave8
#undef dct_interleave16
#undef dct_pass
}

#endif // STBI_SSE2

#ifdef STBI_NEON

// NEON integer IDCT. should produce bit-identical
// results to the generic C version.
static void stbi__idct_simd(stbi_uc *out, int out_stride, short data[64])
{
   int16x8_t row0, row1, row2, row3, row4, row5, row6, row7;

   int16x4_t rot0_0 = vdup_n_s16(stbi__f2f(0.5411961f));
   int16x4_t rot0_1 = vdup_n_s16(stbi__f2f(-1.847759065f));
   int16x4_t rot0_2 = vdup_n_s16(stbi__f2f( 0.765366865f));
   int16x4_t rot1_0 = vdup_n_s16(stbi__f2f( 1.175875602f));
   int16x4_t rot1_1 = vdup_n_s16(stbi__f2f(-0.899976223f));
   int16x4_t rot1_2 = vdup_n_s16(stbi__f2f(-2.562915447f));
   int16x4_t rot2_0 = vdup_n_s16(stbi__f2f(-1.961570560f));
   int16x4_t rot2_1 = vdup_n_s16(stbi__f2f(-0.390180644f));
   int16x4_t rot3_0 = vdup_n_s16(stbi__f2f( 0.298631336f));
   int16x4_t rot3_1 = vdup_n_s16(stbi__f2f( 2.053119869f));
   int16x4_t rot3_2 = vdup_n_s16(stbi__f2f( 3.072711026f));
   int16x4_t rot3_3 = vdup_n_s16(stbi__f2f( 1.501321110f));

#define dct_long_mul(out, inq, coeff) \
   int32x4_t out##_l = vmull_s16(vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmull_s16(vget_high_s16(inq), coeff)

#define dct_long_mac(out, acc, inq, coeff) \
   int32x4_t out##_l = vmlal_s16(acc##_l, vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmlal_s16(acc##_h, vget_high_s16(inq), coeff)

#define dct_widen(out, inq) \
   int32x4_t out##_l = vshll_n_s16(vget_low_s16(inq), 12); \
   int32x4_t out##_h = vshll_n_s16(vget_high_s16(inq), 12)

// wide add
#define dct_wadd(out, a, b) \
   int32x4_t out##_l = vaddq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vaddq_s32(a##_h, b##_h)

// wide sub
#define dct_wsub(out, a, b) \
   int32x4_t out##_l = vsubq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vsubq_s32(a##_h, b##_h)

// butterfly a/b, then shift using "shiftop" by "s" and pack
#define dct_bfly32o(out0,out1, a,b,shiftop,s) \
   { \
      dct_wadd(sum, a, b); \
      dct_wsub(dif, a, b); \
      out0 = vcombine_s16(shiftop(sum_l, s), shiftop(sum_h, s)); \
      out1 = vcombine_s16(shiftop(dif_l, s), shiftop(dif_h, s)); \
   }

#define dct_pass(shiftop, shift) \
   { \
      /* even part */ \
      int16x8_t sum26 = vaddq_s16(row2, row6); \
      dct_long_mul(p1e, sum26, rot0_0); \
      dct_long_mac(t2e, p1e, row6, rot0_1); \
      dct_long_mac(t3e, p1e, row2, rot0_2); \
      int16x8_t sum04 = vaddq_s16(row0, row4); \
      int16x8_t dif04 = vsubq_s16(row0, row4); \
      dct_widen(t0e, sum04); \
      dct_widen(t1e, dif04); \
      dct_wadd(x0, t0e, t3e); \
      dct_wsub(x3, t0e, t3e); \
      dct_wadd(x1, t1e, t2e); \
      dct_wsub(x2, t1e, t2e); \
      /* odd part */ \
      int16x8_t sum15 = vaddq_s16(row1, row5); \
      int16x8_t sum17 = vaddq_s16(row1, row7); \
      int16x8_t sum35 = vaddq_s16(row3, row5); \
      int16x8_t sum37 = vaddq_s16(row3, row7); \
      int16x8_t sumodd = vaddq_s16(sum17, sum35); \
      dct_long_mul(p5o, sumodd, rot1_0); \
      dct_long_mac(p1o, p5o, sum17, rot1_1); \
      dct_long_mac(p2o, p5o, sum35, rot1_2); \
      dct_long_mul(p3o, sum37, rot2_0); \
      dct_long_mul(p4o, sum15, rot2_1); \
      dct_wadd(sump13o, p1o, p3o); \
      dct_wadd(sump24o, p2o, p4o); \
      dct_wadd(sump23o, p2o, p3o); \
      dct_wadd(sump14o, p1o, p4o); \
      dct_long_mac(x4, sump13o, row7, rot3_0); \
      dct_long_mac(x5, sump24o, row5, rot3_1); \
      dct_long_mac(x6, sump23o, row3, rot3_2); \
      dct_long_mac(x7, sump14o, row1, rot3_3); \
      dct_bfly32o(row0,row7, x0,x7,shiftop,shift); \
      dct_bfly32o(row1,row6, x1,x6,shiftop,shift); \
      dct_bfly32o(row2,row5, x2,x5,shiftop,shift); \
      dct_bfly32o(row3,row4, x3,x4,shiftop,shift); \
   }

   // load
   row0 = vld1q_s16(data + 0*8);
   row1 = vld1q_s16(data + 1*8);
   row2 = vld1q_s16(data + 2*8);
   row3 = vld1q_s16(data + 3*8);
   row4 = vld1q_s16(data + 4*8);
   row5 = vld1q_s16(data + 5*8);
   row6 = vld1q_s16(data + 6*8);
   row7 = vld1q_s16(data + 7*8);

   // add DC bias
   row0 = vaddq_s16(row0, vsetq_lane_s16(1024, vdupq_n_s16(0), 0));

   // column pass
   dct_pass(vrshrn_n_s32, 10);

   // 16bit 8x8 transpose
   {
// these three map to a single VTRN.16, VTRN.32, and VSWP, respectively.
// whether compilers actually get this is another story, sadly.
#define dct_trn16(x, y) { int16x8x2_t t = vtrnq_s16(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn32(x, y) { int32x4x2_t t = vtrnq_s32(vreinterpretq_s32_s16(x), vreinterpretq_s32_s16(y)); x = vreinterpretq_s16_s32(t.val[0]); y = vreinterpretq_s16_s32(t.val[1]); }
#define dct_trn64(x, y) { int16x8_t x0 = x; int16x8_t y0 = y; x = vcombine_s16(vget_low_s16(x0), vget_low_s16(y0)); y = vcombine_s16(vget_high_s16(x0), vget_high_s16(y0)); }

      // pass 1
      dct_trn16(row0, row1); // a0b0a2b2a4b4a6b6
      dct_trn16(row2, row3);
      dct_trn16(row4, row5);
      dct_trn16(row6, row7);

      // pass 2
      dct_trn32(row0, row2); // a0b0c0d0a4b4c4d4
      dct_trn32(row1, row3);
      dct_trn32(row4, row6);
      dct_trn32(row5, row7);

      // pass 3
      dct_trn64(row0, row4); // a0b0c0d0e0f0g0h0
      dct_trn64(row1, row5);
      dct_trn64(row2, row6);
      dct_trn64(row3, row7);

#undef dct_trn16
#undef dct_trn32
#undef dct_trn64
   }

   // row pass
   // vrshrn_n_s32 only supports shifts up to 16, we need
   // 17. so do a non-rounding shift of 16 first then follow
   // up with a rounding shift by 1.
   dct_pass(vshrn_n_s32, 16);

   {
      // pack and round
      uint8x8_t p0 = vqrshrun_n_s16(row0, 1);
      uint8x8_t p1 = vqrshrun_n_s16(row1, 1);
      uint8x8_t p2 = vqrshrun_n_s16(row2, 1);
      uint8x8_t p3 = vqrshrun_n_s16(row3, 1);
      uint8x8_t p4 = vqrshrun_n_s16(row4, 1);
      uint8x8_t p5 = vqrshrun_n_s16(row5, 1);
      uint8x8_t p6 = vqrshrun_n_s16(row6, 1);
      uint8x8_t p7 = vqrshrun_n_s16(row7, 1);

      // again, these can translate into one instruction, but often don't.
#define dct_trn8_8(x, y) { uint8x8x2_t t = vtrn_u8(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn8_16(x, y) { uint16x4x2_t t = vtrn_u16(vreinterpret_u16_u8(x), vreinterpret_u16_u8(y)); x = vreinterpret_u8_u16(t.val[0]); y = vreinterpret_u8_u16(t.val[1]); }
#define dct_trn8_32(x, y) { uint32x2x2_t t = vtrn_u32(vreinterpret_u32_u8(x), vreinterpret_u32_u8(y)); x = vreinterpret_u8_u32(t.val[0]); y = vreinterpret_u8_u32(t.val[1]); }

      // sadly can't use interleaved stores here since we only write
      // 8 bytes to each scan line!

      // 8x8 8-bit transpose pass 1
      dct_trn8_8(p0, p1);
      dct_trn8_8(p2, p3);
      dct_trn8_8(p4, p5);
      dct_trn8_8(p6, p7);

      // pass 2
      dct_trn8_16(p0, p2);
      dct_trn8_16(p1, p3);
      dct_trn8_16(p4, p6);
      dct_trn8_16(p5, p7);

      // pass 3
      dct_trn8_32(p0, p4);
      dct_trn8_32(p1, p5);
      dct_trn8_32(p2, p6);
      dct_trn8_32(p3, p7);

      // store
      vst1_u8(out, p0); out += out_stride;
      vst1_u8(out, p1); out += out_stride;
      vst1_u8(out, p2); out += out_stride;
      vst1_u8(out, p3); out += out_stride;
      vst1_u8(out, p4); out += out_stride;
      vst1_u8(out, p5); out += out_stride;
      vst1_u8(out, p6); out += out_stride;
      vst1_u8(out, p7);

#undef dct_trn8_8
#undef dct_trn8_16
#undef dct_trn8_32
   }

#undef dct_long_mul
#undef dct_long_mac
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_pass
}

#endif // STBI_NEON

#define STBI__MARKER_none  0xff
// if there's a pending marker from the entropy stream, return that
// otherwise, fetch from the stream and get a marker. if there's no
// marker, return 0xff, which is never a valid marker value
static stbi_uc stbi__get_marker(stbi__jpeg *j)
{
   stbi_uc x;
   if (j->marker != STBI__MARKER_none) { x = j->marker; j->marker = STBI__MARKER_none; return x; }
   x = stbi__get8(j->s);
   if (x != 0xff) return STBI__MARKER_none;
   while (x == 0xff)
      x = stbi__get8(j->s); // consume repeated 0xff fill bytes
   return x;
}

// in each scan, we'll have scan_n components, and the order
// of the components is specified by order[]
#define STBI__RESTART(x)     ((x) >= 0xd0 && (x) <= 0xd7)

// after a restart interval, stbi__jpeg_reset the entropy decoder and
// the dc prediction
static void stbi__jpeg_reset(stbi__jpeg *j)
{
   j->code_bits = 0;
   j->code_buffer = 0;
   j->nomore = 0;
   j->img_comp[0].dc_pred = j->img_comp[1].dc_pred = j->img_comp[2].dc_pred = j->img_comp[3].dc_pred = 0;
   j->marker = STBI__MARKER_none;
   j->todo = j->restart_interval ? j->restart_interval : 0x7fffffff;
   j->eob_run = 0;
   // no more than 1<<31 MCUs if no restart_interal? that's plenty safe,
   // since we don't even allow 1<<30 pixels
}

static int stbi__parse_entropy_coded_data(stbi__jpeg *z)
{
   stbi__jpeg_reset(z);
   if (!z->progressive) {
      if (z->scan_n == 1) {
         int i,j;
         STBI_SIMD_ALIGN(short, data[64]);
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               int ha = z->img_comp[n].ha;
               if (!stbi__jpeg_decode_block(z, data, z->huff_dc+z->img_comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->img_comp[n].tq])) return 0;
               z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*j*8+i*8, z->img_comp[n].w2, data);
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  // if it's NOT a restart, then just bail, so we get corrupt data
                  // rather than no data
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         STBI_SIMD_ALIGN(short, data[64]);
         for (j=0; j < z->img_mcu_y; ++j) {
            for (i=0; i < z->img_mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->img_comp[n].v; ++y) {
                     for (x=0; x < z->img_comp[n].h; ++x) {
                        int x2 = (i*z->img_comp[n].h + x)*8;
                        int y2 = (j*z->img_comp[n].v + y)*8;
                        int ha = z->img_comp[n].ha;
                        if (!stbi__jpeg_decode_block(z, data, z->huff_dc+z->img_comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->img_comp[n].tq])) return 0;
                        z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*y2+x2, z->img_comp[n].w2, data);
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   } else {
      if (z->scan_n == 1) {
         int i,j;
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);
               if (z->spec_start == 0) {
                  if (!stbi__jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->img_comp[n].hd], n))
                     return 0;
               } else {
                  int ha = z->img_comp[n].ha;
                  if (!stbi__jpeg_decode_block_prog_ac(z, data, &z->huff_ac[ha], z->fast_ac[ha]))
                     return 0;
               }
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         for (j=0; j < z->img_mcu_y; ++j) {
            for (i=0; i < z->img_mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->img_comp[n].v; ++y) {
                     for (x=0; x < z->img_comp[n].h; ++x) {
                        int x2 = (i*z->img_comp[n].h + x);
                        int y2 = (j*z->img_comp[n].v + y);
                        short *data = z->img_comp[n].coeff + 64 * (x2 + y2 * z->img_comp[n].coeff_w);
                        if (!stbi__jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->img_comp[n].hd], n))
                           return 0;
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   }
}

static void stbi__jpeg_dequantize(short *data, stbi__uint16 *dequant)
{
   int i;
   for (i=0; i < 64; ++i)
      data[i] *= dequant[i];
}

static void stbi__jpeg_finish(stbi__jpeg *z)
{
   if (z->progressive) {
      // dequantize and idct the data
      int i,j,n;
      for (n=0; n < z->s->img_n; ++n) {
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);
               stbi__jpeg_dequantize(data, z->dequant[z->img_comp[n].tq]);
               z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*j*8+i*8, z->img_comp[n].w2, data);
            }
         }
      }
   }
}

static int stbi__process_marker(stbi__jpeg *z, int m)
{
   int L;
   switch (m) {
      case STBI__MARKER_none: // no marker found
         return stbi__err("expected marker","Corrupt JPEG");

      case 0xDD: // DRI - specify restart interval
         if (stbi__get16be(z->s) != 4) return stbi__err("bad DRI len","Corrupt JPEG");
         z->restart_interval = stbi__get16be(z->s);
         return 1;

      case 0xDB: // DQT - define quantization table
         L = stbi__get16be(z->s)-2;
         while (L > 0) {
            int q = stbi__get8(z->s);
            int p = q >> 4, sixteen = (p != 0);
            int t = q & 15,i;
            if (p != 0 && p != 1) return stbi__err("bad DQT type","Corrupt JPEG");
            if (t > 3) return stbi__err("bad DQT table","Corrupt JPEG");

            for (i=0; i < 64; ++i)
               z->dequant[t][stbi__jpeg_dezigzag[i]] = (stbi__uint16)(sixteen ? stbi__get16be(z->s) : stbi__get8(z->s));
            L -= (sixteen ? 129 : 65);
         }
         return L==0;

      case 0xC4: // DHT - define huffman table
         L = stbi__get16be(z->s)-2;
         while (L > 0) {
            stbi_uc *v;
            int sizes[16],i,n=0;
            int q = stbi__get8(z->s);
            int tc = q >> 4;
            int th = q & 15;
            if (tc > 1 || th > 3) return stbi__err("bad DHT header","Corrupt JPEG");
            for (i=0; i < 16; ++i) {
               sizes[i] = stbi__get8(z->s);
               n += sizes[i];
            }
            if(n > 256) return stbi__err("bad DHT header","Corrupt JPEG"); // Loop over i < n would write past end of values!
            L -= 17;
            if (tc == 0) {
               if (!stbi__build_huffman(z->huff_dc+th, sizes)) return 0;
               v = z->huff_dc[th].values;
            } else {
               if (!stbi__build_huffman(z->huff_ac+th, sizes)) return 0;
               v = z->huff_ac[th].values;
            }
            for (i=0; i < n; ++i)
               v[i] = stbi__get8(z->s);
            if (tc != 0)
               stbi__build_fast_ac(z->fast_ac[th], z->huff_ac + th);
            L -= n;
         }
         return L==0;
   }

   // check for comment block or APP blocks
   if ((m >= 0xE0 && m <= 0xEF) || m == 0xFE) {
      L = stbi__get16be(z->s);
      if (L < 2) {
         if (m == 0xFE)
            return stbi__err("bad COM len","Corrupt JPEG");
         else
            return stbi__err("bad APP len","Corrupt JPEG");
      }
      L -= 2;

      if (m == 0xE0 && L >= 5) { // JFIF APP0 segment
         static const unsigned char tag[5] = {'J','F','I','F','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 5; ++i)
            if (stbi__get8(z->s) != tag[i])
               ok = 0;
         L -= 5;
         if (ok)
            z->jfif = 1;
      } else if (m == 0xEE && L >= 12) { // Adobe APP14 segment
         static const unsigned char tag[6] = {'A','d','o','b','e','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 6; ++i)
            if (stbi__get8(z->s) != tag[i])
               ok = 0;
         L -= 6;
         if (ok) {
            stbi__get8(z->s); // version
            stbi__get16be(z->s); // flags0
            stbi__get16be(z->s); // flags1
            z->app14_color_transform = stbi__get8(z->s); // color transform
            L -= 6;
         }
      }

      stbi__skip(z->s, L);
      return 1;
   }

   return stbi__err("unknown marker","Corrupt JPEG");
}

// after we see SOS
static int stbi__process_scan_header(stbi__jpeg *z)
{
   int i;
   int Ls = stbi__get16be(z->s);
   z->scan_n = stbi__get8(z->s);
   if (z->scan_n < 1 || z->scan_n > 4 || z->scan_n > (int) z->s->img_n) return stbi__err("bad SOS component count","Corrupt JPEG");
   if (Ls != 6+2*z->scan_n) return stbi__err("bad SOS len","Corrupt JPEG");
   for (i=0; i < z->scan_n; ++i) {
      int id = stbi__get8(z->s), which;
      int q = stbi__get8(z->s);
      for (which = 0; which < z->s->img_n; ++which)
         if (z->img_comp[which].id == id)
            break;
      if (which == z->s->img_n) return 0; // no match
      z->img_comp[which].hd = q >> 4;   if (z->img_comp[which].hd > 3) return stbi__err("bad DC huff","Corrupt JPEG");
      z->img_comp[which].ha = q & 15;   if (z->img_comp[which].ha > 3) return stbi__err("bad AC huff","Corrupt JPEG");
      z->order[i] = which;
   }

   {
      int aa;
      z->spec_start = stbi__get8(z->s);
      z->spec_end   = stbi__get8(z->s); // should be 63, but might be 0
      aa = stbi__get8(z->s);
      z->succ_high = (aa >> 4);
      z->succ_low  = (aa & 15);
      if (z->progressive) {
         if (z->spec_start > 63 || z->spec_end > 63  || z->spec_start > z->spec_end || z->succ_high > 13 || z->succ_low > 13)
            return stbi__err("bad SOS", "Corrupt JPEG");
      } else {
         if (z->spec_start != 0) return stbi__err("bad SOS","Corrupt JPEG");
         if (z->succ_high != 0 || z->succ_low != 0) return stbi__err("bad SOS","Corrupt JPEG");
         z->spec_end = 63;
      }
   }

   return 1;
}

static int stbi__free_jpeg_components(stbi__jpeg *z, int ncomp, int why)
{
   int i;
   for (i=0; i < ncomp; ++i) {
      if (z->img_comp[i].raw_data) {
         STBI_FREE(z->img_comp[i].raw_data);
         z->img_comp[i].raw_data = NULL;
         z->img_comp[i].data = NULL;
      }
      if (z->img_comp[i].raw_coeff) {
         STBI_FREE(z->img_comp[i].raw_coeff);
         z->img_comp[i].raw_coeff = 0;
         z->img_comp[i].coeff = 0;
      }
      if (z->img_comp[i].linebuf) {
         STBI_FREE(z->img_comp[i].linebuf);
         z->img_comp[i].linebuf = NULL;
      }
   }
   return why;
}

static int stbi__process_frame_header(stbi__jpeg *z, int scan)
{
   stbi__context *s = z->s;
   int Lf,p,i,q, h_max=1,v_max=1,c;
   Lf = stbi__get16be(s);         if (Lf < 11) return stbi__err("bad SOF len","Corrupt JPEG"); // JPEG
   p  = stbi__get8(s);            if (p != 8) return stbi__err("only 8-bit","JPEG format not supported: 8-bit only"); // JPEG baseline
   s->img_y = stbi__get16be(s);   if (s->img_y == 0) return stbi__err("no header height", "JPEG format not supported: delayed height"); // Legal, but we don't handle it--but neither does IJG
   s->img_x = stbi__get16be(s);   if (s->img_x == 0) return stbi__err("0 width","Corrupt JPEG"); // JPEG requires
   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   c = stbi__get8(s);
   if (c != 3 && c != 1 && c != 4) return stbi__err("bad component count","Corrupt JPEG");
   s->img_n = c;
   for (i=0; i < c; ++i) {
      z->img_comp[i].data = NULL;
      z->img_comp[i].linebuf = NULL;
   }

   if (Lf != 8+3*s->img_n) return stbi__err("bad SOF len","Corrupt JPEG");

   z->rgb = 0;
   for (i=0; i < s->img_n; ++i) {
      static const unsigned char rgb[3] = { 'R', 'G', 'B' };
      z->img_comp[i].id = stbi__get8(s);
      if (s->img_n == 3 && z->img_comp[i].id == rgb[i])
         ++z->rgb;
      q = stbi__get8(s);
      z->img_comp[i].h = (q >> 4);  if (!z->img_comp[i].h || z->img_comp[i].h > 4) return stbi__err("bad H","Corrupt JPEG");
      z->img_comp[i].v = q & 15;    if (!z->img_comp[i].v || z->img_comp[i].v > 4) return stbi__err("bad V","Corrupt JPEG");
      z->img_comp[i].tq = stbi__get8(s);  if (z->img_comp[i].tq > 3) return stbi__err("bad TQ","Corrupt JPEG");
   }

   if (scan != STBI__SCAN_load) return 1;

   if (!stbi__mad3sizes_valid(s->img_x, s->img_y, s->img_n, 0)) return stbi__err("too large", "Image too large to decode");

   for (i=0; i < s->img_n; ++i) {
      if (z->img_comp[i].h > h_max) h_max = z->img_comp[i].h;
      if (z->img_comp[i].v > v_max) v_max = z->img_comp[i].v;
   }

   // check that plane subsampling factors are integer ratios; our resamplers can't deal with fractional ratios
   // and I've never seen a non-corrupted JPEG file actually use them
   for (i=0; i < s->img_n; ++i) {
      if (h_max % z->img_comp[i].h != 0) return stbi__err("bad H","Corrupt JPEG");
      if (v_max % z->img_comp[i].v != 0) return stbi__err("bad V","Corrupt JPEG");
   }

   // compute interleaved mcu info
   z->img_h_max = h_max;
   z->img_v_max = v_max;
   z->img_mcu_w = h_max * 8;
   z->img_mcu_h = v_max * 8;
   // these sizes can't be more than 17 bits
   z->img_mcu_x = (s->img_x + z->img_mcu_w-1) / z->img_mcu_w;
   z->img_mcu_y = (s->img_y + z->img_mcu_h-1) / z->img_mcu_h;

   for (i=0; i < s->img_n; ++i) {
      // number of effective pixels (e.g. for non-interleaved MCU)
      z->img_comp[i].x = (s->img_x * z->img_comp[i].h + h_max-1) / h_max;
      z->img_comp[i].y = (s->img_y * z->img_comp[i].v + v_max-1) / v_max;
      // to simplify generation, we'll allocate enough memory to decode
      // the bogus oversized data from using interleaved MCUs and their
      // big blocks (e.g. a 16x16 iMCU on an image of width 33); we won't
      // discard the extra data until colorspace conversion
      //
      // img_mcu_x, img_mcu_y: <=17 bits; comp[i].h and .v are <=4 (checked earlier)
      // so these muls can't overflow with 32-bit ints (which we require)
      z->img_comp[i].w2 = z->img_mcu_x * z->img_comp[i].h * 8;
      z->img_comp[i].h2 = z->img_mcu_y * z->img_comp[i].v * 8;
      z->img_comp[i].coeff = 0;
      z->img_comp[i].raw_coeff = 0;
      z->img_comp[i].linebuf = NULL;
      z->img_comp[i].raw_data = stbi__malloc_mad2(z->img_comp[i].w2, z->img_comp[i].h2, 15);
      if (z->img_comp[i].raw_data == NULL)
         return stbi__free_jpeg_components(z, i+1, stbi__err("outofmem", "Out of memory"));
      // align blocks for idct using mmx/sse
      z->img_comp[i].data = (stbi_uc*) (((size_t) z->img_comp[i].raw_data + 15) & ~15);
      if (z->progressive) {
         // w2, h2 are multiples of 8 (see above)
         z->img_comp[i].coeff_w = z->img_comp[i].w2 / 8;
         z->img_comp[i].coeff_h = z->img_comp[i].h2 / 8;
         z->img_comp[i].raw_coeff = stbi__malloc_mad3(z->img_comp[i].w2, z->img_comp[i].h2, sizeof(short), 15);
         if (z->img_comp[i].raw_coeff == NULL)
            return stbi__free_jpeg_components(z, i+1, stbi__err("outofmem", "Out of memory"));
         z->img_comp[i].coeff = (short*) (((size_t) z->img_comp[i].raw_coeff + 15) & ~15);
      }
   }

   return 1;
}

// use comparisons since in some cases we handle more than one case (e.g. SOF)
#define stbi__DNL(x)         ((x) == 0xdc)
#define stbi__SOI(x)         ((x) == 0xd8)
#define stbi__EOI(x)         ((x) == 0xd9)
#define stbi__SOF(x)         ((x) == 0xc0 || (x) == 0xc1 || (x) == 0xc2)
#define stbi__SOS(x)         ((x) == 0xda)

#define stbi__SOF_progressive(x)   ((x) == 0xc2)

static int stbi__decode_jpeg_header(stbi__jpeg *z, int scan)
{
   int m;
   z->jfif = 0;
   z->app14_color_transform = -1; // valid values are 0,1,2
   z->marker = STBI__MARKER_none; // initialize cached marker to empty
   m = stbi__get_marker(z);
   if (!stbi__SOI(m)) return stbi__err("no SOI","Corrupt JPEG");
   if (scan == STBI__SCAN_type) return 1;
   m = stbi__get_marker(z);
   while (!stbi__SOF(m)) {
      if (!stbi__process_marker(z,m)) return 0;
      m = stbi__get_marker(z);
      while (m == STBI__MARKER_none) {
         // some files have extra padding after their blocks, so ok, we'll scan
         if (stbi__at_eof(z->s)) return stbi__err("no SOF", "Corrupt JPEG");
         m = stbi__get_marker(z);
      }
   }
   z->progressive = stbi__SOF_progressive(m);
   if (!stbi__process_frame_header(z, scan)) return 0;
   return 1;
}

static stbi_uc stbi__skip_jpeg_junk_at_end(stbi__jpeg *j)
{
   // some JPEGs have junk at end, skip over it but if we find what looks
   // like a valid marker, resume there
   while (!stbi__at_eof(j->s)) {
      stbi_uc x = stbi__get8(j->s);
      while (x == 0xff) { // might be a marker
         if (stbi__at_eof(j->s)) return STBI__MARKER_none;
         x = stbi__get8(j->s);
         if (x != 0x00 && x != 0xff) {
            // not a stuffed zero or lead-in to another marker, looks
            // like an actual marker, return it
            return x;
         }
         // stuffed zero has x=0 now which ends the loop, meaning we go
         // back to regular scan loop.
         // repeated 0xff keeps trying to read the next byte of the marker.
      }
   }
   return STBI__MARKER_none;
}

// decode image to YCbCr format
static int stbi__decode_jpeg_image(stbi__jpeg *j)
{
   int m;
   for (m = 0; m < 4; m++) {
      j->img_comp[m].raw_data = NULL;
      j->img_comp[m].raw_coeff = NULL;
   }
   j->restart_interval = 0;
   if (!stbi__decode_jpeg_header(j, STBI__SCAN_load)) return 0;
   m = stbi__get_marker(j);
   while (!stbi__EOI(m)) {
      if (stbi__SOS(m)) {
         if (!stbi__process_scan_header(j)) return 0;
         if (!stbi__parse_entropy_coded_data(j)) return 0;
         if (j->marker == STBI__MARKER_none ) {
         j->marker = stbi__skip_jpeg_junk_at_end(j);
            // if we reach eof without hitting a marker, stbi__get_marker() below will fail and we'll eventually return 0
         }
         m = stbi__get_marker(j);
         if (STBI__RESTART(m))
            m = stbi__get_marker(j);
      } else if (stbi__DNL(m)) {
         int Ld = stbi__get16be(j->s);
         stbi__uint32 NL = stbi__get16be(j->s);
         if (Ld != 4) return stbi__err("bad DNL len", "Corrupt JPEG");
         if (NL != j->s->img_y) return stbi__err("bad DNL height", "Corrupt JPEG");
         m = stbi__get_marker(j);
      } else {
         if (!stbi__process_marker(j, m)) return 1;
         m = stbi__get_marker(j);
      }
   }
   if (j->progressive)
      stbi__jpeg_finish(j);
   return 1;
}

// static jfif-centered resampling (across block boundaries)

typedef stbi_uc *(*resample_row_func)(stbi_uc *out, stbi_uc *in0, stbi_uc *in1,
                                    int w, int hs);

#define stbi__div4(x) ((stbi_uc) ((x) >> 2))

static stbi_uc *resample_row_1(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   STBI_NOTUSED(out);
   STBI_NOTUSED(in_far);
   STBI_NOTUSED(w);
   STBI_NOTUSED(hs);
   return in_near;
}

static stbi_uc* stbi__resample_row_v_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate two samples vertically for every one in input
   int i;
   STBI_NOTUSED(hs);
   for (i=0; i < w; ++i)
      out[i] = stbi__div4(3*in_near[i] + in_far[i] + 2);
   return out;
}

static stbi_uc*  stbi__resample_row_h_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate two samples horizontally for every one in input
   int i;
   stbi_uc *input = in_near;

   if (w == 1) {
      // if only one sample, can't do any interpolation
      out[0] = out[1] = input[0];
      return out;
   }

   out[0] = input[0];
   out[1] = stbi__div4(input[0]*3 + input[1] + 2);
   for (i=1; i < w-1; ++i) {
      int n = 3*input[i]+2;
      out[i*2+0] = stbi__div4(n+input[i-1]);
      out[i*2+1] = stbi__div4(n+input[i+1]);
   }
   out[i*2+0] = stbi__div4(input[w-2]*3 + input[w-1] + 2);
   out[i*2+1] = input[w-1];

   STBI_NOTUSED(in_far);
   STBI_NOTUSED(hs);

   return out;
}

#define stbi__div16(x) ((stbi_uc) ((x) >> 4))

static stbi_uc *stbi__resample_row_hv_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate 2x2 samples for every one in input
   int i,t0,t1;
   if (w == 1) {
      out[0] = out[1] = stbi__div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   out[0] = stbi__div4(t1+2);
   for (i=1; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = stbi__div16(3*t0 + t1 + 8);
      out[i*2  ] = stbi__div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = stbi__div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static stbi_uc *stbi__resample_row_hv_2_simd(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate 2x2 samples for every one in input
   int i=0,t0,t1;

   if (w == 1) {
      out[0] = out[1] = stbi__div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   // process groups of 8 pixels for as long as we can.
   // note we can't handle the last pixel in a row in this loop
   // because we need to handle the filter boundary conditions.
   for (; i < ((w-1) & ~7); i += 8) {
#if defined(STBI_SSE2)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      __m128i zero  = _mm_setzero_si128();
      __m128i farb  = _mm_loadl_epi64((__m128i *) (in_far + i));
      __m128i nearb = _mm_loadl_epi64((__m128i *) (in_near + i));
      __m128i farw  = _mm_unpacklo_epi8(farb, zero);
      __m128i nearw = _mm_unpacklo_epi8(nearb, zero);
      __m128i diff  = _mm_sub_epi16(farw, nearw);
      __m128i nears = _mm_slli_epi16(nearw, 2);
      __m128i curr  = _mm_add_epi16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      __m128i prv0 = _mm_slli_si128(curr, 2);
      __m128i nxt0 = _mm_srli_si128(curr, 2);
      __m128i prev = _mm_insert_epi16(prv0, t1, 0);
      __m128i next = _mm_insert_epi16(nxt0, 3*in_near[i+8] + in_far[i+8], 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      __m128i bias  = _mm_set1_epi16(8);
      __m128i curs = _mm_slli_epi16(curr, 2);
      __m128i prvd = _mm_sub_epi16(prev, curr);
      __m128i nxtd = _mm_sub_epi16(next, curr);
      __m128i curb = _mm_add_epi16(curs, bias);
      __m128i even = _mm_add_epi16(prvd, curb);
      __m128i odd  = _mm_add_epi16(nxtd, curb);

      // interleave even and odd pixels, then undo scaling.
      __m128i int0 = _mm_unpacklo_epi16(even, odd);
      __m128i int1 = _mm_unpackhi_epi16(even, odd);
      __m128i de0  = _mm_srli_epi16(int0, 4);
      __m128i de1  = _mm_srli_epi16(int1, 4);

      // pack and write output
      __m128i outv = _mm_packus_epi16(de0, de1);
      _mm_storeu_si128((__m128i *) (out + i*2), outv);
#elif defined(STBI_NEON)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      uint8x8_t farb  = vld1_u8(in_far + i);
      uint8x8_t nearb = vld1_u8(in_near + i);
      int16x8_t diff  = vreinterpretq_s16_u16(vsubl_u8(farb, nearb));
      int16x8_t nears = vreinterpretq_s16_u16(vshll_n_u8(nearb, 2));
      int16x8_t curr  = vaddq_s16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      int16x8_t prv0 = vextq_s16(curr, curr, 7);
      int16x8_t nxt0 = vextq_s16(curr, curr, 1);
      int16x8_t prev = vsetq_lane_s16(t1, prv0, 0);
      int16x8_t next = vsetq_lane_s16(3*in_near[i+8] + in_far[i+8], nxt0, 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      int16x8_t curs = vshlq_n_s16(curr, 2);
      int16x8_t prvd = vsubq_s16(prev, curr);
      int16x8_t nxtd = vsubq_s16(next, curr);
      int16x8_t even = vaddq_s16(curs, prvd);
      int16x8_t odd  = vaddq_s16(curs, nxtd);

      // undo scaling and round, then store with even/odd phases interleaved
      uint8x8x2_t o;
      o.val[0] = vqrshrun_n_s16(even, 4);
      o.val[1] = vqrshrun_n_s16(odd,  4);
      vst2_u8(out + i*2, o);
#endif

      // "previous" value for next iter
      t1 = 3*in_near[i+7] + in_far[i+7];
   }

   t0 = t1;
   t1 = 3*in_near[i] + in_far[i];
   out[i*2] = stbi__div16(3*t1 + t0 + 8);

   for (++i; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = stbi__div16(3*t0 + t1 + 8);
      out[i*2  ] = stbi__div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = stbi__div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}
#endif

static stbi_uc *stbi__resample_row_generic(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // resample with nearest-neighbor
   int i,j;
   STBI_NOTUSED(in_far);
   for (i=0; i < w; ++i)
      for (j=0; j < hs; ++j)
         out[i*hs+j] = in_near[i];
   return out;
}

// this is a reduced-precision calculation of YCbCr-to-RGB introduced
// to make sure the code produces the same results in both SIMD and scalar
#define stbi__float2fixed(x)  (((int) ((x) * 4096.0f + 0.5f)) << 8)
static void stbi__YCbCr_to_RGB_row(stbi_uc *out, const stbi_uc *y, const stbi_uc *pcb, const stbi_uc *pcr, int count, int step)
{
   int i;
   for (i=0; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed +  cr* stbi__float2fixed(1.40200f);
      g = y_fixed + (cr*-stbi__float2fixed(0.71414f)) + ((cb*-stbi__float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                     +   cb* stbi__float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (stbi_uc)r;
      out[1] = (stbi_uc)g;
      out[2] = (stbi_uc)b;
      out[3] = 255;
      out += step;
   }
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static void stbi__YCbCr_to_RGB_simd(stbi_uc *out, stbi_uc const *y, stbi_uc const *pcb, stbi_uc const *pcr, int count, int step)
{
   int i = 0;

#ifdef STBI_SSE2
   // step == 3 is pretty ugly on the final interleave, and i'm not convinced
   // it's useful in practice (you wouldn't use it for textures, for example).
   // so just accelerate step == 4 case.
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      __m128i signflip  = _mm_set1_epi8(-0x80);
      __m128i cr_const0 = _mm_set1_epi16(   (short) ( 1.40200f*4096.0f+0.5f));
      __m128i cr_const1 = _mm_set1_epi16( - (short) ( 0.71414f*4096.0f+0.5f));
      __m128i cb_const0 = _mm_set1_epi16( - (short) ( 0.34414f*4096.0f+0.5f));
      __m128i cb_const1 = _mm_set1_epi16(   (short) ( 1.77200f*4096.0f+0.5f));
      __m128i y_bias = _mm_set1_epi8((char) (unsigned char) 128);
      __m128i xw = _mm_set1_epi16(255); // alpha channel

      for (; i+7 < count; i += 8) {
         // load
         __m128i y_bytes = _mm_loadl_epi64((__m128i *) (y+i));
         __m128i cr_bytes = _mm_loadl_epi64((__m128i *) (pcr+i));
         __m128i cb_bytes = _mm_loadl_epi64((__m128i *) (pcb+i));
         __m128i cr_biased = _mm_xor_si128(cr_bytes, signflip); // -128
         __m128i cb_biased = _mm_xor_si128(cb_bytes, signflip); // -128

         // unpack to short (and left-shift cr, cb by 8)
         __m128i yw  = _mm_unpacklo_epi8(y_bias, y_bytes);
         __m128i crw = _mm_unpacklo_epi8(_mm_setzero_si128(), cr_biased);
         __m128i cbw = _mm_unpacklo_epi8(_mm_setzero_si128(), cb_biased);

         // color transform
         __m128i yws = _mm_srli_epi16(yw, 4);
         __m128i cr0 = _mm_mulhi_epi16(cr_const0, crw);
         __m128i cb0 = _mm_mulhi_epi16(cb_const0, cbw);
         __m128i cb1 = _mm_mulhi_epi16(cbw, cb_const1);
         __m128i cr1 = _mm_mulhi_epi16(crw, cr_const1);
         __m128i rws = _mm_add_epi16(cr0, yws);
         __m128i gwt = _mm_add_epi16(cb0, yws);
         __m128i bws = _mm_add_epi16(yws, cb1);
         __m128i gws = _mm_add_epi16(gwt, cr1);

         // descale
         __m128i rw = _mm_srai_epi16(rws, 4);
         __m128i bw = _mm_srai_epi16(bws, 4);
         __m128i gw = _mm_srai_epi16(gws, 4);

         // back to byte, set up for transpose
         __m128i brb = _mm_packus_epi16(rw, bw);
         __m128i gxb = _mm_packus_epi16(gw, xw);

         // transpose to interleave channels
         __m128i t0 = _mm_unpacklo_epi8(brb, gxb);
         __m128i t1 = _mm_unpackhi_epi8(brb, gxb);
         __m128i o0 = _mm_unpacklo_epi16(t0, t1);
         __m128i o1 = _mm_unpackhi_epi16(t0, t1);

         // store
         _mm_storeu_si128((__m128i *) (out + 0), o0);
         _mm_storeu_si128((__m128i *) (out + 16), o1);
         out += 32;
      }
   }
#endif

#ifdef STBI_NEON
   // in this version, step=3 support would be easy to add. but is there demand?
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      uint8x8_t signflip = vdup_n_u8(0x80);
      int16x8_t cr_const0 = vdupq_n_s16(   (short) ( 1.40200f*4096.0f+0.5f));
      int16x8_t cr_const1 = vdupq_n_s16( - (short) ( 0.71414f*4096.0f+0.5f));
      int16x8_t cb_const0 = vdupq_n_s16( - (short) ( 0.34414f*4096.0f+0.5f));
      int16x8_t cb_const1 = vdupq_n_s16(   (short) ( 1.77200f*4096.0f+0.5f));

      for (; i+7 < count; i += 8) {
         // load
         uint8x8_t y_bytes  = vld1_u8(y + i);
         uint8x8_t cr_bytes = vld1_u8(pcr + i);
         uint8x8_t cb_bytes = vld1_u8(pcb + i);
         int8x8_t cr_biased = vreinterpret_s8_u8(vsub_u8(cr_bytes, signflip));
         int8x8_t cb_biased = vreinterpret_s8_u8(vsub_u8(cb_bytes, signflip));

         // expand to s16
         int16x8_t yws = vreinterpretq_s16_u16(vshll_n_u8(y_bytes, 4));
         int16x8_t crw = vshll_n_s8(cr_biased, 7);
         int16x8_t cbw = vshll_n_s8(cb_biased, 7);

         // color transform
         int16x8_t cr0 = vqdmulhq_s16(crw, cr_const0);
         int16x8_t cb0 = vqdmulhq_s16(cbw, cb_const0);
         int16x8_t cr1 = vqdmulhq_s16(crw, cr_const1);
         int16x8_t cb1 = vqdmulhq_s16(cbw, cb_const1);
         int16x8_t rws = vaddq_s16(yws, cr0);
         int16x8_t gws = vaddq_s16(vaddq_s16(yws, cb0), cr1);
         int16x8_t bws = vaddq_s16(yws, cb1);

         // undo scaling, round, convert to byte
         uint8x8x4_t o;
         o.val[0] = vqrshrun_n_s16(rws, 4);
         o.val[1] = vqrshrun_n_s16(gws, 4);
         o.val[2] = vqrshrun_n_s16(bws, 4);
         o.val[3] = vdup_n_u8(255);

         // store, interleaving r/g/b/a
         vst4_u8(out, o);
         out += 8*4;
      }
   }
#endif

   for (; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed + cr* stbi__float2fixed(1.40200f);
      g = y_fixed + cr*-stbi__float2fixed(0.71414f) + ((cb*-stbi__float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                   +   cb* stbi__float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (stbi_uc)r;
      out[1] = (stbi_uc)g;
      out[2] = (stbi_uc)b;
      out[3] = 255;
      out += step;
   }
}
#endif

// set up the kernels
static void stbi__setup_jpeg(stbi__jpeg *j)
{
   j->idct_block_kernel = stbi__idct_block;
   j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_row;
   j->resample_row_hv_2_kernel = stbi__resample_row_hv_2;

#ifdef STBI_SSE2
   if (stbi__sse2_available()) {
      j->idct_block_kernel = stbi__idct_simd;
      j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_simd;
      j->resample_row_hv_2_kernel = stbi__resample_row_hv_2_simd;
   }
#endif

#ifdef STBI_NEON
   j->idct_block_kernel = stbi__idct_simd;
   j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_simd;
   j->resample_row_hv_2_kernel = stbi__resample_row_hv_2_simd;
#endif
}

// clean up the temporary component buffers
static void stbi__cleanup_jpeg(stbi__jpeg *j)
{
   stbi__free_jpeg_components(j, j->s->img_n, 0);
}

typedef struct
{
   resample_row_func resample;
   stbi_uc *line0,*line1;
   int hs,vs;   // expansion factor in each axis
   int w_lores; // horizontal pixels pre-expansion
   int ystep;   // how far through vertical expansion we are
   int ypos;    // which pre-expansion row we're on
} stbi__resample;

// fast 0..255 * 0..255 => 0..255 rounded multiplication
static stbi_uc stbi__blinn_8x8(stbi_uc x, stbi_uc y)
{
   unsigned int t = x*y + 128;
   return (stbi_uc) ((t + (t >>8)) >> 8);
}

static stbi_uc *load_jpeg_image(stbi__jpeg *z, int *out_x, int *out_y, int *comp, int req_comp)
{
   int n, decode_n, is_rgb;
   z->s->img_n = 0; // make stbi__cleanup_jpeg safe

   // validate req_comp
   if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");

   // load a jpeg image from whichever source, but leave in YCbCr format
   if (!stbi__decode_jpeg_image(z)) { stbi__cleanup_jpeg(z); return NULL; }

   // determine actual number of components to generate
   n = req_comp ? req_comp : z->s->img_n >= 3 ? 3 : 1;

   is_rgb = z->s->img_n == 3 && (z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif));

   if (z->s->img_n == 3 && n < 3 && !is_rgb)
      decode_n = 1;
   else
      decode_n = z->s->img_n;

   // nothing to do if no components requested; check this now to avoid
   // accessing uninitialized coutput[0] later
   if (decode_n <= 0) { stbi__cleanup_jpeg(z); return NULL; }

   // resample and color-convert
   {
      int k;
      unsigned int i,j;
      stbi_uc *output;
      stbi_uc *coutput[4] = { NULL, NULL, NULL, NULL };

      stbi__resample res_comp[4];

      for (k=0; k < decode_n; ++k) {
         stbi__resample *r = &res_comp[k];

         // allocate line buffer big enough for upsampling off the edges
         // with upsample factor of 4
         z->img_comp[k].linebuf = (stbi_uc *) stbi__malloc(z->s->img_x + 3);
         if (!z->img_comp[k].linebuf) { stbi__cleanup_jpeg(z); return stbi__errpuc("outofmem", "Out of memory"); }

         r->hs      = z->img_h_max / z->img_comp[k].h;
         r->vs      = z->img_v_max / z->img_comp[k].v;
         r->ystep   = r->vs >> 1;
         r->w_lores = (z->s->img_x + r->hs-1) / r->hs;
         r->ypos    = 0;
         r->line0   = r->line1 = z->img_comp[k].data;

         if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
         else if (r->hs == 1 && r->vs == 2) r->resample = stbi__resample_row_v_2;
         else if (r->hs == 2 && r->vs == 1) r->resample = stbi__resample_row_h_2;
         else if (r->hs == 2 && r->vs == 2) r->resample = z->resample_row_hv_2_kernel;
         else                               r->resample = stbi__resample_row_generic;
      }

      // can't error after this so, this is safe
      output = (stbi_uc *) stbi__malloc_mad3(n, z->s->img_x, z->s->img_y, 1);
      if (!output) { stbi__cleanup_jpeg(z); return stbi__errpuc("outofmem", "Out of memory"); }

      // now go ahead and resample
      for (j=0; j < z->s->img_y; ++j) {
         stbi_uc *out = output + n * z->s->img_x * j;
         for (k=0; k < decode_n; ++k) {
            stbi__resample *r = &res_comp[k];
            int y_bot = r->ystep >= (r->vs >> 1);
            coutput[k] = r->resample(z->img_comp[k].linebuf,
                                     y_bot ? r->line1 : r->line0,
                                     y_bot ? r->line0 : r->line1,
                                     r->w_lores, r->hs);
            if (++r->ystep >= r->vs) {
               r->ystep = 0;
               r->line0 = r->line1;
               if (++r->ypos < z->img_comp[k].y)
                  r->line1 += z->img_comp[k].w2;
            }
         }
         if (n >= 3) {
            stbi_uc *y = coutput[0];
            if (z->s->img_n == 3) {
               if (is_rgb) {
                  for (i=0; i < z->s->img_x; ++i) {
                     out[0] = y[i];
                     out[1] = coutput[1][i];
                     out[2] = coutput[2][i];
                     out[3] = 255;
                     out += n;
                  }
               } else {
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
               }
            } else if (z->s->img_n == 4) {
               if (z->app14_color_transform == 0) { // CMYK
                  for (i=0; i < z->s->img_x; ++i) {
                     stbi_uc m = coutput[3][i];
                     out[0] = stbi__blinn_8x8(coutput[0][i], m);
                     out[1] = stbi__blinn_8x8(coutput[1][i], m);
                     out[2] = stbi__blinn_8x8(coutput[2][i], m);
                     out[3] = 255;
                     out += n;
                  }
               } else if (z->app14_color_transform == 2) { // YCCK
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
                  for (i=0; i < z->s->img_x; ++i) {
                     stbi_uc m = coutput[3][i];
                     out[0] = stbi__blinn_8x8(255 - out[0], m);
                     out[1] = stbi__blinn_8x8(255 - out[1], m);
                     out[2] = stbi__blinn_8x8(255 - out[2], m);
                     out += n;
                  }
               } else { // YCbCr + alpha?  Ignore the fourth channel for now
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
               }
            } else
               for (i=0; i < z->s->img_x; ++i) {
                  out[0] = out[1] = out[2] = y[i];
                  out[3] = 255; // not used if n==3
                  out += n;
               }
         } else {
            if (is_rgb) {
               if (n == 1)
                  for (i=0; i < z->s->img_x; ++i)
                     *out++ = stbi__compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
               else {
                  for (i=0; i < z->s->img_x; ++i, out += 2) {
                     out[0] = stbi__compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
                     out[1] = 255;
                  }
               }
            } else if (z->s->img_n == 4 && z->app14_color_transform == 0) {
               for (i=0; i < z->s->img_x; ++i) {
                  stbi_uc m = coutput[3][i];
                  stbi_uc r = stbi__blinn_8x8(coutput[0][i], m);
                  stbi_uc g = stbi__blinn_8x8(coutput[1][i], m);
                  stbi_uc b = stbi__blinn_8x8(coutput[2][i], m);
                  out[0] = stbi__compute_y(r, g, b);
                  out[1] = 255;
                  out += n;
               }
            } else if (z->s->img_n == 4 && z->app14_color_transform == 2) {
               for (i=0; i < z->s->img_x; ++i) {
                  out[0] = stbi__blinn_8x8(255 - coutput[0][i], coutput[3][i]);
                  out[1] = 255;
                  out += n;
               }
            } else {
               stbi_uc *y = coutput[0];
               if (n == 1)
                  for (i=0; i < z->s->img_x; ++i) out[i] = y[i];
               else
                  for (i=0; i < z->s->img_x; ++i) { *out++ = y[i]; *out++ = 255; }
            }
         }
      }
      stbi__cleanup_jpeg(z);
      *out_x = z->s->img_x;
      *out_y = z->s->img_y;
      if (comp) *comp = z->s->img_n >= 3 ? 3 : 1; // report original components, not output
      return output;
   }
}

static void *stbi__jpeg_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   unsigned char* result;
   stbi__jpeg* j = (stbi__jpeg*) stbi__malloc(sizeof(stbi__jpeg));
   if (!j) return stbi__errpuc("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   STBI_NOTUSED(ri);
   j->s = s;
   stbi__setup_jpeg(j);
   result = load_jpeg_image(j, x,y,comp,req_comp);
   STBI_FREE(j);
   return result;
}

static int stbi__jpeg_test(stbi__context *s)
{
   int r;
   stbi__jpeg* j = (stbi__jpeg*)stbi__malloc(sizeof(stbi__jpeg));
   if (!j) return stbi__err("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   j->s = s;
   stbi__setup_jpeg(j);
   r = stbi__decode_jpeg_header(j, STBI__SCAN_type);
   stbi__rewind(s);
   STBI_FREE(j);
   return r;
}

static int stbi__jpeg_info_raw(stbi__jpeg *j, int *x, int *y, int *comp)
{
   if (!stbi__decode_jpeg_header(j, STBI__SCAN_header)) {
      stbi__rewind( j->s );
      return 0;
   }
   if (x) *x = j->s->img_x;
   if (y) *y = j->s->img_y;
   if (comp) *comp = j->s->img_n >= 3 ? 3 : 1;
   return 1;
}

static int stbi__jpeg_info(stbi__context *s, int *x, int *y, int *comp)
{
   int result;
   stbi__jpeg* j = (stbi__jpeg*) (stbi__malloc(sizeof(stbi__jpeg)));
   if (!j) return stbi__err("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   j->s = s;
   result = stbi__jpeg_info_raw(j, x, y, comp);
   STBI_FREE(j);
   return result;
}
#endif

// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman

#ifndef STBI_NO_ZLIB

// fast-way is faster to check than jpeg huffman, but slow way is slower
#define STBI__ZFAST_BITS  9 // accelerate all cases in default tables
#define STBI__ZFAST_MASK  ((1 << STBI__ZFAST_BITS) - 1)
#define STBI__ZNSYMS 288 // number of symbols in literal/length alphabet

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
typedef struct
{
   stbi__uint16 fast[1 << STBI__ZFAST_BITS];
   stbi__uint16 firstcode[16];
   int maxcode[17];
   stbi__uint16 firstsymbol[16];
   stbi_uc  size[STBI__ZNSYMS];
   stbi__uint16 value[STBI__ZNSYMS];
} stbi__zhuffman;

stbi_inline static int stbi__bitreverse16(int n)
{
  n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
  n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
  n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
  n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
  return n;
}

stbi_inline static int stbi__bit_reverse(int v, int bits)
{
   STBI_ASSERT(bits <= 16);
   // to bit reverse n bits, reverse 16 and shift
   // e.g. 11 bits, bit reverse and shift away 5
   return stbi__bitreverse16(v) >> (16-bits);
}

static int stbi__zbuild_huffman(stbi__zhuffman *z, const stbi_uc *sizelist, int num)
{
   int i,k=0;
   int code, next_code[16], sizes[17];

   // DEFLATE spec for generating codes
   memset(sizes, 0, sizeof(sizes));
   memset(z->fast, 0, sizeof(z->fast));
   for (i=0; i < num; ++i)
      ++sizes[sizelist[i]];
   sizes[0] = 0;
   for (i=1; i < 16; ++i)
      if (sizes[i] > (1 << i))
         return stbi__err("bad sizes", "Corrupt PNG");
   code = 0;
   for (i=1; i < 16; ++i) {
      next_code[i] = code;
      z->firstcode[i] = (stbi__uint16) code;
      z->firstsymbol[i] = (stbi__uint16) k;
      code = (code + sizes[i]);
      if (sizes[i])
         if (code-1 >= (1 << i)) return stbi__err("bad codelengths","Corrupt PNG");
      z->maxcode[i] = code << (16-i); // preshift for inner loop
      code <<= 1;
      k += sizes[i];
   }
   z->maxcode[16] = 0x10000; // sentinel
   for (i=0; i < num; ++i) {
      int s = sizelist[i];
      if (s) {
         int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
         stbi__uint16 fastv = (stbi__uint16) ((s << 9) | i);
         z->size [c] = (stbi_uc     ) s;
         z->value[c] = (stbi__uint16) i;
         if (s <= STBI__ZFAST_BITS) {
            int j = stbi__bit_reverse(next_code[s],s);
            while (j < (1 << STBI__ZFAST_BITS)) {
               z->fast[j] = fastv;
               j += (1 << s);
            }
         }
         ++next_code[s];
      }
   }
   return 1;
}

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

typedef struct
{
   stbi_uc *zbuffer, *zbuffer_end;
   int num_bits;
   int hit_zeof_once;
   stbi__uint32 code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   stbi__zhuffman z_length, z_distance;
} stbi__zbuf;

stbi_inline static int stbi__zeof(stbi__zbuf *z)
{
   return (z->zbuffer >= z->zbuffer_end);
}

stbi_inline static stbi_uc stbi__zget8(stbi__zbuf *z)
{
   return stbi__zeof(z) ? 0 : *z->zbuffer++;
}

static void stbi__fill_bits(stbi__zbuf *z)
{
   do {
      if (z->code_buffer >= (1U << z->num_bits)) {
        z->zbuffer = z->zbuffer_end;  /* treat this as EOF so we fail. */
        return;
      }
      z->code_buffer |= (unsigned int) stbi__zget8(z) << z->num_bits;
      z->num_bits += 8;
   } while (z->num_bits <= 24);
}

stbi_inline static unsigned int stbi__zreceive(stbi__zbuf *z, int n)
{
   unsigned int k;
   if (z->num_bits < n) stbi__fill_bits(z);
   k = z->code_buffer & ((1 << n) - 1);
   z->code_buffer >>= n;
   z->num_bits -= n;
   return k;
}

static int stbi__zhuffman_decode_slowpath(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s,k;
   // not resolved by fast table, so compute it the slow way
   // use jpeg approach, which requires MSbits at top
   k = stbi__bit_reverse(a->code_buffer, 16);
   for (s=STBI__ZFAST_BITS+1; ; ++s)
      if (k < z->maxcode[s])
         break;
   if (s >= 16) return -1; // invalid code!
   // code size is s, so:
   b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
   if (b >= STBI__ZNSYMS) return -1; // some data was corrupt somewhere!
   if (z->size[b] != s) return -1;  // was originally an assert, but report failure instead.
   a->code_buffer >>= s;
   a->num_bits -= s;
   return z->value[b];
}

stbi_inline static int stbi__zhuffman_decode(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s;
   if (a->num_bits < 16) {
      if (stbi__zeof(a)) {
         if (!a->hit_zeof_once) {
            // This is the first time we hit eof, insert 16 extra padding btis
            // to allow us to keep going; if we actually consume any of them
            // though, that is invalid data. This is caught later.
            a->hit_zeof_once = 1;
            a->num_bits += 16; // add 16 implicit zero bits
         } else {
            // We already inserted our extra 16 padding bits and are again
            // out, this stream is actually prematurely terminated.
            return -1;
         }
      } else {
         stbi__fill_bits(a);
      }
   }
   b = z->fast[a->code_buffer & STBI__ZFAST_MASK];
   if (b) {
      s = b >> 9;
      a->code_buffer >>= s;
      a->num_bits -= s;
      return b & 511;
   }
   return stbi__zhuffman_decode_slowpath(a, z);
}

static int stbi__zexpand(stbi__zbuf *z, char *zout, int n)  // need to make room for n bytes
{
   char *q;
   unsigned int cur, limit, old_limit;
   z->zout = zout;
   if (!z->z_expandable) return stbi__err("output buffer limit","Corrupt PNG");
   cur   = (unsigned int) (z->zout - z->zout_start);
   limit = old_limit = (unsigned) (z->zout_end - z->zout_start);
   if (UINT_MAX - cur < (unsigned) n) return stbi__err("outofmem", "Out of memory");
   while (cur + n > limit) {
      if(limit > UINT_MAX / 2) return stbi__err("outofmem", "Out of memory");
      limit *= 2;
   }
   q = (char *) STBI_REALLOC_SIZED(z->zout_start, old_limit, limit);
   STBI_NOTUSED(old_limit);
   if (q == NULL) return stbi__err("outofmem", "Out of memory");
   z->zout_start = q;
   z->zout       = q + cur;
   z->zout_end   = q + limit;
   return 1;
}

static const int stbi__zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 };

static const int stbi__zlength_extra[31]=
{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };

static const int stbi__zdist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};

static const int stbi__zdist_extra[32] =
{ 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int stbi__parse_huffman_block(stbi__zbuf *a)
{
   char *zout = a->zout;
   for(;;) {
      int z = stbi__zhuffman_decode(a, &a->z_length);
      if (z < 256) {
         if (z < 0) return stbi__err("bad huffman code","Corrupt PNG"); // error in huffman codes
         if (zout >= a->zout_end) {
            if (!stbi__zexpand(a, zout, 1)) return 0;
            zout = a->zout;
         }
         *zout++ = (char) z;
      } else {
         stbi_uc *p;
         int len,dist;
         if (z == 256) {
            a->zout = zout;
            if (a->hit_zeof_once && a->num_bits < 16) {
               // The first time we hit zeof, we inserted 16 extra zero bits into our bit
               // buffer so the decoder can just do its speculative decoding. But if we
               // actually consumed any of those bits (which is the case when num_bits < 16),
               // the stream actually read past the end so it is malformed.
               return stbi__err("unexpected end","Corrupt PNG");
            }
            return 1;
         }
         if (z >= 286) return stbi__err("bad huffman code","Corrupt PNG"); // per DEFLATE, length codes 286 and 287 must not appear in compressed data
         z -= 257;
         len = stbi__zlength_base[z];
         if (stbi__zlength_extra[z]) len += stbi__zreceive(a, stbi__zlength_extra[z]);
         z = stbi__zhuffman_decode(a, &a->z_distance);
         if (z < 0 || z >= 30) return stbi__err("bad huffman code","Corrupt PNG"); // per DEFLATE, distance codes 30 and 31 must not appear in compressed data
         dist = stbi__zdist_base[z];
         if (stbi__zdist_extra[z]) dist += stbi__zreceive(a, stbi__zdist_extra[z]);
         if (zout - a->zout_start < dist) return stbi__err("bad dist","Corrupt PNG");
         if (len > a->zout_end - zout) {
            if (!stbi__zexpand(a, zout, len)) return 0;
            zout = a->zout;
         }
         p = (stbi_uc *) (zout - dist);
         if (dist == 1) { // run of one byte; common in images.
            stbi_uc v = *p;
            if (len) { do *zout++ = v; while (--len); }
         } else {
            if (len) { do *zout++ = *p++; while (--len); }
         }
      }
   }
}

static int stbi__compute_huffman_codes(stbi__zbuf *a)
{
   static const stbi_uc length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   stbi__zhuffman z_codelength;
   stbi_uc lencodes[286+32+137];//padding for maximum single op
   stbi_uc codelength_sizes[19];
   int i,n;

   int hlit  = stbi__zreceive(a,5) + 257;
   int hdist = stbi__zreceive(a,5) + 1;
   int hclen = stbi__zreceive(a,4) + 4;
   int ntot  = hlit + hdist;

   memset(codelength_sizes, 0, sizeof(codelength_sizes));
   for (i=0; i < hclen; ++i) {
      int s = stbi__zreceive(a,3);
      codelength_sizes[length_dezigzag[i]] = (stbi_uc) s;
   }
   if (!stbi__zbuild_huffman(&z_codelength, codelength_sizes, 19)) return 0;

   n = 0;
   while (n < ntot) {
      int c = stbi__zhuffman_decode(a, &z_codelength);
      if (c < 0 || c >= 19) return stbi__err("bad codelengths", "Corrupt PNG");
      if (c < 16)
         lencodes[n++] = (stbi_uc) c;
      else {
         stbi_uc fill = 0;
         if (c == 16) {
            c = stbi__zreceive(a,2)+3;
            if (n == 0) return stbi__err("bad codelengths", "Corrupt PNG");
            fill = lencodes[n-1];
         } else if (c == 17) {
            c = stbi__zreceive(a,3)+3;
         } else if (c == 18) {
            c = stbi__zreceive(a,7)+11;
         } else {
            return stbi__err("bad codelengths", "Corrupt PNG");
         }
         if (ntot - n < c) return stbi__err("bad codelengths", "Corrupt PNG");
         memset(lencodes+n, fill, c);
         n += c;
      }
   }
   if (n != ntot) return stbi__err("bad codelengths","Corrupt PNG");
   if (!stbi__zbuild_huffman(&a->z_length, lencodes, hlit)) return 0;
   if (!stbi__zbuild_huffman(&a->z_distance, lencodes+hlit, hdist)) return 0;
   return 1;
}

static int stbi__parse_uncompressed_block(stbi__zbuf *a)
{
   stbi_uc header[4];
   int len,nlen,k;
   if (a->num_bits & 7)
      stbi__zreceive(a, a->num_bits & 7); // discard
   // drain the bit-packed data into header
   k = 0;
   while (a->num_bits > 0) {
      header[k++] = (stbi_uc) (a->code_buffer & 255); // suppress MSVC run-time check
      a->code_buffer >>= 8;
      a->num_bits -= 8;
   }
   if (a->num_bits < 0) return stbi__err("zlib corrupt","Corrupt PNG");
   // now fill header the normal way
   while (k < 4)
      header[k++] = stbi__zget8(a);
   len  = header[1] * 256 + header[0];
   nlen = header[3] * 256 + header[2];
   if (nlen != (len ^ 0xffff)) return stbi__err("zlib corrupt","Corrupt PNG");
   if (a->zbuffer + len > a->zbuffer_end) return stbi__err("read past buffer","Corrupt PNG");
   if (a->zout + len > a->zout_end)
      if (!stbi__zexpand(a, a->zout, len)) return 0;
   memcpy(a->zout, a->zbuffer, len);
   a->zbuffer += len;
   a->zout += len;
   return 1;
}

static int stbi__parse_zlib_header(stbi__zbuf *a)
{
   int cmf   = stbi__zget8(a);
   int cm    = cmf & 15;
   /* int cinfo = cmf >> 4; */
   int flg   = stbi__zget8(a);
   if (stbi__zeof(a)) return stbi__err("bad zlib header","Corrupt PNG"); // zlib spec
   if ((cmf*256+flg) % 31 != 0) return stbi__err("bad zlib header","Corrupt PNG"); // zlib spec
   if (flg & 32) return stbi__err("no preset dict","Corrupt PNG"); // preset dictionary not allowed in png
   if (cm != 8) return stbi__err("bad compression","Corrupt PNG"); // DEFLATE required for png
   // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
   return 1;
}

static const stbi_uc stbi__zdefault_length[STBI__ZNSYMS] =
{
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};
static const stbi_uc stbi__zdefault_distance[32] =
{
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};
/*
Init algorithm:
{
   int i;   // use <= to match clearly with spec
   for (i=0; i <= 143; ++i)     stbi__zdefault_length[i]   = 8;
   for (   ; i <= 255; ++i)     stbi__zdefault_length[i]   = 9;
   for (   ; i <= 279; ++i)     stbi__zdefault_length[i]   = 7;
   for (   ; i <= 287; ++i)     stbi__zdefault_length[i]   = 8;

   for (i=0; i <=  31; ++i)     stbi__zdefault_distance[i] = 5;
}
*/

static int stbi__parse_zlib(stbi__zbuf *a, int parse_header)
{
   int final, type;
   if (parse_header)
      if (!stbi__parse_zlib_header(a)) return 0;
   a->num_bits = 0;
   a->code_buffer = 0;
   a->hit_zeof_once = 0;
   do {
      final = stbi__zreceive(a,1);
      type = stbi__zreceive(a,2);
      if (type == 0) {
         if (!stbi__parse_uncompressed_block(a)) return 0;
      } else if (type == 3) {
         return 0;
      } else {
         if (type == 1) {
            // use fixed code lengths
            if (!stbi__zbuild_huffman(&a->z_length  , stbi__zdefault_length  , STBI__ZNSYMS)) return 0;
            if (!stbi__zbuild_huffman(&a->z_distance, stbi__zdefault_distance,  32)) return 0;
         } else {
            if (!stbi__compute_huffman_codes(a)) return 0;
         }
         if (!stbi__parse_huffman_block(a)) return 0;
      }
   } while (!final);
   return 1;
}

static int stbi__do_zlib(stbi__zbuf *a, char *obuf, int olen, int exp, int parse_header)
{
   a->zout_start = obuf;
   a->zout       = obuf;
   a->zout_end   = obuf + olen;
   a->z_expandable = exp;

   return stbi__parse_zlib(a, parse_header);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, 1)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF char *stbi_zlib_decode_malloc(char const *buffer, int len, int *outlen)
{
   return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, parse_header)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 1))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}

STBIDEF char *stbi_zlib_decode_noheader_malloc(char const *buffer, int len, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(16384);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer+len;
   if (stbi__do_zlib(&a, p, 16384, 1, 0)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 0))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}
#endif

// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

#ifndef STBI_NO_PNG
typedef struct
{
   stbi__uint32 length;
   stbi__uint32 type;
} stbi__pngchunk;

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
   stbi__pngchunk c;
   c.length = stbi__get32be(s);
   c.type   = stbi__get32be(s);
   return c;
}

static int stbi__check_png_header(stbi__context *s)
{
   static const stbi_uc png_sig[8] = { 137,80,78,71,13,10,26,10 };
   int i;
   for (i=0; i < 8; ++i)
      if (stbi__get8(s) != png_sig[i]) return stbi__err("bad png sig","Not a PNG");
   return 1;
}

typedef struct
{
   stbi__context *s;
   stbi_uc *idata, *expanded, *out;
   int depth;
} stbi__png;


enum {
   STBI__F_none=0,
   STBI__F_sub=1,
   STBI__F_up=2,
   STBI__F_avg=3,
   STBI__F_paeth=4,
   // synthetic filter used for first scanline to avoid needing a dummy row of 0s
   STBI__F_avg_first
};

static stbi_uc first_row_filter[5] =
{
   STBI__F_none,
   STBI__F_sub,
   STBI__F_none,
   STBI__F_avg_first,
   STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int stbi__paeth(int a, int b, int c)
{
   // This formulation looks very different from the reference in the PNG spec, but is
   // actually equivalent and has favorable data dependencies and admits straightforward
   // generation of branch-free code, which helps performance significantly.
   int thresh = c*3 - (a + b);
   int lo = a < b ? a : b;
   int hi = a < b ? b : a;
   int t0 = (hi <= thresh) ? lo : c;
   int t1 = (thresh <= lo) ? hi : t0;
   return t1;
}

static const stbi_uc stbi__depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(stbi_uc *dest, stbi_uc *src, stbi__uint32 x, int img_n)
{
   int i;
   // must process data backwards since we allow dest==src
   if (img_n == 1) {
      for (i=x-1; i >= 0; --i) {
         dest[i*2+1] = 255;
         dest[i*2+0] = src[i];
      }
   } else {
      STBI_ASSERT(img_n == 3);
      for (i=x-1; i >= 0; --i) {
         dest[i*4+3] = 255;
         dest[i*4+2] = src[i*3+2];
         dest[i*4+1] = src[i*3+1];
         dest[i*4+0] = src[i*3+0];
      }
   }
}

// create the png data from post-deflated data
static int stbi__create_png_image_raw(stbi__png *a, stbi_uc *raw, stbi__uint32 raw_len, int out_n, stbi__uint32 x, stbi__uint32 y, int depth, int color)
{
   int bytes = (depth == 16 ? 2 : 1);
   stbi__context *s = a->s;
   stbi__uint32 i,j,stride = x*out_n*bytes;
   stbi__uint32 img_len, img_width_bytes;
   stbi_uc *filter_buf;
   int all_ok = 1;
   int k;
   int img_n = s->img_n; // copy it into a local for later

   int output_bytes = out_n*bytes;
   int filter_bytes = img_n*bytes;
   int width = x;

   STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
   a->out = (stbi_uc *) stbi__malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
   if (!a->out) return stbi__err("outofmem", "Out of memory");

   // note: error exits here don't need to clean up a->out individually,
   // stbi__do_png always does on error.
   if (!stbi__mad3sizes_valid(img_n, x, depth, 7)) return stbi__err("too large", "Corrupt PNG");
   img_width_bytes = (((img_n * x * depth) + 7) >> 3);
   if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return stbi__err("too large", "Corrupt PNG");
   img_len = (img_width_bytes + 1) * y;

   // we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
   // but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
   // so just check for raw_len < img_len always.
   if (raw_len < img_len) return stbi__err("not enough pixels","Corrupt PNG");

   // Allocate two scan lines worth of filter workspace buffer.
   filter_buf = (stbi_uc *) stbi__malloc_mad2(img_width_bytes, 2, 0);
   if (!filter_buf) return stbi__err("outofmem", "Out of memory");

   // Filtering for low-bit-depth images
   if (depth < 8) {
      filter_bytes = 1;
      width = img_width_bytes;
   }

   for (j=0; j < y; ++j) {
      // cur/prior filter buffers alternate
      stbi_uc *cur = filter_buf + (j & 1)*img_width_bytes;
      stbi_uc *prior = filter_buf + (~j & 1)*img_width_bytes;
      stbi_uc *dest = a->out + stride*j;
      int nk = width * filter_bytes;
      int filter = *raw++;

      // check filter type
      if (filter > 4) {
         all_ok = stbi__err("invalid filter","Corrupt PNG");
         break;
      }

      // if first row, use special filter that doesn't sample previous row
      if (j == 0) filter = first_row_filter[filter];

      // perform actual filtering
      switch (filter) {
      case STBI__F_none:
         memcpy(cur, raw, nk);
         break;
      case STBI__F_sub:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
         break;
      case STBI__F_up:
         for (k = 0; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
         break;
      case STBI__F_avg:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
         break;
      case STBI__F_paeth:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + stbi__paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
         break;
      case STBI__F_avg_first:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
         break;
      }

      raw += nk;

      // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
      if (depth < 8) {
         stbi_uc scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
         stbi_uc *in = cur;
         stbi_uc *out = dest;
         stbi_uc inb = 0;
         stbi__uint32 nsmp = x*img_n;

         // expand bits to bytes first
         if (depth == 4) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 1) == 0) inb = *in++;
               *out++ = scale * (inb >> 4);
               inb <<= 4;
            }
         } else if (depth == 2) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 3) == 0) inb = *in++;
               *out++ = scale * (inb >> 6);
               inb <<= 2;
            }
         } else {
            STBI_ASSERT(depth == 1);
            for (i=0; i < nsmp; ++i) {
               if ((i & 7) == 0) inb = *in++;
               *out++ = scale * (inb >> 7);
               inb <<= 1;
            }
         }

         // insert alpha=255 values if desired
         if (img_n != out_n)
            stbi__create_png_alpha_expand8(dest, dest, x, img_n);
      } else if (depth == 8) {
         if (img_n == out_n)
            memcpy(dest, cur, x*img_n);
         else
            stbi__create_png_alpha_expand8(dest, cur, x, img_n);
      } else if (depth == 16) {
         // convert the image data from big-endian to platform-native
         stbi__uint16 *dest16 = (stbi__uint16*)dest;
         stbi__uint32 nsmp = x*img_n;

         if (img_n == out_n) {
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
               *dest16 = (cur[0] << 8) | cur[1];
         } else {
            STBI_ASSERT(img_n+1 == out_n);
            if (img_n == 1) {
               for (i = 0; i < x; ++i, dest16 += 2, cur += 2) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = 0xffff;
               }
            } else {
               STBI_ASSERT(img_n == 3);
               for (i = 0; i < x; ++i, dest16 += 4, cur += 6) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = (cur[2] << 8) | cur[3];
                  dest16[2] = (cur[4] << 8) | cur[5];
                  dest16[3] = 0xffff;
               }
            }
         }
      }
   }

   STBI_FREE(filter_buf);
   if (!all_ok) return 0;

   return 1;
}

static int stbi__create_png_image(stbi__png *a, stbi_uc *image_data, stbi__uint32 image_data_len, int out_n, int depth, int color, int interlaced)
{
   int bytes = (depth == 16 ? 2 : 1);
   int out_bytes = out_n * bytes;
   stbi_uc *final;
   int p;
   if (!interlaced)
      return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

   // de-interlacing
   final = (stbi_uc *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
   if (!final) return stbi__err("outofmem", "Out of memory");
   for (p=0; p < 7; ++p) {
      int xorig[] = { 0,4,0,2,0,1,0 };
      int yorig[] = { 0,0,4,0,2,0,1 };
      int xspc[]  = { 8,8,4,4,2,2,1 };
      int yspc[]  = { 8,8,8,4,4,2,2 };
      int i,j,x,y;
      // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
      x = (a->s->img_x - xorig[p] + xspc[p]-1) / xspc[p];
      y = (a->s->img_y - yorig[p] + yspc[p]-1) / yspc[p];
      if (x && y) {
         stbi__uint32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
         if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color)) {
            STBI_FREE(final);
            return 0;
         }
         for (j=0; j < y; ++j) {
            for (i=0; i < x; ++i) {
               int out_y = j*yspc[p]+yorig[p];
               int out_x = i*xspc[p]+xorig[p];
               memcpy(final + out_y*a->s->img_x*out_bytes + out_x*out_bytes,
                      a->out + (j*x+i)*out_bytes, out_bytes);
            }
         }
         STBI_FREE(a->out);
         image_data += img_len;
         image_data_len -= img_len;
      }
   }
   a->out = final;

   return 1;
}

static int stbi__compute_transparency(stbi__png *z, stbi_uc tc[3], int out_n)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi_uc *p = z->out;

   // compute color-based transparency, assuming we've
   // already got 255 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i=0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 255);
         p += 2;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int stbi__compute_transparency16(stbi__png *z, stbi__uint16 tc[3], int out_n)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi__uint16 *p = (stbi__uint16*) z->out;

   // compute color-based transparency, assuming we've
   // already got 65535 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i = 0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 65535);
         p += 2;
      }
   } else {
      for (i = 0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int stbi__expand_png_palette(stbi__png *a, stbi_uc *palette, int len, int pal_img_n)
{
   stbi__uint32 i, pixel_count = a->s->img_x * a->s->img_y;
   stbi_uc *p, *temp_out, *orig = a->out;

   p = (stbi_uc *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
   if (p == NULL) return stbi__err("outofmem", "Out of memory");

   // between here and free(out) below, exitting would leak
   temp_out = p;

   if (pal_img_n == 3) {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p += 3;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p[3] = palette[n+3];
         p += 4;
      }
   }
   STBI_FREE(a->out);
   a->out = temp_out;

   STBI_NOTUSED(len);

   return 1;
}

static int stbi__unpremultiply_on_load_global = 0;
static int stbi__de_iphone_flag_global = 0;

STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply)
{
   stbi__unpremultiply_on_load_global = flag_true_if_should_unpremultiply;
}

STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert)
{
   stbi__de_iphone_flag_global = flag_true_if_should_convert;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__unpremultiply_on_load  stbi__unpremultiply_on_load_global
#define stbi__de_iphone_flag  stbi__de_iphone_flag_global
#else
static STBI_THREAD_LOCAL int stbi__unpremultiply_on_load_local, stbi__unpremultiply_on_load_set;
static STBI_THREAD_LOCAL int stbi__de_iphone_flag_local, stbi__de_iphone_flag_set;

STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply)
{
   stbi__unpremultiply_on_load_local = flag_true_if_should_unpremultiply;
   stbi__unpremultiply_on_load_set = 1;
}

STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert)
{
   stbi__de_iphone_flag_local = flag_true_if_should_convert;
   stbi__de_iphone_flag_set = 1;
}

#define stbi__unpremultiply_on_load  (stbi__unpremultiply_on_load_set           \
                                       ? stbi__unpremultiply_on_load_local      \
                                       : stbi__unpremultiply_on_load_global)
#define stbi__de_iphone_flag  (stbi__de_iphone_flag_set                         \
                                ? stbi__de_iphone_flag_local                    \
                                : stbi__de_iphone_flag_global)
#endif // STBI_THREAD_LOCAL

static void stbi__de_iphone(stbi__png *z)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi_uc *p = z->out;

   if (s->img_out_n == 3) {  // convert bgr to rgb
      for (i=0; i < pixel_count; ++i) {
         stbi_uc t = p[0];
         p[0] = p[2];
         p[2] = t;
         p += 3;
      }
   } else {
      STBI_ASSERT(s->img_out_n == 4);
      if (stbi__unpremultiply_on_load) {
         // convert bgr to rgb and unpremultiply
         for (i=0; i < pixel_count; ++i) {
            stbi_uc a = p[3];
            stbi_uc t = p[0];
            if (a) {
               stbi_uc half = a / 2;
               p[0] = (p[2] * 255 + half) / a;
               p[1] = (p[1] * 255 + half) / a;
               p[2] = ( t   * 255 + half) / a;
            } else {
               p[0] = p[2];
               p[2] = t;
            }
            p += 4;
         }
      } else {
         // convert bgr to rgb
         for (i=0; i < pixel_count; ++i) {
            stbi_uc t = p[0];
            p[0] = p[2];
            p[2] = t;
            p += 4;
         }
      }
   }
}

#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
   stbi_uc palette[1024], pal_img_n=0;
   stbi_uc has_trans=0, tc[3]={0};
   stbi__uint16 tc16[3];
   stbi__uint32 ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;
   stbi__context *s = z->s;

   z->expanded = NULL;
   z->idata = NULL;
   z->out = NULL;

   if (!stbi__check_png_header(s)) return 0;

   if (scan == STBI__SCAN_type) return 1;

   for (;;) {
      stbi__pngchunk c = stbi__get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            is_iphone = 1;
            stbi__skip(s, c.length);
            break;
         case STBI__PNG_TYPE('I','H','D','R'): {
            int comp,filter;
            if (!first) return stbi__err("multiple IHDR","Corrupt PNG");
            first = 0;
            if (c.length != 13) return stbi__err("bad IHDR len","Corrupt PNG");
            s->img_x = stbi__get32be(s);
            s->img_y = stbi__get32be(s);
            if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
            if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
            z->depth = stbi__get8(s);  if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)  return stbi__err("1/2/4/8/16-bit only","PNG not supported: 1/2/4/8/16-bit only");
            color = stbi__get8(s);  if (color > 6)         return stbi__err("bad ctype","Corrupt PNG");
            if (color == 3 && z->depth == 16)                  return stbi__err("bad ctype","Corrupt PNG");
            if (color == 3) pal_img_n = 3; else if (color & 1) return stbi__err("bad ctype","Corrupt PNG");
            comp  = stbi__get8(s);  if (comp) return stbi__err("bad comp method","Corrupt PNG");
            filter= stbi__get8(s);  if (filter) return stbi__err("bad filter method","Corrupt PNG");
            interlace = stbi__get8(s); if (interlace>1) return stbi__err("bad interlace method","Corrupt PNG");
            if (!s->img_x || !s->img_y) return stbi__err("0-pixel image","Corrupt PNG");
            if (!pal_img_n) {
               s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
               if ((1 << 30) / s->img_x / s->img_n < s->img_y) return stbi__err("too large", "Image too large to decode");
            } else {
               // if paletted, then pal_n is our final components, and
               // img_n is # components to decompress/filter.
               s->img_n = 1;
               if ((1 << 30) / s->img_x / 4 < s->img_y) return stbi__err("too large","Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
         }

         case STBI__PNG_TYPE('P','L','T','E'):  {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (c.length > 256*3) return stbi__err("invalid PLTE","Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length) return stbi__err("invalid PLTE","Corrupt PNG");
            for (i=0; i < pal_len; ++i) {
               palette[i*4+0] = stbi__get8(s);
               palette[i*4+1] = stbi__get8(s);
               palette[i*4+2] = stbi__get8(s);
               palette[i*4+3] = 255;
            }
            break;
         }

         case STBI__PNG_TYPE('t','R','N','S'): {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (z->idata) return stbi__err("tRNS after IDAT","Corrupt PNG");
            if (pal_img_n) {
               if (scan == STBI__SCAN_header) { s->img_n = 4; return 1; }
               if (pal_len == 0) return stbi__err("tRNS before PLTE","Corrupt PNG");
               if (c.length > pal_len) return stbi__err("bad tRNS len","Corrupt PNG");
               pal_img_n = 4;
               for (i=0; i < c.length; ++i)
                  palette[i*4+3] = stbi__get8(s);
            } else {
               if (!(s->img_n & 1)) return stbi__err("tRNS with alpha","Corrupt PNG");
               if (c.length != (stbi__uint32) s->img_n*2) return stbi__err("bad tRNS len","Corrupt PNG");
               has_trans = 1;
               // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
               if (scan == STBI__SCAN_header) { ++s->img_n; return 1; }
               if (z->depth == 16) {
                  for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                     tc16[k] = (stbi__uint16)stbi__get16be(s); // copy the values as-is
               } else {
                  for (k = 0; k < s->img_n && k < 3; ++k)
                     tc[k] = (stbi_uc)(stbi__get16be(s) & 255) * stbi__depth_scale_table[z->depth]; // non 8-bit images will be larger
               }
            }
            break;
         }

         case STBI__PNG_TYPE('I','D','A','T'): {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len) return stbi__err("no PLTE","Corrupt PNG");
            if (scan == STBI__SCAN_header) {
               // header scan definitely stops at first IDAT
               if (pal_img_n)
                  s->img_n = pal_img_n;
               return 1;
            }
            if (c.length > (1u << 30)) return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
            if ((int)(ioff + c.length) < (int)ioff) return 0;
            if (ioff + c.length > idata_limit) {
               stbi__uint32 idata_limit_old = idata_limit;
               stbi_uc *p;
               if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
               while (ioff + c.length > idata_limit)
                  idata_limit *= 2;
               STBI_NOTUSED(idata_limit_old);
               p = (stbi_uc *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit); if (p == NULL) return stbi__err("outofmem", "Out of memory");
               z->idata = p;
            }
            if (!stbi__getn(s, z->idata+ioff,c.length)) return stbi__err("outofdata","Corrupt PNG");
            ioff += c.length;
            break;
         }

         case STBI__PNG_TYPE('I','E','N','D'): {
            stbi__uint32 raw_len, bpl;
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (scan != STBI__SCAN_load) return 1;
            if (z->idata == NULL) return stbi__err("no IDAT","Corrupt PNG");
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
            raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
            z->expanded = (stbi_uc *) stbi_zlib_decode_malloc_guesssize_headerflag((char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
            if (z->expanded == NULL) return 0; // zlib should set error
            STBI_FREE(z->idata); z->idata = NULL;
            if ((req_comp == s->img_n+1 && req_comp != 3 && !pal_img_n) || has_trans)
               s->img_out_n = s->img_n+1;
            else
               s->img_out_n = s->img_n;
            if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) return 0;
            if (has_trans) {
               if (z->depth == 16) {
                  if (!stbi__compute_transparency16(z, tc16, s->img_out_n)) return 0;
               } else {
                  if (!stbi__compute_transparency(z, tc, s->img_out_n)) return 0;
               }
            }
            if (is_iphone && stbi__de_iphone_flag && s->img_out_n > 2)
               stbi__de_iphone(z);
            if (pal_img_n) {
               // pal_img_n == 3 or 4
               s->img_n = pal_img_n; // record the actual colors we had
               s->img_out_n = pal_img_n;
               if (req_comp >= 3) s->img_out_n = req_comp;
               if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
                  return 0;
            } else if (has_trans) {
               // non-paletted image with tRNS -> source image has (constant) alpha
               ++s->img_n;
            }
            STBI_FREE(z->expanded); z->expanded = NULL;
            // end of PNG chunk, read and skip CRC
            stbi__get32be(s);
            return 1;
         }

         default:
            // if critical, fail
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if ((c.type & (1 << 29)) == 0) {
               #ifndef STBI_NO_FAILURE_STRINGS
               // not threadsafe
               static char invalid_chunk[] = "XXXX PNG chunk not known";
               invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
               invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
               invalid_chunk[2] = STBI__BYTECAST(c.type >>  8);
               invalid_chunk[3] = STBI__BYTECAST(c.type >>  0);
               #endif
               return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
            }
            stbi__skip(s, c.length);
            break;
      }
      // end of PNG chunk, read and skip CRC
      stbi__get32be(s);
   }
}

static void *stbi__do_png(stbi__png *p, int *x, int *y, int *n, int req_comp, stbi__result_info *ri)
{
   void *result=NULL;
   if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");
   if (stbi__parse_png_file(p, STBI__SCAN_load, req_comp)) {
      if (p->depth <= 8)
         ri->bits_per_channel = 8;
      else if (p->depth == 16)
         ri->bits_per_channel = 16;
      else
         return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
      result = p->out;
      p->out = NULL;
      if (req_comp && req_comp != p->s->img_out_n) {
         if (ri->bits_per_channel == 8)
            result = stbi__convert_format((unsigned char *) result, p->s->img_out_n, req_comp, p->s->img_x, p->s->img_y);
         else
            result = stbi__convert_format16((stbi__uint16 *) result, p->s->img_out_n, req_comp, p->s->img_x, p->s->img_y);
         p->s->img_out_n = req_comp;
         if (result == NULL) return result;
      }
      *x = p->s->img_x;
      *y = p->s->img_y;
      if (n) *n = p->s->img_n;
   }
   STBI_FREE(p->out);      p->out      = NULL;
   STBI_FREE(p->expanded); p->expanded = NULL;
   STBI_FREE(p->idata);    p->idata    = NULL;

   return result;
}

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi__png p;
   p.s = s;
   return stbi__do_png(&p, x,y,comp,req_comp, ri);
}

static int stbi__png_test(stbi__context *s)
{
   int r;
   r = stbi__check_png_header(s);
   stbi__rewind(s);
   return r;
}

static int stbi__png_info_raw(stbi__png *p, int *x, int *y, int *comp)
{
   if (!stbi__parse_png_file(p, STBI__SCAN_header, 0)) {
      stbi__rewind( p->s );
      return 0;
   }
   if (x) *x = p->s->img_x;
   if (y) *y = p->s->img_y;
   if (comp) *comp = p->s->img_n;
   return 1;
}

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp)
{
   stbi__png p;
   p.s = s;
   return stbi__png_info_raw(&p, x, y, comp);
}

static int stbi__png_is16(stbi__context *s)
{
   stbi__png p;
   p.s = s;
   if (!stbi__png_info_raw(&p, NULL, NULL, NULL))
	   return 0;
   if (p.depth != 16) {
      stbi__rewind(p.s);
      return 0;
   }
   return 1;
}
#endif

// Microsoft/Windows BMP image

#ifndef STBI_NO_BMP
static int stbi__bmp_test_raw(stbi__context *s)
{
   int r;
   int sz;
   if (stbi__get8(s) != 'B') return 0;
   if (stbi__get8(s) != 'M') return 0;
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   stbi__get32le(s); // discard data offset
   sz = stbi__get32le(s);
   r = (sz == 12 || sz == 40 || sz == 56 || sz == 108 || sz == 124);
   return r;
}

static int stbi__bmp_test(stbi__context *s)
{
   int r = stbi__bmp_test_raw(s);
   stbi__rewind(s);
   return r;
}


// returns 0..31 for the highest set bit
static int stbi__high_bit(unsigned int z)
{
   int n=0;
   if (z == 0) return -1;
   if (z >= 0x10000) { n += 16; z >>= 16; }
   if (z >= 0x00100) { n +=  8; z >>=  8; }
   if (z >= 0x00010) { n +=  4; z >>=  4; }
   if (z >= 0x00004) { n +=  2; z >>=  2; }
   if (z >= 0x00002) { n +=  1;/* >>=  1;*/ }
   return n;
}

static int stbi__bitcount(unsigned int a)
{
   a = (a & 0x55555555) + ((a >>  1) & 0x55555555); // max 2
   a = (a & 0x33333333) + ((a >>  2) & 0x33333333); // max 4
   a = (a + (a >> 4)) & 0x0f0f0f0f; // max 8 per 4, now 8 bits
   a = (a + (a >> 8)); // max 16 per 8 bits
   a = (a + (a >> 16)); // max 32 per 8 bits
   return a & 0xff;
}

// extract an arbitrarily-aligned N-bit value (N=bits)
// from v, and then make it 8-bits long and fractionally
// extend it to full full range.
static int stbi__shiftsigned(unsigned int v, int shift, int bits)
{
   static unsigned int mul_table[9] = {
      0,
      0xff/*0b11111111*/, 0x55/*0b01010101*/, 0x49/*0b01001001*/, 0x11/*0b00010001*/,
      0x21/*0b00100001*/, 0x41/*0b01000001*/, 0x81/*0b10000001*/, 0x01/*0b00000001*/,
   };
   static unsigned int shift_table[9] = {
      0, 0,0,1,0,2,4,6,0,
   };
   if (shift < 0)
      v <<= -shift;
   else
      v >>= shift;
   STBI_ASSERT(v < 256);
   v >>= (8-bits);
   STBI_ASSERT(bits >= 0 && bits <= 8);
   return (int) ((unsigned) v * mul_table[bits]) >> shift_table[bits];
}

typedef struct
{
   int bpp, offset, hsz;
   unsigned int mr,mg,mb,ma, all_a;
   int extra_read;
} stbi__bmp_data;

static int stbi__bmp_set_mask_defaults(stbi__bmp_data *info, int compress)
{
   // BI_BITFIELDS specifies masks explicitly, don't override
   if (compress == 3)
      return 1;

   if (compress == 0) {
      if (info->bpp == 16) {
         info->mr = 31u << 10;
         info->mg = 31u <<  5;
         info->mb = 31u <<  0;
      } else if (info->bpp == 32) {
         info->mr = 0xffu << 16;
         info->mg = 0xffu <<  8;
         info->mb = 0xffu <<  0;
         info->ma = 0xffu << 24;
         info->all_a = 0; // if all_a is 0 at end, then we loaded alpha channel but it was all 0
      } else {
         // otherwise, use defaults, which is all-0
         info->mr = info->mg = info->mb = info->ma = 0;
      }
      return 1;
   }
   return 0; // error
}

static void *stbi__bmp_parse_header(stbi__context *s, stbi__bmp_data *info)
{
   int hsz;
   if (stbi__get8(s) != 'B' || stbi__get8(s) != 'M') return stbi__errpuc("not BMP", "Corrupt BMP");
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   info->offset = stbi__get32le(s);
   info->hsz = hsz = stbi__get32le(s);
   info->mr = info->mg = info->mb = info->ma = 0;
   info->extra_read = 14;

   if (info->offset < 0) return stbi__errpuc("bad BMP", "bad BMP");

   if (hsz != 12 && hsz != 40 && hsz != 56 && hsz != 108 && hsz != 124) return stbi__errpuc("unknown BMP", "BMP type not supported: unknown");
   if (hsz == 12) {
      s->img_x = stbi__get16le(s);
      s->img_y = stbi__get16le(s);
   } else {
      s->img_x = stbi__get32le(s);
      s->img_y = stbi__get32le(s);
   }
   if (stbi__get16le(s) != 1) return stbi__errpuc("bad BMP", "bad BMP");
   info->bpp = stbi__get16le(s);
   if (hsz != 12) {
      int compress = stbi__get32le(s);
      if (compress == 1 || compress == 2) return stbi__errpuc("BMP RLE", "BMP type not supported: RLE");
      if (compress >= 4) return stbi__errpuc("BMP JPEG/PNG", "BMP type not supported: unsupported compression"); // this includes PNG/JPEG modes
      if (compress == 3 && info->bpp != 16 && info->bpp != 32) return stbi__errpuc("bad BMP", "bad BMP"); // bitfields requires 16 or 32 bits/pixel
      stbi__get32le(s); // discard sizeof
      stbi__get32le(s); // discard hres
      stbi__get32le(s); // discard vres
      stbi__get32le(s); // discard colorsused
      stbi__get32le(s); // discard max important
      if (hsz == 40 || hsz == 56) {
         if (hsz == 56) {
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
         }
         if (info->bpp == 16 || info->bpp == 32) {
            if (compress == 0) {
               stbi__bmp_set_mask_defaults(info, compress);
            } else if (compress == 3) {
               info->mr = stbi__get32le(s);
               info->mg = stbi__get32le(s);
               info->mb = stbi__get32le(s);
               info->extra_read += 12;
               // not documented, but generated by photoshop and handled by mspaint
               if (info->mr == info->mg && info->mg == info->mb) {
                  // ?!?!?
                  return stbi__errpuc("bad BMP", "bad BMP");
               }
            } else
               return stbi__errpuc("bad BMP", "bad BMP");
         }
      } else {
         // V4/V5 header
         int i;
         if (hsz != 108 && hsz != 124)
            return stbi__errpuc("bad BMP", "bad BMP");
         info->mr = stbi__get32le(s);
         info->mg = stbi__get32le(s);
         info->mb = stbi__get32le(s);
         info->ma = stbi__get32le(s);
         if (compress != 3) // override mr/mg/mb unless in BI_BITFIELDS mode, as per docs
            stbi__bmp_set_mask_defaults(info, compress);
         stbi__get32le(s); // discard color space
         for (i=0; i < 12; ++i)
            stbi__get32le(s); // discard color space parameters
         if (hsz == 124) {
            stbi__get32le(s); // discard rendering intent
            stbi__get32le(s); // discard offset of profile data
            stbi__get32le(s); // discard size of profile data
            stbi__get32le(s); // discard reserved
         }
      }
   }
   return (void *) 1;
}


static void *stbi__bmp_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *out;
   unsigned int mr=0,mg=0,mb=0,ma=0, all_a;
   stbi_uc pal[256][4];
   int psize=0,i,j,width;
   int flip_vertically, pad, target;
   stbi__bmp_data info;
   STBI_NOTUSED(ri);

   info.all_a = 255;
   if (stbi__bmp_parse_header(s, &info) == NULL)
      return NULL; // error code already set

   flip_vertically = ((int) s->img_y) > 0;
   s->img_y = abs((int) s->img_y);

   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   mr = info.mr;
   mg = info.mg;
   mb = info.mb;
   ma = info.ma;
   all_a = info.all_a;

   if (info.hsz == 12) {
      if (info.bpp < 24)
         psize = (info.offset - info.extra_read - 24) / 3;
   } else {
      if (info.bpp < 16)
         psize = (info.offset - info.extra_read - info.hsz) >> 2;
   }
   if (psize == 0) {
      // accept some number of extra bytes after the header, but if the offset points either to before
      // the header ends or implies a large amount of extra data, reject the file as malformed
      int bytes_read_so_far = s->callback_already_read + (int)(s->img_buffer - s->img_buffer_original);
      int header_limit = 1024; // max we actually read is below 256 bytes currently.
      int extra_data_limit = 256*4; // what ordinarily goes here is a palette; 256 entries*4 bytes is its max size.
      if (bytes_read_so_far <= 0 || bytes_read_so_far > header_limit) {
         return stbi__errpuc("bad header", "Corrupt BMP");
      }
      // we established that bytes_read_so_far is positive and sensible.
      // the first half of this test rejects offsets that are either too small positives, or
      // negative, and guarantees that info.offset >= bytes_read_so_far > 0. this in turn
      // ensures the number computed in the second half of the test can't overflow.
      if (info.offset < bytes_read_so_far || info.offset - bytes_read_so_far > extra_data_limit) {
         return stbi__errpuc("bad offset", "Corrupt BMP");
      } else {
         stbi__skip(s, info.offset - bytes_read_so_far);
      }
   }

   if (info.bpp == 24 && ma == 0xff000000)
      s->img_n = 3;
   else
      s->img_n = ma ? 4 : 3;
   if (req_comp && req_comp >= 3) // we can directly decode 3 or 4
      target = req_comp;
   else
      target = s->img_n; // if they want monochrome, we'll post-convert

   // sanity-check size
   if (!stbi__mad3sizes_valid(target, s->img_x, s->img_y, 0))
      return stbi__errpuc("too large", "Corrupt BMP");

   out = (stbi_uc *) stbi__malloc_mad3(target, s->img_x, s->img_y, 0);
   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   if (info.bpp < 16) {
      int z=0;
      if (psize == 0 || psize > 256) { STBI_FREE(out); return stbi__errpuc("invalid", "Corrupt BMP"); }
      for (i=0; i < psize; ++i) {
         pal[i][2] = stbi__get8(s);
         pal[i][1] = stbi__get8(s);
         pal[i][0] = stbi__get8(s);
         if (info.hsz != 12) stbi__get8(s);
         pal[i][3] = 255;
      }
      stbi__skip(s, info.offset - info.extra_read - info.hsz - psize * (info.hsz == 12 ? 3 : 4));
      if (info.bpp == 1) width = (s->img_x + 7) >> 3;
      else if (info.bpp == 4) width = (s->img_x + 1) >> 1;
      else if (info.bpp == 8) width = s->img_x;
      else { STBI_FREE(out); return stbi__errpuc("bad bpp", "Corrupt BMP"); }
      pad = (-width)&3;
      if (info.bpp == 1) {
         for (j=0; j < (int) s->img_y; ++j) {
            int bit_offset = 7, v = stbi__get8(s);
            for (i=0; i < (int) s->img_x; ++i) {
               int color = (v>>bit_offset)&0x1;
               out[z++] = pal[color][0];
               out[z++] = pal[color][1];
               out[z++] = pal[color][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               if((--bit_offset) < 0) {
                  bit_offset = 7;
                  v = stbi__get8(s);
               }
            }
            stbi__skip(s, pad);
         }
      } else {
         for (j=0; j < (int) s->img_y; ++j) {
            for (i=0; i < (int) s->img_x; i += 2) {
               int v=stbi__get8(s),v2=0;
               if (info.bpp == 4) {
                  v2 = v & 15;
                  v >>= 4;
               }
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               v = (info.bpp == 8) ? stbi__get8(s) : v2;
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
            }
            stbi__skip(s, pad);
         }
      }
   } else {
      int rshift=0,gshift=0,bshift=0,ashift=0,rcount=0,gcount=0,bcount=0,acount=0;
      int z = 0;
      int easy=0;
      stbi__skip(s, info.offset - info.extra_read - info.hsz);
      if (info.bpp == 24) width = 3 * s->img_x;
      else if (info.bpp == 16) width = 2*s->img_x;
      else /* bpp = 32 and pad = 0 */ width=0;
      pad = (-width) & 3;
      if (info.bpp == 24) {
         easy = 1;
      } else if (info.bpp == 32) {
         if (mb == 0xff && mg == 0xff00 && mr == 0x00ff0000 && ma == 0xff000000)
            easy = 2;
      }
      if (!easy) {
         if (!mr || !mg || !mb) { STBI_FREE(out); return stbi__errpuc("bad masks", "Corrupt BMP"); }
         // right shift amt to put high bit in position #7
         rshift = stbi__high_bit(mr)-7; rcount = stbi__bitcount(mr);
         gshift = stbi__high_bit(mg)-7; gcount = stbi__bitcount(mg);
         bshift = stbi__high_bit(mb)-7; bcount = stbi__bitcount(mb);
         ashift = stbi__high_bit(ma)-7; acount = stbi__bitcount(ma);
         if (rcount > 8 || gcount > 8 || bcount > 8 || acount > 8) { STBI_FREE(out); return stbi__errpuc("bad masks", "Corrupt BMP"); }
      }
      for (j=0; j < (int) s->img_y; ++j) {
         if (easy) {
            for (i=0; i < (int) s->img_x; ++i) {
               unsigned char a;
               out[z+2] = stbi__get8(s);
               out[z+1] = stbi__get8(s);
               out[z+0] = stbi__get8(s);
               z += 3;
               a = (easy == 2 ? stbi__get8(s) : 255);
               all_a |= a;
               if (target == 4) out[z++] = a;
            }
         } else {
            int bpp = info.bpp;
            for (i=0; i < (int) s->img_x; ++i) {
               stbi__uint32 v = (bpp == 16 ? (stbi__uint32) stbi__get16le(s) : stbi__get32le(s));
               unsigned int a;
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mr, rshift, rcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mg, gshift, gcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mb, bshift, bcount));
               a = (ma ? stbi__shiftsigned(v & ma, ashift, acount) : 255);
               all_a |= a;
               if (target == 4) out[z++] = STBI__BYTECAST(a);
            }
         }
         stbi__skip(s, pad);
      }
   }

   // if alpha channel is all 0s, replace with all 255s
   if (target == 4 && all_a == 0)
      for (i=4*s->img_x*s->img_y-1; i >= 0; i -= 4)
         out[i] = 255;

   if (flip_vertically) {
      stbi_uc t;
      for (j=0; j < (int) s->img_y>>1; ++j) {
         stbi_uc *p1 = out +      j     *s->img_x*target;
         stbi_uc *p2 = out + (s->img_y-1-j)*s->img_x*target;
         for (i=0; i < (int) s->img_x*target; ++i) {
            t = p1[i]; p1[i] = p2[i]; p2[i] = t;
         }
      }
   }

   if (req_comp && req_comp != target) {
      out = stbi__convert_format(out, target, req_comp, s->img_x, s->img_y);
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;
   return out;
}
#endif

// Targa Truevision - TGA
// by Jonathan Dummer
#ifndef STBI_NO_TGA
// returns STBI_rgb or whatever, 0 on error
static int stbi__tga_get_comp(int bits_per_pixel, int is_grey, int* is_rgb16)
{
   // only RGB or RGBA (incl. 16bit) or grey allowed
   if (is_rgb16) *is_rgb16 = 0;
   switch(bits_per_pixel) {
      case 8:  return STBI_grey;
      case 16: if(is_grey) return STBI_grey_alpha;
               // fallthrough
      case 15: if(is_rgb16) *is_rgb16 = 1;
               return STBI_rgb;
      case 24: // fallthrough
      case 32: return bits_per_pixel/8;
      default: return 0;
   }
}

static int stbi__tga_info(stbi__context *s, int *x, int *y, int *comp)
{
    int tga_w, tga_h, tga_comp, tga_image_type, tga_bits_per_pixel, tga_colormap_bpp;
    int sz, tga_colormap_type;
    stbi__get8(s);                   // discard Offset
    tga_colormap_type = stbi__get8(s); // colormap type
    if( tga_colormap_type > 1 ) {
        stbi__rewind(s);
        return 0;      // only RGB or indexed allowed
    }
    tga_image_type = stbi__get8(s); // image type
    if ( tga_colormap_type == 1 ) { // colormapped (paletted) image
        if (tga_image_type != 1 && tga_image_type != 9) {
            stbi__rewind(s);
            return 0;
        }
        stbi__skip(s,4);       // skip index of first colormap entry and number of entries
        sz = stbi__get8(s);    //   check bits per palette color entry
        if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) {
            stbi__rewind(s);
            return 0;
        }
        stbi__skip(s,4);       // skip image x and y origin
        tga_colormap_bpp = sz;
    } else { // "normal" image w/o colormap - only RGB or grey allowed, +/- RLE
        if ( (tga_image_type != 2) && (tga_image_type != 3) && (tga_image_type != 10) && (tga_image_type != 11) ) {
            stbi__rewind(s);
            return 0; // only RGB or grey allowed, +/- RLE
        }
        stbi__skip(s,9); // skip colormap specification and image x/y origin
        tga_colormap_bpp = 0;
    }
    tga_w = stbi__get16le(s);
    if( tga_w < 1 ) {
        stbi__rewind(s);
        return 0;   // test width
    }
    tga_h = stbi__get16le(s);
    if( tga_h < 1 ) {
        stbi__rewind(s);
        return 0;   // test height
    }
    tga_bits_per_pixel = stbi__get8(s); // bits per pixel
    stbi__get8(s); // ignore alpha bits
    if (tga_colormap_bpp != 0) {
        if((tga_bits_per_pixel != 8) && (tga_bits_per_pixel != 16)) {
            // when using a colormap, tga_bits_per_pixel is the size of the indexes
            // I don't think anything but 8 or 16bit indexes makes sense
            stbi__rewind(s);
            return 0;
        }
        tga_comp = stbi__tga_get_comp(tga_colormap_bpp, 0, NULL);
    } else {
        tga_comp = stbi__tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3) || (tga_image_type == 11), NULL);
    }
    if(!tga_comp) {
      stbi__rewind(s);
      return 0;
    }
    if (x) *x = tga_w;
    if (y) *y = tga_h;
    if (comp) *comp = tga_comp;
    return 1;                   // seems to have passed everything
}

static int stbi__tga_test(stbi__context *s)
{
   int res = 0;
   int sz, tga_color_type;
   stbi__get8(s);      //   discard Offset
   tga_color_type = stbi__get8(s);   //   color type
   if ( tga_color_type > 1 ) goto errorEnd;   //   only RGB or indexed allowed
   sz = stbi__get8(s);   //   image type
   if ( tga_color_type == 1 ) { // colormapped (paletted) image
      if (sz != 1 && sz != 9) goto errorEnd; // colortype 1 demands image type 1 or 9
      stbi__skip(s,4);       // skip index of first colormap entry and number of entries
      sz = stbi__get8(s);    //   check bits per palette color entry
      if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;
      stbi__skip(s,4);       // skip image x and y origin
   } else { // "normal" image w/o colormap
      if ( (sz != 2) && (sz != 3) && (sz != 10) && (sz != 11) ) goto errorEnd; // only RGB or grey allowed, +/- RLE
      stbi__skip(s,9); // skip colormap specification and image x/y origin
   }
   if ( stbi__get16le(s) < 1 ) goto errorEnd;      //   test width
   if ( stbi__get16le(s) < 1 ) goto errorEnd;      //   test height
   sz = stbi__get8(s);   //   bits per pixel
   if ( (tga_color_type == 1) && (sz != 8) && (sz != 16) ) goto errorEnd; // for colormapped images, bpp is size of an index
   if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;

   res = 1; // if we got this far, everything's good and we can return 1 instead of 0

errorEnd:
   stbi__rewind(s);
   return res;
}

// read 16bit value and convert to 24bit RGB
static void stbi__tga_read_rgb16(stbi__context *s, stbi_uc* out)
{
   stbi__uint16 px = (stbi__uint16)stbi__get16le(s);
   stbi__uint16 fiveBitMask = 31;
   // we have 3 channels with 5bits each
   int r = (px >> 10) & fiveBitMask;
   int g = (px >> 5) & fiveBitMask;
   int b = px & fiveBitMask;
   // Note that this saves the data in RGB(A) order, so it doesn't need to be swapped later
   out[0] = (stbi_uc)((r * 255)/31);
   out[1] = (stbi_uc)((g * 255)/31);
   out[2] = (stbi_uc)((b * 255)/31);

   // some people claim that the most significant bit might be used for alpha
   // (possibly if an alpha-bit is set in the "image descriptor byte")
   // but that only made 16bit test images completely translucent..
   // so let's treat all 15 and 16bit TGAs as RGB with no alpha.
}

static void *stbi__tga_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   //   read in the TGA header stuff
   int tga_offset = stbi__get8(s);
   int tga_indexed = stbi__get8(s);
   int tga_image_type = stbi__get8(s);
   int tga_is_RLE = 0;
   int tga_palette_start = stbi__get16le(s);
   int tga_palette_len = stbi__get16le(s);
   int tga_palette_bits = stbi__get8(s);
   int tga_x_origin = stbi__get16le(s);
   int tga_y_origin = stbi__get16le(s);
   int tga_width = stbi__get16le(s);
   int tga_height = stbi__get16le(s);
   int tga_bits_per_pixel = stbi__get8(s);
   int tga_comp, tga_rgb16=0;
   int tga_inverted = stbi__get8(s);
   // int tga_alpha_bits = tga_inverted & 15; // the 4 lowest bits - unused (useless?)
   //   image data
   unsigned char *tga_data;
   unsigned char *tga_palette = NULL;
   int i, j;
   unsigned char raw_data[4] = {0};
   int RLE_count = 0;
   int RLE_repeating = 0;
   int read_next_pixel = 1;
   STBI_NOTUSED(ri);
   STBI_NOTUSED(tga_x_origin); // @TODO
   STBI_NOTUSED(tga_y_origin); // @TODO

   if (tga_height > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (tga_width > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   //   do a tiny bit of precessing
   if ( tga_image_type >= 8 )
   {
      tga_image_type -= 8;
      tga_is_RLE = 1;
   }
   tga_inverted = 1 - ((tga_inverted >> 5) & 1);

   //   If I'm paletted, then I'll use the number of bits from the palette
   if ( tga_indexed ) tga_comp = stbi__tga_get_comp(tga_palette_bits, 0, &tga_rgb16);
   else tga_comp = stbi__tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3), &tga_rgb16);

   if(!tga_comp) // shouldn't really happen, stbi__tga_test() should have ensured basic consistency
      return stbi__errpuc("bad format", "Can't find out TGA pixelformat");

   //   tga info
   *x = tga_width;
   *y = tga_height;
   if (comp) *comp = tga_comp;

   if (!stbi__mad3sizes_valid(tga_width, tga_height, tga_comp, 0))
      return stbi__errpuc("too large", "Corrupt TGA");

   tga_data = (unsigned char*)stbi__malloc_mad3(tga_width, tga_height, tga_comp, 0);
   if (!tga_data) return stbi__errpuc("outofmem", "Out of memory");

   // skip to the data's starting position (offset usually = 0)
   stbi__skip(s, tga_offset );

   if ( !tga_indexed && !tga_is_RLE && !tga_rgb16 ) {
      for (i=0; i < tga_height; ++i) {
         int row = tga_inverted ? tga_height -i - 1 : i;
         stbi_uc *tga_row = tga_data + row*tga_width*tga_comp;
         stbi__getn(s, tga_row, tga_width * tga_comp);
      }
   } else  {
      //   do I need to load a palette?
      if ( tga_indexed)
      {
         if (tga_palette_len == 0) {  /* you have to have at least one entry! */
            STBI_FREE(tga_data);
            return stbi__errpuc("bad palette", "Corrupt TGA");
         }

         //   any data to skip? (offset usually = 0)
         stbi__skip(s, tga_palette_start );
         //   load the palette
         tga_palette = (unsigned char*)stbi__malloc_mad2(tga_palette_len, tga_comp, 0);
         if (!tga_palette) {
            STBI_FREE(tga_data);
            return stbi__errpuc("outofmem", "Out of memory");
         }
         if (tga_rgb16) {
            stbi_uc *pal_entry = tga_palette;
            STBI_ASSERT(tga_comp == STBI_rgb);
            for (i=0; i < tga_palette_len; ++i) {
               stbi__tga_read_rgb16(s, pal_entry);
               pal_entry += tga_comp;
            }
         } else if (!stbi__getn(s, tga_palette, tga_palette_len * tga_comp)) {
               STBI_FREE(tga_data);
               STBI_FREE(tga_palette);
               return stbi__errpuc("bad palette", "Corrupt TGA");
         }
      }
      //   load the data
      for (i=0; i < tga_width * tga_height; ++i)
      {
         //   if I'm in RLE mode, do I need to get a RLE stbi__pngchunk?
         if ( tga_is_RLE )
         {
            if ( RLE_count == 0 )
            {
               //   yep, get the next byte as a RLE command
               int RLE_cmd = stbi__get8(s);
               RLE_count = 1 + (RLE_cmd & 127);
               RLE_repeating = RLE_cmd >> 7;
               read_next_pixel = 1;
            } else if ( !RLE_repeating )
            {
               read_next_pixel = 1;
            }
         } else
         {
            read_next_pixel = 1;
         }
         //   OK, if I need to read a pixel, do it now
         if ( read_next_pixel )
         {
            //   load however much data we did have
            if ( tga_indexed )
            {
               // read in index, then perform the lookup
               int pal_idx = (tga_bits_per_pixel == 8) ? stbi__get8(s) : stbi__get16le(s);
               if ( pal_idx >= tga_palette_len ) {
                  // invalid index
                  pal_idx = 0;
               }
               pal_idx *= tga_comp;
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = tga_palette[pal_idx+j];
               }
            } else if(tga_rgb16) {
               STBI_ASSERT(tga_comp == STBI_rgb);
               stbi__tga_read_rgb16(s, raw_data);
            } else {
               //   read in the data raw
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = stbi__get8(s);
               }
            }
            //   clear the reading flag for the next pixel
            read_next_pixel = 0;
         } // end of reading a pixel

         // copy data
         for (j = 0; j < tga_comp; ++j)
           tga_data[i*tga_comp+j] = raw_data[j];

         //   in case we're in RLE mode, keep counting down
         --RLE_count;
      }
      //   do I need to invert the image?
      if ( tga_inverted )
      {
         for (j = 0; j*2 < tga_height; ++j)
         {
            int index1 = j * tga_width * tga_comp;
            int index2 = (tga_height - 1 - j) * tga_width * tga_comp;
            for (i = tga_width * tga_comp; i > 0; --i)
            {
               unsigned char temp = tga_data[index1];
               tga_data[index1] = tga_data[index2];
               tga_data[index2] = temp;
               ++index1;
               ++index2;
            }
         }
      }
      //   clear my palette, if I had one
      if ( tga_palette != NULL )
      {
         STBI_FREE( tga_palette );
      }
   }

   // swap RGB - if the source data was RGB16, it already is in the right order
   if (tga_comp >= 3 && !tga_rgb16)
   {
      unsigned char* tga_pixel = tga_data;
      for (i=0; i < tga_width * tga_height; ++i)
      {
         unsigned char temp = tga_pixel[0];
         tga_pixel[0] = tga_pixel[2];
         tga_pixel[2] = temp;
         tga_pixel += tga_comp;
      }
   }

   // convert to target component count
   if (req_comp && req_comp != tga_comp)
      tga_data = stbi__convert_format(tga_data, tga_comp, req_comp, tga_width, tga_height);

   //   the things I do to get rid of an error message, and yet keep
   //   Microsoft's C compilers happy... [8^(
   tga_palette_start = tga_palette_len = tga_palette_bits =
         tga_x_origin = tga_y_origin = 0;
   STBI_NOTUSED(tga_palette_start);
   //   OK, done
   return tga_data;
}
#endif

// *************************************************************************************************
// Photoshop PSD loader -- PD by Thatcher Ulrich, integration by Nicolas Schulz, tweaked by STB

#ifndef STBI_NO_PSD
static int stbi__psd_test(stbi__context *s)
{
   int r = (stbi__get32be(s) == 0x38425053);
   stbi__rewind(s);
   return r;
}

static int stbi__psd_decode_rle(stbi__context *s, stbi_uc *p, int pixelCount)
{
   int count, nleft, len;

   count = 0;
   while ((nleft = pixelCount - count) > 0) {
      len = stbi__get8(s);
      if (len == 128) {
         // No-op.
      } else if (len < 128) {
         // Copy next len+1 bytes literally.
         len++;
         if (len > nleft) return 0; // corrupt data
         count += len;
         while (len) {
            *p = stbi__get8(s);
            p += 4;
            len--;
         }
      } else if (len > 128) {
         stbi_uc   val;
         // Next -len+1 bytes in the dest are replicated from next source byte.
         // (Interpret len as a negative 8-bit int.)
         len = 257 - len;
         if (len > nleft) return 0; // corrupt data
         val = stbi__get8(s);
         count += len;
         while (len) {
            *p = val;
            p += 4;
            len--;
         }
      }
   }

   return 1;
}

static void *stbi__psd_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
   int pixelCount;
   int channelCount, compression;
   int channel, i;
   int bitdepth;
   int w,h;
   stbi_uc *out;
   STBI_NOTUSED(ri);

   // Check identifier
   if (stbi__get32be(s) != 0x38425053)   // "8BPS"
      return stbi__errpuc("not PSD", "Corrupt PSD image");

   // Check file type version.
   if (stbi__get16be(s) != 1)
      return stbi__errpuc("wrong version", "Unsupported version of PSD image");

   // Skip 6 reserved bytes.
   stbi__skip(s, 6 );

   // Read the number of channels (R, G, B, A, etc).
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16)
      return stbi__errpuc("wrong channel count", "Unsupported number of channels in PSD image");

   // Read the rows and columns of the image.
   h = stbi__get32be(s);
   w = stbi__get32be(s);

   if (h > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (w > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   // Make sure the depth is 8 bits.
   bitdepth = stbi__get16be(s);
   if (bitdepth != 8 && bitdepth != 16)
      return stbi__errpuc("unsupported bit depth", "PSD bit depth is not 8 or 16 bit");

   // Make sure the color mode is RGB.
   // Valid options are:
   //   0: Bitmap
   //   1: Grayscale
   //   2: Indexed color
   //   3: RGB color
   //   4: CMYK color
   //   7: Multichannel
   //   8: Duotone
   //   9: Lab color
   if (stbi__get16be(s) != 3)
      return stbi__errpuc("wrong color format", "PSD is not in RGB color format");

   // Skip the Mode Data.  (It's the palette for indexed color; other info for other modes.)
   stbi__skip(s,stbi__get32be(s) );

   // Skip the image resources.  (resolution, pen tool paths, etc)
   stbi__skip(s, stbi__get32be(s) );

   // Skip the reserved data.
   stbi__skip(s, stbi__get32be(s) );

   // Find out if the data is compressed.
   // Known values:
   //   0: no compression
   //   1: RLE compressed
   compression = stbi__get16be(s);
   if (compression > 1)
      return stbi__errpuc("bad compression", "PSD has an unknown compression format");

   // Check size
   if (!stbi__mad3sizes_valid(4, w, h, 0))
      return stbi__errpuc("too large", "Corrupt PSD");

   // Create the destination image.

   if (!compression && bitdepth == 16 && bpc == 16) {
      out = (stbi_uc *) stbi__malloc_mad3(8, w, h, 0);
      ri->bits_per_channel = 16;
   } else
      out = (stbi_uc *) stbi__malloc(4 * w*h);

   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   pixelCount = w*h;

   // Initialize the data to zero.
   //memset( out, 0, pixelCount * 4 );

   // Finally, the image data.
   if (compression) {
      // RLE as used by .PSD and .TIFF
      // Loop until you get the number of unpacked bytes you are expecting:
      //     Read the next source byte into n.
      //     If n is between 0 and 127 inclusive, copy the next n+1 bytes literally.
      //     Else if n is between -127 and -1 inclusive, copy the next byte -n+1 times.
      //     Else if n is 128, noop.
      // Endloop

      // The RLE-compressed data is preceded by a 2-byte data count for each row in the data,
      // which we're going to just skip.
      stbi__skip(s, h * channelCount * 2 );

      // Read the RLE data by channel.
      for (channel = 0; channel < 4; channel++) {
         stbi_uc *p;

         p = out+channel;
         if (channel >= channelCount) {
            // Fill this channel with default data.
            for (i = 0; i < pixelCount; i++, p += 4)
               *p = (channel == 3 ? 255 : 0);
         } else {
            // Read the RLE data.
            if (!stbi__psd_decode_rle(s, p, pixelCount)) {
               STBI_FREE(out);
               return stbi__errpuc("corrupt", "bad RLE data");
            }
         }
      }

   } else {
      // We're at the raw image data.  It's each channel in order (Red, Green, Blue, Alpha, ...)
      // where each channel consists of an 8-bit (or 16-bit) value for each pixel in the image.

      // Read the data by channel.
      for (channel = 0; channel < 4; channel++) {
         if (channel >= channelCount) {
            // Fill this channel with default data.
            if (bitdepth == 16 && bpc == 16) {
               stbi__uint16 *q = ((stbi__uint16 *) out) + channel;
               stbi__uint16 val = channel == 3 ? 65535 : 0;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = val;
            } else {
               stbi_uc *p = out+channel;
               stbi_uc val = channel == 3 ? 255 : 0;
               for (i = 0; i < pixelCount; i++, p += 4)
                  *p = val;
            }
         } else {
            if (ri->bits_per_channel == 16) {    // output bpc
               stbi__uint16 *q = ((stbi__uint16 *) out) + channel;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = (stbi__uint16) stbi__get16be(s);
            } else {
               stbi_uc *p = out+channel;
               if (bitdepth == 16) {  // input bpc
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = (stbi_uc) (stbi__get16be(s) >> 8);
               } else {
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = stbi__get8(s);
               }
            }
         }
      }
   }

   // remove weird white matte from PSD
   if (channelCount >= 4) {
      if (ri->bits_per_channel == 16) {
         for (i=0; i < w*h; ++i) {
            stbi__uint16 *pixel = (stbi__uint16 *) out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 65535) {
               float a = pixel[3] / 65535.0f;
               float ra = 1.0f / a;
               float inv_a = 65535.0f * (1 - ra);
               pixel[0] = (stbi__uint16) (pixel[0]*ra + inv_a);
               pixel[1] = (stbi__uint16) (pixel[1]*ra + inv_a);
               pixel[2] = (stbi__uint16) (pixel[2]*ra + inv_a);
            }
         }
      } else {
         for (i=0; i < w*h; ++i) {
            unsigned char *pixel = out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 255) {
               float a = pixel[3] / 255.0f;
               float ra = 1.0f / a;
               float inv_a = 255.0f * (1 - ra);
               pixel[0] = (unsigned char) (pixel[0]*ra + inv_a);
               pixel[1] = (unsigned char) (pixel[1]*ra + inv_a);
               pixel[2] = (unsigned char) (pixel[2]*ra + inv_a);
            }
         }
      }
   }

   // convert to desired output format
   if (req_comp && req_comp != 4) {
      if (ri->bits_per_channel == 16)
         out = (stbi_uc *) stbi__convert_format16((stbi__uint16 *) out, 4, req_comp, w, h);
      else
         out = stbi__convert_format(out, 4, req_comp, w, h);
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }

   if (comp) *comp = 4;
   *y = h;
   *x = w;

   return out;
}
#endif

// *************************************************************************************************
// Softimage PIC loader
// by Tom Seddon
//
// See http://softimage.wiki.softimage.com/index.php/INFO:_PIC_file_format
// See http://ozviz.wasp.uwa.edu.au/~pbourke/dataformats/softimagepic/

#ifndef STBI_NO_PIC
static int stbi__pic_is4(stbi__context *s,const char *str)
{
   int i;
   for (i=0; i<4; ++i)
      if (stbi__get8(s) != (stbi_uc)str[i])
         return 0;

   return 1;
}

static int stbi__pic_test_core(stbi__context *s)
{
   int i;

   if (!stbi__pic_is4(s,"\x53\x80\xF6\x34"))
      return 0;

   for(i=0;i<84;++i)
      stbi__get8(s);

   if (!stbi__pic_is4(s,"PICT"))
      return 0;

   return 1;
}

typedef struct
{
   stbi_uc size,type,channel;
} stbi__pic_packet;

static stbi_uc *stbi__readval(stbi__context *s, int channel, stbi_uc *dest)
{
   int mask=0x80, i;

   for (i=0; i<4; ++i, mask>>=1) {
      if (channel & mask) {
         if (stbi__at_eof(s)) return stbi__errpuc("bad file","PIC file too short");
         dest[i]=stbi__get8(s);
      }
   }

   return dest;
}

static void stbi__copyval(int channel,stbi_uc *dest,const stbi_uc *src)
{
   int mask=0x80,i;

   for (i=0;i<4; ++i, mask>>=1)
      if (channel&mask)
         dest[i]=src[i];
}

static stbi_uc *stbi__pic_load_core(stbi__context *s,int width,int height,int *comp, stbi_uc *result)
{
   int act_comp=0,num_packets=0,y,chained;
   stbi__pic_packet packets[10];

   // this will (should...) cater for even some bizarre stuff like having data
    // for the same channel in multiple packets.
   do {
      stbi__pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return stbi__errpuc("bad format","too many packets");

      packet = &packets[num_packets++];

      chained = stbi__get8(s);
      packet->size    = stbi__get8(s);
      packet->type    = stbi__get8(s);
      packet->channel = stbi__get8(s);

      act_comp |= packet->channel;

      if (stbi__at_eof(s))          return stbi__errpuc("bad file","file too short (reading packets)");
      if (packet->size != 8)  return stbi__errpuc("bad format","packet isn't 8bpp");
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3); // has alpha channel?

   for(y=0; y<height; ++y) {
      int packet_idx;

      for(packet_idx=0; packet_idx < num_packets; ++packet_idx) {
         stbi__pic_packet *packet = &packets[packet_idx];
         stbi_uc *dest = result+y*width*4;

         switch (packet->type) {
            default:
               return stbi__errpuc("bad format","packet has bad compression type");

            case 0: {//uncompressed
               int x;

               for(x=0;x<width;++x, dest+=4)
                  if (!stbi__readval(s,packet->channel,dest))
                     return 0;
               break;
            }

            case 1://Pure RLE
               {
                  int left=width, i;

                  while (left>0) {
                     stbi_uc count,value[4];

                     count=stbi__get8(s);
                     if (stbi__at_eof(s))   return stbi__errpuc("bad file","file too short (pure read count)");

                     if (count > left)
                        count = (stbi_uc) left;

                     if (!stbi__readval(s,packet->channel,value))  return 0;

                     for(i=0; i<count; ++i,dest+=4)
                        stbi__copyval(packet->channel,dest,value);
                     left -= count;
                  }
               }
               break;

            case 2: {//Mixed RLE
               int left=width;
               while (left>0) {
                  int count = stbi__get8(s), i;
                  if (stbi__at_eof(s))  return stbi__errpuc("bad file","file too short (mixed read count)");

                  if (count >= 128) { // Repeated
                     stbi_uc value[4];

                     if (count==128)
                        count = stbi__get16be(s);
                     else
                        count -= 127;
                     if (count > left)
                        return stbi__errpuc("bad file","scanline overrun");

                     if (!stbi__readval(s,packet->channel,value))
                        return 0;

                     for(i=0;i<count;++i, dest += 4)
                        stbi__copyval(packet->channel,dest,value);
                  } else { // Raw
                     ++count;
                     if (count>left) return stbi__errpuc("bad file","scanline overrun");

                     for(i=0;i<count;++i, dest+=4)
                        if (!stbi__readval(s,packet->channel,dest))
                           return 0;
                  }
                  left-=count;
               }
               break;
            }
         }
      }
   }

   return result;
}

static void *stbi__pic_load(stbi__context *s,int *px,int *py,int *comp,int req_comp, stbi__result_info *ri)
{
   stbi_uc *result;
   int i, x,y, internal_comp;
   STBI_NOTUSED(ri);

   if (!comp) comp = &internal_comp;

   for (i=0; i<92; ++i)
      stbi__get8(s);

   x = stbi__get16be(s);
   y = stbi__get16be(s);

   if (y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   if (stbi__at_eof(s))  return stbi__errpuc("bad file","file too short (pic header)");
   if (!stbi__mad3sizes_valid(x, y, 4, 0)) return stbi__errpuc("too large", "PIC image too large to decode");

   stbi__get32be(s); //skip `ratio'
   stbi__get16be(s); //skip `fields'
   stbi__get16be(s); //skip `pad'

   // intermediate buffer is RGBA
   result = (stbi_uc *) stbi__malloc_mad3(x, y, 4, 0);
   if (!result) return stbi__errpuc("outofmem", "Out of memory");
   memset(result, 0xff, x*y*4);

   if (!stbi__pic_load_core(s,x,y,comp, result)) {
      STBI_FREE(result);
      result=0;
   }
   *px = x;
   *py = y;
   if (req_comp == 0) req_comp = *comp;
   result=stbi__convert_format(result,4,req_comp,x,y);

   return result;
}

static int stbi__pic_test(stbi__context *s)
{
   int r = stbi__pic_test_core(s);
   stbi__rewind(s);
   return r;
}
#endif

// *************************************************************************************************
// GIF loader -- public domain by Jean-Marc Lienher -- simplified/shrunk by stb

#ifndef STBI_NO_GIF
typedef struct
{
   stbi__int16 prefix;
   stbi_uc first;
   stbi_uc suffix;
} stbi__gif_lzw;

typedef struct
{
   int w,h;
   stbi_uc *out;                 // output buffer (always 4 components)
   stbi_uc *background;          // The current "background" as far as a gif is concerned
   stbi_uc *history;
   int flags, bgindex, ratio, transparent, eflags;
   stbi_uc  pal[256][4];
   stbi_uc lpal[256][4];
   stbi__gif_lzw codes[8192];
   stbi_uc *color_table;
   int parse, step;
   int lflags;
   int start_x, start_y;
   int max_x, max_y;
   int cur_x, cur_y;
   int line_size;
   int delay;
} stbi__gif;

static int stbi__gif_test_raw(stbi__context *s)
{
   int sz;
   if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8') return 0;
   sz = stbi__get8(s);
   if (sz != '9' && sz != '7') return 0;
   if (stbi__get8(s) != 'a') return 0;
   return 1;
}

static int stbi__gif_test(stbi__context *s)
{
   int r = stbi__gif_test_raw(s);
   stbi__rewind(s);
   return r;
}

static void stbi__gif_parse_colortable(stbi__context *s, stbi_uc pal[256][4], int num_entries, int transp)
{
   int i;
   for (i=0; i < num_entries; ++i) {
      pal[i][2] = stbi__get8(s);
      pal[i][1] = stbi__get8(s);
      pal[i][0] = stbi__get8(s);
      pal[i][3] = transp == i ? 0 : 255;
   }
}

static int stbi__gif_header(stbi__context *s, stbi__gif *g, int *comp, int is_info)
{
   stbi_uc version;
   if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8')
      return stbi__err("not GIF", "Corrupt GIF");

   version = stbi__get8(s);
   if (version != '7' && version != '9')    return stbi__err("not GIF", "Corrupt GIF");
   if (stbi__get8(s) != 'a')                return stbi__err("not GIF", "Corrupt GIF");

   stbi__g_failure_reason = "";
   g->w = stbi__get16le(s);
   g->h = stbi__get16le(s);
   g->flags = stbi__get8(s);
   g->bgindex = stbi__get8(s);
   g->ratio = stbi__get8(s);
   g->transparent = -1;

   if (g->w > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   if (g->h > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");

   if (comp != 0) *comp = 4;  // can't actually tell whether it's 3 or 4 until we parse the comments

   if (is_info) return 1;

   if (g->flags & 0x80)
      stbi__gif_parse_colortable(s,g->pal, 2 << (g->flags & 7), -1);

   return 1;
}

static int stbi__gif_info_raw(stbi__context *s, int *x, int *y, int *comp)
{
   stbi__gif* g = (stbi__gif*) stbi__malloc(sizeof(stbi__gif));
   if (!g) return stbi__err("outofmem", "Out of memory");
   if (!stbi__gif_header(s, g, comp, 1)) {
      STBI_FREE(g);
      stbi__rewind( s );
      return 0;
   }
   if (x) *x = g->w;
   if (y) *y = g->h;
   STBI_FREE(g);
   return 1;
}

static void stbi__out_gif_code(stbi__gif *g, stbi__uint16 code)
{
   stbi_uc *p, *c;
   int idx;

   // recurse to decode the prefixes, since the linked-list is backwards,
   // and working backwards through an interleaved image would be nasty
   if (g->codes[code].prefix >= 0)
      stbi__out_gif_code(g, g->codes[code].prefix);

   if (g->cur_y >= g->max_y) return;

   idx = g->cur_x + g->cur_y;
   p = &g->out[idx];
   g->history[idx / 4] = 1;

   c = &g->color_table[g->codes[code].suffix * 4];
   if (c[3] > 128) { // don't render transparent pixels;
      p[0] = c[2];
      p[1] = c[1];
      p[2] = c[0];
      p[3] = c[3];
   }
   g->cur_x += 4;

   if (g->cur_x >= g->max_x) {
      g->cur_x = g->start_x;
      g->cur_y += g->step;

      while (g->cur_y >= g->max_y && g->parse > 0) {
         g->step = (1 << g->parse) * g->line_size;
         g->cur_y = g->start_y + (g->step >> 1);
         --g->parse;
      }
   }
}

static stbi_uc *stbi__process_gif_raster(stbi__context *s, stbi__gif *g)
{
   stbi_uc lzw_cs;
   stbi__int32 len, init_code;
   stbi__uint32 first;
   stbi__int32 codesize, codemask, avail, oldcode, bits, valid_bits, clear;
   stbi__gif_lzw *p;

   lzw_cs = stbi__get8(s);
   if (lzw_cs > 12) return NULL;
   clear = 1 << lzw_cs;
   first = 1;
   codesize = lzw_cs + 1;
   codemask = (1 << codesize) - 1;
   bits = 0;
   valid_bits = 0;
   for (init_code = 0; init_code < clear; init_code++) {
      g->codes[init_code].prefix = -1;
      g->codes[init_code].first = (stbi_uc) init_code;
      g->codes[init_code].suffix = (stbi_uc) init_code;
   }

   // support no starting clear code
   avail = clear+2;
   oldcode = -1;

   len = 0;
   for(;;) {
      if (valid_bits < codesize) {
         if (len == 0) {
            len = stbi__get8(s); // start new block
            if (len == 0)
               return g->out;
         }
         --len;
         bits |= (stbi__int32) stbi__get8(s) << valid_bits;
         valid_bits += 8;
      } else {
         stbi__int32 code = bits & codemask;
         bits >>= codesize;
         valid_bits -= codesize;
         // @OPTIMIZE: is there some way we can accelerate the non-clear path?
         if (code == clear) {  // clear code
            codesize = lzw_cs + 1;
            codemask = (1 << codesize) - 1;
            avail = clear + 2;
            oldcode = -1;
            first = 0;
         } else if (code == clear + 1) { // end of stream code
            stbi__skip(s, len);
            while ((len = stbi__get8(s)) > 0)
               stbi__skip(s,len);
            return g->out;
         } else if (code <= avail) {
            if (first) {
               return stbi__errpuc("no clear code", "Corrupt GIF");
            }

            if (oldcode >= 0) {
               p = &g->codes[avail++];
               if (avail > 8192) {
                  return stbi__errpuc("too many codes", "Corrupt GIF");
               }

               p->prefix = (stbi__int16) oldcode;
               p->first = g->codes[oldcode].first;
               p->suffix = (code == avail) ? p->first : g->codes[code].first;
            } else if (code == avail)
               return stbi__errpuc("illegal code in raster", "Corrupt GIF");

            stbi__out_gif_code(g, (stbi__uint16) code);

            if ((avail & codemask) == 0 && avail <= 0x0FFF) {
               codesize++;
               codemask = (1 << codesize) - 1;
            }

            oldcode = code;
         } else {
            return stbi__errpuc("illegal code in raster", "Corrupt GIF");
         }
      }
   }
}

// this function is designed to support animated gifs, although stb_image doesn't support it
// two back is the image from two frames ago, used for a very specific disposal format
static stbi_uc *stbi__gif_load_next(stbi__context *s, stbi__gif *g, int *comp, int req_comp, stbi_uc *two_back)
{
   int dispose;
   int first_frame;
   int pi;
   int pcount;
   STBI_NOTUSED(req_comp);

   // on first frame, any non-written pixels get the background colour (non-transparent)
   first_frame = 0;
   if (g->out == 0) {
      if (!stbi__gif_header(s, g, comp,0)) return 0; // stbi__g_failure_reason set by stbi__gif_header
      if (!stbi__mad3sizes_valid(4, g->w, g->h, 0))
         return stbi__errpuc("too large", "GIF image is too large");
      pcount = g->w * g->h;
      g->out = (stbi_uc *) stbi__malloc(4 * pcount);
      g->background = (stbi_uc *) stbi__malloc(4 * pcount);
      g->history = (stbi_uc *) stbi__malloc(pcount);
      if (!g->out || !g->background || !g->history)
         return stbi__errpuc("outofmem", "Out of memory");

      // image is treated as "transparent" at the start - ie, nothing overwrites the current background;
      // background colour is only used for pixels that are not rendered first frame, after that "background"
      // color refers to the color that was there the previous frame.
      memset(g->out, 0x00, 4 * pcount);
      memset(g->background, 0x00, 4 * pcount); // state of the background (starts transparent)
      memset(g->history, 0x00, pcount);        // pixels that were affected previous frame
      first_frame = 1;
   } else {
      // second frame - how do we dispose of the previous one?
      dispose = (g->eflags & 0x1C) >> 2;
      pcount = g->w * g->h;

      if ((dispose == 3) && (two_back == 0)) {
         dispose = 2; // if I don't have an image to revert back to, default to the old background
      }

      if (dispose == 3) { // use previous graphic
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &two_back[pi * 4], 4 );
            }
         }
      } else if (dispose == 2) {
         // restore what was changed last frame to background before that frame;
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &g->background[pi * 4], 4 );
            }
         }
      } else {
         // This is a non-disposal case eithe way, so just
         // leave the pixels as is, and they will become the new background
         // 1: do not dispose
         // 0:  not specified.
      }

      // background is what out is after the undoing of the previou frame;
      memcpy( g->background, g->out, 4 * g->w * g->h );
   }

   // clear my history;
   memset( g->history, 0x00, g->w * g->h );        // pixels that were affected previous frame

   for (;;) {
      int tag = stbi__get8(s);
      switch (tag) {
         case 0x2C: /* Image Descriptor */
         {
            stbi__int32 x, y, w, h;
            stbi_uc *o;

            x = stbi__get16le(s);
            y = stbi__get16le(s);
            w = stbi__get16le(s);
            h = stbi__get16le(s);
            if (((x + w) > (g->w)) || ((y + h) > (g->h)))
               return stbi__errpuc("bad Image Descriptor", "Corrupt GIF");

            g->line_size = g->w * 4;
            g->start_x = x * 4;
            g->start_y = y * g->line_size;
            g->max_x   = g->start_x + w * 4;
            g->max_y   = g->start_y + h * g->line_size;
            g->cur_x   = g->start_x;
            g->cur_y   = g->start_y;

            // if the width of the specified rectangle is 0, that means
            // we may not see *any* pixels or the image is malformed;
            // to make sure this is caught, move the current y down to
            // max_y (which is what out_gif_code checks).
            if (w == 0)
               g->cur_y = g->max_y;

            g->lflags = stbi__get8(s);

            if (g->lflags & 0x40) {
               g->step = 8 * g->line_size; // first interlaced spacing
               g->parse = 3;
            } else {
               g->step = g->line_size;
               g->parse = 0;
            }

            if (g->lflags & 0x80) {
               stbi__gif_parse_colortable(s,g->lpal, 2 << (g->lflags & 7), g->eflags & 0x01 ? g->transparent : -1);
               g->color_table = (stbi_uc *) g->lpal;
            } else if (g->flags & 0x80) {
               g->color_table = (stbi_uc *) g->pal;
            } else
               return stbi__errpuc("missing color table", "Corrupt GIF");

            o = stbi__process_gif_raster(s, g);
            if (!o) return NULL;

            // if this was the first frame,
            pcount = g->w * g->h;
            if (first_frame && (g->bgindex > 0)) {
               // if first frame, any pixel not drawn to gets the background color
               for (pi = 0; pi < pcount; ++pi) {
                  if (g->history[pi] == 0) {
                     g->pal[g->bgindex][3] = 255; // just in case it was made transparent, undo that; It will be reset next frame if need be;
                     memcpy( &g->out[pi * 4], &g->pal[g->bgindex], 4 );
                  }
               }
            }

            return o;
         }

         case 0x21: // Comment Extension.
         {
            int len;
            int ext = stbi__get8(s);
            if (ext == 0xF9) { // Graphic Control Extension.
               len = stbi__get8(s);
               if (len == 4) {
                  g->eflags = stbi__get8(s);
                  g->delay = 10 * stbi__get16le(s); // delay - 1/100th of a second, saving as 1/1000ths.

                  // unset old transparent
                  if (g->transparent >= 0) {
                     g->pal[g->transparent][3] = 255;
                  }
                  if (g->eflags & 0x01) {
                     g->transparent = stbi__get8(s);
                     if (g->transparent >= 0) {
                        g->pal[g->transparent][3] = 0;
                     }
                  } else {
                     // don't need transparent
                     stbi__skip(s, 1);
                     g->transparent = -1;
                  }
               } else {
                  stbi__skip(s, len);
                  break;
               }
            }
            while ((len = stbi__get8(s)) != 0) {
               stbi__skip(s, len);
            }
            break;
         }

         case 0x3B: // gif stream termination code
            return (stbi_uc *) s; // using '1' causes warning on some compilers

         default:
            return stbi__errpuc("unknown code", "Corrupt GIF");
      }
   }
}

static void *stbi__load_gif_main_outofmem(stbi__gif *g, stbi_uc *out, int **delays)
{
   STBI_FREE(g->out);
   STBI_FREE(g->history);
   STBI_FREE(g->background);

   if (out) STBI_FREE(out);
   if (delays && *delays) STBI_FREE(*delays);
   return stbi__errpuc("outofmem", "Out of memory");
}

static void *stbi__load_gif_main(stbi__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   if (stbi__gif_test(s)) {
      int layers = 0;
      stbi_uc *u = 0;
      stbi_uc *out = 0;
      stbi_uc *two_back = 0;
      stbi__gif g;
      int stride;
      int out_size = 0;
      int delays_size = 0;

      STBI_NOTUSED(out_size);
      STBI_NOTUSED(delays_size);

      memset(&g, 0, sizeof(g));
      if (delays) {
         *delays = 0;
      }

      do {
         u = stbi__gif_load_next(s, &g, comp, req_comp, two_back);
         if (u == (stbi_uc *) s) u = 0;  // end of animated gif marker

         if (u) {
            *x = g.w;
            *y = g.h;
            ++layers;
            stride = g.w * g.h * 4;

            if (out) {
               void *tmp = (stbi_uc*) STBI_REALLOC_SIZED( out, out_size, layers * stride );
               if (!tmp)
                  return stbi__load_gif_main_outofmem(&g, out, delays);
               else {
                   out = (stbi_uc*) tmp;
                   out_size = layers * stride;
               }

               if (delays) {
                  int *new_delays = (int*) STBI_REALLOC_SIZED( *delays, delays_size, sizeof(int) * layers );
                  if (!new_delays)
                     return stbi__load_gif_main_outofmem(&g, out, delays);
                  *delays = new_delays;
                  delays_size = layers * sizeof(int);
               }
            } else {
               out = (stbi_uc*)stbi__malloc( layers * stride );
               if (!out)
                  return stbi__load_gif_main_outofmem(&g, out, delays);
               out_size = layers * stride;
               if (delays) {
                  *delays = (int*) stbi__malloc( layers * sizeof(int) );
                  if (!*delays)
                     return stbi__load_gif_main_outofmem(&g, out, delays);
                  delays_size = layers * sizeof(int);
               }
            }
            memcpy( out + ((layers - 1) * stride), u, stride );
            if (layers >= 2) {
               two_back = out - 2 * stride;
            }

            if (delays) {
               (*delays)[layers - 1U] = g.delay;
            }
         }
      } while (u != 0);

      // free temp buffer;
      STBI_FREE(g.out);
      STBI_FREE(g.history);
      STBI_FREE(g.background);

      // do the final conversion after loading everything;
      if (req_comp && req_comp != 4)
         out = stbi__convert_format(out, 4, req_comp, layers * g.w, g.h);

      *z = layers;
      return out;
   } else {
      return stbi__errpuc("not GIF", "Image was not as a gif type.");
   }
}

static void *stbi__gif_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *u = 0;
   stbi__gif g;
   memset(&g, 0, sizeof(g));
   STBI_NOTUSED(ri);

   u = stbi__gif_load_next(s, &g, comp, req_comp, 0);
   if (u == (stbi_uc *) s) u = 0;  // end of animated gif marker
   if (u) {
      *x = g.w;
      *y = g.h;

      // moved conversion to after successful load so that the same
      // can be done for multiple frames.
      if (req_comp && req_comp != 4)
         u = stbi__convert_format(u, 4, req_comp, g.w, g.h);
   } else if (g.out) {
      // if there was an error and we allocated an image buffer, free it!
      STBI_FREE(g.out);
   }

   // free buffers needed for multiple frame loading;
   STBI_FREE(g.history);
   STBI_FREE(g.background);

   return u;
}

static int stbi__gif_info(stbi__context *s, int *x, int *y, int *comp)
{
   return stbi__gif_info_raw(s,x,y,comp);
}
#endif

// *************************************************************************************************
// Radiance RGBE HDR loader
// originally by Nicolas Schulz
#ifndef STBI_NO_HDR
static int stbi__hdr_test_core(stbi__context *s, const char *signature)
{
   int i;
   for (i=0; signature[i]; ++i)
      if (stbi__get8(s) != signature[i])
          return 0;
   stbi__rewind(s);
   return 1;
}

static int stbi__hdr_test(stbi__context* s)
{
   int r = stbi__hdr_test_core(s, "#?RADIANCE\n");
   stbi__rewind(s);
   if(!r) {
       r = stbi__hdr_test_core(s, "#?RGBE\n");
       stbi__rewind(s);
   }
   return r;
}

#define STBI__HDR_BUFLEN  1024
static char *stbi__hdr_gettoken(stbi__context *z, char *buffer)
{
   int len=0;
   char c = '\0';

   c = (char) stbi__get8(z);

   while (!stbi__at_eof(z) && c != '\n') {
      buffer[len++] = c;
      if (len == STBI__HDR_BUFLEN-1) {
         // flush to end of line
         while (!stbi__at_eof(z) && stbi__get8(z) != '\n')
            ;
         break;
      }
      c = (char) stbi__get8(z);
   }

   buffer[len] = 0;
   return buffer;
}

static void stbi__hdr_convert(float *output, stbi_uc *input, int req_comp)
{
   if ( input[3] != 0 ) {
      float f1;
      // Exponent
      f1 = (float) ldexp(1.0f, input[3] - (int)(128 + 8));
      if (req_comp <= 2)
         output[0] = (input[0] + input[1] + input[2]) * f1 / 3;
      else {
         output[0] = input[0] * f1;
         output[1] = input[1] * f1;
         output[2] = input[2] * f1;
      }
      if (req_comp == 2) output[1] = 1;
      if (req_comp == 4) output[3] = 1;
   } else {
      switch (req_comp) {
         case 4: output[3] = 1; /* fallthrough */
         case 3: output[0] = output[1] = output[2] = 0;
                 break;
         case 2: output[1] = 1; /* fallthrough */
         case 1: output[0] = 0;
                 break;
      }
   }
}

static float *stbi__hdr_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int width, height;
   stbi_uc *scanline;
   float *hdr_data;
   int len;
   unsigned char count, value;
   int i, j, k, c1,c2, z;
   const char *headerToken;
   STBI_NOTUSED(ri);

   // Check identifier
   headerToken = stbi__hdr_gettoken(s,buffer);
   if (strcmp(headerToken, "#?RADIANCE") != 0 && strcmp(headerToken, "#?RGBE") != 0)
      return stbi__errpf("not HDR", "Corrupt HDR image");

   // Parse header
   for(;;) {
      token = stbi__hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid)    return stbi__errpf("unsupported format", "Unsupported HDR format");

   // Parse width and height
   // can't use sscanf() if we're not using stdio!
   token = stbi__hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3))  return stbi__errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   height = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3))  return stbi__errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   width = (int) strtol(token, NULL, 10);

   if (height > STBI_MAX_DIMENSIONS) return stbi__errpf("too large","Very large image (corrupt?)");
   if (width > STBI_MAX_DIMENSIONS) return stbi__errpf("too large","Very large image (corrupt?)");

   *x = width;
   *y = height;

   if (comp) *comp = 3;
   if (req_comp == 0) req_comp = 3;

   if (!stbi__mad4sizes_valid(width, height, req_comp, sizeof(float), 0))
      return stbi__errpf("too large", "HDR image is too large");

   // Read data
   hdr_data = (float *) stbi__malloc_mad4(width, height, req_comp, sizeof(float), 0);
   if (!hdr_data)
      return stbi__errpf("outofmem", "Out of memory");

   // Load image data
   // image data is stored as some number of sca
   if ( width < 8 || width >= 32768) {
      // Read flat data
      for (j=0; j < height; ++j) {
         for (i=0; i < width; ++i) {
            stbi_uc rgbe[4];
           main_decode_loop:
            stbi__getn(s, rgbe, 4);
            stbi__hdr_convert(hdr_data + j * width * req_comp + i * req_comp, rgbe, req_comp);
         }
      }
   } else {
      // Read RLE-encoded data
      scanline = NULL;

      for (j = 0; j < height; ++j) {
         c1 = stbi__get8(s);
         c2 = stbi__get8(s);
         len = stbi__get8(s);
         if (c1 != 2 || c2 != 2 || (len & 0x80)) {
            // not run-length encoded, so we have to actually use THIS data as a decoded
            // pixel (note this can't be a valid pixel--one of RGB must be >= 128)
            stbi_uc rgbe[4];
            rgbe[0] = (stbi_uc) c1;
            rgbe[1] = (stbi_uc) c2;
            rgbe[2] = (stbi_uc) len;
            rgbe[3] = (stbi_uc) stbi__get8(s);
            stbi__hdr_convert(hdr_data, rgbe, req_comp);
            i = 1;
            j = 0;
            STBI_FREE(scanline);
            goto main_decode_loop; // yes, this makes no sense
         }
         len <<= 8;
         len |= stbi__get8(s);
         if (len != width) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("invalid decoded scanline length", "corrupt HDR"); }
         if (scanline == NULL) {
            scanline = (stbi_uc *) stbi__malloc_mad2(width, 4, 0);
            if (!scanline) {
               STBI_FREE(hdr_data);
               return stbi__errpf("outofmem", "Out of memory");
            }
         }

         for (k = 0; k < 4; ++k) {
            int nleft;
            i = 0;
            while ((nleft = width - i) > 0) {
               count = stbi__get8(s);
               if (count > 128) {
                  // Run
                  value = stbi__get8(s);
                  count -= 128;
                  if ((count == 0) || (count > nleft)) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = value;
               } else {
                  // Dump
                  if ((count == 0) || (count > nleft)) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = stbi__get8(s);
               }
            }
         }
         for (i=0; i < width; ++i)
            stbi__hdr_convert(hdr_data+(j*width + i)*req_comp, scanline + i*4, req_comp);
      }
      if (scanline)
         STBI_FREE(scanline);
   }

   return hdr_data;
}

static int stbi__hdr_info(stbi__context *s, int *x, int *y, int *comp)
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int dummy;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (stbi__hdr_test(s) == 0) {
       stbi__rewind( s );
       return 0;
   }

   for(;;) {
      token = stbi__hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid) {
       stbi__rewind( s );
       return 0;
   }
   token = stbi__hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3)) {
       stbi__rewind( s );
       return 0;
   }
   token += 3;
   *y = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3)) {
       stbi__rewind( s );
       return 0;
   }
   token += 3;
   *x = (int) strtol(token, NULL, 10);
   *comp = 3;
   return 1;
}
#endif // STBI_NO_HDR

#ifndef STBI_NO_BMP
static int stbi__bmp_info(stbi__context *s, int *x, int *y, int *comp)
{
   void *p;
   stbi__bmp_data info;

   info.all_a = 255;
   p = stbi__bmp_parse_header(s, &info);
   if (p == NULL) {
      stbi__rewind( s );
      return 0;
   }
   if (x) *x = s->img_x;
   if (y) *y = s->img_y;
   if (comp) {
      if (info.bpp == 24 && info.ma == 0xff000000)
         *comp = 3;
      else
         *comp = info.ma ? 4 : 3;
   }
   return 1;
}
#endif

#ifndef STBI_NO_PSD
static int stbi__psd_info(stbi__context *s, int *x, int *y, int *comp)
{
   int channelCount, dummy, depth;
   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;
   if (stbi__get32be(s) != 0x38425053) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 1) {
       stbi__rewind( s );
       return 0;
   }
   stbi__skip(s, 6);
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       stbi__rewind( s );
       return 0;
   }
   *y = stbi__get32be(s);
   *x = stbi__get32be(s);
   depth = stbi__get16be(s);
   if (depth != 8 && depth != 16) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 3) {
       stbi__rewind( s );
       return 0;
   }
   *comp = 4;
   return 1;
}

static int stbi__psd_is16(stbi__context *s)
{
   int channelCount, depth;
   if (stbi__get32be(s) != 0x38425053) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 1) {
       stbi__rewind( s );
       return 0;
   }
   stbi__skip(s, 6);
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       stbi__rewind( s );
       return 0;
   }
   STBI_NOTUSED(stbi__get32be(s));
   STBI_NOTUSED(stbi__get32be(s));
   depth = stbi__get16be(s);
   if (depth != 16) {
       stbi__rewind( s );
       return 0;
   }
   return 1;
}
#endif

#ifndef STBI_NO_PIC
static int stbi__pic_info(stbi__context *s, int *x, int *y, int *comp)
{
   int act_comp=0,num_packets=0,chained,dummy;
   stbi__pic_packet packets[10];

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (!stbi__pic_is4(s,"\x53\x80\xF6\x34")) {
      stbi__rewind(s);
      return 0;
   }

   stbi__skip(s, 88);

   *x = stbi__get16be(s);
   *y = stbi__get16be(s);
   if (stbi__at_eof(s)) {
      stbi__rewind( s);
      return 0;
   }
   if ( (*x) != 0 && (1 << 28) / (*x) < (*y)) {
      stbi__rewind( s );
      return 0;
   }

   stbi__skip(s, 8);

   do {
      stbi__pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return 0;

      packet = &packets[num_packets++];
      chained = stbi__get8(s);
      packet->size    = stbi__get8(s);
      packet->type    = stbi__get8(s);
      packet->channel = stbi__get8(s);
      act_comp |= packet->channel;

      if (stbi__at_eof(s)) {
          stbi__rewind( s );
          return 0;
      }
      if (packet->size != 8) {
          stbi__rewind( s );
          return 0;
      }
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3);

   return 1;
}
#endif

// *************************************************************************************************
// Portable Gray Map and Portable Pixel Map loader
// by Ken Miller
//
// PGM: http://netpbm.sourceforge.net/doc/pgm.html
// PPM: http://netpbm.sourceforge.net/doc/ppm.html
//
// Known limitations:
//    Does not support comments in the header section
//    Does not support ASCII image data (formats P2 and P3)

#ifndef STBI_NO_PNM

static int      stbi__pnm_test(stbi__context *s)
{
   char p, t;
   p = (char) stbi__get8(s);
   t = (char) stbi__get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       stbi__rewind( s );
       return 0;
   }
   return 1;
}

static void *stbi__pnm_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *out;
   STBI_NOTUSED(ri);

   ri->bits_per_channel = stbi__pnm_info(s, (int *)&s->img_x, (int *)&s->img_y, (int *)&s->img_n);
   if (ri->bits_per_channel == 0)
      return 0;

   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;

   if (!stbi__mad4sizes_valid(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0))
      return stbi__errpuc("too large", "PNM too large");

   out = (stbi_uc *) stbi__malloc_mad4(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0);
   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   if (!stbi__getn(s, out, s->img_n * s->img_x * s->img_y * (ri->bits_per_channel / 8))) {
      STBI_FREE(out);
      return stbi__errpuc("bad PNM", "PNM file truncated");
   }

   if (req_comp && req_comp != s->img_n) {
      if (ri->bits_per_channel == 16) {
         out = (stbi_uc *) stbi__convert_format16((stbi__uint16 *) out, s->img_n, req_comp, s->img_x, s->img_y);
      } else {
         out = stbi__convert_format(out, s->img_n, req_comp, s->img_x, s->img_y);
      }
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }
   return out;
}

static int      stbi__pnm_isspace(char c)
{
   return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void     stbi__pnm_skip_whitespace(stbi__context *s, char *c)
{
   for (;;) {
      while (!stbi__at_eof(s) && stbi__pnm_isspace(*c))
         *c = (char) stbi__get8(s);

      if (stbi__at_eof(s) || *c != '#')
         break;

      while (!stbi__at_eof(s) && *c != '\n' && *c != '\r' )
         *c = (char) stbi__get8(s);
   }
}

static int      stbi__pnm_isdigit(char c)
{
   return c >= '0' && c <= '9';
}

static int      stbi__pnm_getinteger(stbi__context *s, char *c)
{
   int value = 0;

   while (!stbi__at_eof(s) && stbi__pnm_isdigit(*c)) {
      value = value*10 + (*c - '0');
      *c = (char) stbi__get8(s);
      if((value > 214748364) || (value == 214748364 && *c > '7'))
          return stbi__err("integer parse overflow", "Parsing an integer in the PPM header overflowed a 32-bit int");
   }

   return value;
}

static int      stbi__pnm_info(stbi__context *s, int *x, int *y, int *comp)
{
   int maxv, dummy;
   char c, p, t;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   stbi__rewind(s);

   // Get identifier
   p = (char) stbi__get8(s);
   t = (char) stbi__get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       stbi__rewind(s);
       return 0;
   }

   *comp = (t == '6') ? 3 : 1;  // '5' is 1-component .pgm; '6' is 3-component .ppm

   c = (char) stbi__get8(s);
   stbi__pnm_skip_whitespace(s, &c);

   *x = stbi__pnm_getinteger(s, &c); // read width
   if(*x == 0)
       return stbi__err("invalid width", "PPM image header had zero or overflowing width");
   stbi__pnm_skip_whitespace(s, &c);

   *y = stbi__pnm_getinteger(s, &c); // read height
   if (*y == 0)
       return stbi__err("invalid width", "PPM image header had zero or overflowing width");
   stbi__pnm_skip_whitespace(s, &c);

   maxv = stbi__pnm_getinteger(s, &c);  // read max value
   if (maxv > 65535)
      return stbi__err("max value > 65535", "PPM image supports only 8-bit and 16-bit images");
   else if (maxv > 255)
      return 16;
   else
      return 8;
}

static int stbi__pnm_is16(stbi__context *s)
{
   if (stbi__pnm_info(s, NULL, NULL, NULL) == 16)
	   return 1;
   return 0;
}
#endif

static int stbi__info_main(stbi__context *s, int *x, int *y, int *comp)
{
   #ifndef STBI_NO_JPEG
   if (stbi__jpeg_info(s, x, y, comp)) return 1;
   #endif

   #ifndef STBI_NO_PNG
   if (stbi__png_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_GIF
   if (stbi__gif_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_BMP
   if (stbi__bmp_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (stbi__psd_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PIC
   if (stbi__pic_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (stbi__pnm_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_HDR
   if (stbi__hdr_info(s, x, y, comp))  return 1;
   #endif

   // test tga last because it's a crappy test!
   #ifndef STBI_NO_TGA
   if (stbi__tga_info(s, x, y, comp))
       return 1;
   #endif
   return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

static int stbi__is_16_main(stbi__context *s)
{
   #ifndef STBI_NO_PNG
   if (stbi__png_is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (stbi__psd_is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (stbi__pnm_is16(s))  return 1;
   #endif
   return 0;
}

#ifndef STBI_NO_STDIO
STBIDEF int stbi_info(char const *filename, int *x, int *y, int *comp)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_info_from_file(f, x, y, comp);
    fclose(f);
    return result;
}

STBIDEF int stbi_info_from_file(FILE *f, int *x, int *y, int *comp)
{
   int r;
   stbi__context s;
   long pos = ftell(f);
   stbi__start_file(&s, f);
   r = stbi__info_main(&s,x,y,comp);
   fseek(f,pos,SEEK_SET);
   return r;
}

STBIDEF int stbi_is_16_bit(char const *filename)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_is_16_bit_from_file(f);
    fclose(f);
    return result;
}

STBIDEF int stbi_is_16_bit_from_file(FILE *f)
{
   int r;
   stbi__context s;
   long pos = ftell(f);
   stbi__start_file(&s, f);
   r = stbi__is_16_main(&s);
   fseek(f,pos,SEEK_SET);
   return r;
}
#endif // !STBI_NO_STDIO

STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__info_main(&s,x,y,comp);
}

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *c, void *user, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
   return stbi__info_main(&s,x,y,comp);
}

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__is_16_main(&s);
}

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *c, void *user)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
   return stbi__is_16_main(&s);
}

#endif // STB_IMAGE_IMPLEMENTATION

/*
   revision history:
      2.20  (2019-02-07) support utf8 filenames in Windows; fix warnings and platform ifdefs
      2.19  (2018-02-11) fix warning
      2.18  (2018-01-30) fix warnings
      2.17  (2018-01-29) change sbti__shiftsigned to avoid clang -O2 bug
                         1-bit BMP
                         *_is_16_bit api
                         avoid warnings
      2.16  (2017-07-23) all functions have 16-bit variants;
                         STBI_NO_STDIO works again;
                         compilation fixes;
                         fix rounding in unpremultiply;
                         optimize vertical flip;
                         disable raw_len validation;
                         documentation fixes
      2.15  (2017-03-18) fix png-1,2,4 bug; now all Imagenet JPGs decode;
                         warning fixes; disable run-time SSE detection on gcc;
                         uniform handling of optional "return" values;
                         thread-safe initialization of zlib tables
      2.14  (2017-03-03) remove deprecated STBI_JPEG_OLD; fixes for Imagenet JPGs
      2.13  (2016-11-29) add 16-bit API, only supported for PNG right now
      2.12  (2016-04-02) fix typo in 2.11 PSD fix that caused crashes
      2.11  (2016-04-02) allocate large structures on the stack
                         remove white matting for transparent PSD
                         fix reported channel count for PNG & BMP
                         re-enable SSE2 in non-gcc 64-bit
                         support RGB-formatted JPEG
                         read 16-bit PNGs (only as 8-bit)
      2.10  (2016-01-22) avoid warning introduced in 2.09 by STBI_REALLOC_SIZED
      2.09  (2016-01-16) allow comments in PNM files
                         16-bit-per-pixel TGA (not bit-per-component)
                         info() for TGA could break due to .hdr handling
                         info() for BMP to shares code instead of sloppy parse
                         can use STBI_REALLOC_SIZED if allocator doesn't support realloc
                         code cleanup
      2.08  (2015-09-13) fix to 2.07 cleanup, reading RGB PSD as RGBA
      2.07  (2015-09-13) fix compiler warnings
                         partial animated GIF support
                         limited 16-bpc PSD support
                         #ifdef unused functions
                         bug with < 92 byte PIC,PNM,HDR,TGA
      2.06  (2015-04-19) fix bug where PSD returns wrong '*comp' value
      2.05  (2015-04-19) fix bug in progressive JPEG handling, fix warning
      2.04  (2015-04-15) try to re-enable SIMD on MinGW 64-bit
      2.03  (2015-04-12) extra corruption checking (mmozeiko)
                         stbi_set_flip_vertically_on_load (nguillemot)
                         fix NEON support; fix mingw support
      2.02  (2015-01-19) fix incorrect assert, fix warning
      2.01  (2015-01-17) fix various warnings; suppress SIMD on gcc 32-bit without -msse2
      2.00b (2014-12-25) fix STBI_MALLOC in progressive JPEG
      2.00  (2014-12-25) optimize JPG, including x86 SSE2 & NEON SIMD (ryg)
                         progressive JPEG (stb)
                         PGM/PPM support (Ken Miller)
                         STBI_MALLOC,STBI_REALLOC,STBI_FREE
                         GIF bugfix -- seemingly never worked
                         STBI_NO_*, STBI_ONLY_*
      1.48  (2014-12-14) fix incorrectly-named assert()
      1.47  (2014-12-14) 1/2/4-bit PNG support, both direct and paletted (Omar Cornut & stb)
                         optimize PNG (ryg)
                         fix bug in interlaced PNG with user-specified channel count (stb)
      1.46  (2014-08-26)
              fix broken tRNS chunk (colorkey-style transparency) in non-paletted PNG
      1.45  (2014-08-16)
              fix MSVC-ARM internal compiler error by wrapping malloc
      1.44  (2014-08-07)
              various warning fixes from Ronny Chevalier
      1.43  (2014-07-15)
              fix MSVC-only compiler problem in code changed in 1.42
      1.42  (2014-07-09)
              don't define _CRT_SECURE_NO_WARNINGS (affects user code)
              fixes to stbi__cleanup_jpeg path
              added STBI_ASSERT to avoid requiring assert.h
      1.41  (2014-06-25)
              fix search&replace from 1.36 that messed up comments/error messages
      1.40  (2014-06-22)
              fix gcc struct-initialization warning
      1.39  (2014-06-15)
              fix to TGA optimization when req_comp != number of components in TGA;
              fix to GIF loading because BMP wasn't rewinding (whoops, no GIFs in my test suite)
              add support for BMP version 5 (more ignored fields)
      1.38  (2014-06-06)
              suppress MSVC warnings on integer casts truncating values
              fix accidental rename of 'skip' field of I/O
      1.37  (2014-06-04)
              remove duplicate typedef
      1.36  (2014-06-03)
              convert to header file single-file library
              if de-iphone isn't set, load iphone images color-swapped instead of returning NULL
      1.35  (2014-05-27)
              various warnings
              fix broken STBI_SIMD path
              fix bug where stbi_load_from_file no longer left file pointer in correct place
              fix broken non-easy path for 32-bit BMP (possibly never used)
              TGA optimization by Arseny Kapoulkine
      1.34  (unknown)
              use STBI_NOTUSED in stbi__resample_row_generic(), fix one more leak in tga failure case
      1.33  (2011-07-14)
              make stbi_is_hdr work in STBI_NO_HDR (as specified), minor compiler-friendly improvements
      1.32  (2011-07-13)
              support for "info" function for all supported filetypes (SpartanJ)
      1.31  (2011-06-20)
              a few more leak fixes, bug in PNG handling (SpartanJ)
      1.30  (2011-06-11)
              added ability to load files via callbacks to accomidate custom input streams (Ben Wenger)
              removed deprecated format-specific test/load functions
              removed support for installable file formats (stbi_loader) -- would have been broken for IO callbacks anyway
              error cases in bmp and tga give messages and don't leak (Raymond Barbiero, grisha)
              fix inefficiency in decoding 32-bit BMP (David Woo)
      1.29  (2010-08-16)
              various warning fixes from Aurelien Pocheville
      1.28  (2010-08-01)
              fix bug in GIF palette transparency (SpartanJ)
      1.27  (2010-08-01)
              cast-to-stbi_uc to fix warnings
      1.26  (2010-07-24)
              fix bug in file buffering for PNG reported by SpartanJ
      1.25  (2010-07-17)
              refix trans_data warning (Won Chun)
      1.24  (2010-07-12)
              perf improvements reading from files on platforms with lock-heavy fgetc()
              minor perf improvements for jpeg
              deprecated type-specific functions so we'll get feedback if they're needed
              attempt to fix trans_data warning (Won Chun)
      1.23    fixed bug in iPhone support
      1.22  (2010-07-10)
              removed image *writing* support
              stbi_info support from Jetro Lauha
              GIF support from Jean-Marc Lienher
              iPhone PNG-extensions from James Brown
              warning-fixes from Nicolas Schulz and Janez Zemva (i.stbi__err. Janez (U+017D)emva)
      1.21    fix use of 'stbi_uc' in header (reported by jon blow)
      1.20    added support for Softimage PIC, by Tom Seddon
      1.19    bug in interlaced PNG corruption check (found by ryg)
      1.18  (2008-08-02)
              fix a threading bug (local mutable static)
      1.17    support interlaced PNG
      1.16    major bugfix - stbi__convert_format converted one too many pixels
      1.15    initialize some fields for thread safety
      1.14    fix threadsafe conversion bug
              header-file-only version (#define STBI_HEADER_FILE_ONLY before including)
      1.13    threadsafe
      1.12    const qualifiers in the API
      1.11    Support installable IDCT, colorspace conversion routines
      1.10    Fixes for 64-bit (don't use "unsigned long")
              optimized upsampling by Fabian "ryg" Giesen
      1.09    Fix format-conversion for PSD code (bad global variables!)
      1.08    Thatcher Ulrich's PSD code integrated by Nicolas Schulz
      1.07    attempt to fix C++ warning/errors again
      1.06    attempt to fix C++ warning/errors again
      1.05    fix TGA loading to return correct *comp and use good luminance calc
      1.04    default float alpha is 1, not 255; use 'void *' for stbi_image_free
      1.03    bugfixes to STBI_NO_STDIO, STBI_NO_HDR
      1.02    support for (subset of) HDR files, float interface for preferred access to them
      1.01    fix bug: possible bug in handling right-side up bmps... not sure
              fix bug: the stbi__bmp_load() and stbi__tga_load() functions didn't work at all
      1.00    interface to zlib that skips zlib header
      0.99    correct handling of alpha in palette
      0.98    TGA loader by lonesock; dynamically add loaders (untested)
      0.97    jpeg errors on too large a file; also catch another malloc failure
      0.96    fix detection of invalid v value - particleman@mollyrocket forum
      0.95    during header scan, seek to markers in case of padding
      0.94    STBI_NO_STDIO to disable stdio usage; rename all #defines the same
      0.93    handle jpegtran output; verbose errors
      0.92    read 4,8,16,24,32-bit BMP files of several formats
      0.91    output 24-bit Windows 3.0 BMP files
      0.90    fix a few more warnings; bump version number to approach 1.0
      0.61    bugfixes due to Marc LeBlanc, Christopher Lloyd
      0.60    fix compiling as c++
      0.59    fix warnings: merge Dave Moore's -Wall fixes
      0.58    fix bug: zlib uncompressed mode len/nlen was wrong endian
      0.57    fix bug: jpg last huffman symbol before marker was >9 bits but less than 16 available
      0.56    fix bug: zlib uncompressed mode len vs. nlen
      0.55    fix bug: restart_interval not initialized to 0
      0.54    allow NULL for 'int *comp'
      0.53    fix bug in png 3->4; speedup png decoding
      0.52    png handles req_comp=3,4 directly; minor cleanup; jpeg comments
      0.51    obey req_comp requests, 1-component jpegs return as 1-component,
              on 'test' only check type, not whether we support this variant
      0.50  (2006-11-19)
              first released version
*/


/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
/* stb_image_write - v1.16 - public domain - http://nothings.org/stb
   writes out PNG/BMP/TGA/JPEG/HDR images to C stdio - Sean Barrett 2010-2015
                                     no warranty implied; use at your own risk

   Before #including,

       #define STB_IMAGE_WRITE_IMPLEMENTATION

   in the file that you want to have the implementation.

   Will probably not work correctly with strict-aliasing optimizations.

ABOUT:

   This header file is a library for writing images to C stdio or a callback.

   The PNG output is not optimal; it is 20-50% larger than the file
   written by a decent optimizing implementation; though providing a custom
   zlib compress function (see STBIW_ZLIB_COMPRESS) can mitigate that.
   This library is designed for source code compactness and simplicity,
   not optimal image file size or run-time performance.

BUILDING:

   You can #define STBIW_ASSERT(x) before the #include to avoid using assert.h.
   You can #define STBIW_MALLOC(), STBIW_REALLOC(), and STBIW_FREE() to replace
   malloc,realloc,free.
   You can #define STBIW_MEMMOVE() to replace memmove()
   You can #define STBIW_ZLIB_COMPRESS to use a custom zlib-style compress function
   for PNG compression (instead of the builtin one), it must have the following signature:
   unsigned char * my_compress(unsigned char *data, int data_len, int *out_len, int quality);
   The returned data will be freed with STBIW_FREE() (free() by default),
   so it must be heap allocated with STBIW_MALLOC() (malloc() by default),

UNICODE:

   If compiling for Windows and you wish to use Unicode filenames, compile
   with
       #define STBIW_WINDOWS_UTF8
   and pass utf8-encoded filenames. Call stbiw_convert_wchar_to_utf8 to convert
   Windows wchar_t filenames to utf8.

USAGE:

   There are five functions, one for each image file format:

     int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
     int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_tga(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);
     int stbi_write_hdr(char const *filename, int w, int h, int comp, const float *data);

     void stbi_flip_vertically_on_write(int flag); // flag is non-zero to flip data vertically

   There are also five equivalent functions that use an arbitrary write function. You are
   expected to open/close your file-equivalent before and after calling these:

     int stbi_write_png_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data, int stride_in_bytes);
     int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_tga_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const float *data);
     int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int quality);

   where the callback is:
      void stbi_write_func(void *context, void *data, int size);

   You can configure it with these global variables:
      int stbi_write_tga_with_rle;             // defaults to true; set to 0 to disable RLE
      int stbi_write_png_compression_level;    // defaults to 8; set to higher for more compression
      int stbi_write_force_png_filter;         // defaults to -1; set to 0..5 to force a filter mode


   You can define STBI_WRITE_NO_STDIO to disable the file variant of these
   functions, so the library will not use stdio.h at all. However, this will
   also disable HDR writing, because it requires stdio for formatted output.

   Each function returns 0 on failure and non-0 on success.

   The functions create an image file defined by the parameters. The image
   is a rectangle of pixels stored from left-to-right, top-to-bottom.
   Each pixel contains 'comp' channels of data stored interleaved with 8-bits
   per channel, in the following order: 1=Y, 2=YA, 3=RGB, 4=RGBA. (Y is
   monochrome color.) The rectangle is 'w' pixels wide and 'h' pixels tall.
   The *data pointer points to the first byte of the top-left-most pixel.
   For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
   a row of pixels to the first byte of the next row of pixels.

   PNG creates output files with the same number of components as the input.
   The BMP format expands Y to RGB in the file format and does not
   output alpha.

   PNG supports writing rectangles of data even when the bytes storing rows of
   data are not consecutive in memory (e.g. sub-rectangles of a larger image),
   by supplying the stride between the beginning of adjacent rows. The other
   formats do not. (Thus you cannot write a native-format BMP through the BMP
   writer, both because it is in BGR order and because it may have padding
   at the end of the line.)

   PNG allows you to set the deflate compression level by setting the global
   variable 'stbi_write_png_compression_level' (it defaults to 8).

   HDR expects linear float data. Since the format is always 32-bit rgb(e)
   data, alpha (if provided) is discarded, and for monochrome data it is
   replicated across all three channels.

   TGA supports RLE or non-RLE compressed data. To use non-RLE-compressed
   data, set the global variable 'stbi_write_tga_with_rle' to 0.

   JPEG does ignore alpha channels in input data; quality is between 1 and 100.
   Higher quality looks better but results in a bigger image.
   JPEG baseline (no JPEG progressive).

CREDITS:


   Sean Barrett           -    PNG/BMP/TGA
   Baldur Karlsson        -    HDR
   Jean-Sebastien Guay    -    TGA monochrome
   Tim Kelsey             -    misc enhancements
   Alan Hickman           -    TGA RLE
   Emmanuel Julien        -    initial file IO callback implementation
   Jon Olick              -    original jo_jpeg.cpp code
   Daniel Gibson          -    integrate JPEG, allow external zlib
   Aarni Koskela          -    allow choosing PNG filter

   bugfixes:
      github:Chribba
      Guillaume Chereau
      github:jry2
      github:romigrou
      Sergio Gonzalez
      Jonas Karlsson
      Filip Wasil
      Thatcher Ulrich
      github:poppolopoppo
      Patrick Boettcher
      github:xeekworx
      Cap Petschulat
      Simon Rodriguez
      Ivan Tikhonov
      github:ignotion
      Adam Schackart
      Andrew Kensler

LICENSE

  See end of file for license information.

*/

#ifndef INCLUDE_STB_IMAGE_WRITE_H
#define INCLUDE_STB_IMAGE_WRITE_H

#include <stdlib.h>

// if STB_IMAGE_WRITE_STATIC causes problems, try defining STBIWDEF to 'inline' or 'static inline'
#ifndef STBIWDEF
#ifdef STB_IMAGE_WRITE_STATIC
#define STBIWDEF  static
#else
#ifdef __cplusplus
#define STBIWDEF  extern "C"
#else
#define STBIWDEF  extern
#endif
#endif
#endif

#ifndef STB_IMAGE_WRITE_STATIC  // C++ forbids static forward declarations
STBIWDEF int stbi_write_tga_with_rle;
STBIWDEF int stbi_write_png_compression_level;
STBIWDEF int stbi_write_force_png_filter;
#endif

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_png(char const *filename, int w, int h, int comp, const void  *data, int stride_in_bytes);
STBIWDEF int stbi_write_bmp(char const *filename, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_tga(char const *filename, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_hdr(char const *filename, int w, int h, int comp, const float *data);
STBIWDEF int stbi_write_jpg(char const *filename, int x, int y, int comp, const void  *data, int quality);

#ifdef STBIW_WINDOWS_UTF8
STBIWDEF int stbiw_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input);
#endif
#endif

typedef void stbi_write_func(void *context, void *data, int size);

STBIWDEF int stbi_write_png_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data, int stride_in_bytes);
STBIWDEF int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_tga_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
STBIWDEF int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const float *data);
STBIWDEF int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void  *data, int quality);

STBIWDEF void stbi_flip_vertically_on_write(int flip_boolean);

#endif//INCLUDE_STB_IMAGE_WRITE_H

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef _WIN32
   #ifndef _CRT_SECURE_NO_WARNINGS
   #define _CRT_SECURE_NO_WARNINGS
   #endif
   #ifndef _CRT_NONSTDC_NO_DEPRECATE
   #define _CRT_NONSTDC_NO_DEPRECATE
   #endif
#endif

#ifndef STBI_WRITE_NO_STDIO
#include <stdio.h>
#endif // STBI_WRITE_NO_STDIO

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(STBIW_MALLOC) && defined(STBIW_FREE) && (defined(STBIW_REALLOC) || defined(STBIW_REALLOC_SIZED))
// ok
#elif !defined(STBIW_MALLOC) && !defined(STBIW_FREE) && !defined(STBIW_REALLOC) && !defined(STBIW_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBIW_MALLOC, STBIW_FREE, and STBIW_REALLOC (or STBIW_REALLOC_SIZED)."
#endif

#ifndef STBIW_MALLOC
#define STBIW_MALLOC(sz)        malloc(sz)
#define STBIW_REALLOC(p,newsz)  realloc(p,newsz)
#define STBIW_FREE(p)           free(p)
#endif

#ifndef STBIW_REALLOC_SIZED
#define STBIW_REALLOC_SIZED(p,oldsz,newsz) STBIW_REALLOC(p,newsz)
#endif


#ifndef STBIW_MEMMOVE
#define STBIW_MEMMOVE(a,b,sz) memmove(a,b,sz)
#endif


#ifndef STBIW_ASSERT
#include <assert.h>
#define STBIW_ASSERT(x) assert(x)
#endif

#define STBIW_UCHAR(x) (unsigned char) ((x) & 0xff)

#ifdef STB_IMAGE_WRITE_STATIC
static int stbi_write_png_compression_level = 8;
static int stbi_write_tga_with_rle = 1;
static int stbi_write_force_png_filter = -1;
#else
int stbi_write_png_compression_level = 8;
int stbi_write_tga_with_rle = 1;
int stbi_write_force_png_filter = -1;
#endif

static int stbi__flip_vertically_on_write = 0;

STBIWDEF void stbi_flip_vertically_on_write(int flag)
{
   stbi__flip_vertically_on_write = flag;
}

typedef struct
{
   stbi_write_func *func;
   void *context;
   unsigned char buffer[64];
   int buf_used;
} stbi__write_context;

// initialize a callback-based context
static void stbi__start_write_callbacks(stbi__write_context *s, stbi_write_func *c, void *context)
{
   s->func    = c;
   s->context = context;
}

#ifndef STBI_WRITE_NO_STDIO

static void stbi__stdio_write(void *context, void *data, int size)
{
   fwrite(data,1,size,(FILE*) context);
}

#if defined(_WIN32) && defined(STBIW_WINDOWS_UTF8)
#ifdef __cplusplus
#define STBIW_EXTERN extern "C"
#else
#define STBIW_EXTERN extern
#endif
STBIW_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBIW_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);

STBIWDEF int stbiw_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
   return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
}
#endif

static FILE *stbiw__fopen(char const *filename, char const *mode)
{
   FILE *f;
#if defined(_WIN32) && defined(STBIW_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

   if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != _wfopen_s(&f, wFilename, wMode))
      f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
   f = fopen(filename, mode);
#endif
   return f;
}

static int stbi__start_write_file(stbi__write_context *s, const char *filename)
{
   FILE *f = stbiw__fopen(filename, "wb");
   stbi__start_write_callbacks(s, stbi__stdio_write, (void *) f);
   return f != NULL;
}

static void stbi__end_write_file(stbi__write_context *s)
{
   fclose((FILE *)s->context);
}

#endif // !STBI_WRITE_NO_STDIO

typedef unsigned int stbiw_uint32;
typedef int stb_image_write_test[sizeof(stbiw_uint32)==4 ? 1 : -1];

static void stbiw__writefv(stbi__write_context *s, const char *fmt, va_list v)
{
   while (*fmt) {
      switch (*fmt++) {
         case ' ': break;
         case '1': { unsigned char x = STBIW_UCHAR(va_arg(v, int));
                     s->func(s->context,&x,1);
                     break; }
         case '2': { int x = va_arg(v,int);
                     unsigned char b[2];
                     b[0] = STBIW_UCHAR(x);
                     b[1] = STBIW_UCHAR(x>>8);
                     s->func(s->context,b,2);
                     break; }
         case '4': { stbiw_uint32 x = va_arg(v,int);
                     unsigned char b[4];
                     b[0]=STBIW_UCHAR(x);
                     b[1]=STBIW_UCHAR(x>>8);
                     b[2]=STBIW_UCHAR(x>>16);
                     b[3]=STBIW_UCHAR(x>>24);
                     s->func(s->context,b,4);
                     break; }
         default:
            STBIW_ASSERT(0);
            return;
      }
   }
}

static void stbiw__writef(stbi__write_context *s, const char *fmt, ...)
{
   va_list v;
   va_start(v, fmt);
   stbiw__writefv(s, fmt, v);
   va_end(v);
}

static void stbiw__write_flush(stbi__write_context *s)
{
   if (s->buf_used) {
      s->func(s->context, &s->buffer, s->buf_used);
      s->buf_used = 0;
   }
}

static void stbiw__putc(stbi__write_context *s, unsigned char c)
{
   s->func(s->context, &c, 1);
}

static void stbiw__write1(stbi__write_context *s, unsigned char a)
{
   if ((size_t)s->buf_used + 1 > sizeof(s->buffer))
      stbiw__write_flush(s);
   s->buffer[s->buf_used++] = a;
}

static void stbiw__write3(stbi__write_context *s, unsigned char a, unsigned char b, unsigned char c)
{
   int n;
   if ((size_t)s->buf_used + 3 > sizeof(s->buffer))
      stbiw__write_flush(s);
   n = s->buf_used;
   s->buf_used = n+3;
   s->buffer[n+0] = a;
   s->buffer[n+1] = b;
   s->buffer[n+2] = c;
}

static void stbiw__write_pixel(stbi__write_context *s, int rgb_dir, int comp, int write_alpha, int expand_mono, unsigned char *d)
{
   unsigned char bg[3] = { 255, 0, 255}, px[3];
   int k;

   if (write_alpha < 0)
      stbiw__write1(s, d[comp - 1]);

   switch (comp) {
      case 2: // 2 pixels = mono + alpha, alpha is written separately, so same as 1-channel case
      case 1:
         if (expand_mono)
            stbiw__write3(s, d[0], d[0], d[0]); // monochrome bmp
         else
            stbiw__write1(s, d[0]);  // monochrome TGA
         break;
      case 4:
         if (!write_alpha) {
            // composite against pink background
            for (k = 0; k < 3; ++k)
               px[k] = bg[k] + ((d[k] - bg[k]) * d[3]) / 255;
            stbiw__write3(s, px[1 - rgb_dir], px[1], px[1 + rgb_dir]);
            break;
         }
         /* FALLTHROUGH */
      case 3:
         stbiw__write3(s, d[1 - rgb_dir], d[1], d[1 + rgb_dir]);
         break;
   }
   if (write_alpha > 0)
      stbiw__write1(s, d[comp - 1]);
}

static void stbiw__write_pixels(stbi__write_context *s, int rgb_dir, int vdir, int x, int y, int comp, void *data, int write_alpha, int scanline_pad, int expand_mono)
{
   stbiw_uint32 zero = 0;
   int i,j, j_end;

   if (y <= 0)
      return;

   if (stbi__flip_vertically_on_write)
      vdir *= -1;

   if (vdir < 0) {
      j_end = -1; j = y-1;
   } else {
      j_end =  y; j = 0;
   }

   for (; j != j_end; j += vdir) {
      for (i=0; i < x; ++i) {
         unsigned char *d = (unsigned char *) data + (j*x+i)*comp;
         stbiw__write_pixel(s, rgb_dir, comp, write_alpha, expand_mono, d);
      }
      stbiw__write_flush(s);
      s->func(s->context, &zero, scanline_pad);
   }
}

static int stbiw__outfile(stbi__write_context *s, int rgb_dir, int vdir, int x, int y, int comp, int expand_mono, void *data, int alpha, int pad, const char *fmt, ...)
{
   if (y < 0 || x < 0) {
      return 0;
   } else {
      va_list v;
      va_start(v, fmt);
      stbiw__writefv(s, fmt, v);
      va_end(v);
      stbiw__write_pixels(s,rgb_dir,vdir,x,y,comp,data,alpha,pad, expand_mono);
      return 1;
   }
}

static int stbi_write_bmp_core(stbi__write_context *s, int x, int y, int comp, const void *data)
{
   if (comp != 4) {
      // write RGB bitmap
      int pad = (-x*3) & 3;
      return stbiw__outfile(s,-1,-1,x,y,comp,1,(void *) data,0,pad,
              "11 4 22 4" "4 44 22 444444",
              'B', 'M', 14+40+(x*3+pad)*y, 0,0, 14+40,  // file header
               40, x,y, 1,24, 0,0,0,0,0,0);             // bitmap header
   } else {
      // RGBA bitmaps need a v4 header
      // use BI_BITFIELDS mode with 32bpp and alpha mask
      // (straight BI_RGB with alpha mask doesn't work in most readers)
      return stbiw__outfile(s,-1,-1,x,y,comp,1,(void *)data,1,0,
         "11 4 22 4" "4 44 22 444444 4444 4 444 444 444 444",
         'B', 'M', 14+108+x*y*4, 0, 0, 14+108, // file header
         108, x,y, 1,32, 3,0,0,0,0,0, 0xff0000,0xff00,0xff,0xff000000u, 0, 0,0,0, 0,0,0, 0,0,0, 0,0,0); // bitmap V4 header
   }
}

STBIWDEF int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_bmp_core(&s, x, y, comp, data);
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_bmp(char const *filename, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_bmp_core(&s, x, y, comp, data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif //!STBI_WRITE_NO_STDIO

static int stbi_write_tga_core(stbi__write_context *s, int x, int y, int comp, void *data)
{
   int has_alpha = (comp == 2 || comp == 4);
   int colorbytes = has_alpha ? comp-1 : comp;
   int format = colorbytes < 2 ? 3 : 2; // 3 color channels (RGB/RGBA) = 2, 1 color channel (Y/YA) = 3

   if (y < 0 || x < 0)
      return 0;

   if (!stbi_write_tga_with_rle) {
      return stbiw__outfile(s, -1, -1, x, y, comp, 0, (void *) data, has_alpha, 0,
         "111 221 2222 11", 0, 0, format, 0, 0, 0, 0, 0, x, y, (colorbytes + has_alpha) * 8, has_alpha * 8);
   } else {
      int i,j,k;
      int jend, jdir;

      stbiw__writef(s, "111 221 2222 11", 0,0,format+8, 0,0,0, 0,0,x,y, (colorbytes + has_alpha) * 8, has_alpha * 8);

      if (stbi__flip_vertically_on_write) {
         j = 0;
         jend = y;
         jdir = 1;
      } else {
         j = y-1;
         jend = -1;
         jdir = -1;
      }
      for (; j != jend; j += jdir) {
         unsigned char *row = (unsigned char *) data + j * x * comp;
         int len;

         for (i = 0; i < x; i += len) {
            unsigned char *begin = row + i * comp;
            int diff = 1;
            len = 1;

            if (i < x - 1) {
               ++len;
               diff = memcmp(begin, row + (i + 1) * comp, comp);
               if (diff) {
                  const unsigned char *prev = begin;
                  for (k = i + 2; k < x && len < 128; ++k) {
                     if (memcmp(prev, row + k * comp, comp)) {
                        prev += comp;
                        ++len;
                     } else {
                        --len;
                        break;
                     }
                  }
               } else {
                  for (k = i + 2; k < x && len < 128; ++k) {
                     if (!memcmp(begin, row + k * comp, comp)) {
                        ++len;
                     } else {
                        break;
                     }
                  }
               }
            }

            if (diff) {
               unsigned char header = STBIW_UCHAR(len - 1);
               stbiw__write1(s, header);
               for (k = 0; k < len; ++k) {
                  stbiw__write_pixel(s, -1, comp, has_alpha, 0, begin + k * comp);
               }
            } else {
               unsigned char header = STBIW_UCHAR(len - 129);
               stbiw__write1(s, header);
               stbiw__write_pixel(s, -1, comp, has_alpha, 0, begin);
            }
         }
      }
      stbiw__write_flush(s);
   }
   return 1;
}

STBIWDEF int stbi_write_tga_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_tga_core(&s, x, y, comp, (void *) data);
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_tga(char const *filename, int x, int y, int comp, const void *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_tga_core(&s, x, y, comp, (void *) data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif

// *************************************************************************************************
// Radiance RGBE HDR writer
// by Baldur Karlsson

#define stbiw__max(a, b)  ((a) > (b) ? (a) : (b))

#ifndef STBI_WRITE_NO_STDIO

static void stbiw__linear_to_rgbe(unsigned char *rgbe, float *linear)
{
   int exponent;
   float maxcomp = stbiw__max(linear[0], stbiw__max(linear[1], linear[2]));

   if (maxcomp < 1e-32f) {
      rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
   } else {
      float normalize = (float) frexp(maxcomp, &exponent) * 256.0f/maxcomp;

      rgbe[0] = (unsigned char)(linear[0] * normalize);
      rgbe[1] = (unsigned char)(linear[1] * normalize);
      rgbe[2] = (unsigned char)(linear[2] * normalize);
      rgbe[3] = (unsigned char)(exponent + 128);
   }
}

static void stbiw__write_run_data(stbi__write_context *s, int length, unsigned char databyte)
{
   unsigned char lengthbyte = STBIW_UCHAR(length+128);
   STBIW_ASSERT(length+128 <= 255);
   s->func(s->context, &lengthbyte, 1);
   s->func(s->context, &databyte, 1);
}

static void stbiw__write_dump_data(stbi__write_context *s, int length, unsigned char *data)
{
   unsigned char lengthbyte = STBIW_UCHAR(length);
   STBIW_ASSERT(length <= 128); // inconsistent with spec but consistent with official code
   s->func(s->context, &lengthbyte, 1);
   s->func(s->context, data, length);
}

static void stbiw__write_hdr_scanline(stbi__write_context *s, int width, int ncomp, unsigned char *scratch, float *scanline)
{
   unsigned char scanlineheader[4] = { 2, 2, 0, 0 };
   unsigned char rgbe[4];
   float linear[3];
   int x;

   scanlineheader[2] = (width&0xff00)>>8;
   scanlineheader[3] = (width&0x00ff);

   /* skip RLE for images too small or large */
   if (width < 8 || width >= 32768) {
      for (x=0; x < width; x++) {
         switch (ncomp) {
            case 4: /* fallthrough */
            case 3: linear[2] = scanline[x*ncomp + 2];
                    linear[1] = scanline[x*ncomp + 1];
                    linear[0] = scanline[x*ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x*ncomp + 0];
                    break;
         }
         stbiw__linear_to_rgbe(rgbe, linear);
         s->func(s->context, rgbe, 4);
      }
   } else {
      int c,r;
      /* encode into scratch buffer */
      for (x=0; x < width; x++) {
         switch(ncomp) {
            case 4: /* fallthrough */
            case 3: linear[2] = scanline[x*ncomp + 2];
                    linear[1] = scanline[x*ncomp + 1];
                    linear[0] = scanline[x*ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x*ncomp + 0];
                    break;
         }
         stbiw__linear_to_rgbe(rgbe, linear);
         scratch[x + width*0] = rgbe[0];
         scratch[x + width*1] = rgbe[1];
         scratch[x + width*2] = rgbe[2];
         scratch[x + width*3] = rgbe[3];
      }

      s->func(s->context, scanlineheader, 4);

      /* RLE each component separately */
      for (c=0; c < 4; c++) {
         unsigned char *comp = &scratch[width*c];

         x = 0;
         while (x < width) {
            // find first run
            r = x;
            while (r+2 < width) {
               if (comp[r] == comp[r+1] && comp[r] == comp[r+2])
                  break;
               ++r;
            }
            if (r+2 >= width)
               r = width;
            // dump up to first run
            while (x < r) {
               int len = r-x;
               if (len > 128) len = 128;
               stbiw__write_dump_data(s, len, &comp[x]);
               x += len;
            }
            // if there's a run, output it
            if (r+2 < width) { // same test as what we break out of in search loop, so only true if we break'd
               // find next byte after run
               while (r < width && comp[r] == comp[x])
                  ++r;
               // output run up to r
               while (x < r) {
                  int len = r-x;
                  if (len > 127) len = 127;
                  stbiw__write_run_data(s, len, comp[x]);
                  x += len;
               }
            }
         }
      }
   }
}

static int stbi_write_hdr_core(stbi__write_context *s, int x, int y, int comp, float *data)
{
   if (y <= 0 || x <= 0 || data == NULL)
      return 0;
   else {
      // Each component is stored separately. Allocate scratch space for full output scanline.
      unsigned char *scratch = (unsigned char *) STBIW_MALLOC(x*4);
      int i, len;
      char buffer[128];
      char header[] = "#?RADIANCE\n# Written by stb_image_write.h\nFORMAT=32-bit_rle_rgbe\n";
      s->func(s->context, header, sizeof(header)-1);

#ifdef __STDC_LIB_EXT1__
      len = sprintf_s(buffer, sizeof(buffer), "EXPOSURE=          1.0000000000000\n\n-Y %d +X %d\n", y, x);
#else
      len = sprintf(buffer, "EXPOSURE=          1.0000000000000\n\n-Y %d +X %d\n", y, x);
#endif
      s->func(s->context, buffer, len);

      for(i=0; i < y; i++)
         stbiw__write_hdr_scanline(s, x, comp, scratch, data + comp*x*(stbi__flip_vertically_on_write ? y-1-i : i));
      STBIW_FREE(scratch);
      return 1;
   }
}

STBIWDEF int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const float *data)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_hdr_core(&s, x, y, comp, (float *) data);
}

STBIWDEF int stbi_write_hdr(char const *filename, int x, int y, int comp, const float *data)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_hdr_core(&s, x, y, comp, (float *) data);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif // STBI_WRITE_NO_STDIO


//////////////////////////////////////////////////////////////////////////////
//
// PNG writer
//

#ifndef STBIW_ZLIB_COMPRESS
// stretchy buffer; stbiw__sbpush() == vector<>::push_back() -- stbiw__sbcount() == vector<>::size()
#define stbiw__sbraw(a) ((int *) (void *) (a) - 2)
#define stbiw__sbm(a)   stbiw__sbraw(a)[0]
#define stbiw__sbn(a)   stbiw__sbraw(a)[1]

#define stbiw__sbneedgrow(a,n)  ((a)==0 || stbiw__sbn(a)+n >= stbiw__sbm(a))
#define stbiw__sbmaybegrow(a,n) (stbiw__sbneedgrow(a,(n)) ? stbiw__sbgrow(a,n) : 0)
#define stbiw__sbgrow(a,n)  stbiw__sbgrowf((void **) &(a), (n), sizeof(*(a)))

#define stbiw__sbpush(a, v)      (stbiw__sbmaybegrow(a,1), (a)[stbiw__sbn(a)++] = (v))
#define stbiw__sbcount(a)        ((a) ? stbiw__sbn(a) : 0)
#define stbiw__sbfree(a)         ((a) ? STBIW_FREE(stbiw__sbraw(a)),0 : 0)

static void *stbiw__sbgrowf(void **arr, int increment, int itemsize)
{
   int m = *arr ? 2*stbiw__sbm(*arr)+increment : increment+1;
   void *p = STBIW_REALLOC_SIZED(*arr ? stbiw__sbraw(*arr) : 0, *arr ? (stbiw__sbm(*arr)*itemsize + sizeof(int)*2) : 0, itemsize * m + sizeof(int)*2);
   STBIW_ASSERT(p);
   if (p) {
      if (!*arr) ((int *) p)[1] = 0;
      *arr = (void *) ((int *) p + 2);
      stbiw__sbm(*arr) = m;
   }
   return *arr;
}

static unsigned char *stbiw__zlib_flushf(unsigned char *data, unsigned int *bitbuffer, int *bitcount)
{
   while (*bitcount >= 8) {
      stbiw__sbpush(data, STBIW_UCHAR(*bitbuffer));
      *bitbuffer >>= 8;
      *bitcount -= 8;
   }
   return data;
}

static int stbiw__zlib_bitrev(int code, int codebits)
{
   int res=0;
   while (codebits--) {
      res = (res << 1) | (code & 1);
      code >>= 1;
   }
   return res;
}

static unsigned int stbiw__zlib_countm(unsigned char *a, unsigned char *b, int limit)
{
   int i;
   for (i=0; i < limit && i < 258; ++i)
      if (a[i] != b[i]) break;
   return i;
}

static unsigned int stbiw__zhash(unsigned char *data)
{
   stbiw_uint32 hash = data[0] + (data[1] << 8) + (data[2] << 16);
   hash ^= hash << 3;
   hash += hash >> 5;
   hash ^= hash << 4;
   hash += hash >> 17;
   hash ^= hash << 25;
   hash += hash >> 6;
   return hash;
}

#define stbiw__zlib_flush() (out = stbiw__zlib_flushf(out, &bitbuf, &bitcount))
#define stbiw__zlib_add(code,codebits) \
      (bitbuf |= (code) << bitcount, bitcount += (codebits), stbiw__zlib_flush())
#define stbiw__zlib_huffa(b,c)  stbiw__zlib_add(stbiw__zlib_bitrev(b,c),c)
// default huffman tables
#define stbiw__zlib_huff1(n)  stbiw__zlib_huffa(0x30 + (n), 8)
#define stbiw__zlib_huff2(n)  stbiw__zlib_huffa(0x190 + (n)-144, 9)
#define stbiw__zlib_huff3(n)  stbiw__zlib_huffa(0 + (n)-256,7)
#define stbiw__zlib_huff4(n)  stbiw__zlib_huffa(0xc0 + (n)-280,8)
#define stbiw__zlib_huff(n)  ((n) <= 143 ? stbiw__zlib_huff1(n) : (n) <= 255 ? stbiw__zlib_huff2(n) : (n) <= 279 ? stbiw__zlib_huff3(n) : stbiw__zlib_huff4(n))
#define stbiw__zlib_huffb(n) ((n) <= 143 ? stbiw__zlib_huff1(n) : stbiw__zlib_huff2(n))

#define stbiw__ZHASH   16384

#endif // STBIW_ZLIB_COMPRESS

STBIWDEF unsigned char * stbi_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
#ifdef STBIW_ZLIB_COMPRESS
   // user provided a zlib compress implementation, use that
   return STBIW_ZLIB_COMPRESS(data, data_len, out_len, quality);
#else // use builtin
   static unsigned short lengthc[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258, 259 };
   static unsigned char  lengtheb[]= { 0,0,0,0,0,0,0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0 };
   static unsigned short distc[]   = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 32768 };
   static unsigned char  disteb[]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
   unsigned int bitbuf=0;
   int i,j, bitcount=0;
   unsigned char *out = NULL;
   unsigned char ***hash_table = (unsigned char***) STBIW_MALLOC(stbiw__ZHASH * sizeof(unsigned char**));
   if (hash_table == NULL)
      return NULL;
   if (quality < 5) quality = 5;

   stbiw__sbpush(out, 0x78);   // DEFLATE 32K window
   stbiw__sbpush(out, 0x5e);   // FLEVEL = 1
   stbiw__zlib_add(1,1);  // BFINAL = 1
   stbiw__zlib_add(1,2);  // BTYPE = 1 -- fixed huffman

   for (i=0; i < stbiw__ZHASH; ++i)
      hash_table[i] = NULL;

   i=0;
   while (i < data_len-3) {
      // hash next 3 bytes of data to be compressed
      int h = stbiw__zhash(data+i)&(stbiw__ZHASH-1), best=3;
      unsigned char *bestloc = 0;
      unsigned char **hlist = hash_table[h];
      int n = stbiw__sbcount(hlist);
      for (j=0; j < n; ++j) {
         if (hlist[j]-data > i-32768) { // if entry lies within window
            int d = stbiw__zlib_countm(hlist[j], data+i, data_len-i);
            if (d >= best) { best=d; bestloc=hlist[j]; }
         }
      }
      // when hash table entry is too long, delete half the entries
      if (hash_table[h] && stbiw__sbn(hash_table[h]) == 2*quality) {
         STBIW_MEMMOVE(hash_table[h], hash_table[h]+quality, sizeof(hash_table[h][0])*quality);
         stbiw__sbn(hash_table[h]) = quality;
      }
      stbiw__sbpush(hash_table[h],data+i);

      if (bestloc) {
         // "lazy matching" - check match at *next* byte, and if it's better, do cur byte as literal
         h = stbiw__zhash(data+i+1)&(stbiw__ZHASH-1);
         hlist = hash_table[h];
         n = stbiw__sbcount(hlist);
         for (j=0; j < n; ++j) {
            if (hlist[j]-data > i-32767) {
               int e = stbiw__zlib_countm(hlist[j], data+i+1, data_len-i-1);
               if (e > best) { // if next match is better, bail on current match
                  bestloc = NULL;
                  break;
               }
            }
         }
      }

      if (bestloc) {
         int d = (int) (data+i - bestloc); // distance back
         STBIW_ASSERT(d <= 32767 && best <= 258);
         for (j=0; best > lengthc[j+1]-1; ++j);
         stbiw__zlib_huff(j+257);
         if (lengtheb[j]) stbiw__zlib_add(best - lengthc[j], lengtheb[j]);
         for (j=0; d > distc[j+1]-1; ++j);
         stbiw__zlib_add(stbiw__zlib_bitrev(j,5),5);
         if (disteb[j]) stbiw__zlib_add(d - distc[j], disteb[j]);
         i += best;
      } else {
         stbiw__zlib_huffb(data[i]);
         ++i;
      }
   }
   // write out final bytes
   for (;i < data_len; ++i)
      stbiw__zlib_huffb(data[i]);
   stbiw__zlib_huff(256); // end of block
   // pad with 0 bits to byte boundary
   while (bitcount)
      stbiw__zlib_add(0,1);

   for (i=0; i < stbiw__ZHASH; ++i)
      (void) stbiw__sbfree(hash_table[i]);
   STBIW_FREE(hash_table);

   // store uncompressed instead if compression was worse
   if (stbiw__sbn(out) > data_len + 2 + ((data_len+32766)/32767)*5) {
      stbiw__sbn(out) = 2;  // truncate to DEFLATE 32K window and FLEVEL = 1
      for (j = 0; j < data_len;) {
         int blocklen = data_len - j;
         if (blocklen > 32767) blocklen = 32767;
         stbiw__sbpush(out, data_len - j == blocklen); // BFINAL = ?, BTYPE = 0 -- no compression
         stbiw__sbpush(out, STBIW_UCHAR(blocklen)); // LEN
         stbiw__sbpush(out, STBIW_UCHAR(blocklen >> 8));
         stbiw__sbpush(out, STBIW_UCHAR(~blocklen)); // NLEN
         stbiw__sbpush(out, STBIW_UCHAR(~blocklen >> 8));
         memcpy(out+stbiw__sbn(out), data+j, blocklen);
         stbiw__sbn(out) += blocklen;
         j += blocklen;
      }
   }

   {
      // compute adler32 on input
      unsigned int s1=1, s2=0;
      int blocklen = (int) (data_len % 5552);
      j=0;
      while (j < data_len) {
         for (i=0; i < blocklen; ++i) { s1 += data[j+i]; s2 += s1; }
         s1 %= 65521; s2 %= 65521;
         j += blocklen;
         blocklen = 5552;
      }
      stbiw__sbpush(out, STBIW_UCHAR(s2 >> 8));
      stbiw__sbpush(out, STBIW_UCHAR(s2));
      stbiw__sbpush(out, STBIW_UCHAR(s1 >> 8));
      stbiw__sbpush(out, STBIW_UCHAR(s1));
   }
   *out_len = stbiw__sbn(out);
   // make returned pointer freeable
   STBIW_MEMMOVE(stbiw__sbraw(out), out, *out_len);
   return (unsigned char *) stbiw__sbraw(out);
#endif // STBIW_ZLIB_COMPRESS
}

static unsigned int stbiw__crc32(unsigned char *buffer, int len)
{
#ifdef STBIW_CRC32
    return STBIW_CRC32(buffer, len);
#else
   static unsigned int crc_table[256] =
   {
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
      0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
      0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
      0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
      0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
      0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
      0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
      0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
      0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
      0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
      0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
      0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
      0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
      0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
      0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
      0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
      0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
      0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
      0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
      0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
      0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
      0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
      0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
      0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
      0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
      0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
      0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
      0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
   };

   unsigned int crc = ~0u;
   int i;
   for (i=0; i < len; ++i)
      crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];
   return ~crc;
#endif
}

#define stbiw__wpng4(o,a,b,c,d) ((o)[0]=STBIW_UCHAR(a),(o)[1]=STBIW_UCHAR(b),(o)[2]=STBIW_UCHAR(c),(o)[3]=STBIW_UCHAR(d),(o)+=4)
#define stbiw__wp32(data,v) stbiw__wpng4(data, (v)>>24,(v)>>16,(v)>>8,(v));
#define stbiw__wptag(data,s) stbiw__wpng4(data, s[0],s[1],s[2],s[3])

static void stbiw__wpcrc(unsigned char **data, int len)
{
   unsigned int crc = stbiw__crc32(*data - len - 4, len+4);
   stbiw__wp32(*data, crc);
}

static unsigned char stbiw__paeth(int a, int b, int c)
{
   int p = a + b - c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
   if (pa <= pb && pa <= pc) return STBIW_UCHAR(a);
   if (pb <= pc) return STBIW_UCHAR(b);
   return STBIW_UCHAR(c);
}

// @OPTIMIZE: provide an option that always forces left-predict or paeth predict
static void stbiw__encode_png_line(unsigned char *pixels, int stride_bytes, int width, int height, int y, int n, int filter_type, signed char *line_buffer)
{
   static int mapping[] = { 0,1,2,3,4 };
   static int firstmap[] = { 0,1,0,5,6 };
   int *mymap = (y != 0) ? mapping : firstmap;
   int i;
   int type = mymap[filter_type];
   unsigned char *z = pixels + stride_bytes * (stbi__flip_vertically_on_write ? height-1-y : y);
   int signed_stride = stbi__flip_vertically_on_write ? -stride_bytes : stride_bytes;

   if (type==0) {
      memcpy(line_buffer, z, width*n);
      return;
   }

   // first loop isn't optimized since it's just one pixel
   for (i = 0; i < n; ++i) {
      switch (type) {
         case 1: line_buffer[i] = z[i]; break;
         case 2: line_buffer[i] = z[i] - z[i-signed_stride]; break;
         case 3: line_buffer[i] = z[i] - (z[i-signed_stride]>>1); break;
         case 4: line_buffer[i] = (signed char) (z[i] - stbiw__paeth(0,z[i-signed_stride],0)); break;
         case 5: line_buffer[i] = z[i]; break;
         case 6: line_buffer[i] = z[i]; break;
      }
   }
   switch (type) {
      case 1: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-n]; break;
      case 2: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - z[i-signed_stride]; break;
      case 3: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - ((z[i-n] + z[i-signed_stride])>>1); break;
      case 4: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], z[i-signed_stride], z[i-signed_stride-n]); break;
      case 5: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - (z[i-n]>>1); break;
      case 6: for (i=n; i < width*n; ++i) line_buffer[i] = z[i] - stbiw__paeth(z[i-n], 0,0); break;
   }
}

STBIWDEF unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len)
{
   int force_filter = stbi_write_force_png_filter;
   int ctype[5] = { -1, 0, 4, 2, 6 };
   unsigned char sig[8] = { 137,80,78,71,13,10,26,10 };
   unsigned char *out,*o, *filt, *zlib;
   signed char *line_buffer;
   int j,zlen;

   if (stride_bytes == 0)
      stride_bytes = x * n;

   if (force_filter >= 5) {
      force_filter = -1;
   }

   filt = (unsigned char *) STBIW_MALLOC((x*n+1) * y); if (!filt) return 0;
   line_buffer = (signed char *) STBIW_MALLOC(x * n); if (!line_buffer) { STBIW_FREE(filt); return 0; }
   for (j=0; j < y; ++j) {
      int filter_type;
      if (force_filter > -1) {
         filter_type = force_filter;
         stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, force_filter, line_buffer);
      } else { // Estimate the best filter by running through all of them:
         int best_filter = 0, best_filter_val = 0x7fffffff, est, i;
         for (filter_type = 0; filter_type < 5; filter_type++) {
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, filter_type, line_buffer);

            // Estimate the entropy of the line using this filter; the less, the better.
            est = 0;
            for (i = 0; i < x*n; ++i) {
               est += abs((signed char) line_buffer[i]);
            }
            if (est < best_filter_val) {
               best_filter_val = est;
               best_filter = filter_type;
            }
         }
         if (filter_type != best_filter) {  // If the last iteration already got us the best filter, don't redo it
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, n, best_filter, line_buffer);
            filter_type = best_filter;
         }
      }
      // when we get here, filter_type contains the filter type, and line_buffer contains the data
      filt[j*(x*n+1)] = (unsigned char) filter_type;
      STBIW_MEMMOVE(filt+j*(x*n+1)+1, line_buffer, x*n);
   }
   STBIW_FREE(line_buffer);
   zlib = stbi_zlib_compress(filt, y*( x*n+1), &zlen, stbi_write_png_compression_level);
   STBIW_FREE(filt);
   if (!zlib) return 0;

   // each tag requires 12 bytes of overhead
   out = (unsigned char *) STBIW_MALLOC(8 + 12+13 + 12+zlen + 12);
   if (!out) return 0;
   *out_len = 8 + 12+13 + 12+zlen + 12;

   o=out;
   STBIW_MEMMOVE(o,sig,8); o+= 8;
   stbiw__wp32(o, 13); // header length
   stbiw__wptag(o, "IHDR");
   stbiw__wp32(o, x);
   stbiw__wp32(o, y);
   *o++ = 8;
   *o++ = STBIW_UCHAR(ctype[n]);
   *o++ = 0;
   *o++ = 0;
   *o++ = 0;
   stbiw__wpcrc(&o,13);

   stbiw__wp32(o, zlen);
   stbiw__wptag(o, "IDAT");
   STBIW_MEMMOVE(o, zlib, zlen);
   o += zlen;
   STBIW_FREE(zlib);
   stbiw__wpcrc(&o, zlen);

   stbiw__wp32(o,0);
   stbiw__wptag(o, "IEND");
   stbiw__wpcrc(&o,0);

   STBIW_ASSERT(o == out + *out_len);

   return out;
}

#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_png(char const *filename, int x, int y, int comp, const void *data, int stride_bytes)
{
   FILE *f;
   int len;
   unsigned char *png = stbi_write_png_to_mem((const unsigned char *) data, stride_bytes, x, y, comp, &len);
   if (png == NULL) return 0;

   f = stbiw__fopen(filename, "wb");
   if (!f) { STBIW_FREE(png); return 0; }
   fwrite(png, 1, len, f);
   fclose(f);
   STBIW_FREE(png);
   return 1;
}
#endif

STBIWDEF int stbi_write_png_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int stride_bytes)
{
   int len;
   unsigned char *png = stbi_write_png_to_mem((const unsigned char *) data, stride_bytes, x, y, comp, &len);
   if (png == NULL) return 0;
   func(context, png, len);
   STBIW_FREE(png);
   return 1;
}


/* ***************************************************************************
 *
 * JPEG writer
 *
 * This is based on Jon Olick's jo_jpeg.cpp:
 * public domain Simple, Minimalistic JPEG writer - http://www.jonolick.com/code.html
 */

static const unsigned char stbiw__jpg_ZigZag[] = { 0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,
      24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63 };

static void stbiw__jpg_writeBits(stbi__write_context *s, int *bitBufP, int *bitCntP, const unsigned short *bs) {
   int bitBuf = *bitBufP, bitCnt = *bitCntP;
   bitCnt += bs[1];
   bitBuf |= bs[0] << (24 - bitCnt);
   while(bitCnt >= 8) {
      unsigned char c = (bitBuf >> 16) & 255;
      stbiw__putc(s, c);
      if(c == 255) {
         stbiw__putc(s, 0);
      }
      bitBuf <<= 8;
      bitCnt -= 8;
   }
   *bitBufP = bitBuf;
   *bitCntP = bitCnt;
}

static void stbiw__jpg_DCT(float *d0p, float *d1p, float *d2p, float *d3p, float *d4p, float *d5p, float *d6p, float *d7p) {
   float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p, d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
   float z1, z2, z3, z4, z5, z11, z13;

   float tmp0 = d0 + d7;
   float tmp7 = d0 - d7;
   float tmp1 = d1 + d6;
   float tmp6 = d1 - d6;
   float tmp2 = d2 + d5;
   float tmp5 = d2 - d5;
   float tmp3 = d3 + d4;
   float tmp4 = d3 - d4;

   // Even part
   float tmp10 = tmp0 + tmp3;   // phase 2
   float tmp13 = tmp0 - tmp3;
   float tmp11 = tmp1 + tmp2;
   float tmp12 = tmp1 - tmp2;

   d0 = tmp10 + tmp11;       // phase 3
   d4 = tmp10 - tmp11;

   z1 = (tmp12 + tmp13) * 0.707106781f; // c4
   d2 = tmp13 + z1;       // phase 5
   d6 = tmp13 - z1;

   // Odd part
   tmp10 = tmp4 + tmp5;       // phase 2
   tmp11 = tmp5 + tmp6;
   tmp12 = tmp6 + tmp7;

   // The rotator is modified from fig 4-8 to avoid extra negations.
   z5 = (tmp10 - tmp12) * 0.382683433f; // c6
   z2 = tmp10 * 0.541196100f + z5; // c2-c6
   z4 = tmp12 * 1.306562965f + z5; // c2+c6
   z3 = tmp11 * 0.707106781f; // c4

   z11 = tmp7 + z3;      // phase 5
   z13 = tmp7 - z3;

   *d5p = z13 + z2;         // phase 6
   *d3p = z13 - z2;
   *d1p = z11 + z4;
   *d7p = z11 - z4;

   *d0p = d0;  *d2p = d2;  *d4p = d4;  *d6p = d6;
}

static void stbiw__jpg_calcBits(int val, unsigned short bits[2]) {
   int tmp1 = val < 0 ? -val : val;
   val = val < 0 ? val-1 : val;
   bits[1] = 1;
   while(tmp1 >>= 1) {
      ++bits[1];
   }
   bits[0] = val & ((1<<bits[1])-1);
}

static int stbiw__jpg_processDU(stbi__write_context *s, int *bitBuf, int *bitCnt, float *CDU, int du_stride, float *fdtbl, int DC, const unsigned short HTDC[256][2], const unsigned short HTAC[256][2]) {
   const unsigned short EOB[2] = { HTAC[0x00][0], HTAC[0x00][1] };
   const unsigned short M16zeroes[2] = { HTAC[0xF0][0], HTAC[0xF0][1] };
   int dataOff, i, j, n, diff, end0pos, x, y;
   int DU[64];

   // DCT rows
   for(dataOff=0, n=du_stride*8; dataOff<n; dataOff+=du_stride) {
      stbiw__jpg_DCT(&CDU[dataOff], &CDU[dataOff+1], &CDU[dataOff+2], &CDU[dataOff+3], &CDU[dataOff+4], &CDU[dataOff+5], &CDU[dataOff+6], &CDU[dataOff+7]);
   }
   // DCT columns
   for(dataOff=0; dataOff<8; ++dataOff) {
      stbiw__jpg_DCT(&CDU[dataOff], &CDU[dataOff+du_stride], &CDU[dataOff+du_stride*2], &CDU[dataOff+du_stride*3], &CDU[dataOff+du_stride*4],
                     &CDU[dataOff+du_stride*5], &CDU[dataOff+du_stride*6], &CDU[dataOff+du_stride*7]);
   }
   // Quantize/descale/zigzag the coefficients
   for(y = 0, j=0; y < 8; ++y) {
      for(x = 0; x < 8; ++x,++j) {
         float v;
         i = y*du_stride+x;
         v = CDU[i]*fdtbl[j];
         // DU[stbiw__jpg_ZigZag[j]] = (int)(v < 0 ? ceilf(v - 0.5f) : floorf(v + 0.5f));
         // ceilf() and floorf() are C99, not C89, but I /think/ they're not needed here anyway?
         DU[stbiw__jpg_ZigZag[j]] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
      }
   }

   // Encode DC
   diff = DU[0] - DC;
   if (diff == 0) {
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, HTDC[0]);
   } else {
      unsigned short bits[2];
      stbiw__jpg_calcBits(diff, bits);
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, HTDC[bits[1]]);
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, bits);
   }
   // Encode ACs
   end0pos = 63;
   for(; (end0pos>0)&&(DU[end0pos]==0); --end0pos) {
   }
   // end0pos = first element in reverse order !=0
   if(end0pos == 0) {
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, EOB);
      return DU[0];
   }
   for(i = 1; i <= end0pos; ++i) {
      int startpos = i;
      int nrzeroes;
      unsigned short bits[2];
      for (; DU[i]==0 && i<=end0pos; ++i) {
      }
      nrzeroes = i-startpos;
      if ( nrzeroes >= 16 ) {
         int lng = nrzeroes>>4;
         int nrmarker;
         for (nrmarker=1; nrmarker <= lng; ++nrmarker)
            stbiw__jpg_writeBits(s, bitBuf, bitCnt, M16zeroes);
         nrzeroes &= 15;
      }
      stbiw__jpg_calcBits(DU[i], bits);
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, HTAC[(nrzeroes<<4)+bits[1]]);
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, bits);
   }
   if(end0pos != 63) {
      stbiw__jpg_writeBits(s, bitBuf, bitCnt, EOB);
   }
   return DU[0];
}

static int stbi_write_jpg_core(stbi__write_context *s, int width, int height, int comp, const void* data, int quality) {
   // Constants that don't pollute global namespace
   static const unsigned char std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
   static const unsigned char std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
   static const unsigned char std_ac_luminance_values[] = {
      0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
      0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
      0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
      0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
      0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
      0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
      0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   static const unsigned char std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
   static const unsigned char std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
   static const unsigned char std_ac_chrominance_values[] = {
      0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
      0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
      0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
      0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
      0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
      0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
      0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   // Huffman tables
   static const unsigned short YDC_HT[256][2] = { {0,2},{2,3},{3,3},{4,3},{5,3},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9}};
   static const unsigned short UVDC_HT[256][2] = { {0,2},{1,2},{2,2},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9},{1022,10},{2046,11}};
   static const unsigned short YAC_HT[256][2] = {
      {10,4},{0,2},{1,2},{4,3},{11,4},{26,5},{120,7},{248,8},{1014,10},{65410,16},{65411,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {12,4},{27,5},{121,7},{502,9},{2038,11},{65412,16},{65413,16},{65414,16},{65415,16},{65416,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {28,5},{249,8},{1015,10},{4084,12},{65417,16},{65418,16},{65419,16},{65420,16},{65421,16},{65422,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{503,9},{4085,12},{65423,16},{65424,16},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1016,10},{65430,16},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2039,11},{65438,16},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {123,7},{4086,12},{65446,16},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {250,8},{4087,12},{65454,16},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{32704,15},{65462,16},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65470,16},{65471,16},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65479,16},{65480,16},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1017,10},{65488,16},{65489,16},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{65497,16},{65498,16},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2040,11},{65506,16},{65507,16},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {65515,16},{65516,16},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65525,16},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
   static const unsigned short UVAC_HT[256][2] = {
      {0,2},{1,2},{4,3},{10,4},{24,5},{25,5},{56,6},{120,7},{500,9},{1014,10},{4084,12},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {11,4},{57,6},{246,8},{501,9},{2038,11},{4085,12},{65416,16},{65417,16},{65418,16},{65419,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {26,5},{247,8},{1015,10},{4086,12},{32706,15},{65420,16},{65421,16},{65422,16},{65423,16},{65424,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {27,5},{248,8},{1016,10},{4087,12},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{65430,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{502,9},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{65438,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1017,10},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{65446,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {121,7},{2039,11},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{65454,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2040,11},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{65462,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {249,8},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{65470,16},{65471,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {503,9},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{65479,16},{65480,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{65488,16},{65489,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{65497,16},{65498,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{65506,16},{65507,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{65515,16},{65516,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {16352,14},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{65525,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{32707,15},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
   static const int YQT[] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
                             37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
   static const int UVQT[] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                              99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
   static const float aasf[] = { 1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f, 1.175875602f * 2.828427125f,
                                 1.0f * 2.828427125f, 0.785694958f * 2.828427125f, 0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f };

   int row, col, i, k, subsample;
   float fdtbl_Y[64], fdtbl_UV[64];
   unsigned char YTable[64], UVTable[64];

   if(!data || !width || !height || comp > 4 || comp < 1) {
      return 0;
   }

   quality = quality ? quality : 90;
   subsample = quality <= 90 ? 1 : 0;
   quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
   quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

   for(i = 0; i < 64; ++i) {
      int uvti, yti = (YQT[i]*quality+50)/100;
      YTable[stbiw__jpg_ZigZag[i]] = (unsigned char) (yti < 1 ? 1 : yti > 255 ? 255 : yti);
      uvti = (UVQT[i]*quality+50)/100;
      UVTable[stbiw__jpg_ZigZag[i]] = (unsigned char) (uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
   }

   for(row = 0, k = 0; row < 8; ++row) {
      for(col = 0; col < 8; ++col, ++k) {
         fdtbl_Y[k]  = 1 / (YTable [stbiw__jpg_ZigZag[k]] * aasf[row] * aasf[col]);
         fdtbl_UV[k] = 1 / (UVTable[stbiw__jpg_ZigZag[k]] * aasf[row] * aasf[col]);
      }
   }

   // Write Headers
   {
      static const unsigned char head0[] = { 0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xDB,0,0x84,0 };
      static const unsigned char head2[] = { 0xFF,0xDA,0,0xC,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
      const unsigned char head1[] = { 0xFF,0xC0,0,0x11,8,(unsigned char)(height>>8),STBIW_UCHAR(height),(unsigned char)(width>>8),STBIW_UCHAR(width),
                                      3,1,(unsigned char)(subsample?0x22:0x11),0,2,0x11,1,3,0x11,1,0xFF,0xC4,0x01,0xA2,0 };
      s->func(s->context, (void*)head0, sizeof(head0));
      s->func(s->context, (void*)YTable, sizeof(YTable));
      stbiw__putc(s, 1);
      s->func(s->context, UVTable, sizeof(UVTable));
      s->func(s->context, (void*)head1, sizeof(head1));
      s->func(s->context, (void*)(std_dc_luminance_nrcodes+1), sizeof(std_dc_luminance_nrcodes)-1);
      s->func(s->context, (void*)std_dc_luminance_values, sizeof(std_dc_luminance_values));
      stbiw__putc(s, 0x10); // HTYACinfo
      s->func(s->context, (void*)(std_ac_luminance_nrcodes+1), sizeof(std_ac_luminance_nrcodes)-1);
      s->func(s->context, (void*)std_ac_luminance_values, sizeof(std_ac_luminance_values));
      stbiw__putc(s, 1); // HTUDCinfo
      s->func(s->context, (void*)(std_dc_chrominance_nrcodes+1), sizeof(std_dc_chrominance_nrcodes)-1);
      s->func(s->context, (void*)std_dc_chrominance_values, sizeof(std_dc_chrominance_values));
      stbiw__putc(s, 0x11); // HTUACinfo
      s->func(s->context, (void*)(std_ac_chrominance_nrcodes+1), sizeof(std_ac_chrominance_nrcodes)-1);
      s->func(s->context, (void*)std_ac_chrominance_values, sizeof(std_ac_chrominance_values));
      s->func(s->context, (void*)head2, sizeof(head2));
   }

   // Encode 8x8 macroblocks
   {
      static const unsigned short fillBits[] = {0x7F, 7};
      int DCY=0, DCU=0, DCV=0;
      int bitBuf=0, bitCnt=0;
      // comp == 2 is grey+alpha (alpha is ignored)
      int ofsG = comp > 2 ? 1 : 0, ofsB = comp > 2 ? 2 : 0;
      const unsigned char *dataR = (const unsigned char *)data;
      const unsigned char *dataG = dataR + ofsG;
      const unsigned char *dataB = dataR + ofsB;
      int x, y, pos;
      if(subsample) {
         for(y = 0; y < height; y += 16) {
            for(x = 0; x < width; x += 16) {
               float Y[256], U[256], V[256];
               for(row = y, pos = 0; row < y+16; ++row) {
                  // row >= height => use last input row
                  int clamped_row = (row < height) ? row : height - 1;
                  int base_p = (stbi__flip_vertically_on_write ? (height-1-clamped_row) : clamped_row)*width*comp;
                  for(col = x; col < x+16; ++col, ++pos) {
                     // if col >= width => use pixel from last input column
                     int p = base_p + ((col < width) ? col : (width-1))*comp;
                     float r = dataR[p], g = dataG[p], b = dataB[p];
                     Y[pos]= +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                     U[pos]= -0.16874f*r - 0.33126f*g + 0.50000f*b;
                     V[pos]= +0.50000f*r - 0.41869f*g - 0.08131f*b;
                  }
               }
               DCY = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, Y+0,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, Y+8,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, Y+128, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
               DCY = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, Y+136, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);

               // subsample U,V
               {
                  float subU[64], subV[64];
                  int yy, xx;
                  for(yy = 0, pos = 0; yy < 8; ++yy) {
                     for(xx = 0; xx < 8; ++xx, ++pos) {
                        int j = yy*32+xx*2;
                        subU[pos] = (U[j+0] + U[j+1] + U[j+16] + U[j+17]) * 0.25f;
                        subV[pos] = (V[j+0] + V[j+1] + V[j+16] + V[j+17]) * 0.25f;
                     }
                  }
                  DCU = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, subU, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
                  DCV = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, subV, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
               }
            }
         }
      } else {
         for(y = 0; y < height; y += 8) {
            for(x = 0; x < width; x += 8) {
               float Y[64], U[64], V[64];
               for(row = y, pos = 0; row < y+8; ++row) {
                  // row >= height => use last input row
                  int clamped_row = (row < height) ? row : height - 1;
                  int base_p = (stbi__flip_vertically_on_write ? (height-1-clamped_row) : clamped_row)*width*comp;
                  for(col = x; col < x+8; ++col, ++pos) {
                     // if col >= width => use pixel from last input column
                     int p = base_p + ((col < width) ? col : (width-1))*comp;
                     float r = dataR[p], g = dataG[p], b = dataB[p];
                     Y[pos]= +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                     U[pos]= -0.16874f*r - 0.33126f*g + 0.50000f*b;
                     V[pos]= +0.50000f*r - 0.41869f*g - 0.08131f*b;
                  }
               }

               DCY = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, Y, 8, fdtbl_Y,  DCY, YDC_HT, YAC_HT);
               DCU = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, U, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
               DCV = stbiw__jpg_processDU(s, &bitBuf, &bitCnt, V, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
            }
         }
      }

      // Do the bit alignment of the EOI marker
      stbiw__jpg_writeBits(s, &bitBuf, &bitCnt, fillBits);
   }

   // EOI
   stbiw__putc(s, 0xFF);
   stbiw__putc(s, 0xD9);

   return 1;
}

STBIWDEF int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int quality)
{
   stbi__write_context s = { 0 };
   stbi__start_write_callbacks(&s, func, context);
   return stbi_write_jpg_core(&s, x, y, comp, (void *) data, quality);
}


#ifndef STBI_WRITE_NO_STDIO
STBIWDEF int stbi_write_jpg(char const *filename, int x, int y, int comp, const void *data, int quality)
{
   stbi__write_context s = { 0 };
   if (stbi__start_write_file(&s,filename)) {
      int r = stbi_write_jpg_core(&s, x, y, comp, data, quality);
      stbi__end_write_file(&s);
      return r;
   } else
      return 0;
}
#endif

#endif // STB_IMAGE_WRITE_IMPLEMENTATION

/* Revision history
      1.16  (2021-07-11)
             make Deflate code emit uncompressed blocks when it would otherwise expand
             support writing BMPs with alpha channel
      1.15  (2020-07-13) unknown
      1.14  (2020-02-02) updated JPEG writer to downsample chroma channels
      1.13
      1.12
      1.11  (2019-08-11)

      1.10  (2019-02-07)
             support utf8 filenames in Windows; fix warnings and platform ifdefs
      1.09  (2018-02-11)
             fix typo in zlib quality API, improve STB_I_W_STATIC in C++
      1.08  (2018-01-29)
             add stbi__flip_vertically_on_write, external zlib, zlib quality, choose PNG filter
      1.07  (2017-07-24)
             doc fix
      1.06 (2017-07-23)
             writing JPEG (using Jon Olick's code)
      1.05   ???
      1.04 (2017-03-03)
             monochrome BMP expansion
      1.03   ???
      1.02 (2016-04-02)
             avoid allocating large structures on the stack
      1.01 (2016-01-16)
             STBIW_REALLOC_SIZED: support allocators with no realloc support
             avoid race-condition in crc initialization
             minor compile issues
      1.00 (2015-09-14)
             installable file IO function
      0.99 (2015-09-13)
             warning fixes; TGA rle support
      0.98 (2015-04-08)
             added STBIW_MALLOC, STBIW_ASSERT etc
      0.97 (2015-01-18)
             fixed HDR asserts, rewrote HDR rle logic
      0.96 (2015-01-17)
             add HDR output
             fix monochrome BMP
      0.95 (2014-08-17)
             add monochrome TGA output
      0.94 (2014-05-31)
             rename private functions to avoid conflicts with stb_image.h
      0.93 (2014-05-27)
             warning fixes
      0.92 (2010-08-01)
             casts to unsigned char to fix warnings
      0.91 (2010-07-17)
             first public release
      0.90   first internal release
*/

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/

#define STB_TRUETYPE_IMPLEMENTATION
// stb_truetype.h - v1.26 - public domain
// authored from 2009-2021 by Sean Barrett / RAD Game Tools
//
// =======================================================================
//
//    NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES
//
// This library does no range checking of the offsets found in the file,
// meaning an attacker can use it to read arbitrary memory.
//
// =======================================================================
//
//   This library processes TrueType files:
//        parse files
//        extract glyph metrics
//        extract glyph shapes
//        render glyphs to one-channel bitmaps with antialiasing (box filter)
//        render glyphs to one-channel SDF bitmaps (signed-distance field/function)
//
//   Todo:
//        non-MS cmaps
//        crashproof on bad data
//        hinting? (no longer patented)
//        cleartype-style AA?
//        optimize: use simple memory allocator for intermediates
//        optimize: build edge-list directly from curves
//        optimize: rasterize directly from curves?
//
// ADDITIONAL CONTRIBUTORS
//
//   Mikko Mononen: compound shape support, more cmap formats
//   Tor Andersson: kerning, subpixel rendering
//   Dougall Johnson: OpenType / Type 2 font handling
//   Daniel Ribeiro Maciel: basic GPOS-based kerning
//
//   Misc other:
//       Ryan Gordon
//       Simon Glass
//       github:IntellectualKitty
//       Imanol Celaya
//       Daniel Ribeiro Maciel
//
//   Bug/warning reports/fixes:
//       "Zer" on mollyrocket       Fabian "ryg" Giesen   github:NiLuJe
//       Cass Everitt               Martins Mozeiko       github:aloucks
//       stoiko (Haemimont Games)   Cap Petschulat        github:oyvindjam
//       Brian Hook                 Omar Cornut           github:vassvik
//       Walter van Niftrik         Ryan Griege
//       David Gow                  Peter LaValle
//       David Given                Sergey Popov
//       Ivan-Assen Ivanov          Giumo X. Clanjor
//       Anthony Pesch              Higor Euripedes
//       Johan Duparc               Thomas Fields
//       Hou Qiming                 Derek Vinyard
//       Rob Loach                  Cort Stratton
//       Kenney Phillis Jr.         Brian Costabile
//       Ken Voskuil (kaesve)       Yakov Galka
//
// VERSION HISTORY
//
//   1.26 (2021-08-28) fix broken rasterizer
//   1.25 (2021-07-11) many fixes
//   1.24 (2020-02-05) fix warning
//   1.23 (2020-02-02) query SVG data for glyphs; query whole kerning table (but only kern not GPOS)
//   1.22 (2019-08-11) minimize missing-glyph duplication; fix kerning if both 'GPOS' and 'kern' are defined
//   1.21 (2019-02-25) fix warning
//   1.20 (2019-02-07) PackFontRange skips missing codepoints; GetScaleFontVMetrics()
//   1.19 (2018-02-11) GPOS kerning, STBTT_fmod
//   1.18 (2018-01-29) add missing function
//   1.17 (2017-07-23) make more arguments const; doc fix
//   1.16 (2017-07-12) SDF support
//   1.15 (2017-03-03) make more arguments const
//   1.14 (2017-01-16) num-fonts-in-TTC function
//   1.13 (2017-01-02) support OpenType fonts, certain Apple fonts
//   1.12 (2016-10-25) suppress warnings about casting away const with -Wcast-qual
//   1.11 (2016-04-02) fix unused-variable warning
//   1.10 (2016-04-02) user-defined fabs(); rare memory leak; remove duplicate typedef
//   1.09 (2016-01-16) warning fix; avoid crash on outofmem; use allocation userdata properly
//   1.08 (2015-09-13) document stbtt_Rasterize(); fixes for vertical & horizontal edges
//   1.07 (2015-08-01) allow PackFontRanges to accept arrays of sparse codepoints;
//                     variant PackFontRanges to pack and render in separate phases;
//                     fix stbtt_GetFontOFfsetForIndex (never worked for non-0 input?);
//                     fixed an assert() bug in the new rasterizer
//                     replace assert() with STBTT_assert() in new rasterizer
//
//   Full history can be found at the end of this file.
//
// LICENSE
//
//   See end of file for license information.
//
// USAGE
//
//   Include this file in whatever places need to refer to it. In ONE C/C++
//   file, write:
//      #define STB_TRUETYPE_IMPLEMENTATION
//   before the #include of this file. This expands out the actual
//   implementation into that C/C++ file.
//
//   To make the implementation private to the file that generates the implementation,
//      #define STBTT_STATIC
//
//   Simple 3D API (don't ship this, but it's fine for tools and quick start)
//           stbtt_BakeFontBitmap()               -- bake a font to a bitmap for use as texture
//           stbtt_GetBakedQuad()                 -- compute quad to draw for a given char
//
//   Improved 3D API (more shippable):
//           #include "stb_rect_pack.h"           -- optional, but you really want it
//           stbtt_PackBegin()
//           stbtt_PackSetOversampling()          -- for improved quality on small fonts
//           stbtt_PackFontRanges()               -- pack and renders
//           stbtt_PackEnd()
//           stbtt_GetPackedQuad()
//
//   "Load" a font file from a memory buffer (you have to keep the buffer loaded)
//           stbtt_InitFont()
//           stbtt_GetFontOffsetForIndex()        -- indexing for TTC font collections
//           stbtt_GetNumberOfFonts()             -- number of fonts for TTC font collections
//
//   Render a unicode codepoint to a bitmap
//           stbtt_GetCodepointBitmap()           -- allocates and returns a bitmap
//           stbtt_MakeCodepointBitmap()          -- renders into bitmap you provide
//           stbtt_GetCodepointBitmapBox()        -- how big the bitmap must be
//
//   Character advance/positioning
//           stbtt_GetCodepointHMetrics()
//           stbtt_GetFontVMetrics()
//           stbtt_GetFontVMetricsOS2()
//           stbtt_GetCodepointKernAdvance()
//
//   Starting with version 1.06, the rasterizer was replaced with a new,
//   faster and generally-more-precise rasterizer. The new rasterizer more
//   accurately measures pixel coverage for anti-aliasing, except in the case
//   where multiple shapes overlap, in which case it overestimates the AA pixel
//   coverage. Thus, anti-aliasing of intersecting shapes may look wrong. If
//   this turns out to be a problem, you can re-enable the old rasterizer with
//        #define STBTT_RASTERIZER_VERSION 1
//   which will incur about a 15% speed hit.
//
// ADDITIONAL DOCUMENTATION
//
//   Immediately after this block comment are a series of sample programs.
//
//   After the sample programs is the "header file" section. This section
//   includes documentation for each API function.
//
//   Some important concepts to understand to use this library:
//
//      Codepoint
//         Characters are defined by unicode codepoints, e.g. 65 is
//         uppercase A, 231 is lowercase c with a cedilla, 0x7e30 is
//         the hiragana for "ma".
//
//      Glyph
//         A visual character shape (every codepoint is rendered as
//         some glyph)
//
//      Glyph index
//         A font-specific integer ID representing a glyph
//
//      Baseline
//         Glyph shapes are defined relative to a baseline, which is the
//         bottom of uppercase characters. Characters extend both above
//         and below the baseline.
//
//      Current Point
//         As you draw text to the screen, you keep track of a "current point"
//         which is the origin of each character. The current point's vertical
//         position is the baseline. Even "baked fonts" use this model.
//
//      Vertical Font Metrics
//         The vertical qualities of the font, used to vertically position
//         and space the characters. See docs for stbtt_GetFontVMetrics.
//
//      Font Size in Pixels or Points
//         The preferred interface for specifying font sizes in stb_truetype
//         is to specify how tall the font's vertical extent should be in pixels.
//         If that sounds good enough, skip the next paragraph.
//
//         Most font APIs instead use "points", which are a common typographic
//         measurement for describing font size, defined as 72 points per inch.
//         stb_truetype provides a point API for compatibility. However, true
//         "per inch" conventions don't make much sense on computer displays
//         since different monitors have different number of pixels per
//         inch. For example, Windows traditionally uses a convention that
//         there are 96 pixels per inch, thus making 'inch' measurements have
//         nothing to do with inches, and thus effectively defining a point to
//         be 1.333 pixels. Additionally, the TrueType font data provides
//         an explicit scale factor to scale a given font's glyphs to points,
//         but the author has observed that this scale factor is often wrong
//         for non-commercial fonts, thus making fonts scaled in points
//         according to the TrueType spec incoherently sized in practice.
//
// DETAILED USAGE:
//
//  Scale:
//    Select how high you want the font to be, in points or pixels.
//    Call ScaleForPixelHeight or ScaleForMappingEmToPixels to compute
//    a scale factor SF that will be used by all other functions.
//
//  Baseline:
//    You need to select a y-coordinate that is the baseline of where
//    your text will appear. Call GetFontBoundingBox to get the baseline-relative
//    bounding box for all characters. SF*-y0 will be the distance in pixels
//    that the worst-case character could extend above the baseline, so if
//    you want the top edge of characters to appear at the top of the
//    screen where y=0, then you would set the baseline to SF*-y0.
//
//  Current point:
//    Set the current point where the first character will appear. The
//    first character could extend left of the current point; this is font
//    dependent. You can either choose a current point that is the leftmost
//    point and hope, or add some padding, or check the bounding box or
//    left-side-bearing of the first character to be displayed and set
//    the current point based on that.
//
//  Displaying a character:
//    Compute the bounding box of the character. It will contain signed values
//    relative to <current_point, baseline>. I.e. if it returns x0,y0,x1,y1,
//    then the character should be displayed in the rectangle from
//    <current_point+SF*x0, baseline+SF*y0> to <current_point+SF*x1,baseline+SF*y1).
//
//  Advancing for the next character:
//    Call GlyphHMetrics, and compute 'current_point += SF * advance'.
//
//
// ADVANCED USAGE
//
//   Quality:
//
//    - Use the functions with Subpixel at the end to allow your characters
//      to have subpixel positioning. Since the font is anti-aliased, not
//      hinted, this is very import for quality. (This is not possible with
//      baked fonts.)
//
//    - Kerning is now supported, and if you're supporting subpixel rendering
//      then kerning is worth using to give your text a polished look.
//
//   Performance:
//
//    - Convert Unicode codepoints to glyph indexes and operate on the glyphs;
//      if you don't do this, stb_truetype is forced to do the conversion on
//      every call.
//
//    - There are a lot of memory allocations. We should modify it to take
//      a temp buffer and allocate from the temp buffer (without freeing),
//      should help performance a lot.
//
// NOTES
//
//   The system uses the raw data found in the .ttf file without changing it
//   and without building auxiliary data structures. This is a bit inefficient
//   on little-endian systems (the data is big-endian), but assuming you're
//   caching the bitmaps or glyph shapes this shouldn't be a big deal.
//
//   It appears to be very hard to programmatically determine what font a
//   given file is in a general way. I provide an API for this, but I don't
//   recommend it.
//
//
// PERFORMANCE MEASUREMENTS FOR 1.06:
//
//                      32-bit     64-bit
//   Previous release:  8.83 s     7.68 s
//   Pool allocations:  7.72 s     6.34 s
//   Inline sort     :  6.54 s     5.65 s
//   New rasterizer  :  5.63 s     5.00 s

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////  SAMPLE PROGRAMS
////
//
//  Incomplete text-in-3d-api example, which draws quads properly aligned to be lossless.
//  See "tests/truetype_demo_win32.c" for a complete version.
#if 0
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_truetype.h"

unsigned char ttf_buffer[1<<20];
unsigned char temp_bitmap[512*512];

stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs
GLuint ftex;

void my_stbtt_initfont(void)
{
   fread(ttf_buffer, 1, 1<<20, fopen("c:/windows/fonts/times.ttf", "rb"));
   stbtt_BakeFontBitmap(ttf_buffer,0, 32.0, temp_bitmap,512,512, 32,96, cdata); // no guarantee this fits!
   // can free ttf_buffer at this point
   glGenTextures(1, &ftex);
   glBindTexture(GL_TEXTURE_2D, ftex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512,512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
   // can free temp_bitmap at this point
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void my_stbtt_print(float x, float y, char *text)
{
   // assume orthographic projection with units = screen pixels, origin at top left
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glEnable(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D, ftex);
   glBegin(GL_QUADS);
   while (*text) {
      if (*text >= 32 && *text < 128) {
         stbtt_aligned_quad q;
         stbtt_GetBakedQuad(cdata, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
         glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
         glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
         glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
         glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
      }
      ++text;
   }
   glEnd();
}
#endif
//
//
//////////////////////////////////////////////////////////////////////////////
//
// Complete program (this compiles): get a single bitmap, print as ASCII art
//
#if 0
#include <stdio.h>
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_truetype.h"

char ttf_buffer[1<<25];

int main(int argc, char **argv)
{
   stbtt_fontinfo font;
   unsigned char *bitmap;
   int w,h,i,j,c = (argc > 1 ? atoi(argv[1]) : 'a'), s = (argc > 2 ? atoi(argv[2]) : 20);

   fread(ttf_buffer, 1, 1<<25, fopen(argc > 3 ? argv[3] : "c:/windows/fonts/arialbd.ttf", "rb"));

   stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer,0));
   bitmap = stbtt_GetCodepointBitmap(&font, 0,stbtt_ScaleForPixelHeight(&font, s), c, &w, &h, 0,0);

   for (j=0; j < h; ++j) {
      for (i=0; i < w; ++i)
         putchar(" .:ioVM@"[bitmap[j*w+i]>>5]);
      putchar('\n');
   }
   return 0;
}
#endif
//
// Output:
//
//     .ii.
//    @@@@@@.
//   V@Mio@@o
//   :i.  V@V
//     :oM@@M
//   :@@@MM@M
//   @@o  o@M
//  :@@.  M@M
//   @@@o@@@@
//   :M@@V:@@.
//
//////////////////////////////////////////////////////////////////////////////
//
// Complete program: print "Hello World!" banner, with bugs
//
#if 0
char buffer[24<<20];
unsigned char screen[20][79];

int main(int arg, char **argv)
{
   stbtt_fontinfo font;
   int i,j,ascent,baseline,ch=0;
   float scale, xpos=2; // leave a little padding in case the character extends left
   char *text = "Heljo World!"; // intentionally misspelled to show 'lj' brokenness

   fread(buffer, 1, 1000000, fopen("c:/windows/fonts/arialbd.ttf", "rb"));
   stbtt_InitFont(&font, buffer, 0);

   scale = stbtt_ScaleForPixelHeight(&font, 15);
   stbtt_GetFontVMetrics(&font, &ascent,0,0);
   baseline = (int) (ascent*scale);

   while (text[ch]) {
      int advance,lsb,x0,y0,x1,y1;
      float x_shift = xpos - (float) floor(xpos);
      stbtt_GetCodepointHMetrics(&font, text[ch], &advance, &lsb);
      stbtt_GetCodepointBitmapBoxSubpixel(&font, text[ch], scale,scale,x_shift,0, &x0,&y0,&x1,&y1);
      stbtt_MakeCodepointBitmapSubpixel(&font, &screen[baseline + y0][(int) xpos + x0], x1-x0,y1-y0, 79, scale,scale,x_shift,0, text[ch]);
      // note that this stomps the old data, so where character boxes overlap (e.g. 'lj') it's wrong
      // because this API is really for baking character bitmaps into textures. if you want to render
      // a sequence of characters, you really need to render each bitmap to a temp buffer, then
      // "alpha blend" that into the working buffer
      xpos += (advance * scale);
      if (text[ch+1])
         xpos += scale*stbtt_GetCodepointKernAdvance(&font, text[ch],text[ch+1]);
      ++ch;
   }

   for (j=0; j < 20; ++j) {
      for (i=0; i < 78; ++i)
         putchar(" .:ioVM@"[screen[j][i]>>5]);
      putchar('\n');
   }

   return 0;
}
#endif


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////   INTEGRATION WITH YOUR CODEBASE
////
////   The following sections allow you to supply alternate definitions
////   of C library functions used by stb_truetype, e.g. if you don't
////   link with the C runtime library.

#ifdef STB_TRUETYPE_IMPLEMENTATION
   // #define your own (u)stbtt_int8/16/32 before including to override this
   #ifndef stbtt_uint8
   typedef unsigned char   stbtt_uint8;
   typedef signed   char   stbtt_int8;
   typedef unsigned short  stbtt_uint16;
   typedef signed   short  stbtt_int16;
   typedef unsigned int    stbtt_uint32;
   typedef signed   int    stbtt_int32;
   #endif

   typedef char stbtt__check_size32[sizeof(stbtt_int32)==4 ? 1 : -1];
   typedef char stbtt__check_size16[sizeof(stbtt_int16)==2 ? 1 : -1];

   // e.g. #define your own STBTT_ifloor/STBTT_iceil() to avoid math.h
   #ifndef STBTT_ifloor
   #include <math.h>
   #define STBTT_ifloor(x)   ((int) floor(x))
   #define STBTT_iceil(x)    ((int) ceil(x))
   #endif

   #ifndef STBTT_sqrt
   #include <math.h>
   #define STBTT_sqrt(x)      sqrt(x)
   #define STBTT_pow(x,y)     pow(x,y)
   #endif

   #ifndef STBTT_fmod
   #include <math.h>
   #define STBTT_fmod(x,y)    fmod(x,y)
   #endif

   #ifndef STBTT_cos
   #include <math.h>
   #define STBTT_cos(x)       cos(x)
   #define STBTT_acos(x)      acos(x)
   #endif

   #ifndef STBTT_fabs
   #include <math.h>
   #define STBTT_fabs(x)      fabs(x)
   #endif

   // #define your own functions "STBTT_malloc" / "STBTT_free" to avoid malloc.h
   #ifndef STBTT_malloc
   #include <stdlib.h>
   #define STBTT_malloc(x,u)  ((void)(u),malloc(x))
   #define STBTT_free(x,u)    ((void)(u),free(x))
   #endif

   #ifndef STBTT_assert
   #include <assert.h>
   #define STBTT_assert(x)    assert(x)
   #endif

   #ifndef STBTT_strlen
   #include <string.h>
   #define STBTT_strlen(x)    strlen(x)
   #endif

   #ifndef STBTT_memcpy
   #include <string.h>
   #define STBTT_memcpy       memcpy
   #define STBTT_memset       memset
   #endif
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////   INTERFACE
////
////

#ifndef __STB_INCLUDE_STB_TRUETYPE_H__
#define __STB_INCLUDE_STB_TRUETYPE_H__

#ifdef STBTT_STATIC
#define STBTT_DEF static
#else
#define STBTT_DEF extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

// private structure
typedef struct
{
   unsigned char *data;
   int cursor;
   int size;
} stbtt__buf;

//////////////////////////////////////////////////////////////////////////////
//
// TEXTURE BAKING API
//
// If you use this API, you only have to call two functions ever.
//

typedef struct
{
   unsigned short x0,y0,x1,y1; // coordinates of bbox in bitmap
   float xoff,yoff,xadvance;
} stbtt_bakedchar;

STBTT_DEF int stbtt_BakeFontBitmap(const unsigned char *data, int offset,  // font location (use offset=0 for plain .ttf)
                                float pixel_height,                     // height of font in pixels
                                unsigned char *pixels, int pw, int ph,  // bitmap to be filled in
                                int first_char, int num_chars,          // characters to bake
                                stbtt_bakedchar *chardata);             // you allocate this, it's num_chars long
// if return is positive, the first unused row of the bitmap
// if return is negative, returns the negative of the number of characters that fit
// if return is 0, no characters fit and no rows were used
// This uses a very crappy packing.

typedef struct
{
   float x0,y0,s0,t0; // top-left
   float x1,y1,s1,t1; // bottom-right
} stbtt_aligned_quad;

STBTT_DEF void stbtt_GetBakedQuad(const stbtt_bakedchar *chardata, int pw, int ph,  // same data as above
                               int char_index,             // character to display
                               float *xpos, float *ypos,   // pointers to current position in screen pixel space
                               stbtt_aligned_quad *q,      // output: quad to draw
                               int opengl_fillrule);       // true if opengl fill rule; false if DX9 or earlier
// Call GetBakedQuad with char_index = 'character - first_char', and it
// creates the quad you need to draw and advances the current position.
//
// The coordinate system used assumes y increases downwards.
//
// Characters will extend both above and below the current position;
// see discussion of "BASELINE" above.
//
// It's inefficient; you might want to c&p it and optimize it.

STBTT_DEF void stbtt_GetScaledFontVMetrics(const unsigned char *fontdata, int index, float size, float *ascent, float *descent, float *lineGap);
// Query the font vertical metrics without having to create a font first.


//////////////////////////////////////////////////////////////////////////////
//
// NEW TEXTURE BAKING API
//
// This provides options for packing multiple fonts into one atlas, not
// perfectly but better than nothing.

typedef struct
{
   unsigned short x0,y0,x1,y1; // coordinates of bbox in bitmap
   float xoff,yoff,xadvance;
   float xoff2,yoff2;
} stbtt_packedchar;

typedef struct stbtt_pack_context stbtt_pack_context;
typedef struct stbtt_fontinfo stbtt_fontinfo;
#ifndef STB_RECT_PACK_VERSION
typedef struct stbrp_rect stbrp_rect;
#endif

STBTT_DEF int  stbtt_PackBegin(stbtt_pack_context *spc, unsigned char *pixels, int width, int height, int stride_in_bytes, int padding, void *alloc_context);
// Initializes a packing context stored in the passed-in stbtt_pack_context.
// Future calls using this context will pack characters into the bitmap passed
// in here: a 1-channel bitmap that is width * height. stride_in_bytes is
// the distance from one row to the next (or 0 to mean they are packed tightly
// together). "padding" is the amount of padding to leave between each
// character (normally you want '1' for bitmaps you'll use as textures with
// bilinear filtering).
//
// Returns 0 on failure, 1 on success.

STBTT_DEF void stbtt_PackEnd  (stbtt_pack_context *spc);
// Cleans up the packing context and frees all memory.

#define STBTT_POINT_SIZE(x)   (-(x))

STBTT_DEF int  stbtt_PackFontRange(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, float font_size,
                                int first_unicode_char_in_range, int num_chars_in_range, stbtt_packedchar *chardata_for_range);
// Creates character bitmaps from the font_index'th font found in fontdata (use
// font_index=0 if you don't know what that is). It creates num_chars_in_range
// bitmaps for characters with unicode values starting at first_unicode_char_in_range
// and increasing. Data for how to render them is stored in chardata_for_range;
// pass these to stbtt_GetPackedQuad to get back renderable quads.
//
// font_size is the full height of the character from ascender to descender,
// as computed by stbtt_ScaleForPixelHeight. To use a point size as computed
// by stbtt_ScaleForMappingEmToPixels, wrap the point size in STBTT_POINT_SIZE()
// and pass that result as 'font_size':
//       ...,                  20 , ... // font max minus min y is 20 pixels tall
//       ..., STBTT_POINT_SIZE(20), ... // 'M' is 20 pixels tall

typedef struct
{
   float font_size;
   int first_unicode_codepoint_in_range;  // if non-zero, then the chars are continuous, and this is the first codepoint
   int *array_of_unicode_codepoints;       // if non-zero, then this is an array of unicode codepoints
   int num_chars;
   stbtt_packedchar *chardata_for_range; // output
   unsigned char h_oversample, v_oversample; // don't set these, they're used internally
} stbtt_pack_range;

STBTT_DEF int  stbtt_PackFontRanges(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, stbtt_pack_range *ranges, int num_ranges);
// Creates character bitmaps from multiple ranges of characters stored in
// ranges. This will usually create a better-packed bitmap than multiple
// calls to stbtt_PackFontRange. Note that you can call this multiple
// times within a single PackBegin/PackEnd.

STBTT_DEF void stbtt_PackSetOversampling(stbtt_pack_context *spc, unsigned int h_oversample, unsigned int v_oversample);
// Oversampling a font increases the quality by allowing higher-quality subpixel
// positioning, and is especially valuable at smaller text sizes.
//
// This function sets the amount of oversampling for all following calls to
// stbtt_PackFontRange(s) or stbtt_PackFontRangesGatherRects for a given
// pack context. The default (no oversampling) is achieved by h_oversample=1
// and v_oversample=1. The total number of pixels required is
// h_oversample*v_oversample larger than the default; for example, 2x2
// oversampling requires 4x the storage of 1x1. For best results, render
// oversampled textures with bilinear filtering. Look at the readme in
// stb/tests/oversample for information about oversampled fonts
//
// To use with PackFontRangesGather etc., you must set it before calls
// call to PackFontRangesGatherRects.

STBTT_DEF void stbtt_PackSetSkipMissingCodepoints(stbtt_pack_context *spc, int skip);
// If skip != 0, this tells stb_truetype to skip any codepoints for which
// there is no corresponding glyph. If skip=0, which is the default, then
// codepoints without a glyph recived the font's "missing character" glyph,
// typically an empty box by convention.

STBTT_DEF void stbtt_GetPackedQuad(const stbtt_packedchar *chardata, int pw, int ph,  // same data as above
                               int char_index,             // character to display
                               float *xpos, float *ypos,   // pointers to current position in screen pixel space
                               stbtt_aligned_quad *q,      // output: quad to draw
                               int align_to_integer);

STBTT_DEF int  stbtt_PackFontRangesGatherRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects);
STBTT_DEF void stbtt_PackFontRangesPackRects(stbtt_pack_context *spc, stbrp_rect *rects, int num_rects);
STBTT_DEF int  stbtt_PackFontRangesRenderIntoRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects);
// Calling these functions in sequence is roughly equivalent to calling
// stbtt_PackFontRanges(). If you more control over the packing of multiple
// fonts, or if you want to pack custom data into a font texture, take a look
// at the source to of stbtt_PackFontRanges() and create a custom version
// using these functions, e.g. call GatherRects multiple times,
// building up a single array of rects, then call PackRects once,
// then call RenderIntoRects repeatedly. This may result in a
// better packing than calling PackFontRanges multiple times
// (or it may not).

// this is an opaque structure that you shouldn't mess with which holds
// all the context needed from PackBegin to PackEnd.
struct stbtt_pack_context {
   void *user_allocator_context;
   void *pack_info;
   int   width;
   int   height;
   int   stride_in_bytes;
   int   padding;
   int   skip_missing;
   unsigned int   h_oversample, v_oversample;
   unsigned char *pixels;
   void  *nodes;
};

//////////////////////////////////////////////////////////////////////////////
//
// FONT LOADING
//
//

STBTT_DEF int stbtt_GetNumberOfFonts(const unsigned char *data);
// This function will determine the number of fonts in a font file.  TrueType
// collection (.ttc) files may contain multiple fonts, while TrueType font
// (.ttf) files only contain one font. The number of fonts can be used for
// indexing with the previous function where the index is between zero and one
// less than the total fonts. If an error occurs, -1 is returned.

STBTT_DEF int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index);
// Each .ttf/.ttc file may have more than one font. Each font has a sequential
// index number starting from 0. Call this function to get the font offset for
// a given index; it returns -1 if the index is out of range. A regular .ttf
// file will only define one font and it always be at offset 0, so it will
// return '0' for index 0, and -1 for all other indices.

// The following structure is defined publicly so you can declare one on
// the stack or as a global or etc, but you should treat it as opaque.
struct stbtt_fontinfo
{
   void           * userdata;
   unsigned char  * data;              // pointer to .ttf file
   int              fontstart;         // offset of start of font

   int numGlyphs;                     // number of glyphs, needed for range checking

   int loca,head,glyf,hhea,hmtx,kern,gpos,svg; // table locations as offset from start of .ttf
   int index_map;                     // a cmap mapping for our chosen character encoding
   int indexToLocFormat;              // format needed to map from glyph index to glyph

   stbtt__buf cff;                    // cff font data
   stbtt__buf charstrings;            // the charstring index
   stbtt__buf gsubrs;                 // global charstring subroutines index
   stbtt__buf subrs;                  // private charstring subroutines index
   stbtt__buf fontdicts;              // array of font dicts
   stbtt__buf fdselect;               // map from glyph to fontdict
};

STBTT_DEF int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset);
// Given an offset into the file that defines a font, this function builds
// the necessary cached info for the rest of the system. You must allocate
// the stbtt_fontinfo yourself, and stbtt_InitFont will fill it out. You don't
// need to do anything special to free it, because the contents are pure
// value data with no additional data structures. Returns 0 on failure.


//////////////////////////////////////////////////////////////////////////////
//
// CHARACTER TO GLYPH-INDEX CONVERSIOn

STBTT_DEF int stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint);
// If you're going to perform multiple operations on the same character
// and you want a speed-up, call this function with the character you're
// going to process, then use glyph-based functions instead of the
// codepoint-based functions.
// Returns 0 if the character codepoint is not defined in the font.


//////////////////////////////////////////////////////////////////////////////
//
// CHARACTER PROPERTIES
//

STBTT_DEF float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float pixels);
// computes a scale factor to produce a font whose "height" is 'pixels' tall.
// Height is measured as the distance from the highest ascender to the lowest
// descender; in other words, it's equivalent to calling stbtt_GetFontVMetrics
// and computing:
//       scale = pixels / (ascent - descent)
// so if you prefer to measure height by the ascent only, use a similar calculation.

STBTT_DEF float stbtt_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels);
// computes a scale factor to produce a font whose EM size is mapped to
// 'pixels' tall. This is probably what traditional APIs compute, but
// I'm not positive.

STBTT_DEF void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap);
// ascent is the coordinate above the baseline the font extends; descent
// is the coordinate below the baseline the font extends (i.e. it is typically negative)
// lineGap is the spacing between one row's descent and the next row's ascent...
// so you should advance the vertical position by "*ascent - *descent + *lineGap"
//   these are expressed in unscaled coordinates, so you must multiply by
//   the scale factor for a given size

STBTT_DEF int  stbtt_GetFontVMetricsOS2(const stbtt_fontinfo *info, int *typoAscent, int *typoDescent, int *typoLineGap);
// analogous to GetFontVMetrics, but returns the "typographic" values from the OS/2
// table (specific to MS/Windows TTF files).
//
// Returns 1 on success (table present), 0 on failure.

STBTT_DEF void stbtt_GetFontBoundingBox(const stbtt_fontinfo *info, int *x0, int *y0, int *x1, int *y1);
// the bounding box around all possible characters

STBTT_DEF void stbtt_GetCodepointHMetrics(const stbtt_fontinfo *info, int codepoint, int *advanceWidth, int *leftSideBearing);
// leftSideBearing is the offset from the current horizontal position to the left edge of the character
// advanceWidth is the offset from the current horizontal position to the next horizontal position
//   these are expressed in unscaled coordinates

STBTT_DEF int  stbtt_GetCodepointKernAdvance(const stbtt_fontinfo *info, int ch1, int ch2);
// an additional amount to add to the 'advance' value between ch1 and ch2

STBTT_DEF int stbtt_GetCodepointBox(const stbtt_fontinfo *info, int codepoint, int *x0, int *y0, int *x1, int *y1);
// Gets the bounding box of the visible part of the glyph, in unscaled coordinates

STBTT_DEF void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing);
STBTT_DEF int  stbtt_GetGlyphKernAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2);
STBTT_DEF int  stbtt_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);
// as above, but takes one or more glyph indices for greater efficiency

typedef struct stbtt_kerningentry
{
   int glyph1; // use stbtt_FindGlyphIndex
   int glyph2;
   int advance;
} stbtt_kerningentry;

STBTT_DEF int  stbtt_GetKerningTableLength(const stbtt_fontinfo *info);
STBTT_DEF int  stbtt_GetKerningTable(const stbtt_fontinfo *info, stbtt_kerningentry* table, int table_length);
// Retrieves a complete list of all of the kerning pairs provided by the font
// stbtt_GetKerningTable never writes more than table_length entries and returns how many entries it did write.
// The table will be sorted by (a.glyph1 == b.glyph1)?(a.glyph2 < b.glyph2):(a.glyph1 < b.glyph1)

//////////////////////////////////////////////////////////////////////////////
//
// GLYPH SHAPES (you probably don't need these, but they have to go before
// the bitmaps for C declaration-order reasons)
//

#ifndef STBTT_vmove // you can predefine these to use different values (but why?)
   enum {
      STBTT_vmove=1,
      STBTT_vline,
      STBTT_vcurve,
      STBTT_vcubic
   };
#endif

#ifndef stbtt_vertex // you can predefine this to use different values
                   // (we share this with other code at RAD)
   #define stbtt_vertex_type short // can't use stbtt_int16 because that's not visible in the header file
   typedef struct
   {
      stbtt_vertex_type x,y,cx,cy,cx1,cy1;
      unsigned char type,padding;
   } stbtt_vertex;
#endif

STBTT_DEF int stbtt_IsGlyphEmpty(const stbtt_fontinfo *info, int glyph_index);
// returns non-zero if nothing is drawn for this glyph

STBTT_DEF int stbtt_GetCodepointShape(const stbtt_fontinfo *info, int unicode_codepoint, stbtt_vertex **vertices);
STBTT_DEF int stbtt_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **vertices);
// returns # of vertices and fills *vertices with the pointer to them
//   these are expressed in "unscaled" coordinates
//
// The shape is a series of contours. Each one starts with
// a STBTT_moveto, then consists of a series of mixed
// STBTT_lineto and STBTT_curveto segments. A lineto
// draws a line from previous endpoint to its x,y; a curveto
// draws a quadratic bezier from previous endpoint to
// its x,y, using cx,cy as the bezier control point.

STBTT_DEF void stbtt_FreeShape(const stbtt_fontinfo *info, stbtt_vertex *vertices);
// frees the data allocated above

STBTT_DEF unsigned char *stbtt_FindSVGDoc(const stbtt_fontinfo *info, int gl);
STBTT_DEF int stbtt_GetCodepointSVG(const stbtt_fontinfo *info, int unicode_codepoint, const char **svg);
STBTT_DEF int stbtt_GetGlyphSVG(const stbtt_fontinfo *info, int gl, const char **svg);
// fills svg with the character's SVG data.
// returns data size or 0 if SVG not found.

//////////////////////////////////////////////////////////////////////////////
//
// BITMAP RENDERING
//

STBTT_DEF void stbtt_FreeBitmap(unsigned char *bitmap, void *userdata);
// frees the bitmap allocated below

STBTT_DEF unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff);
// allocates a large-enough single-channel 8bpp bitmap and renders the
// specified character/glyph at the specified scale into it, with
// antialiasing. 0 is no coverage (transparent), 255 is fully covered (opaque).
// *width & *height are filled out with the width & height of the bitmap,
// which is stored left-to-right, top-to-bottom.
//
// xoff/yoff are the offset it pixel space from the glyph origin to the top-left of the bitmap

STBTT_DEF unsigned char *stbtt_GetCodepointBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint, int *width, int *height, int *xoff, int *yoff);
// the same as stbtt_GetCodepoitnBitmap, but you can specify a subpixel
// shift for the character

STBTT_DEF void stbtt_MakeCodepointBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int codepoint);
// the same as stbtt_GetCodepointBitmap, but you pass in storage for the bitmap
// in the form of 'output', with row spacing of 'out_stride' bytes. the bitmap
// is clipped to out_w/out_h bytes. Call stbtt_GetCodepointBitmapBox to get the
// width and height and positioning info for it first.

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint);
// same as stbtt_MakeCodepointBitmap, but you can specify a subpixel
// shift for the character

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int codepoint);
// same as stbtt_MakeCodepointBitmapSubpixel, but prefiltering
// is performed (see stbtt_PackSetOversampling)

STBTT_DEF void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);
// get the bbox of the bitmap centered around the glyph origin; so the
// bitmap width is ix1-ix0, height is iy1-iy0, and location to place
// the bitmap top left is (leftSideBearing*scale,iy0).
// (Note that the bitmap uses y-increases-down, but the shape uses
// y-increases-up, so CodepointBitmapBox and CodepointBox are inverted.)

STBTT_DEF void stbtt_GetCodepointBitmapBoxSubpixel(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1);
// same as stbtt_GetCodepointBitmapBox, but you can specify a subpixel
// shift for the character

// the following functions are equivalent to the above functions, but operate
// on glyph indices instead of Unicode codepoints (for efficiency)
STBTT_DEF unsigned char *stbtt_GetGlyphBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int glyph, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF unsigned char *stbtt_GetGlyphBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int glyph, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph);
STBTT_DEF void stbtt_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph);
STBTT_DEF void stbtt_MakeGlyphBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int glyph);
STBTT_DEF void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);
STBTT_DEF void stbtt_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y,float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1);


// @TODO: don't expose this structure
typedef struct
{
   int w,h,stride;
   unsigned char *pixels;
} stbtt__bitmap;

// rasterize a shape with quadratic beziers into a bitmap
STBTT_DEF void stbtt_Rasterize(stbtt__bitmap *result,        // 1-channel bitmap to draw into
                               float flatness_in_pixels,     // allowable error of curve in pixels
                               stbtt_vertex *vertices,       // array of vertices defining shape
                               int num_verts,                // number of vertices in above array
                               float scale_x, float scale_y, // scale applied to input vertices
                               float shift_x, float shift_y, // translation applied to input vertices
                               int x_off, int y_off,         // another translation applied to input
                               int invert,                   // if non-zero, vertically flip shape
                               void *userdata);              // context for to STBTT_MALLOC

//////////////////////////////////////////////////////////////////////////////
//
// Signed Distance Function (or Field) rendering

STBTT_DEF void stbtt_FreeSDF(unsigned char *bitmap, void *userdata);
// frees the SDF bitmap allocated below

STBTT_DEF unsigned char * stbtt_GetGlyphSDF(const stbtt_fontinfo *info, float scale, int glyph, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff);
STBTT_DEF unsigned char * stbtt_GetCodepointSDF(const stbtt_fontinfo *info, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff);
// These functions compute a discretized SDF field for a single character, suitable for storing
// in a single-channel texture, sampling with bilinear filtering, and testing against
// larger than some threshold to produce scalable fonts.
//        info              --  the font
//        scale             --  controls the size of the resulting SDF bitmap, same as it would be creating a regular bitmap
//        glyph/codepoint   --  the character to generate the SDF for
//        padding           --  extra "pixels" around the character which are filled with the distance to the character (not 0),
//                                 which allows effects like bit outlines
//        onedge_value      --  value 0-255 to test the SDF against to reconstruct the character (i.e. the isocontour of the character)
//        pixel_dist_scale  --  what value the SDF should increase by when moving one SDF "pixel" away from the edge (on the 0..255 scale)
//                                 if positive, > onedge_value is inside; if negative, < onedge_value is inside
//        width,height      --  output height & width of the SDF bitmap (including padding)
//        xoff,yoff         --  output origin of the character
//        return value      --  a 2D array of bytes 0..255, width*height in size
//
// pixel_dist_scale & onedge_value are a scale & bias that allows you to make
// optimal use of the limited 0..255 for your application, trading off precision
// and special effects. SDF values outside the range 0..255 are clamped to 0..255.
//
// Example:
//      scale = stbtt_ScaleForPixelHeight(22)
//      padding = 5
//      onedge_value = 180
//      pixel_dist_scale = 180/5.0 = 36.0
//
//      This will create an SDF bitmap in which the character is about 22 pixels
//      high but the whole bitmap is about 22+5+5=32 pixels high. To produce a filled
//      shape, sample the SDF at each pixel and fill the pixel if the SDF value
//      is greater than or equal to 180/255. (You'll actually want to antialias,
//      which is beyond the scope of this example.) Additionally, you can compute
//      offset outlines (e.g. to stroke the character border inside & outside,
//      or only outside). For example, to fill outside the character up to 3 SDF
//      pixels, you would compare against (180-36.0*3)/255 = 72/255. The above
//      choice of variables maps a range from 5 pixels outside the shape to
//      2 pixels inside the shape to 0..255; this is intended primarily for apply
//      outside effects only (the interior range is needed to allow proper
//      antialiasing of the font at *smaller* sizes)
//
// The function computes the SDF analytically at each SDF pixel, not by e.g.
// building a higher-res bitmap and approximating it. In theory the quality
// should be as high as possible for an SDF of this size & representation, but
// unclear if this is true in practice (perhaps building a higher-res bitmap
// and computing from that can allow drop-out prevention).
//
// The algorithm has not been optimized at all, so expect it to be slow
// if computing lots of characters or very large sizes.



//////////////////////////////////////////////////////////////////////////////
//
// Finding the right font...
//
// You should really just solve this offline, keep your own tables
// of what font is what, and don't try to get it out of the .ttf file.
// That's because getting it out of the .ttf file is really hard, because
// the names in the file can appear in many possible encodings, in many
// possible languages, and e.g. if you need a case-insensitive comparison,
// the details of that depend on the encoding & language in a complex way
// (actually underspecified in truetype, but also gigantic).
//
// But you can use the provided functions in two possible ways:
//     stbtt_FindMatchingFont() will use *case-sensitive* comparisons on
//             unicode-encoded names to try to find the font you want;
//             you can run this before calling stbtt_InitFont()
//
//     stbtt_GetFontNameString() lets you get any of the various strings
//             from the file yourself and do your own comparisons on them.
//             You have to have called stbtt_InitFont() first.


STBTT_DEF int stbtt_FindMatchingFont(const unsigned char *fontdata, const char *name, int flags);
// returns the offset (not index) of the font that matches, or -1 if none
//   if you use STBTT_MACSTYLE_DONTCARE, use a font name like "Arial Bold".
//   if you use any other flag, use a font name like "Arial"; this checks
//     the 'macStyle' header field; i don't know if fonts set this consistently
#define STBTT_MACSTYLE_DONTCARE     0
#define STBTT_MACSTYLE_BOLD         1
#define STBTT_MACSTYLE_ITALIC       2
#define STBTT_MACSTYLE_UNDERSCORE   4
#define STBTT_MACSTYLE_NONE         8   // <= not same as 0, this makes us check the bitfield is 0

STBTT_DEF int stbtt_CompareUTF8toUTF16_bigendian(const char *s1, int len1, const char *s2, int len2);
// returns 1/0 whether the first string interpreted as utf8 is identical to
// the second string interpreted as big-endian utf16... useful for strings from next func

STBTT_DEF const char *stbtt_GetFontNameString(const stbtt_fontinfo *font, int *length, int platformID, int encodingID, int languageID, int nameID);
// returns the string (which may be big-endian double byte, e.g. for unicode)
// and puts the length in bytes in *length.
//
// some of the values for the IDs are below; for more see the truetype spec:
//     http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
//     http://www.microsoft.com/typography/otspec/name.htm

enum { // platformID
   STBTT_PLATFORM_ID_UNICODE   =0,
   STBTT_PLATFORM_ID_MAC       =1,
   STBTT_PLATFORM_ID_ISO       =2,
   STBTT_PLATFORM_ID_MICROSOFT =3
};

enum { // encodingID for STBTT_PLATFORM_ID_UNICODE
   STBTT_UNICODE_EID_UNICODE_1_0    =0,
   STBTT_UNICODE_EID_UNICODE_1_1    =1,
   STBTT_UNICODE_EID_ISO_10646      =2,
   STBTT_UNICODE_EID_UNICODE_2_0_BMP=3,
   STBTT_UNICODE_EID_UNICODE_2_0_FULL=4
};

enum { // encodingID for STBTT_PLATFORM_ID_MICROSOFT
   STBTT_MS_EID_SYMBOL        =0,
   STBTT_MS_EID_UNICODE_BMP   =1,
   STBTT_MS_EID_SHIFTJIS      =2,
   STBTT_MS_EID_UNICODE_FULL  =10
};

enum { // encodingID for STBTT_PLATFORM_ID_MAC; same as Script Manager codes
   STBTT_MAC_EID_ROMAN        =0,   STBTT_MAC_EID_ARABIC       =4,
   STBTT_MAC_EID_JAPANESE     =1,   STBTT_MAC_EID_HEBREW       =5,
   STBTT_MAC_EID_CHINESE_TRAD =2,   STBTT_MAC_EID_GREEK        =6,
   STBTT_MAC_EID_KOREAN       =3,   STBTT_MAC_EID_RUSSIAN      =7
};

enum { // languageID for STBTT_PLATFORM_ID_MICROSOFT; same as LCID...
       // problematic because there are e.g. 16 english LCIDs and 16 arabic LCIDs
   STBTT_MS_LANG_ENGLISH     =0x0409,   STBTT_MS_LANG_ITALIAN     =0x0410,
   STBTT_MS_LANG_CHINESE     =0x0804,   STBTT_MS_LANG_JAPANESE    =0x0411,
   STBTT_MS_LANG_DUTCH       =0x0413,   STBTT_MS_LANG_KOREAN      =0x0412,
   STBTT_MS_LANG_FRENCH      =0x040c,   STBTT_MS_LANG_RUSSIAN     =0x0419,
   STBTT_MS_LANG_GERMAN      =0x0407,   STBTT_MS_LANG_SPANISH     =0x0409,
   STBTT_MS_LANG_HEBREW      =0x040d,   STBTT_MS_LANG_SWEDISH     =0x041D
};

enum { // languageID for STBTT_PLATFORM_ID_MAC
   STBTT_MAC_LANG_ENGLISH      =0 ,   STBTT_MAC_LANG_JAPANESE     =11,
   STBTT_MAC_LANG_ARABIC       =12,   STBTT_MAC_LANG_KOREAN       =23,
   STBTT_MAC_LANG_DUTCH        =4 ,   STBTT_MAC_LANG_RUSSIAN      =32,
   STBTT_MAC_LANG_FRENCH       =1 ,   STBTT_MAC_LANG_SPANISH      =6 ,
   STBTT_MAC_LANG_GERMAN       =2 ,   STBTT_MAC_LANG_SWEDISH      =5 ,
   STBTT_MAC_LANG_HEBREW       =10,   STBTT_MAC_LANG_CHINESE_SIMPLIFIED =33,
   STBTT_MAC_LANG_ITALIAN      =3 ,   STBTT_MAC_LANG_CHINESE_TRAD =19
};

#ifdef __cplusplus
}
#endif

#endif // __STB_INCLUDE_STB_TRUETYPE_H__

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////
////   IMPLEMENTATION
////
////

#ifdef STB_TRUETYPE_IMPLEMENTATION

#ifndef STBTT_MAX_OVERSAMPLE
#define STBTT_MAX_OVERSAMPLE   8
#endif

#if STBTT_MAX_OVERSAMPLE > 255
#error "STBTT_MAX_OVERSAMPLE cannot be > 255"
#endif

typedef int stbtt__test_oversample_pow2[(STBTT_MAX_OVERSAMPLE & (STBTT_MAX_OVERSAMPLE-1)) == 0 ? 1 : -1];

#ifndef STBTT_RASTERIZER_VERSION
#define STBTT_RASTERIZER_VERSION 2
#endif

#ifdef _MSC_VER
#define STBTT__NOTUSED(v)  (void)(v)
#else
#define STBTT__NOTUSED(v)  (void)sizeof(v)
#endif

//////////////////////////////////////////////////////////////////////////
//
// stbtt__buf helpers to parse data from file
//

static stbtt_uint8 stbtt__buf_get8(stbtt__buf *b)
{
   if (b->cursor >= b->size)
      return 0;
   return b->data[b->cursor++];
}

static stbtt_uint8 stbtt__buf_peek8(stbtt__buf *b)
{
   if (b->cursor >= b->size)
      return 0;
   return b->data[b->cursor];
}

static void stbtt__buf_seek(stbtt__buf *b, int o)
{
   STBTT_assert(!(o > b->size || o < 0));
   b->cursor = (o > b->size || o < 0) ? b->size : o;
}

static void stbtt__buf_skip(stbtt__buf *b, int o)
{
   stbtt__buf_seek(b, b->cursor + o);
}

static stbtt_uint32 stbtt__buf_get(stbtt__buf *b, int n)
{
   stbtt_uint32 v = 0;
   int i;
   STBTT_assert(n >= 1 && n <= 4);
   for (i = 0; i < n; i++)
      v = (v << 8) | stbtt__buf_get8(b);
   return v;
}

static stbtt__buf stbtt__new_buf(const void *p, size_t size)
{
   stbtt__buf r;
   STBTT_assert(size < 0x40000000);
   r.data = (stbtt_uint8*) p;
   r.size = (int) size;
   r.cursor = 0;
   return r;
}

#define stbtt__buf_get16(b)  stbtt__buf_get((b), 2)
#define stbtt__buf_get32(b)  stbtt__buf_get((b), 4)

static stbtt__buf stbtt__buf_range(const stbtt__buf *b, int o, int s)
{
   stbtt__buf r = stbtt__new_buf(NULL, 0);
   if (o < 0 || s < 0 || o > b->size || s > b->size - o) return r;
   r.data = b->data + o;
   r.size = s;
   return r;
}

static stbtt__buf stbtt__cff_get_index(stbtt__buf *b)
{
   int count, start, offsize;
   start = b->cursor;
   count = stbtt__buf_get16(b);
   if (count) {
      offsize = stbtt__buf_get8(b);
      STBTT_assert(offsize >= 1 && offsize <= 4);
      stbtt__buf_skip(b, offsize * count);
      stbtt__buf_skip(b, stbtt__buf_get(b, offsize) - 1);
   }
   return stbtt__buf_range(b, start, b->cursor - start);
}

static stbtt_uint32 stbtt__cff_int(stbtt__buf *b)
{
   int b0 = stbtt__buf_get8(b);
   if (b0 >= 32 && b0 <= 246)       return b0 - 139;
   else if (b0 >= 247 && b0 <= 250) return (b0 - 247)*256 + stbtt__buf_get8(b) + 108;
   else if (b0 >= 251 && b0 <= 254) return -(b0 - 251)*256 - stbtt__buf_get8(b) - 108;
   else if (b0 == 28)               return stbtt__buf_get16(b);
   else if (b0 == 29)               return stbtt__buf_get32(b);
   STBTT_assert(0);
   return 0;
}

static void stbtt__cff_skip_operand(stbtt__buf *b) {
   int v, b0 = stbtt__buf_peek8(b);
   STBTT_assert(b0 >= 28);
   if (b0 == 30) {
      stbtt__buf_skip(b, 1);
      while (b->cursor < b->size) {
         v = stbtt__buf_get8(b);
         if ((v & 0xF) == 0xF || (v >> 4) == 0xF)
            break;
      }
   } else {
      stbtt__cff_int(b);
   }
}

static stbtt__buf stbtt__dict_get(stbtt__buf *b, int key)
{
   stbtt__buf_seek(b, 0);
   while (b->cursor < b->size) {
      int start = b->cursor, end, op;
      while (stbtt__buf_peek8(b) >= 28)
         stbtt__cff_skip_operand(b);
      end = b->cursor;
      op = stbtt__buf_get8(b);
      if (op == 12)  op = stbtt__buf_get8(b) | 0x100;
      if (op == key) return stbtt__buf_range(b, start, end-start);
   }
   return stbtt__buf_range(b, 0, 0);
}

static void stbtt__dict_get_ints(stbtt__buf *b, int key, int outcount, stbtt_uint32 *out)
{
   int i;
   stbtt__buf operands = stbtt__dict_get(b, key);
   for (i = 0; i < outcount && operands.cursor < operands.size; i++)
      out[i] = stbtt__cff_int(&operands);
}

static int stbtt__cff_index_count(stbtt__buf *b)
{
   stbtt__buf_seek(b, 0);
   return stbtt__buf_get16(b);
}

static stbtt__buf stbtt__cff_index_get(stbtt__buf b, int i)
{
   int count, offsize, start, end;
   stbtt__buf_seek(&b, 0);
   count = stbtt__buf_get16(&b);
   offsize = stbtt__buf_get8(&b);
   STBTT_assert(i >= 0 && i < count);
   STBTT_assert(offsize >= 1 && offsize <= 4);
   stbtt__buf_skip(&b, i*offsize);
   start = stbtt__buf_get(&b, offsize);
   end = stbtt__buf_get(&b, offsize);
   return stbtt__buf_range(&b, 2+(count+1)*offsize+start, end - start);
}

//////////////////////////////////////////////////////////////////////////
//
// accessors to parse data from file
//

// on platforms that don't allow misaligned reads, if we want to allow
// truetype fonts that aren't padded to alignment, define ALLOW_UNALIGNED_TRUETYPE

#define ttBYTE(p)     (* (stbtt_uint8 *) (p))
#define ttCHAR(p)     (* (stbtt_int8 *) (p))
#define ttFixed(p)    ttLONG(p)

static stbtt_uint16 ttUSHORT(stbtt_uint8 *p) { return p[0]*256 + p[1]; }
static stbtt_int16 ttSHORT(stbtt_uint8 *p)   { return p[0]*256 + p[1]; }
static stbtt_uint32 ttULONG(stbtt_uint8 *p)  { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
static stbtt_int32 ttLONG(stbtt_uint8 *p)    { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }

#define stbtt_tag4(p,c0,c1,c2,c3) ((p)[0] == (c0) && (p)[1] == (c1) && (p)[2] == (c2) && (p)[3] == (c3))
#define stbtt_tag(p,str)           stbtt_tag4(p,str[0],str[1],str[2],str[3])

static int stbtt__isfont(stbtt_uint8 *font)
{
   // check the version number
   if (stbtt_tag4(font, '1',0,0,0))  return 1; // TrueType 1
   if (stbtt_tag(font, "typ1"))   return 1; // TrueType with type 1 font -- we don't support this!
   if (stbtt_tag(font, "OTTO"))   return 1; // OpenType with CFF
   if (stbtt_tag4(font, 0,1,0,0)) return 1; // OpenType 1.0
   if (stbtt_tag(font, "true"))   return 1; // Apple specification for TrueType fonts
   return 0;
}

// @OPTIMIZE: binary search
static stbtt_uint32 stbtt__find_table(stbtt_uint8 *data, stbtt_uint32 fontstart, const char *tag)
{
   stbtt_int32 num_tables = ttUSHORT(data+fontstart+4);
   stbtt_uint32 tabledir = fontstart + 12;
   stbtt_int32 i;
   for (i=0; i < num_tables; ++i) {
      stbtt_uint32 loc = tabledir + 16*i;
      if (stbtt_tag(data+loc+0, tag))
         return ttULONG(data+loc+8);
   }
   return 0;
}

static int stbtt_GetFontOffsetForIndex_internal(unsigned char *font_collection, int index)
{
   // if it's just a font, there's only one valid index
   if (stbtt__isfont(font_collection))
      return index == 0 ? 0 : -1;

   // check if it's a TTC
   if (stbtt_tag(font_collection, "ttcf")) {
      // version 1?
      if (ttULONG(font_collection+4) == 0x00010000 || ttULONG(font_collection+4) == 0x00020000) {
         stbtt_int32 n = ttLONG(font_collection+8);
         if (index >= n)
            return -1;
         return ttULONG(font_collection+12+index*4);
      }
   }
   return -1;
}

static int stbtt_GetNumberOfFonts_internal(unsigned char *font_collection)
{
   // if it's just a font, there's only one valid font
   if (stbtt__isfont(font_collection))
      return 1;

   // check if it's a TTC
   if (stbtt_tag(font_collection, "ttcf")) {
      // version 1?
      if (ttULONG(font_collection+4) == 0x00010000 || ttULONG(font_collection+4) == 0x00020000) {
         return ttLONG(font_collection+8);
      }
   }
   return 0;
}

static stbtt__buf stbtt__get_subrs(stbtt__buf cff, stbtt__buf fontdict)
{
   stbtt_uint32 subrsoff = 0, private_loc[2] = { 0, 0 };
   stbtt__buf pdict;
   stbtt__dict_get_ints(&fontdict, 18, 2, private_loc);
   if (!private_loc[1] || !private_loc[0]) return stbtt__new_buf(NULL, 0);
   pdict = stbtt__buf_range(&cff, private_loc[1], private_loc[0]);
   stbtt__dict_get_ints(&pdict, 19, 1, &subrsoff);
   if (!subrsoff) return stbtt__new_buf(NULL, 0);
   stbtt__buf_seek(&cff, private_loc[1]+subrsoff);
   return stbtt__cff_get_index(&cff);
}

// since most people won't use this, find this table the first time it's needed
static int stbtt__get_svg(stbtt_fontinfo *info)
{
   stbtt_uint32 t;
   if (info->svg < 0) {
      t = stbtt__find_table(info->data, info->fontstart, "SVG ");
      if (t) {
         stbtt_uint32 offset = ttULONG(info->data + t + 2);
         info->svg = t + offset;
      } else {
         info->svg = 0;
      }
   }
   return info->svg;
}

static int stbtt_InitFont_internal(stbtt_fontinfo *info, unsigned char *data, int fontstart)
{
   stbtt_uint32 cmap, t;
   stbtt_int32 i,numTables;

   info->data = data;
   info->fontstart = fontstart;
   info->cff = stbtt__new_buf(NULL, 0);

   cmap = stbtt__find_table(data, fontstart, "cmap");       // required
   info->loca = stbtt__find_table(data, fontstart, "loca"); // required
   info->head = stbtt__find_table(data, fontstart, "head"); // required
   info->glyf = stbtt__find_table(data, fontstart, "glyf"); // required
   info->hhea = stbtt__find_table(data, fontstart, "hhea"); // required
   info->hmtx = stbtt__find_table(data, fontstart, "hmtx"); // required
   info->kern = stbtt__find_table(data, fontstart, "kern"); // not required
   info->gpos = stbtt__find_table(data, fontstart, "GPOS"); // not required

   if (!cmap || !info->head || !info->hhea || !info->hmtx)
      return 0;
   if (info->glyf) {
      // required for truetype
      if (!info->loca) return 0;
   } else {
      // initialization for CFF / Type2 fonts (OTF)
      stbtt__buf b, topdict, topdictidx;
      stbtt_uint32 cstype = 2, charstrings = 0, fdarrayoff = 0, fdselectoff = 0;
      stbtt_uint32 cff;

      cff = stbtt__find_table(data, fontstart, "CFF ");
      if (!cff) return 0;

      info->fontdicts = stbtt__new_buf(NULL, 0);
      info->fdselect = stbtt__new_buf(NULL, 0);

      // @TODO this should use size from table (not 512MB)
      info->cff = stbtt__new_buf(data+cff, 512*1024*1024);
      b = info->cff;

      // read the header
      stbtt__buf_skip(&b, 2);
      stbtt__buf_seek(&b, stbtt__buf_get8(&b)); // hdrsize

      // @TODO the name INDEX could list multiple fonts,
      // but we just use the first one.
      stbtt__cff_get_index(&b);  // name INDEX
      topdictidx = stbtt__cff_get_index(&b);
      topdict = stbtt__cff_index_get(topdictidx, 0);
      stbtt__cff_get_index(&b);  // string INDEX
      info->gsubrs = stbtt__cff_get_index(&b);

      stbtt__dict_get_ints(&topdict, 17, 1, &charstrings);
      stbtt__dict_get_ints(&topdict, 0x100 | 6, 1, &cstype);
      stbtt__dict_get_ints(&topdict, 0x100 | 36, 1, &fdarrayoff);
      stbtt__dict_get_ints(&topdict, 0x100 | 37, 1, &fdselectoff);
      info->subrs = stbtt__get_subrs(b, topdict);

      // we only support Type 2 charstrings
      if (cstype != 2) return 0;
      if (charstrings == 0) return 0;

      if (fdarrayoff) {
         // looks like a CID font
         if (!fdselectoff) return 0;
         stbtt__buf_seek(&b, fdarrayoff);
         info->fontdicts = stbtt__cff_get_index(&b);
         info->fdselect = stbtt__buf_range(&b, fdselectoff, b.size-fdselectoff);
      }

      stbtt__buf_seek(&b, charstrings);
      info->charstrings = stbtt__cff_get_index(&b);
   }

   t = stbtt__find_table(data, fontstart, "maxp");
   if (t)
      info->numGlyphs = ttUSHORT(data+t+4);
   else
      info->numGlyphs = 0xffff;

   info->svg = -1;

   // find a cmap encoding table we understand *now* to avoid searching
   // later. (todo: could make this installable)
   // the same regardless of glyph.
   numTables = ttUSHORT(data + cmap + 2);
   info->index_map = 0;
   for (i=0; i < numTables; ++i) {
      stbtt_uint32 encoding_record = cmap + 4 + 8 * i;
      // find an encoding we understand:
      switch(ttUSHORT(data+encoding_record)) {
         case STBTT_PLATFORM_ID_MICROSOFT:
            switch (ttUSHORT(data+encoding_record+2)) {
               case STBTT_MS_EID_UNICODE_BMP:
               case STBTT_MS_EID_UNICODE_FULL:
                  // MS/Unicode
                  info->index_map = cmap + ttULONG(data+encoding_record+4);
                  break;
            }
            break;
        case STBTT_PLATFORM_ID_UNICODE:
            // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            info->index_map = cmap + ttULONG(data+encoding_record+4);
            break;
      }
   }
   if (info->index_map == 0)
      return 0;

   info->indexToLocFormat = ttUSHORT(data+info->head + 50);
   return 1;
}

STBTT_DEF int stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint)
{
   stbtt_uint8 *data = info->data;
   stbtt_uint32 index_map = info->index_map;

   stbtt_uint16 format = ttUSHORT(data + index_map + 0);
   if (format == 0) { // apple byte encoding
      stbtt_int32 bytes = ttUSHORT(data + index_map + 2);
      if (unicode_codepoint < bytes-6)
         return ttBYTE(data + index_map + 6 + unicode_codepoint);
      return 0;
   } else if (format == 6) {
      stbtt_uint32 first = ttUSHORT(data + index_map + 6);
      stbtt_uint32 count = ttUSHORT(data + index_map + 8);
      if ((stbtt_uint32) unicode_codepoint >= first && (stbtt_uint32) unicode_codepoint < first+count)
         return ttUSHORT(data + index_map + 10 + (unicode_codepoint - first)*2);
      return 0;
   } else if (format == 2) {
      STBTT_assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
      return 0;
   } else if (format == 4) { // standard mapping for windows fonts: binary search collection of ranges
      stbtt_uint16 segcount = ttUSHORT(data+index_map+6) >> 1;
      stbtt_uint16 searchRange = ttUSHORT(data+index_map+8) >> 1;
      stbtt_uint16 entrySelector = ttUSHORT(data+index_map+10);
      stbtt_uint16 rangeShift = ttUSHORT(data+index_map+12) >> 1;

      // do a binary search of the segments
      stbtt_uint32 endCount = index_map + 14;
      stbtt_uint32 search = endCount;

      if (unicode_codepoint > 0xffff)
         return 0;

      // they lie from endCount .. endCount + segCount
      // but searchRange is the nearest power of two, so...
      if (unicode_codepoint >= ttUSHORT(data + search + rangeShift*2))
         search += rangeShift*2;

      // now decrement to bias correctly to find smallest
      search -= 2;
      while (entrySelector) {
         stbtt_uint16 end;
         searchRange >>= 1;
         end = ttUSHORT(data + search + searchRange*2);
         if (unicode_codepoint > end)
            search += searchRange*2;
         --entrySelector;
      }
      search += 2;

      {
         stbtt_uint16 offset, start, last;
         stbtt_uint16 item = (stbtt_uint16) ((search - endCount) >> 1);

         start = ttUSHORT(data + index_map + 14 + segcount*2 + 2 + 2*item);
         last = ttUSHORT(data + endCount + 2*item);
         if (unicode_codepoint < start || unicode_codepoint > last)
            return 0;

         offset = ttUSHORT(data + index_map + 14 + segcount*6 + 2 + 2*item);
         if (offset == 0)
            return (stbtt_uint16) (unicode_codepoint + ttSHORT(data + index_map + 14 + segcount*4 + 2 + 2*item));

         return ttUSHORT(data + offset + (unicode_codepoint-start)*2 + index_map + 14 + segcount*6 + 2 + 2*item);
      }
   } else if (format == 12 || format == 13) {
      stbtt_uint32 ngroups = ttULONG(data+index_map+12);
      stbtt_int32 low,high;
      low = 0; high = (stbtt_int32)ngroups;
      // Binary search the right group.
      while (low < high) {
         stbtt_int32 mid = low + ((high-low) >> 1); // rounds down, so low <= mid < high
         stbtt_uint32 start_char = ttULONG(data+index_map+16+mid*12);
         stbtt_uint32 end_char = ttULONG(data+index_map+16+mid*12+4);
         if ((stbtt_uint32) unicode_codepoint < start_char)
            high = mid;
         else if ((stbtt_uint32) unicode_codepoint > end_char)
            low = mid+1;
         else {
            stbtt_uint32 start_glyph = ttULONG(data+index_map+16+mid*12+8);
            if (format == 12)
               return start_glyph + unicode_codepoint-start_char;
            else // format == 13
               return start_glyph;
         }
      }
      return 0; // not found
   }
   // @TODO
   STBTT_assert(0);
   return 0;
}

STBTT_DEF int stbtt_GetCodepointShape(const stbtt_fontinfo *info, int unicode_codepoint, stbtt_vertex **vertices)
{
   return stbtt_GetGlyphShape(info, stbtt_FindGlyphIndex(info, unicode_codepoint), vertices);
}

static void stbtt_setvertex(stbtt_vertex *v, stbtt_uint8 type, stbtt_int32 x, stbtt_int32 y, stbtt_int32 cx, stbtt_int32 cy)
{
   v->type = type;
   v->x = (stbtt_int16) x;
   v->y = (stbtt_int16) y;
   v->cx = (stbtt_int16) cx;
   v->cy = (stbtt_int16) cy;
}

static int stbtt__GetGlyfOffset(const stbtt_fontinfo *info, int glyph_index)
{
   int g1,g2;

   STBTT_assert(!info->cff.size);

   if (glyph_index >= info->numGlyphs) return -1; // glyph index out of range
   if (info->indexToLocFormat >= 2)    return -1; // unknown index->glyph map format

   if (info->indexToLocFormat == 0) {
      g1 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2) * 2;
      g2 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2 + 2) * 2;
   } else {
      g1 = info->glyf + ttULONG (info->data + info->loca + glyph_index * 4);
      g2 = info->glyf + ttULONG (info->data + info->loca + glyph_index * 4 + 4);
   }

   return g1==g2 ? -1 : g1; // if length is 0, return -1
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);

STBTT_DEF int stbtt_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
   if (info->cff.size) {
      stbtt__GetGlyphInfoT2(info, glyph_index, x0, y0, x1, y1);
   } else {
      int g = stbtt__GetGlyfOffset(info, glyph_index);
      if (g < 0) return 0;

      if (x0) *x0 = ttSHORT(info->data + g + 2);
      if (y0) *y0 = ttSHORT(info->data + g + 4);
      if (x1) *x1 = ttSHORT(info->data + g + 6);
      if (y1) *y1 = ttSHORT(info->data + g + 8);
   }
   return 1;
}

STBTT_DEF int stbtt_GetCodepointBox(const stbtt_fontinfo *info, int codepoint, int *x0, int *y0, int *x1, int *y1)
{
   return stbtt_GetGlyphBox(info, stbtt_FindGlyphIndex(info,codepoint), x0,y0,x1,y1);
}

STBTT_DEF int stbtt_IsGlyphEmpty(const stbtt_fontinfo *info, int glyph_index)
{
   stbtt_int16 numberOfContours;
   int g;
   if (info->cff.size)
      return stbtt__GetGlyphInfoT2(info, glyph_index, NULL, NULL, NULL, NULL) == 0;
   g = stbtt__GetGlyfOffset(info, glyph_index);
   if (g < 0) return 1;
   numberOfContours = ttSHORT(info->data + g);
   return numberOfContours == 0;
}

static int stbtt__close_shape(stbtt_vertex *vertices, int num_vertices, int was_off, int start_off,
    stbtt_int32 sx, stbtt_int32 sy, stbtt_int32 scx, stbtt_int32 scy, stbtt_int32 cx, stbtt_int32 cy)
{
   if (start_off) {
      if (was_off)
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx+scx)>>1, (cy+scy)>>1, cx,cy);
      stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, sx,sy,scx,scy);
   } else {
      if (was_off)
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve,sx,sy,cx,cy);
      else
         stbtt_setvertex(&vertices[num_vertices++], STBTT_vline,sx,sy,0,0);
   }
   return num_vertices;
}

static int stbtt__GetGlyphShapeTT(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   stbtt_int16 numberOfContours;
   stbtt_uint8 *endPtsOfContours;
   stbtt_uint8 *data = info->data;
   stbtt_vertex *vertices=0;
   int num_vertices=0;
   int g = stbtt__GetGlyfOffset(info, glyph_index);

   *pvertices = NULL;

   if (g < 0) return 0;

   numberOfContours = ttSHORT(data + g);

   if (numberOfContours > 0) {
      stbtt_uint8 flags=0,flagcount;
      stbtt_int32 ins, i,j=0,m,n, next_move, was_off=0, off, start_off=0;
      stbtt_int32 x,y,cx,cy,sx,sy, scx,scy;
      stbtt_uint8 *points;
      endPtsOfContours = (data + g + 10);
      ins = ttUSHORT(data + g + 10 + numberOfContours * 2);
      points = data + g + 10 + numberOfContours * 2 + 2 + ins;

      n = 1+ttUSHORT(endPtsOfContours + numberOfContours*2-2);

      m = n + 2*numberOfContours;  // a loose bound on how many vertices we might need
      vertices = (stbtt_vertex *) STBTT_malloc(m * sizeof(vertices[0]), info->userdata);
      if (vertices == 0)
         return 0;

      next_move = 0;
      flagcount=0;

      // in first pass, we load uninterpreted data into the allocated array
      // above, shifted to the end of the array so we won't overwrite it when
      // we create our final data starting from the front

      off = m - n; // starting offset for uninterpreted data, regardless of how m ends up being calculated

      // first load flags

      for (i=0; i < n; ++i) {
         if (flagcount == 0) {
            flags = *points++;
            if (flags & 8)
               flagcount = *points++;
         } else
            --flagcount;
         vertices[off+i].type = flags;
      }

      // now load x coordinates
      x=0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         if (flags & 2) {
            stbtt_int16 dx = *points++;
            x += (flags & 16) ? dx : -dx; // ???
         } else {
            if (!(flags & 16)) {
               x = x + (stbtt_int16) (points[0]*256 + points[1]);
               points += 2;
            }
         }
         vertices[off+i].x = (stbtt_int16) x;
      }

      // now load y coordinates
      y=0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         if (flags & 4) {
            stbtt_int16 dy = *points++;
            y += (flags & 32) ? dy : -dy; // ???
         } else {
            if (!(flags & 32)) {
               y = y + (stbtt_int16) (points[0]*256 + points[1]);
               points += 2;
            }
         }
         vertices[off+i].y = (stbtt_int16) y;
      }

      // now convert them to our format
      num_vertices=0;
      sx = sy = cx = cy = scx = scy = 0;
      for (i=0; i < n; ++i) {
         flags = vertices[off+i].type;
         x     = (stbtt_int16) vertices[off+i].x;
         y     = (stbtt_int16) vertices[off+i].y;

         if (next_move == i) {
            if (i != 0)
               num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx,sy,scx,scy,cx,cy);

            // now start the new one
            start_off = !(flags & 1);
            if (start_off) {
               // if we start off with an off-curve point, then when we need to find a point on the curve
               // where we can start, and we need to save some state for when we wraparound.
               scx = x;
               scy = y;
               if (!(vertices[off+i+1].type & 1)) {
                  // next point is also a curve point, so interpolate an on-point curve
                  sx = (x + (stbtt_int32) vertices[off+i+1].x) >> 1;
                  sy = (y + (stbtt_int32) vertices[off+i+1].y) >> 1;
               } else {
                  // otherwise just use the next point as our start point
                  sx = (stbtt_int32) vertices[off+i+1].x;
                  sy = (stbtt_int32) vertices[off+i+1].y;
                  ++i; // we're using point i+1 as the starting point, so skip it
               }
            } else {
               sx = x;
               sy = y;
            }
            stbtt_setvertex(&vertices[num_vertices++], STBTT_vmove,sx,sy,0,0);
            was_off = 0;
            next_move = 1 + ttUSHORT(endPtsOfContours+j*2);
            ++j;
         } else {
            if (!(flags & 1)) { // if it's a curve
               if (was_off) // two off-curve control points in a row means interpolate an on-curve midpoint
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx+x)>>1, (cy+y)>>1, cx, cy);
               cx = x;
               cy = y;
               was_off = 1;
            } else {
               if (was_off)
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, x,y, cx, cy);
               else
                  stbtt_setvertex(&vertices[num_vertices++], STBTT_vline, x,y,0,0);
               was_off = 0;
            }
         }
      }
      num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx,sy,scx,scy,cx,cy);
   } else if (numberOfContours < 0) {
      // Compound shapes.
      int more = 1;
      stbtt_uint8 *comp = data + g + 10;
      num_vertices = 0;
      vertices = 0;
      while (more) {
         stbtt_uint16 flags, gidx;
         int comp_num_verts = 0, i;
         stbtt_vertex *comp_verts = 0, *tmp = 0;
         float mtx[6] = {1,0,0,1,0,0}, m, n;

         flags = ttSHORT(comp); comp+=2;
         gidx = ttSHORT(comp); comp+=2;

         if (flags & 2) { // XY values
            if (flags & 1) { // shorts
               mtx[4] = ttSHORT(comp); comp+=2;
               mtx[5] = ttSHORT(comp); comp+=2;
            } else {
               mtx[4] = ttCHAR(comp); comp+=1;
               mtx[5] = ttCHAR(comp); comp+=1;
            }
         }
         else {
            // @TODO handle matching point
            STBTT_assert(0);
         }
         if (flags & (1<<3)) { // WE_HAVE_A_SCALE
            mtx[0] = mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = mtx[2] = 0;
         } else if (flags & (1<<6)) { // WE_HAVE_AN_X_AND_YSCALE
            mtx[0] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = mtx[2] = 0;
            mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
         } else if (flags & (1<<7)) { // WE_HAVE_A_TWO_BY_TWO
            mtx[0] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[1] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[2] = ttSHORT(comp)/16384.0f; comp+=2;
            mtx[3] = ttSHORT(comp)/16384.0f; comp+=2;
         }

         // Find transformation scales.
         m = (float) STBTT_sqrt(mtx[0]*mtx[0] + mtx[1]*mtx[1]);
         n = (float) STBTT_sqrt(mtx[2]*mtx[2] + mtx[3]*mtx[3]);

         // Get indexed glyph.
         comp_num_verts = stbtt_GetGlyphShape(info, gidx, &comp_verts);
         if (comp_num_verts > 0) {
            // Transform vertices.
            for (i = 0; i < comp_num_verts; ++i) {
               stbtt_vertex* v = &comp_verts[i];
               stbtt_vertex_type x,y;
               x=v->x; y=v->y;
               v->x = (stbtt_vertex_type)(m * (mtx[0]*x + mtx[2]*y + mtx[4]));
               v->y = (stbtt_vertex_type)(n * (mtx[1]*x + mtx[3]*y + mtx[5]));
               x=v->cx; y=v->cy;
               v->cx = (stbtt_vertex_type)(m * (mtx[0]*x + mtx[2]*y + mtx[4]));
               v->cy = (stbtt_vertex_type)(n * (mtx[1]*x + mtx[3]*y + mtx[5]));
            }
            // Append vertices.
            tmp = (stbtt_vertex*)STBTT_malloc((num_vertices+comp_num_verts)*sizeof(stbtt_vertex), info->userdata);
            if (!tmp) {
               if (vertices) STBTT_free(vertices, info->userdata);
               if (comp_verts) STBTT_free(comp_verts, info->userdata);
               return 0;
            }
            if (num_vertices > 0 && vertices) STBTT_memcpy(tmp, vertices, num_vertices*sizeof(stbtt_vertex));
            STBTT_memcpy(tmp+num_vertices, comp_verts, comp_num_verts*sizeof(stbtt_vertex));
            if (vertices) STBTT_free(vertices, info->userdata);
            vertices = tmp;
            STBTT_free(comp_verts, info->userdata);
            num_vertices += comp_num_verts;
         }
         // More components ?
         more = flags & (1<<5);
      }
   } else {
      // numberOfCounters == 0, do nothing
   }

   *pvertices = vertices;
   return num_vertices;
}

typedef struct
{
   int bounds;
   int started;
   float first_x, first_y;
   float x, y;
   stbtt_int32 min_x, max_x, min_y, max_y;

   stbtt_vertex *pvertices;
   int num_vertices;
} stbtt__csctx;

#define STBTT__CSCTX_INIT(bounds) {bounds,0, 0,0, 0,0, 0,0,0,0, NULL, 0}

static void stbtt__track_vertex(stbtt__csctx *c, stbtt_int32 x, stbtt_int32 y)
{
   if (x > c->max_x || !c->started) c->max_x = x;
   if (y > c->max_y || !c->started) c->max_y = y;
   if (x < c->min_x || !c->started) c->min_x = x;
   if (y < c->min_y || !c->started) c->min_y = y;
   c->started = 1;
}

static void stbtt__csctx_v(stbtt__csctx *c, stbtt_uint8 type, stbtt_int32 x, stbtt_int32 y, stbtt_int32 cx, stbtt_int32 cy, stbtt_int32 cx1, stbtt_int32 cy1)
{
   if (c->bounds) {
      stbtt__track_vertex(c, x, y);
      if (type == STBTT_vcubic) {
         stbtt__track_vertex(c, cx, cy);
         stbtt__track_vertex(c, cx1, cy1);
      }
   } else {
      stbtt_setvertex(&c->pvertices[c->num_vertices], type, x, y, cx, cy);
      c->pvertices[c->num_vertices].cx1 = (stbtt_int16) cx1;
      c->pvertices[c->num_vertices].cy1 = (stbtt_int16) cy1;
   }
   c->num_vertices++;
}

static void stbtt__csctx_close_shape(stbtt__csctx *ctx)
{
   if (ctx->first_x != ctx->x || ctx->first_y != ctx->y)
      stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->first_x, (int)ctx->first_y, 0, 0, 0, 0);
}

static void stbtt__csctx_rmove_to(stbtt__csctx *ctx, float dx, float dy)
{
   stbtt__csctx_close_shape(ctx);
   ctx->first_x = ctx->x = ctx->x + dx;
   ctx->first_y = ctx->y = ctx->y + dy;
   stbtt__csctx_v(ctx, STBTT_vmove, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rline_to(stbtt__csctx *ctx, float dx, float dy)
{
   ctx->x += dx;
   ctx->y += dy;
   stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rccurve_to(stbtt__csctx *ctx, float dx1, float dy1, float dx2, float dy2, float dx3, float dy3)
{
   float cx1 = ctx->x + dx1;
   float cy1 = ctx->y + dy1;
   float cx2 = cx1 + dx2;
   float cy2 = cy1 + dy2;
   ctx->x = cx2 + dx3;
   ctx->y = cy2 + dy3;
   stbtt__csctx_v(ctx, STBTT_vcubic, (int)ctx->x, (int)ctx->y, (int)cx1, (int)cy1, (int)cx2, (int)cy2);
}

static stbtt__buf stbtt__get_subr(stbtt__buf idx, int n)
{
   int count = stbtt__cff_index_count(&idx);
   int bias = 107;
   if (count >= 33900)
      bias = 32768;
   else if (count >= 1240)
      bias = 1131;
   n += bias;
   if (n < 0 || n >= count)
      return stbtt__new_buf(NULL, 0);
   return stbtt__cff_index_get(idx, n);
}

static stbtt__buf stbtt__cid_get_glyph_subrs(const stbtt_fontinfo *info, int glyph_index)
{
   stbtt__buf fdselect = info->fdselect;
   int nranges, start, end, v, fmt, fdselector = -1, i;

   stbtt__buf_seek(&fdselect, 0);
   fmt = stbtt__buf_get8(&fdselect);
   if (fmt == 0) {
      // untested
      stbtt__buf_skip(&fdselect, glyph_index);
      fdselector = stbtt__buf_get8(&fdselect);
   } else if (fmt == 3) {
      nranges = stbtt__buf_get16(&fdselect);
      start = stbtt__buf_get16(&fdselect);
      for (i = 0; i < nranges; i++) {
         v = stbtt__buf_get8(&fdselect);
         end = stbtt__buf_get16(&fdselect);
         if (glyph_index >= start && glyph_index < end) {
            fdselector = v;
            break;
         }
         start = end;
      }
   }
   if (fdselector == -1) stbtt__new_buf(NULL, 0);
   return stbtt__get_subrs(info->cff, stbtt__cff_index_get(info->fontdicts, fdselector));
}

static int stbtt__run_charstring(const stbtt_fontinfo *info, int glyph_index, stbtt__csctx *c)
{
   int in_header = 1, maskbits = 0, subr_stack_height = 0, sp = 0, v, i, b0;
   int has_subrs = 0, clear_stack;
   float s[48];
   stbtt__buf subr_stack[10], subrs = info->subrs, b;
   float f;

#define STBTT__CSERR(s) (0)

   // this currently ignores the initial width value, which isn't needed if we have hmtx
   b = stbtt__cff_index_get(info->charstrings, glyph_index);
   while (b.cursor < b.size) {
      i = 0;
      clear_stack = 1;
      b0 = stbtt__buf_get8(&b);
      switch (b0) {
      // @TODO implement hinting
      case 0x13: // hintmask
      case 0x14: // cntrmask
         if (in_header)
            maskbits += (sp / 2); // implicit "vstem"
         in_header = 0;
         stbtt__buf_skip(&b, (maskbits + 7) / 8);
         break;

      case 0x01: // hstem
      case 0x03: // vstem
      case 0x12: // hstemhm
      case 0x17: // vstemhm
         maskbits += (sp / 2);
         break;

      case 0x15: // rmoveto
         in_header = 0;
         if (sp < 2) return STBTT__CSERR("rmoveto stack");
         stbtt__csctx_rmove_to(c, s[sp-2], s[sp-1]);
         break;
      case 0x04: // vmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("vmoveto stack");
         stbtt__csctx_rmove_to(c, 0, s[sp-1]);
         break;
      case 0x16: // hmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("hmoveto stack");
         stbtt__csctx_rmove_to(c, s[sp-1], 0);
         break;

      case 0x05: // rlineto
         if (sp < 2) return STBTT__CSERR("rlineto stack");
         for (; i + 1 < sp; i += 2)
            stbtt__csctx_rline_to(c, s[i], s[i+1]);
         break;

      // hlineto/vlineto and vhcurveto/hvcurveto alternate horizontal and vertical
      // starting from a different place.

      case 0x07: // vlineto
         if (sp < 1) return STBTT__CSERR("vlineto stack");
         goto vlineto;
      case 0x06: // hlineto
         if (sp < 1) return STBTT__CSERR("hlineto stack");
         for (;;) {
            if (i >= sp) break;
            stbtt__csctx_rline_to(c, s[i], 0);
            i++;
      vlineto:
            if (i >= sp) break;
            stbtt__csctx_rline_to(c, 0, s[i]);
            i++;
         }
         break;

      case 0x1F: // hvcurveto
         if (sp < 4) return STBTT__CSERR("hvcurveto stack");
         goto hvcurveto;
      case 0x1E: // vhcurveto
         if (sp < 4) return STBTT__CSERR("vhcurveto stack");
         for (;;) {
            if (i + 3 >= sp) break;
            stbtt__csctx_rccurve_to(c, 0, s[i], s[i+1], s[i+2], s[i+3], (sp - i == 5) ? s[i + 4] : 0.0f);
            i += 4;
      hvcurveto:
            if (i + 3 >= sp) break;
            stbtt__csctx_rccurve_to(c, s[i], 0, s[i+1], s[i+2], (sp - i == 5) ? s[i+4] : 0.0f, s[i+3]);
            i += 4;
         }
         break;

      case 0x08: // rrcurveto
         if (sp < 6) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp; i += 6)
            stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x18: // rcurveline
         if (sp < 8) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp - 2; i += 6)
            stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         if (i + 1 >= sp) return STBTT__CSERR("rcurveline stack");
         stbtt__csctx_rline_to(c, s[i], s[i+1]);
         break;

      case 0x19: // rlinecurve
         if (sp < 8) return STBTT__CSERR("rlinecurve stack");
         for (; i + 1 < sp - 6; i += 2)
            stbtt__csctx_rline_to(c, s[i], s[i+1]);
         if (i + 5 >= sp) return STBTT__CSERR("rlinecurve stack");
         stbtt__csctx_rccurve_to(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x1A: // vvcurveto
      case 0x1B: // hhcurveto
         if (sp < 4) return STBTT__CSERR("(vv|hh)curveto stack");
         f = 0.0;
         if (sp & 1) { f = s[i]; i++; }
         for (; i + 3 < sp; i += 4) {
            if (b0 == 0x1B)
               stbtt__csctx_rccurve_to(c, s[i], f, s[i+1], s[i+2], s[i+3], 0.0);
            else
               stbtt__csctx_rccurve_to(c, f, s[i], s[i+1], s[i+2], 0.0, s[i+3]);
            f = 0.0;
         }
         break;

      case 0x0A: // callsubr
         if (!has_subrs) {
            if (info->fdselect.size)
               subrs = stbtt__cid_get_glyph_subrs(info, glyph_index);
            has_subrs = 1;
         }
         // FALLTHROUGH
      case 0x1D: // callgsubr
         if (sp < 1) return STBTT__CSERR("call(g|)subr stack");
         v = (int) s[--sp];
         if (subr_stack_height >= 10) return STBTT__CSERR("recursion limit");
         subr_stack[subr_stack_height++] = b;
         b = stbtt__get_subr(b0 == 0x0A ? subrs : info->gsubrs, v);
         if (b.size == 0) return STBTT__CSERR("subr not found");
         b.cursor = 0;
         clear_stack = 0;
         break;

      case 0x0B: // return
         if (subr_stack_height <= 0) return STBTT__CSERR("return outside subr");
         b = subr_stack[--subr_stack_height];
         clear_stack = 0;
         break;

      case 0x0E: // endchar
         stbtt__csctx_close_shape(c);
         return 1;

      case 0x0C: { // two-byte escape
         float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
         float dx, dy;
         int b1 = stbtt__buf_get8(&b);
         switch (b1) {
         // @TODO These "flex" implementations ignore the flex-depth and resolution,
         // and always draw beziers.
         case 0x22: // hflex
            if (sp < 7) return STBTT__CSERR("hflex stack");
            dx1 = s[0];
            dx2 = s[1];
            dy2 = s[2];
            dx3 = s[3];
            dx4 = s[4];
            dx5 = s[5];
            dx6 = s[6];
            stbtt__csctx_rccurve_to(c, dx1, 0, dx2, dy2, dx3, 0);
            stbtt__csctx_rccurve_to(c, dx4, 0, dx5, -dy2, dx6, 0);
            break;

         case 0x23: // flex
            if (sp < 13) return STBTT__CSERR("flex stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = s[10];
            dy6 = s[11];
            //fd is s[12]
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
            stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         case 0x24: // hflex1
            if (sp < 9) return STBTT__CSERR("hflex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dx4 = s[5];
            dx5 = s[6];
            dy5 = s[7];
            dx6 = s[8];
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, 0);
            stbtt__csctx_rccurve_to(c, dx4, 0, dx5, dy5, dx6, -(dy1+dy2+dy5));
            break;

         case 0x25: // flex1
            if (sp < 11) return STBTT__CSERR("flex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = dy6 = s[10];
            dx = dx1+dx2+dx3+dx4+dx5;
            dy = dy1+dy2+dy3+dy4+dy5;
            if (STBTT_fabs(dx) > STBTT_fabs(dy))
               dy6 = -dy;
            else
               dx6 = -dx;
            stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
            stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         default:
            return STBTT__CSERR("unimplemented");
         }
      } break;

      default:
         if (b0 != 255 && b0 != 28 && b0 < 32)
            return STBTT__CSERR("reserved operator");

         // push immediate
         if (b0 == 255) {
            f = (float)(stbtt_int32)stbtt__buf_get32(&b) / 0x10000;
         } else {
            stbtt__buf_skip(&b, -1);
            f = (float)(stbtt_int16)stbtt__cff_int(&b);
         }
         if (sp >= 48) return STBTT__CSERR("push stack overflow");
         s[sp++] = f;
         clear_stack = 0;
         break;
      }
      if (clear_stack) sp = 0;
   }
   return STBTT__CSERR("no endchar");

#undef STBTT__CSERR
}

static int stbtt__GetGlyphShapeT2(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   // runs the charstring twice, once to count and once to output (to avoid realloc)
   stbtt__csctx count_ctx = STBTT__CSCTX_INIT(1);
   stbtt__csctx output_ctx = STBTT__CSCTX_INIT(0);
   if (stbtt__run_charstring(info, glyph_index, &count_ctx)) {
      *pvertices = (stbtt_vertex*)STBTT_malloc(count_ctx.num_vertices*sizeof(stbtt_vertex), info->userdata);
      output_ctx.pvertices = *pvertices;
      if (stbtt__run_charstring(info, glyph_index, &output_ctx)) {
         STBTT_assert(output_ctx.num_vertices == count_ctx.num_vertices);
         return output_ctx.num_vertices;
      }
   }
   *pvertices = NULL;
   return 0;
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
   stbtt__csctx c = STBTT__CSCTX_INIT(1);
   int r = stbtt__run_charstring(info, glyph_index, &c);
   if (x0)  *x0 = r ? c.min_x : 0;
   if (y0)  *y0 = r ? c.min_y : 0;
   if (x1)  *x1 = r ? c.max_x : 0;
   if (y1)  *y1 = r ? c.max_y : 0;
   return r ? c.num_vertices : 0;
}

STBTT_DEF int stbtt_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
   if (!info->cff.size)
      return stbtt__GetGlyphShapeTT(info, glyph_index, pvertices);
   else
      return stbtt__GetGlyphShapeT2(info, glyph_index, pvertices);
}

STBTT_DEF void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing)
{
   stbtt_uint16 numOfLongHorMetrics = ttUSHORT(info->data+info->hhea + 34);
   if (glyph_index < numOfLongHorMetrics) {
      if (advanceWidth)     *advanceWidth    = ttSHORT(info->data + info->hmtx + 4*glyph_index);
      if (leftSideBearing)  *leftSideBearing = ttSHORT(info->data + info->hmtx + 4*glyph_index + 2);
   } else {
      if (advanceWidth)     *advanceWidth    = ttSHORT(info->data + info->hmtx + 4*(numOfLongHorMetrics-1));
      if (leftSideBearing)  *leftSideBearing = ttSHORT(info->data + info->hmtx + 4*numOfLongHorMetrics + 2*(glyph_index - numOfLongHorMetrics));
   }
}

STBTT_DEF int  stbtt_GetKerningTableLength(const stbtt_fontinfo *info)
{
   stbtt_uint8 *data = info->data + info->kern;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   return ttUSHORT(data+10);
}

STBTT_DEF int stbtt_GetKerningTable(const stbtt_fontinfo *info, stbtt_kerningentry* table, int table_length)
{
   stbtt_uint8 *data = info->data + info->kern;
   int k, length;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   length = ttUSHORT(data+10);
   if (table_length < length)
      length = table_length;

   for (k = 0; k < length; k++)
   {
      table[k].glyph1 = ttUSHORT(data+18+(k*6));
      table[k].glyph2 = ttUSHORT(data+20+(k*6));
      table[k].advance = ttSHORT(data+22+(k*6));
   }

   return length;
}

static int stbtt__GetGlyphKernInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
   stbtt_uint8 *data = info->data + info->kern;
   stbtt_uint32 needle, straw;
   int l, r, m;

   // we only look at the first table. it must be 'horizontal' and format 0.
   if (!info->kern)
      return 0;
   if (ttUSHORT(data+2) < 1) // number of tables, need at least 1
      return 0;
   if (ttUSHORT(data+8) != 1) // horizontal flag must be set in format
      return 0;

   l = 0;
   r = ttUSHORT(data+10) - 1;
   needle = glyph1 << 16 | glyph2;
   while (l <= r) {
      m = (l + r) >> 1;
      straw = ttULONG(data+18+(m*6)); // note: unaligned read
      if (needle < straw)
         r = m - 1;
      else if (needle > straw)
         l = m + 1;
      else
         return ttSHORT(data+22+(m*6));
   }
   return 0;
}

static stbtt_int32 stbtt__GetCoverageIndex(stbtt_uint8 *coverageTable, int glyph)
{
   stbtt_uint16 coverageFormat = ttUSHORT(coverageTable);
   switch (coverageFormat) {
      case 1: {
         stbtt_uint16 glyphCount = ttUSHORT(coverageTable + 2);

         // Binary search.
         stbtt_int32 l=0, r=glyphCount-1, m;
         int straw, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *glyphArray = coverageTable + 4;
            stbtt_uint16 glyphID;
            m = (l + r) >> 1;
            glyphID = ttUSHORT(glyphArray + 2 * m);
            straw = glyphID;
            if (needle < straw)
               r = m - 1;
            else if (needle > straw)
               l = m + 1;
            else {
               return m;
            }
         }
         break;
      }

      case 2: {
         stbtt_uint16 rangeCount = ttUSHORT(coverageTable + 2);
         stbtt_uint8 *rangeArray = coverageTable + 4;

         // Binary search.
         stbtt_int32 l=0, r=rangeCount-1, m;
         int strawStart, strawEnd, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *rangeRecord;
            m = (l + r) >> 1;
            rangeRecord = rangeArray + 6 * m;
            strawStart = ttUSHORT(rangeRecord);
            strawEnd = ttUSHORT(rangeRecord + 2);
            if (needle < strawStart)
               r = m - 1;
            else if (needle > strawEnd)
               l = m + 1;
            else {
               stbtt_uint16 startCoverageIndex = ttUSHORT(rangeRecord + 4);
               return startCoverageIndex + glyph - strawStart;
            }
         }
         break;
      }

      default: return -1; // unsupported
   }

   return -1;
}

static stbtt_int32  stbtt__GetGlyphClass(stbtt_uint8 *classDefTable, int glyph)
{
   stbtt_uint16 classDefFormat = ttUSHORT(classDefTable);
   switch (classDefFormat)
   {
      case 1: {
         stbtt_uint16 startGlyphID = ttUSHORT(classDefTable + 2);
         stbtt_uint16 glyphCount = ttUSHORT(classDefTable + 4);
         stbtt_uint8 *classDef1ValueArray = classDefTable + 6;

         if (glyph >= startGlyphID && glyph < startGlyphID + glyphCount)
            return (stbtt_int32)ttUSHORT(classDef1ValueArray + 2 * (glyph - startGlyphID));
         break;
      }

      case 2: {
         stbtt_uint16 classRangeCount = ttUSHORT(classDefTable + 2);
         stbtt_uint8 *classRangeRecords = classDefTable + 4;

         // Binary search.
         stbtt_int32 l=0, r=classRangeCount-1, m;
         int strawStart, strawEnd, needle=glyph;
         while (l <= r) {
            stbtt_uint8 *classRangeRecord;
            m = (l + r) >> 1;
            classRangeRecord = classRangeRecords + 6 * m;
            strawStart = ttUSHORT(classRangeRecord);
            strawEnd = ttUSHORT(classRangeRecord + 2);
            if (needle < strawStart)
               r = m - 1;
            else if (needle > strawEnd)
               l = m + 1;
            else
               return (stbtt_int32)ttUSHORT(classRangeRecord + 4);
         }
         break;
      }

      default:
         return -1; // Unsupported definition type, return an error.
   }

   // "All glyphs not assigned to a class fall into class 0". (OpenType spec)
   return 0;
}

// Define to STBTT_assert(x) if you want to break on unimplemented formats.
#define STBTT_GPOS_TODO_assert(x)

static stbtt_int32 stbtt__GetGlyphGPOSInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
   stbtt_uint16 lookupListOffset;
   stbtt_uint8 *lookupList;
   stbtt_uint16 lookupCount;
   stbtt_uint8 *data;
   stbtt_int32 i, sti;

   if (!info->gpos) return 0;

   data = info->data + info->gpos;

   if (ttUSHORT(data+0) != 1) return 0; // Major version 1
   if (ttUSHORT(data+2) != 0) return 0; // Minor version 0

   lookupListOffset = ttUSHORT(data+8);
   lookupList = data + lookupListOffset;
   lookupCount = ttUSHORT(lookupList);

   for (i=0; i<lookupCount; ++i) {
      stbtt_uint16 lookupOffset = ttUSHORT(lookupList + 2 + 2 * i);
      stbtt_uint8 *lookupTable = lookupList + lookupOffset;

      stbtt_uint16 lookupType = ttUSHORT(lookupTable);
      stbtt_uint16 subTableCount = ttUSHORT(lookupTable + 4);
      stbtt_uint8 *subTableOffsets = lookupTable + 6;
      if (lookupType != 2) // Pair Adjustment Positioning Subtable
         continue;

      for (sti=0; sti<subTableCount; sti++) {
         stbtt_uint16 subtableOffset = ttUSHORT(subTableOffsets + 2 * sti);
         stbtt_uint8 *table = lookupTable + subtableOffset;
         stbtt_uint16 posFormat = ttUSHORT(table);
         stbtt_uint16 coverageOffset = ttUSHORT(table + 2);
         stbtt_int32 coverageIndex = stbtt__GetCoverageIndex(table + coverageOffset, glyph1);
         if (coverageIndex == -1) continue;

         switch (posFormat) {
            case 1: {
               stbtt_int32 l, r, m;
               int straw, needle;
               stbtt_uint16 valueFormat1 = ttUSHORT(table + 4);
               stbtt_uint16 valueFormat2 = ttUSHORT(table + 6);
               if (valueFormat1 == 4 && valueFormat2 == 0) { // Support more formats?
                  stbtt_int32 valueRecordPairSizeInBytes = 2;
                  stbtt_uint16 pairSetCount = ttUSHORT(table + 8);
                  stbtt_uint16 pairPosOffset = ttUSHORT(table + 10 + 2 * coverageIndex);
                  stbtt_uint8 *pairValueTable = table + pairPosOffset;
                  stbtt_uint16 pairValueCount = ttUSHORT(pairValueTable);
                  stbtt_uint8 *pairValueArray = pairValueTable + 2;

                  if (coverageIndex >= pairSetCount) return 0;

                  needle=glyph2;
                  r=pairValueCount-1;
                  l=0;

                  // Binary search.
                  while (l <= r) {
                     stbtt_uint16 secondGlyph;
                     stbtt_uint8 *pairValue;
                     m = (l + r) >> 1;
                     pairValue = pairValueArray + (2 + valueRecordPairSizeInBytes) * m;
                     secondGlyph = ttUSHORT(pairValue);
                     straw = secondGlyph;
                     if (needle < straw)
                        r = m - 1;
                     else if (needle > straw)
                        l = m + 1;
                     else {
                        stbtt_int16 xAdvance = ttSHORT(pairValue + 2);
                        return xAdvance;
                     }
                  }
               } else
                  return 0;
               break;
            }

            case 2: {
               stbtt_uint16 valueFormat1 = ttUSHORT(table + 4);
               stbtt_uint16 valueFormat2 = ttUSHORT(table + 6);
               if (valueFormat1 == 4 && valueFormat2 == 0) { // Support more formats?
                  stbtt_uint16 classDef1Offset = ttUSHORT(table + 8);
                  stbtt_uint16 classDef2Offset = ttUSHORT(table + 10);
                  int glyph1class = stbtt__GetGlyphClass(table + classDef1Offset, glyph1);
                  int glyph2class = stbtt__GetGlyphClass(table + classDef2Offset, glyph2);

                  stbtt_uint16 class1Count = ttUSHORT(table + 12);
                  stbtt_uint16 class2Count = ttUSHORT(table + 14);
                  stbtt_uint8 *class1Records, *class2Records;
                  stbtt_int16 xAdvance;

                  if (glyph1class < 0 || glyph1class >= class1Count) return 0; // malformed
                  if (glyph2class < 0 || glyph2class >= class2Count) return 0; // malformed

                  class1Records = table + 16;
                  class2Records = class1Records + 2 * (glyph1class * class2Count);
                  xAdvance = ttSHORT(class2Records + 2 * glyph2class);
                  return xAdvance;
               } else
                  return 0;
               break;
            }

            default:
               return 0; // Unsupported position format
         }
      }
   }

   return 0;
}

STBTT_DEF int  stbtt_GetGlyphKernAdvance(const stbtt_fontinfo *info, int g1, int g2)
{
   int xAdvance = 0;

   if (info->gpos)
      xAdvance += stbtt__GetGlyphGPOSInfoAdvance(info, g1, g2);
   else if (info->kern)
      xAdvance += stbtt__GetGlyphKernInfoAdvance(info, g1, g2);

   return xAdvance;
}

STBTT_DEF int  stbtt_GetCodepointKernAdvance(const stbtt_fontinfo *info, int ch1, int ch2)
{
   if (!info->kern && !info->gpos) // if no kerning table, don't waste time looking up both codepoint->glyphs
      return 0;
   return stbtt_GetGlyphKernAdvance(info, stbtt_FindGlyphIndex(info,ch1), stbtt_FindGlyphIndex(info,ch2));
}

STBTT_DEF void stbtt_GetCodepointHMetrics(const stbtt_fontinfo *info, int codepoint, int *advanceWidth, int *leftSideBearing)
{
   stbtt_GetGlyphHMetrics(info, stbtt_FindGlyphIndex(info,codepoint), advanceWidth, leftSideBearing);
}

STBTT_DEF void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap)
{
   if (ascent ) *ascent  = ttSHORT(info->data+info->hhea + 4);
   if (descent) *descent = ttSHORT(info->data+info->hhea + 6);
   if (lineGap) *lineGap = ttSHORT(info->data+info->hhea + 8);
}

STBTT_DEF int  stbtt_GetFontVMetricsOS2(const stbtt_fontinfo *info, int *typoAscent, int *typoDescent, int *typoLineGap)
{
   int tab = stbtt__find_table(info->data, info->fontstart, "OS/2");
   if (!tab)
      return 0;
   if (typoAscent ) *typoAscent  = ttSHORT(info->data+tab + 68);
   if (typoDescent) *typoDescent = ttSHORT(info->data+tab + 70);
   if (typoLineGap) *typoLineGap = ttSHORT(info->data+tab + 72);
   return 1;
}

STBTT_DEF void stbtt_GetFontBoundingBox(const stbtt_fontinfo *info, int *x0, int *y0, int *x1, int *y1)
{
   *x0 = ttSHORT(info->data + info->head + 36);
   *y0 = ttSHORT(info->data + info->head + 38);
   *x1 = ttSHORT(info->data + info->head + 40);
   *y1 = ttSHORT(info->data + info->head + 42);
}

STBTT_DEF float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float height)
{
   int fheight = ttSHORT(info->data + info->hhea + 4) - ttSHORT(info->data + info->hhea + 6);
   return (float) height / fheight;
}

STBTT_DEF float stbtt_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels)
{
   int unitsPerEm = ttUSHORT(info->data + info->head + 18);
   return pixels / unitsPerEm;
}

STBTT_DEF void stbtt_FreeShape(const stbtt_fontinfo *info, stbtt_vertex *v)
{
   STBTT_free(v, info->userdata);
}

STBTT_DEF stbtt_uint8 *stbtt_FindSVGDoc(const stbtt_fontinfo *info, int gl)
{
   int i;
   stbtt_uint8 *data = info->data;
   stbtt_uint8 *svg_doc_list = data + stbtt__get_svg((stbtt_fontinfo *) info);

   int numEntries = ttUSHORT(svg_doc_list);
   stbtt_uint8 *svg_docs = svg_doc_list + 2;

   for(i=0; i<numEntries; i++) {
      stbtt_uint8 *svg_doc = svg_docs + (12 * i);
      if ((gl >= ttUSHORT(svg_doc)) && (gl <= ttUSHORT(svg_doc + 2)))
         return svg_doc;
   }
   return 0;
}

STBTT_DEF int stbtt_GetGlyphSVG(const stbtt_fontinfo *info, int gl, const char **svg)
{
   stbtt_uint8 *data = info->data;
   stbtt_uint8 *svg_doc;

   if (info->svg == 0)
      return 0;

   svg_doc = stbtt_FindSVGDoc(info, gl);
   if (svg_doc != NULL) {
      *svg = (char *) data + info->svg + ttULONG(svg_doc + 4);
      return ttULONG(svg_doc + 8);
   } else {
      return 0;
   }
}

STBTT_DEF int stbtt_GetCodepointSVG(const stbtt_fontinfo *info, int unicode_codepoint, const char **svg)
{
   return stbtt_GetGlyphSVG(info, stbtt_FindGlyphIndex(info, unicode_codepoint), svg);
}

//////////////////////////////////////////////////////////////////////////////
//
// antialiasing software rasterizer
//

STBTT_DEF void stbtt_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y,float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   int x0=0,y0=0,x1,y1; // =0 suppresses compiler warning
   if (!stbtt_GetGlyphBox(font, glyph, &x0,&y0,&x1,&y1)) {
      // e.g. space character
      if (ix0) *ix0 = 0;
      if (iy0) *iy0 = 0;
      if (ix1) *ix1 = 0;
      if (iy1) *iy1 = 0;
   } else {
      // move to integral bboxes (treating pixels as little squares, what pixels get touched)?
      if (ix0) *ix0 = STBTT_ifloor( x0 * scale_x + shift_x);
      if (iy0) *iy0 = STBTT_ifloor(-y1 * scale_y + shift_y);
      if (ix1) *ix1 = STBTT_iceil ( x1 * scale_x + shift_x);
      if (iy1) *iy1 = STBTT_iceil (-y0 * scale_y + shift_y);
   }
}

STBTT_DEF void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetGlyphBitmapBoxSubpixel(font, glyph, scale_x, scale_y,0.0f,0.0f, ix0, iy0, ix1, iy1);
}

STBTT_DEF void stbtt_GetCodepointBitmapBoxSubpixel(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetGlyphBitmapBoxSubpixel(font, stbtt_FindGlyphIndex(font,codepoint), scale_x, scale_y,shift_x,shift_y, ix0,iy0,ix1,iy1);
}

STBTT_DEF void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
   stbtt_GetCodepointBitmapBoxSubpixel(font, codepoint, scale_x, scale_y,0.0f,0.0f, ix0,iy0,ix1,iy1);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Rasterizer

typedef struct stbtt__hheap_chunk
{
   struct stbtt__hheap_chunk *next;
} stbtt__hheap_chunk;

typedef struct stbtt__hheap
{
   struct stbtt__hheap_chunk *head;
   void   *first_free;
   int    num_remaining_in_head_chunk;
} stbtt__hheap;

static void *stbtt__hheap_alloc(stbtt__hheap *hh, size_t size, void *userdata)
{
   if (hh->first_free) {
      void *p = hh->first_free;
      hh->first_free = * (void **) p;
      return p;
   } else {
      if (hh->num_remaining_in_head_chunk == 0) {
         int count = (size < 32 ? 2000 : size < 128 ? 800 : 100);
         stbtt__hheap_chunk *c = (stbtt__hheap_chunk *) STBTT_malloc(sizeof(stbtt__hheap_chunk) + size * count, userdata);
         if (c == NULL)
            return NULL;
         c->next = hh->head;
         hh->head = c;
         hh->num_remaining_in_head_chunk = count;
      }
      --hh->num_remaining_in_head_chunk;
      return (char *) (hh->head) + sizeof(stbtt__hheap_chunk) + size * hh->num_remaining_in_head_chunk;
   }
}

static void stbtt__hheap_free(stbtt__hheap *hh, void *p)
{
   *(void **) p = hh->first_free;
   hh->first_free = p;
}

static void stbtt__hheap_cleanup(stbtt__hheap *hh, void *userdata)
{
   stbtt__hheap_chunk *c = hh->head;
   while (c) {
      stbtt__hheap_chunk *n = c->next;
      STBTT_free(c, userdata);
      c = n;
   }
}

typedef struct stbtt__edge {
   float x0,y0, x1,y1;
   int invert;
} stbtt__edge;


typedef struct stbtt__active_edge
{
   struct stbtt__active_edge *next;
   #if STBTT_RASTERIZER_VERSION==1
   int x,dx;
   float ey;
   int direction;
   #elif STBTT_RASTERIZER_VERSION==2
   float fx,fdx,fdy;
   float direction;
   float sy;
   float ey;
   #else
   #error "Unrecognized value of STBTT_RASTERIZER_VERSION"
   #endif
} stbtt__active_edge;

#if STBTT_RASTERIZER_VERSION == 1
#define STBTT_FIXSHIFT   10
#define STBTT_FIX        (1 << STBTT_FIXSHIFT)
#define STBTT_FIXMASK    (STBTT_FIX-1)

static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point, void *userdata)
{
   stbtt__active_edge *z = (stbtt__active_edge *) stbtt__hheap_alloc(hh, sizeof(*z), userdata);
   float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
   STBTT_assert(z != NULL);
   if (!z) return z;

   // round dx down to avoid overshooting
   if (dxdy < 0)
      z->dx = -STBTT_ifloor(STBTT_FIX * -dxdy);
   else
      z->dx = STBTT_ifloor(STBTT_FIX * dxdy);

   z->x = STBTT_ifloor(STBTT_FIX * e->x0 + z->dx * (start_point - e->y0)); // use z->dx so when we offset later it's by the same amount
   z->x -= off_x * STBTT_FIX;

   z->ey = e->y1;
   z->next = 0;
   z->direction = e->invert ? 1 : -1;
   return z;
}
#elif STBTT_RASTERIZER_VERSION == 2
static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point, void *userdata)
{
   stbtt__active_edge *z = (stbtt__active_edge *) stbtt__hheap_alloc(hh, sizeof(*z), userdata);
   float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
   STBTT_assert(z != NULL);
   //STBTT_assert(e->y0 <= start_point);
   if (!z) return z;
   z->fdx = dxdy;
   z->fdy = dxdy != 0.0f ? (1.0f/dxdy) : 0.0f;
   z->fx = e->x0 + dxdy * (start_point - e->y0);
   z->fx -= off_x;
   z->direction = e->invert ? 1.0f : -1.0f;
   z->sy = e->y0;
   z->ey = e->y1;
   z->next = 0;
   return z;
}
#else
#error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif

#if STBTT_RASTERIZER_VERSION == 1
// note: this routine clips fills that extend off the edges... ideally this
// wouldn't happen, but it could happen if the truetype glyph bounding boxes
// are wrong, or if the user supplies a too-small bitmap
static void stbtt__fill_active_edges(unsigned char *scanline, int len, stbtt__active_edge *e, int max_weight)
{
   // non-zero winding fill
   int x0=0, w=0;

   while (e) {
      if (w == 0) {
         // if we're currently at zero, we need to record the edge start point
         x0 = e->x; w += e->direction;
      } else {
         int x1 = e->x; w += e->direction;
         // if we went to zero, we need to draw
         if (w == 0) {
            int i = x0 >> STBTT_FIXSHIFT;
            int j = x1 >> STBTT_FIXSHIFT;

            if (i < len && j >= 0) {
               if (i == j) {
                  // x0,x1 are the same pixel, so compute combined coverage
                  scanline[i] = scanline[i] + (stbtt_uint8) ((x1 - x0) * max_weight >> STBTT_FIXSHIFT);
               } else {
                  if (i >= 0) // add antialiasing for x0
                     scanline[i] = scanline[i] + (stbtt_uint8) (((STBTT_FIX - (x0 & STBTT_FIXMASK)) * max_weight) >> STBTT_FIXSHIFT);
                  else
                     i = -1; // clip

                  if (j < len) // add antialiasing for x1
                     scanline[j] = scanline[j] + (stbtt_uint8) (((x1 & STBTT_FIXMASK) * max_weight) >> STBTT_FIXSHIFT);
                  else
                     j = len; // clip

                  for (++i; i < j; ++i) // fill pixels between x0 and x1
                     scanline[i] = scanline[i] + (stbtt_uint8) max_weight;
               }
            }
         }
      }

      e = e->next;
   }
}

static void stbtt__rasterize_sorted_edges(stbtt__bitmap *result, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y, void *userdata)
{
   stbtt__hheap hh = { 0, 0, 0 };
   stbtt__active_edge *active = NULL;
   int y,j=0;
   int max_weight = (255 / vsubsample);  // weight per vertical scanline
   int s; // vertical subsample index
   unsigned char scanline_data[512], *scanline;

   if (result->w > 512)
      scanline = (unsigned char *) STBTT_malloc(result->w, userdata);
   else
      scanline = scanline_data;

   y = off_y * vsubsample;
   e[n].y0 = (off_y + result->h) * (float) vsubsample + 1;

   while (j < result->h) {
      STBTT_memset(scanline, 0, result->w);
      for (s=0; s < vsubsample; ++s) {
         // find center of pixel for this scanline
         float scan_y = y + 0.5f;
         stbtt__active_edge **step = &active;

         // update all active edges;
         // remove all active edges that terminate before the center of this scanline
         while (*step) {
            stbtt__active_edge * z = *step;
            if (z->ey <= scan_y) {
               *step = z->next; // delete from list
               STBTT_assert(z->direction);
               z->direction = 0;
               stbtt__hheap_free(&hh, z);
            } else {
               z->x += z->dx; // advance to position for current scanline
               step = &((*step)->next); // advance through list
            }
         }

         // resort the list if needed
         for(;;) {
            int changed=0;
            step = &active;
            while (*step && (*step)->next) {
               if ((*step)->x > (*step)->next->x) {
                  stbtt__active_edge *t = *step;
                  stbtt__active_edge *q = t->next;

                  t->next = q->next;
                  q->next = t;
                  *step = q;
                  changed = 1;
               }
               step = &(*step)->next;
            }
            if (!changed) break;
         }

         // insert all edges that start before the center of this scanline -- omit ones that also end on this scanline
         while (e->y0 <= scan_y) {
            if (e->y1 > scan_y) {
               stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y, userdata);
               if (z != NULL) {
                  // find insertion point
                  if (active == NULL)
                     active = z;
                  else if (z->x < active->x) {
                     // insert at front
                     z->next = active;
                     active = z;
                  } else {
                     // find thing to insert AFTER
                     stbtt__active_edge *p = active;
                     while (p->next && p->next->x < z->x)
                        p = p->next;
                     // at this point, p->next->x is NOT < z->x
                     z->next = p->next;
                     p->next = z;
                  }
               }
            }
            ++e;
         }

         // now process all active edges in XOR fashion
         if (active)
            stbtt__fill_active_edges(scanline, result->w, active, max_weight);

         ++y;
      }
      STBTT_memcpy(result->pixels + j * result->stride, scanline, result->w);
      ++j;
   }

   stbtt__hheap_cleanup(&hh, userdata);

   if (scanline != scanline_data)
      STBTT_free(scanline, userdata);
}

#elif STBTT_RASTERIZER_VERSION == 2

// the edge passed in here does not cross the vertical line at x or the vertical line at x+1
// (i.e. it has already been clipped to those)
static void stbtt__handle_clipped_edge(float *scanline, int x, stbtt__active_edge *e, float x0, float y0, float x1, float y1)
{
   if (y0 == y1) return;
   STBTT_assert(y0 < y1);
   STBTT_assert(e->sy <= e->ey);
   if (y0 > e->ey) return;
   if (y1 < e->sy) return;
   if (y0 < e->sy) {
      x0 += (x1-x0) * (e->sy - y0) / (y1-y0);
      y0 = e->sy;
   }
   if (y1 > e->ey) {
      x1 += (x1-x0) * (e->ey - y1) / (y1-y0);
      y1 = e->ey;
   }

   if (x0 == x)
      STBTT_assert(x1 <= x+1);
   else if (x0 == x+1)
      STBTT_assert(x1 >= x);
   else if (x0 <= x)
      STBTT_assert(x1 <= x);
   else if (x0 >= x+1)
      STBTT_assert(x1 >= x+1);
   else
      STBTT_assert(x1 >= x && x1 <= x+1);

   if (x0 <= x && x1 <= x)
      scanline[x] += e->direction * (y1-y0);
   else if (x0 >= x+1 && x1 >= x+1)
      ;
   else {
      STBTT_assert(x0 >= x && x0 <= x+1 && x1 >= x && x1 <= x+1);
      scanline[x] += e->direction * (y1-y0) * (1-((x0-x)+(x1-x))/2); // coverage = 1 - average x position
   }
}

static float stbtt__sized_trapezoid_area(float height, float top_width, float bottom_width)
{
   STBTT_assert(top_width >= 0);
   STBTT_assert(bottom_width >= 0);
   return (top_width + bottom_width) / 2.0f * height;
}

static float stbtt__position_trapezoid_area(float height, float tx0, float tx1, float bx0, float bx1)
{
   return stbtt__sized_trapezoid_area(height, tx1 - tx0, bx1 - bx0);
}

static float stbtt__sized_triangle_area(float height, float width)
{
   return height * width / 2;
}

static void stbtt__fill_active_edges_new(float *scanline, float *scanline_fill, int len, stbtt__active_edge *e, float y_top)
{
   float y_bottom = y_top+1;

   while (e) {
      // brute force every pixel

      // compute intersection points with top & bottom
      STBTT_assert(e->ey >= y_top);

      if (e->fdx == 0) {
         float x0 = e->fx;
         if (x0 < len) {
            if (x0 >= 0) {
               stbtt__handle_clipped_edge(scanline,(int) x0,e, x0,y_top, x0,y_bottom);
               stbtt__handle_clipped_edge(scanline_fill-1,(int) x0+1,e, x0,y_top, x0,y_bottom);
            } else {
               stbtt__handle_clipped_edge(scanline_fill-1,0,e, x0,y_top, x0,y_bottom);
            }
         }
      } else {
         float x0 = e->fx;
         float dx = e->fdx;
         float xb = x0 + dx;
         float x_top, x_bottom;
         float sy0,sy1;
         float dy = e->fdy;
         STBTT_assert(e->sy <= y_bottom && e->ey >= y_top);

         // compute endpoints of line segment clipped to this scanline (if the
         // line segment starts on this scanline. x0 is the intersection of the
         // line with y_top, but that may be off the line segment.
         if (e->sy > y_top) {
            x_top = x0 + dx * (e->sy - y_top);
            sy0 = e->sy;
         } else {
            x_top = x0;
            sy0 = y_top;
         }
         if (e->ey < y_bottom) {
            x_bottom = x0 + dx * (e->ey - y_top);
            sy1 = e->ey;
         } else {
            x_bottom = xb;
            sy1 = y_bottom;
         }

         if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len) {
            // from here on, we don't have to range check x values

            if ((int) x_top == (int) x_bottom) {
               float height;
               // simple case, only spans one pixel
               int x = (int) x_top;
               height = (sy1 - sy0) * e->direction;
               STBTT_assert(x >= 0 && x < len);
               scanline[x]      += stbtt__position_trapezoid_area(height, x_top, x+1.0f, x_bottom, x+1.0f);
               scanline_fill[x] += height; // everything right of this pixel is filled
            } else {
               int x,x1,x2;
               float y_crossing, y_final, step, sign, area;
               // covers 2+ pixels
               if (x_top > x_bottom) {
                  // flip scanline vertically; signed area is the same
                  float t;
                  sy0 = y_bottom - (sy0 - y_top);
                  sy1 = y_bottom - (sy1 - y_top);
                  t = sy0, sy0 = sy1, sy1 = t;
                  t = x_bottom, x_bottom = x_top, x_top = t;
                  dx = -dx;
                  dy = -dy;
                  t = x0, x0 = xb, xb = t;
               }
               STBTT_assert(dy >= 0);
               STBTT_assert(dx >= 0);

               x1 = (int) x_top;
               x2 = (int) x_bottom;
               // compute intersection with y axis at x1+1
               y_crossing = y_top + dy * (x1+1 - x0);

               // compute intersection with y axis at x2
               y_final = y_top + dy * (x2 - x0);

               //           x1    x_top                            x2    x_bottom
               //     y_top  +------|-----+------------+------------+--------|---+------------+
               //            |            |            |            |            |            |
               //            |            |            |            |            |            |
               //       sy0  |      Txxxxx|............|............|............|............|
               // y_crossing |            *xxxxx.......|............|............|............|
               //            |            |     xxxxx..|............|............|............|
               //            |            |     /-   xx*xxxx........|............|............|
               //            |            | dy <       |    xxxxxx..|............|............|
               //   y_final  |            |     \-     |          xx*xxx.........|............|
               //       sy1  |            |            |            |   xxxxxB...|............|
               //            |            |            |            |            |            |
               //            |            |            |            |            |            |
               //  y_bottom  +------------+------------+------------+------------+------------+
               //
               // goal is to measure the area covered by '.' in each pixel

               // if x2 is right at the right edge of x1, y_crossing can blow up, github #1057
               // @TODO: maybe test against sy1 rather than y_bottom?
               if (y_crossing > y_bottom)
                  y_crossing = y_bottom;

               sign = e->direction;

               // area of the rectangle covered from sy0..y_crossing
               area = sign * (y_crossing-sy0);

               // area of the triangle (x_top,sy0), (x1+1,sy0), (x1+1,y_crossing)
               scanline[x1] += stbtt__sized_triangle_area(area, x1+1 - x_top);

               // check if final y_crossing is blown up; no test case for this
               if (y_final > y_bottom) {
                  y_final = y_bottom;
                  dy = (y_final - y_crossing ) / (x2 - (x1+1)); // if denom=0, y_final = y_crossing, so y_final <= y_bottom
               }

               // in second pixel, area covered by line segment found in first pixel
               // is always a rectangle 1 wide * the height of that line segment; this
               // is exactly what the variable 'area' stores. it also gets a contribution
               // from the line segment within it. the THIRD pixel will get the first
               // pixel's rectangle contribution, the second pixel's rectangle contribution,
               // and its own contribution. the 'own contribution' is the same in every pixel except
               // the leftmost and rightmost, a trapezoid that slides down in each pixel.
               // the second pixel's contribution to the third pixel will be the
               // rectangle 1 wide times the height change in the second pixel, which is dy.

               step = sign * dy * 1; // dy is dy/dx, change in y for every 1 change in x,
               // which multiplied by 1-pixel-width is how much pixel area changes for each step in x
               // so the area advances by 'step' every time

               for (x = x1+1; x < x2; ++x) {
                  scanline[x] += area + step/2; // area of trapezoid is 1*step/2
                  area += step;
               }
               STBTT_assert(STBTT_fabs(area) <= 1.01f); // accumulated error from area += step unless we round step down
               STBTT_assert(sy1 > y_final-0.01f);

               // area covered in the last pixel is the rectangle from all the pixels to the left,
               // plus the trapezoid filled by the line segment in this pixel all the way to the right edge
               scanline[x2] += area + sign * stbtt__position_trapezoid_area(sy1-y_final, (float) x2, x2+1.0f, x_bottom, x2+1.0f);

               // the rest of the line is filled based on the total height of the line segment in this pixel
               scanline_fill[x2] += sign * (sy1-sy0);
            }
         } else {
            // if edge goes outside of box we're drawing, we require
            // clipping logic. since this does not match the intended use
            // of this library, we use a different, very slow brute
            // force implementation
            // note though that this does happen some of the time because
            // x_top and x_bottom can be extrapolated at the top & bottom of
            // the shape and actually lie outside the bounding box
            int x;
            for (x=0; x < len; ++x) {
               // cases:
               //
               // there can be up to two intersections with the pixel. any intersection
               // with left or right edges can be handled by splitting into two (or three)
               // regions. intersections with top & bottom do not necessitate case-wise logic.
               //
               // the old way of doing this found the intersections with the left & right edges,
               // then used some simple logic to produce up to three segments in sorted order
               // from top-to-bottom. however, this had a problem: if an x edge was epsilon
               // across the x border, then the corresponding y position might not be distinct
               // from the other y segment, and it might ignored as an empty segment. to avoid
               // that, we need to explicitly produce segments based on x positions.

               // rename variables to clearly-defined pairs
               float y0 = y_top;
               float x1 = (float) (x);
               float x2 = (float) (x+1);
               float x3 = xb;
               float y3 = y_bottom;

               // x = e->x + e->dx * (y-y_top)
               // (y-y_top) = (x - e->x) / e->dx
               // y = (x - e->x) / e->dx + y_top
               float y1 = (x - x0) / dx + y_top;
               float y2 = (x+1 - x0) / dx + y_top;

               if (x0 < x1 && x3 > x2) {         // three segments descending down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else if (x3 < x1 && x0 > x2) {  // three segments descending down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x0 < x1 && x3 > x1) {  // two segments across x, down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x3 < x1 && x0 > x1) {  // two segments across x, down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x1,y1);
                  stbtt__handle_clipped_edge(scanline,x,e, x1,y1, x3,y3);
               } else if (x0 < x2 && x3 > x2) {  // two segments across x+1, down-right
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else if (x3 < x2 && x0 > x2) {  // two segments across x+1, down-left
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x2,y2);
                  stbtt__handle_clipped_edge(scanline,x,e, x2,y2, x3,y3);
               } else {  // one segment
                  stbtt__handle_clipped_edge(scanline,x,e, x0,y0, x3,y3);
               }
            }
         }
      }
      e = e->next;
   }
}

// directly AA rasterize edges w/o supersampling
static void stbtt__rasterize_sorted_edges(stbtt__bitmap *result, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y, void *userdata)
{
   stbtt__hheap hh = { 0, 0, 0 };
   stbtt__active_edge *active = NULL;
   int y,j=0, i;
   float scanline_data[129], *scanline, *scanline2;

   STBTT__NOTUSED(vsubsample);

   if (result->w > 64)
      scanline = (float *) STBTT_malloc((result->w*2+1) * sizeof(float), userdata);
   else
      scanline = scanline_data;

   scanline2 = scanline + result->w;

   y = off_y;
   e[n].y0 = (float) (off_y + result->h) + 1;

   while (j < result->h) {
      // find center of pixel for this scanline
      float scan_y_top    = y + 0.0f;
      float scan_y_bottom = y + 1.0f;
      stbtt__active_edge **step = &active;

      STBTT_memset(scanline , 0, result->w*sizeof(scanline[0]));
      STBTT_memset(scanline2, 0, (result->w+1)*sizeof(scanline[0]));

      // update all active edges;
      // remove all active edges that terminate before the top of this scanline
      while (*step) {
         stbtt__active_edge * z = *step;
         if (z->ey <= scan_y_top) {
            *step = z->next; // delete from list
            STBTT_assert(z->direction);
            z->direction = 0;
            stbtt__hheap_free(&hh, z);
         } else {
            step = &((*step)->next); // advance through list
         }
      }

      // insert all edges that start before the bottom of this scanline
      while (e->y0 <= scan_y_bottom) {
         if (e->y0 != e->y1) {
            stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y_top, userdata);
            if (z != NULL) {
               if (j == 0 && off_y != 0) {
                  if (z->ey < scan_y_top) {
                     // this can happen due to subpixel positioning and some kind of fp rounding error i think
                     z->ey = scan_y_top;
                  }
               }
               STBTT_assert(z->ey >= scan_y_top); // if we get really unlucky a tiny bit of an edge can be out of bounds
               // insert at front
               z->next = active;
               active = z;
            }
         }
         ++e;
      }

      // now process all active edges
      if (active)
         stbtt__fill_active_edges_new(scanline, scanline2+1, result->w, active, scan_y_top);

      {
         float sum = 0;
         for (i=0; i < result->w; ++i) {
            float k;
            int m;
            sum += scanline2[i];
            k = scanline[i] + sum;
            k = (float) STBTT_fabs(k)*255 + 0.5f;
            m = (int) k;
            if (m > 255) m = 255;
            result->pixels[j*result->stride + i] = (unsigned char) m;
         }
      }
      // advance all the edges
      step = &active;
      while (*step) {
         stbtt__active_edge *z = *step;
         z->fx += z->fdx; // advance to position for current scanline
         step = &((*step)->next); // advance through list
      }

      ++y;
      ++j;
   }

   stbtt__hheap_cleanup(&hh, userdata);

   if (scanline != scanline_data)
      STBTT_free(scanline, userdata);
}
#else
#error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif

#define STBTT__COMPARE(a,b)  ((a)->y0 < (b)->y0)

static void stbtt__sort_edges_ins_sort(stbtt__edge *p, int n)
{
   int i,j;
   for (i=1; i < n; ++i) {
      stbtt__edge t = p[i], *a = &t;
      j = i;
      while (j > 0) {
         stbtt__edge *b = &p[j-1];
         int c = STBTT__COMPARE(a,b);
         if (!c) break;
         p[j] = p[j-1];
         --j;
      }
      if (i != j)
         p[j] = t;
   }
}

static void stbtt__sort_edges_quicksort(stbtt__edge *p, int n)
{
   /* threshold for transitioning to insertion sort */
   while (n > 12) {
      stbtt__edge t;
      int c01,c12,c,m,i,j;

      /* compute median of three */
      m = n >> 1;
      c01 = STBTT__COMPARE(&p[0],&p[m]);
      c12 = STBTT__COMPARE(&p[m],&p[n-1]);
      /* if 0 >= mid >= end, or 0 < mid < end, then use mid */
      if (c01 != c12) {
         /* otherwise, we'll need to swap something else to middle */
         int z;
         c = STBTT__COMPARE(&p[0],&p[n-1]);
         /* 0>mid && mid<n:  0>n => n; 0<n => 0 */
         /* 0<mid && mid>n:  0>n => 0; 0<n => n */
         z = (c == c12) ? 0 : n-1;
         t = p[z];
         p[z] = p[m];
         p[m] = t;
      }
      /* now p[m] is the median-of-three */
      /* swap it to the beginning so it won't move around */
      t = p[0];
      p[0] = p[m];
      p[m] = t;

      /* partition loop */
      i=1;
      j=n-1;
      for(;;) {
         /* handling of equality is crucial here */
         /* for sentinels & efficiency with duplicates */
         for (;;++i) {
            if (!STBTT__COMPARE(&p[i], &p[0])) break;
         }
         for (;;--j) {
            if (!STBTT__COMPARE(&p[0], &p[j])) break;
         }
         /* make sure we haven't crossed */
         if (i >= j) break;
         t = p[i];
         p[i] = p[j];
         p[j] = t;

         ++i;
         --j;
      }
      /* recurse on smaller side, iterate on larger */
      if (j < (n-i)) {
         stbtt__sort_edges_quicksort(p,j);
         p = p+i;
         n = n-i;
      } else {
         stbtt__sort_edges_quicksort(p+i, n-i);
         n = j;
      }
   }
}

static void stbtt__sort_edges(stbtt__edge *p, int n)
{
   stbtt__sort_edges_quicksort(p, n);
   stbtt__sort_edges_ins_sort(p, n);
}

typedef struct
{
   float x,y;
} stbtt__point;

static void stbtt__rasterize(stbtt__bitmap *result, stbtt__point *pts, int *wcount, int windings, float scale_x, float scale_y, float shift_x, float shift_y, int off_x, int off_y, int invert, void *userdata)
{
   float y_scale_inv = invert ? -scale_y : scale_y;
   stbtt__edge *e;
   int n,i,j,k,m;
#if STBTT_RASTERIZER_VERSION == 1
   int vsubsample = result->h < 8 ? 15 : 5;
#elif STBTT_RASTERIZER_VERSION == 2
   int vsubsample = 1;
#else
   #error "Unrecognized value of STBTT_RASTERIZER_VERSION"
#endif
   // vsubsample should divide 255 evenly; otherwise we won't reach full opacity

   // now we have to blow out the windings into explicit edge lists
   n = 0;
   for (i=0; i < windings; ++i)
      n += wcount[i];

   e = (stbtt__edge *) STBTT_malloc(sizeof(*e) * (n+1), userdata); // add an extra one as a sentinel
   if (e == 0) return;
   n = 0;

   m=0;
   for (i=0; i < windings; ++i) {
      stbtt__point *p = pts + m;
      m += wcount[i];
      j = wcount[i]-1;
      for (k=0; k < wcount[i]; j=k++) {
         int a=k,b=j;
         // skip the edge if horizontal
         if (p[j].y == p[k].y)
            continue;
         // add edge from j to k to the list
         e[n].invert = 0;
         if (invert ? p[j].y > p[k].y : p[j].y < p[k].y) {
            e[n].invert = 1;
            a=j,b=k;
         }
         e[n].x0 = p[a].x * scale_x + shift_x;
         e[n].y0 = (p[a].y * y_scale_inv + shift_y) * vsubsample;
         e[n].x1 = p[b].x * scale_x + shift_x;
         e[n].y1 = (p[b].y * y_scale_inv + shift_y) * vsubsample;
         ++n;
      }
   }

   // now sort the edges by their highest point (should snap to integer, and then by x)
   //STBTT_sort(e, n, sizeof(e[0]), stbtt__edge_compare);
   stbtt__sort_edges(e, n);

   // now, traverse the scanlines and find the intersections on each scanline, use xor winding rule
   stbtt__rasterize_sorted_edges(result, e, n, vsubsample, off_x, off_y, userdata);

   STBTT_free(e, userdata);
}

static void stbtt__add_point(stbtt__point *points, int n, float x, float y)
{
   if (!points) return; // during first pass, it's unallocated
   points[n].x = x;
   points[n].y = y;
}

// tessellate until threshold p is happy... @TODO warped to compensate for non-linear stretching
static int stbtt__tesselate_curve(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float objspace_flatness_squared, int n)
{
   // midpoint
   float mx = (x0 + 2*x1 + x2)/4;
   float my = (y0 + 2*y1 + y2)/4;
   // versus directly drawn line
   float dx = (x0+x2)/2 - mx;
   float dy = (y0+y2)/2 - my;
   if (n > 16) // 65536 segments on one curve better be enough!
      return 1;
   if (dx*dx+dy*dy > objspace_flatness_squared) { // half-pixel error allowed... need to be smaller if AA
      stbtt__tesselate_curve(points, num_points, x0,y0, (x0+x1)/2.0f,(y0+y1)/2.0f, mx,my, objspace_flatness_squared,n+1);
      stbtt__tesselate_curve(points, num_points, mx,my, (x1+x2)/2.0f,(y1+y2)/2.0f, x2,y2, objspace_flatness_squared,n+1);
   } else {
      stbtt__add_point(points, *num_points,x2,y2);
      *num_points = *num_points+1;
   }
   return 1;
}

static void stbtt__tesselate_cubic(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float objspace_flatness_squared, int n)
{
   // @TODO this "flatness" calculation is just made-up nonsense that seems to work well enough
   float dx0 = x1-x0;
   float dy0 = y1-y0;
   float dx1 = x2-x1;
   float dy1 = y2-y1;
   float dx2 = x3-x2;
   float dy2 = y3-y2;
   float dx = x3-x0;
   float dy = y3-y0;
   float longlen = (float) (STBTT_sqrt(dx0*dx0+dy0*dy0)+STBTT_sqrt(dx1*dx1+dy1*dy1)+STBTT_sqrt(dx2*dx2+dy2*dy2));
   float shortlen = (float) STBTT_sqrt(dx*dx+dy*dy);
   float flatness_squared = longlen*longlen-shortlen*shortlen;

   if (n > 16) // 65536 segments on one curve better be enough!
      return;

   if (flatness_squared > objspace_flatness_squared) {
      float x01 = (x0+x1)/2;
      float y01 = (y0+y1)/2;
      float x12 = (x1+x2)/2;
      float y12 = (y1+y2)/2;
      float x23 = (x2+x3)/2;
      float y23 = (y2+y3)/2;

      float xa = (x01+x12)/2;
      float ya = (y01+y12)/2;
      float xb = (x12+x23)/2;
      float yb = (y12+y23)/2;

      float mx = (xa+xb)/2;
      float my = (ya+yb)/2;

      stbtt__tesselate_cubic(points, num_points, x0,y0, x01,y01, xa,ya, mx,my, objspace_flatness_squared,n+1);
      stbtt__tesselate_cubic(points, num_points, mx,my, xb,yb, x23,y23, x3,y3, objspace_flatness_squared,n+1);
   } else {
      stbtt__add_point(points, *num_points,x3,y3);
      *num_points = *num_points+1;
   }
}

// returns number of contours
static stbtt__point *stbtt_FlattenCurves(stbtt_vertex *vertices, int num_verts, float objspace_flatness, int **contour_lengths, int *num_contours, void *userdata)
{
   stbtt__point *points=0;
   int num_points=0;

   float objspace_flatness_squared = objspace_flatness * objspace_flatness;
   int i,n=0,start=0, pass;

   // count how many "moves" there are to get the contour count
   for (i=0; i < num_verts; ++i)
      if (vertices[i].type == STBTT_vmove)
         ++n;

   *num_contours = n;
   if (n == 0) return 0;

   *contour_lengths = (int *) STBTT_malloc(sizeof(**contour_lengths) * n, userdata);

   if (*contour_lengths == 0) {
      *num_contours = 0;
      return 0;
   }

   // make two passes through the points so we don't need to realloc
   for (pass=0; pass < 2; ++pass) {
      float x=0,y=0;
      if (pass == 1) {
         points = (stbtt__point *) STBTT_malloc(num_points * sizeof(points[0]), userdata);
         if (points == NULL) goto error;
      }
      num_points = 0;
      n= -1;
      for (i=0; i < num_verts; ++i) {
         switch (vertices[i].type) {
            case STBTT_vmove:
               // start the next contour
               if (n >= 0)
                  (*contour_lengths)[n] = num_points - start;
               ++n;
               start = num_points;

               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x,y);
               break;
            case STBTT_vline:
               x = vertices[i].x, y = vertices[i].y;
               stbtt__add_point(points, num_points++, x, y);
               break;
            case STBTT_vcurve:
               stbtt__tesselate_curve(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
            case STBTT_vcubic:
               stbtt__tesselate_cubic(points, &num_points, x,y,
                                        vertices[i].cx, vertices[i].cy,
                                        vertices[i].cx1, vertices[i].cy1,
                                        vertices[i].x,  vertices[i].y,
                                        objspace_flatness_squared, 0);
               x = vertices[i].x, y = vertices[i].y;
               break;
         }
      }
      (*contour_lengths)[n] = num_points - start;
   }

   return points;
error:
   STBTT_free(points, userdata);
   STBTT_free(*contour_lengths, userdata);
   *contour_lengths = 0;
   *num_contours = 0;
   return NULL;
}

STBTT_DEF void stbtt_Rasterize(stbtt__bitmap *result, float flatness_in_pixels, stbtt_vertex *vertices, int num_verts, float scale_x, float scale_y, float shift_x, float shift_y, int x_off, int y_off, int invert, void *userdata)
{
   float scale            = scale_x > scale_y ? scale_y : scale_x;
   int winding_count      = 0;
   int *winding_lengths   = NULL;
   stbtt__point *windings = stbtt_FlattenCurves(vertices, num_verts, flatness_in_pixels / scale, &winding_lengths, &winding_count, userdata);
   if (windings) {
      stbtt__rasterize(result, windings, winding_lengths, winding_count, scale_x, scale_y, shift_x, shift_y, x_off, y_off, invert, userdata);
      STBTT_free(winding_lengths, userdata);
      STBTT_free(windings, userdata);
   }
}

STBTT_DEF void stbtt_FreeBitmap(unsigned char *bitmap, void *userdata)
{
   STBTT_free(bitmap, userdata);
}

STBTT_DEF unsigned char *stbtt_GetGlyphBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
   int ix0,iy0,ix1,iy1;
   stbtt__bitmap gbm;
   stbtt_vertex *vertices;
   int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);

   if (scale_x == 0) scale_x = scale_y;
   if (scale_y == 0) {
      if (scale_x == 0) {
         STBTT_free(vertices, info->userdata);
         return NULL;
      }
      scale_y = scale_x;
   }

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0,&iy0,&ix1,&iy1);

   // now we get the size
   gbm.w = (ix1 - ix0);
   gbm.h = (iy1 - iy0);
   gbm.pixels = NULL; // in case we error

   if (width ) *width  = gbm.w;
   if (height) *height = gbm.h;
   if (xoff  ) *xoff   = ix0;
   if (yoff  ) *yoff   = iy0;

   if (gbm.w && gbm.h) {
      gbm.pixels = (unsigned char *) STBTT_malloc(gbm.w * gbm.h, info->userdata);
      if (gbm.pixels) {
         gbm.stride = gbm.w;

         stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0, iy0, 1, info->userdata);
      }
   }
   STBTT_free(vertices, info->userdata);
   return gbm.pixels;
}

STBTT_DEF unsigned char *stbtt_GetGlyphBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y, 0.0f, 0.0f, glyph, width, height, xoff, yoff);
}

STBTT_DEF void stbtt_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph)
{
   int ix0,iy0;
   stbtt_vertex *vertices;
   int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);
   stbtt__bitmap gbm;

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0,&iy0,0,0);
   gbm.pixels = output;
   gbm.w = out_w;
   gbm.h = out_h;
   gbm.stride = out_stride;

   if (gbm.w && gbm.h)
      stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0,iy0, 1, info->userdata);

   STBTT_free(vertices, info->userdata);
}

STBTT_DEF void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph)
{
   stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f,0.0f, glyph);
}

STBTT_DEF unsigned char *stbtt_GetCodepointBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y,shift_x,shift_y, stbtt_FindGlyphIndex(info,codepoint), width,height,xoff,yoff);
}

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int codepoint)
{
   stbtt_MakeGlyphBitmapSubpixelPrefilter(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, oversample_x, oversample_y, sub_x, sub_y, stbtt_FindGlyphIndex(info,codepoint));
}

STBTT_DEF void stbtt_MakeCodepointBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint)
{
   stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, stbtt_FindGlyphIndex(info,codepoint));
}

STBTT_DEF unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetCodepointBitmapSubpixel(info, scale_x, scale_y, 0.0f,0.0f, codepoint, width,height,xoff,yoff);
}

STBTT_DEF void stbtt_MakeCodepointBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int codepoint)
{
   stbtt_MakeCodepointBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f,0.0f, codepoint);
}

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-CRAPPY packing to keep source code small

static int stbtt_BakeFontBitmap_internal(unsigned char *data, int offset,  // font location (use offset=0 for plain .ttf)
                                float pixel_height,                     // height of font in pixels
                                unsigned char *pixels, int pw, int ph,  // bitmap to be filled in
                                int first_char, int num_chars,          // characters to bake
                                stbtt_bakedchar *chardata)
{
   float scale;
   int x,y,bottom_y, i;
   stbtt_fontinfo f;
   f.userdata = NULL;
   if (!stbtt_InitFont(&f, data, offset))
      return -1;
   STBTT_memset(pixels, 0, pw*ph); // background of 0 around pixels
   x=y=1;
   bottom_y = 1;

   scale = stbtt_ScaleForPixelHeight(&f, pixel_height);

   for (i=0; i < num_chars; ++i) {
      int advance, lsb, x0,y0,x1,y1,gw,gh;
      int g = stbtt_FindGlyphIndex(&f, first_char + i);
      stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
      stbtt_GetGlyphBitmapBox(&f, g, scale,scale, &x0,&y0,&x1,&y1);
      gw = x1-x0;
      gh = y1-y0;
      if (x + gw + 1 >= pw)
         y = bottom_y, x = 1; // advance to next row
      if (y + gh + 1 >= ph) // check if it fits vertically AFTER potentially moving to next row
         return -i;
      STBTT_assert(x+gw < pw);
      STBTT_assert(y+gh < ph);
      stbtt_MakeGlyphBitmap(&f, pixels+x+y*pw, gw,gh,pw, scale,scale, g);
      chardata[i].x0 = (stbtt_int16) x;
      chardata[i].y0 = (stbtt_int16) y;
      chardata[i].x1 = (stbtt_int16) (x + gw);
      chardata[i].y1 = (stbtt_int16) (y + gh);
      chardata[i].xadvance = scale * advance;
      chardata[i].xoff     = (float) x0;
      chardata[i].yoff     = (float) y0;
      x = x + gw + 1;
      if (y+gh+1 > bottom_y)
         bottom_y = y+gh+1;
   }
   return bottom_y;
}

STBTT_DEF void stbtt_GetBakedQuad(const stbtt_bakedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int opengl_fillrule)
{
   float d3d_bias = opengl_fillrule ? 0 : -0.5f;
   float ipw = 1.0f / pw, iph = 1.0f / ph;
   const stbtt_bakedchar *b = chardata + char_index;
   int round_x = STBTT_ifloor((*xpos + b->xoff) + 0.5f);
   int round_y = STBTT_ifloor((*ypos + b->yoff) + 0.5f);

   q->x0 = round_x + d3d_bias;
   q->y0 = round_y + d3d_bias;
   q->x1 = round_x + b->x1 - b->x0 + d3d_bias;
   q->y1 = round_y + b->y1 - b->y0 + d3d_bias;

   q->s0 = b->x0 * ipw;
   q->t0 = b->y0 * iph;
   q->s1 = b->x1 * ipw;
   q->t1 = b->y1 * iph;

   *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// rectangle packing replacement routines if you don't have stb_rect_pack.h
//

#ifndef STB_RECT_PACK_VERSION

typedef int stbrp_coord;

////////////////////////////////////////////////////////////////////////////////////
//                                                                                //
//                                                                                //
// COMPILER WARNING ?!?!?                                                         //
//                                                                                //
//                                                                                //
// if you get a compile warning due to these symbols being defined more than      //
// once, move #include "stb_rect_pack.h" before #include "stb_truetype.h"         //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
   int width,height;
   int x,y,bottom_y;
} stbrp_context;

typedef struct
{
   unsigned char x;
} stbrp_node;

struct stbrp_rect
{
   stbrp_coord x,y;
   int id,w,h,was_packed;
};

static void stbrp_init_target(stbrp_context *con, int pw, int ph, stbrp_node *nodes, int num_nodes)
{
   con->width  = pw;
   con->height = ph;
   con->x = 0;
   con->y = 0;
   con->bottom_y = 0;
   STBTT__NOTUSED(nodes);
   STBTT__NOTUSED(num_nodes);
}

static void stbrp_pack_rects(stbrp_context *con, stbrp_rect *rects, int num_rects)
{
   int i;
   for (i=0; i < num_rects; ++i) {
      if (con->x + rects[i].w > con->width) {
         con->x = 0;
         con->y = con->bottom_y;
      }
      if (con->y + rects[i].h > con->height)
         break;
      rects[i].x = con->x;
      rects[i].y = con->y;
      rects[i].was_packed = 1;
      con->x += rects[i].w;
      if (con->y + rects[i].h > con->bottom_y)
         con->bottom_y = con->y + rects[i].h;
   }
   for (   ; i < num_rects; ++i)
      rects[i].was_packed = 0;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-AWESOME (tm Ryan Gordon) packing using stb_rect_pack.h. If
// stb_rect_pack.h isn't available, it uses the BakeFontBitmap strategy.

STBTT_DEF int stbtt_PackBegin(stbtt_pack_context *spc, unsigned char *pixels, int pw, int ph, int stride_in_bytes, int padding, void *alloc_context)
{
   stbrp_context *context = (stbrp_context *) STBTT_malloc(sizeof(*context)            ,alloc_context);
   int            num_nodes = pw - padding;
   stbrp_node    *nodes   = (stbrp_node    *) STBTT_malloc(sizeof(*nodes  ) * num_nodes,alloc_context);

   if (context == NULL || nodes == NULL) {
      if (context != NULL) STBTT_free(context, alloc_context);
      if (nodes   != NULL) STBTT_free(nodes  , alloc_context);
      return 0;
   }

   spc->user_allocator_context = alloc_context;
   spc->width = pw;
   spc->height = ph;
   spc->pixels = pixels;
   spc->pack_info = context;
   spc->nodes = nodes;
   spc->padding = padding;
   spc->stride_in_bytes = stride_in_bytes != 0 ? stride_in_bytes : pw;
   spc->h_oversample = 1;
   spc->v_oversample = 1;
   spc->skip_missing = 0;

   stbrp_init_target(context, pw-padding, ph-padding, nodes, num_nodes);

   if (pixels)
      STBTT_memset(pixels, 0, pw*ph); // background of 0 around pixels

   return 1;
}

STBTT_DEF void stbtt_PackEnd  (stbtt_pack_context *spc)
{
   STBTT_free(spc->nodes    , spc->user_allocator_context);
   STBTT_free(spc->pack_info, spc->user_allocator_context);
}

STBTT_DEF void stbtt_PackSetOversampling(stbtt_pack_context *spc, unsigned int h_oversample, unsigned int v_oversample)
{
   STBTT_assert(h_oversample <= STBTT_MAX_OVERSAMPLE);
   STBTT_assert(v_oversample <= STBTT_MAX_OVERSAMPLE);
   if (h_oversample <= STBTT_MAX_OVERSAMPLE)
      spc->h_oversample = h_oversample;
   if (v_oversample <= STBTT_MAX_OVERSAMPLE)
      spc->v_oversample = v_oversample;
}

STBTT_DEF void stbtt_PackSetSkipMissingCodepoints(stbtt_pack_context *spc, int skip)
{
   spc->skip_missing = skip;
}

#define STBTT__OVER_MASK  (STBTT_MAX_OVERSAMPLE-1)

static void stbtt__h_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
   unsigned char buffer[STBTT_MAX_OVERSAMPLE];
   int safe_w = w - kernel_width;
   int j;
   STBTT_memset(buffer, 0, STBTT_MAX_OVERSAMPLE); // suppress bogus warning from VS2013 -analyze
   for (j=0; j < h; ++j) {
      int i;
      unsigned int total;
      STBTT_memset(buffer, 0, kernel_width);

      total = 0;

      // make kernel_width a constant in common cases so compiler can optimize out the divide
      switch (kernel_width) {
         case 2:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 2);
            }
            break;
         case 3:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 3);
            }
            break;
         case 4:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 4);
            }
            break;
         case 5:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / 5);
            }
            break;
         default:
            for (i=0; i <= safe_w; ++i) {
               total += pixels[i] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i];
               pixels[i] = (unsigned char) (total / kernel_width);
            }
            break;
      }

      for (; i < w; ++i) {
         STBTT_assert(pixels[i] == 0);
         total -= buffer[i & STBTT__OVER_MASK];
         pixels[i] = (unsigned char) (total / kernel_width);
      }

      pixels += stride_in_bytes;
   }
}

static void stbtt__v_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
   unsigned char buffer[STBTT_MAX_OVERSAMPLE];
   int safe_h = h - kernel_width;
   int j;
   STBTT_memset(buffer, 0, STBTT_MAX_OVERSAMPLE); // suppress bogus warning from VS2013 -analyze
   for (j=0; j < w; ++j) {
      int i;
      unsigned int total;
      STBTT_memset(buffer, 0, kernel_width);

      total = 0;

      // make kernel_width a constant in common cases so compiler can optimize out the divide
      switch (kernel_width) {
         case 2:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 2);
            }
            break;
         case 3:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 3);
            }
            break;
         case 4:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 4);
            }
            break;
         case 5:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / 5);
            }
            break;
         default:
            for (i=0; i <= safe_h; ++i) {
               total += pixels[i*stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
               buffer[(i+kernel_width) & STBTT__OVER_MASK] = pixels[i*stride_in_bytes];
               pixels[i*stride_in_bytes] = (unsigned char) (total / kernel_width);
            }
            break;
      }

      for (; i < h; ++i) {
         STBTT_assert(pixels[i*stride_in_bytes] == 0);
         total -= buffer[i & STBTT__OVER_MASK];
         pixels[i*stride_in_bytes] = (unsigned char) (total / kernel_width);
      }

      pixels += 1;
   }
}

static float stbtt__oversample_shift(int oversample)
{
   if (!oversample)
      return 0.0f;

   // The prefilter is a box filter of width "oversample",
   // which shifts phase by (oversample - 1)/2 pixels in
   // oversampled space. We want to shift in the opposite
   // direction to counter this.
   return (float)-(oversample - 1) / (2.0f * (float)oversample);
}

// rects array must be big enough to accommodate all characters in the given ranges
STBTT_DEF int stbtt_PackFontRangesGatherRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects)
{
   int i,j,k;
   int missing_glyph_added = 0;

   k=0;
   for (i=0; i < num_ranges; ++i) {
      float fh = ranges[i].font_size;
      float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
      ranges[i].h_oversample = (unsigned char) spc->h_oversample;
      ranges[i].v_oversample = (unsigned char) spc->v_oversample;
      for (j=0; j < ranges[i].num_chars; ++j) {
         int x0,y0,x1,y1;
         int codepoint = ranges[i].array_of_unicode_codepoints == NULL ? ranges[i].first_unicode_codepoint_in_range + j : ranges[i].array_of_unicode_codepoints[j];
         int glyph = stbtt_FindGlyphIndex(info, codepoint);
         if (glyph == 0 && (spc->skip_missing || missing_glyph_added)) {
            rects[k].w = rects[k].h = 0;
         } else {
            stbtt_GetGlyphBitmapBoxSubpixel(info,glyph,
                                            scale * spc->h_oversample,
                                            scale * spc->v_oversample,
                                            0,0,
                                            &x0,&y0,&x1,&y1);
            rects[k].w = (stbrp_coord) (x1-x0 + spc->padding + spc->h_oversample-1);
            rects[k].h = (stbrp_coord) (y1-y0 + spc->padding + spc->v_oversample-1);
            if (glyph == 0)
               missing_glyph_added = 1;
         }
         ++k;
      }
   }

   return k;
}

STBTT_DEF void stbtt_MakeGlyphBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int prefilter_x, int prefilter_y, float *sub_x, float *sub_y, int glyph)
{
   stbtt_MakeGlyphBitmapSubpixel(info,
                                 output,
                                 out_w - (prefilter_x - 1),
                                 out_h - (prefilter_y - 1),
                                 out_stride,
                                 scale_x,
                                 scale_y,
                                 shift_x,
                                 shift_y,
                                 glyph);

   if (prefilter_x > 1)
      stbtt__h_prefilter(output, out_w, out_h, out_stride, prefilter_x);

   if (prefilter_y > 1)
      stbtt__v_prefilter(output, out_w, out_h, out_stride, prefilter_y);

   *sub_x = stbtt__oversample_shift(prefilter_x);
   *sub_y = stbtt__oversample_shift(prefilter_y);
}

// rects array must be big enough to accommodate all characters in the given ranges
STBTT_DEF int stbtt_PackFontRangesRenderIntoRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *ranges, int num_ranges, stbrp_rect *rects)
{
   int i,j,k, missing_glyph = -1, return_value = 1;

   // save current values
   int old_h_over = spc->h_oversample;
   int old_v_over = spc->v_oversample;

   k = 0;
   for (i=0; i < num_ranges; ++i) {
      float fh = ranges[i].font_size;
      float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
      float recip_h,recip_v,sub_x,sub_y;
      spc->h_oversample = ranges[i].h_oversample;
      spc->v_oversample = ranges[i].v_oversample;
      recip_h = 1.0f / spc->h_oversample;
      recip_v = 1.0f / spc->v_oversample;
      sub_x = stbtt__oversample_shift(spc->h_oversample);
      sub_y = stbtt__oversample_shift(spc->v_oversample);
      for (j=0; j < ranges[i].num_chars; ++j) {
         stbrp_rect *r = &rects[k];
         if (r->was_packed && r->w != 0 && r->h != 0) {
            stbtt_packedchar *bc = &ranges[i].chardata_for_range[j];
            int advance, lsb, x0,y0,x1,y1;
            int codepoint = ranges[i].array_of_unicode_codepoints == NULL ? ranges[i].first_unicode_codepoint_in_range + j : ranges[i].array_of_unicode_codepoints[j];
            int glyph = stbtt_FindGlyphIndex(info, codepoint);
            stbrp_coord pad = (stbrp_coord) spc->padding;

            // pad on left and top
            r->x += pad;
            r->y += pad;
            r->w -= pad;
            r->h -= pad;
            stbtt_GetGlyphHMetrics(info, glyph, &advance, &lsb);
            stbtt_GetGlyphBitmapBox(info, glyph,
                                    scale * spc->h_oversample,
                                    scale * spc->v_oversample,
                                    &x0,&y0,&x1,&y1);
            stbtt_MakeGlyphBitmapSubpixel(info,
                                          spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                          r->w - spc->h_oversample+1,
                                          r->h - spc->v_oversample+1,
                                          spc->stride_in_bytes,
                                          scale * spc->h_oversample,
                                          scale * spc->v_oversample,
                                          0,0,
                                          glyph);

            if (spc->h_oversample > 1)
               stbtt__h_prefilter(spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                  r->w, r->h, spc->stride_in_bytes,
                                  spc->h_oversample);

            if (spc->v_oversample > 1)
               stbtt__v_prefilter(spc->pixels + r->x + r->y*spc->stride_in_bytes,
                                  r->w, r->h, spc->stride_in_bytes,
                                  spc->v_oversample);

            bc->x0       = (stbtt_int16)  r->x;
            bc->y0       = (stbtt_int16)  r->y;
            bc->x1       = (stbtt_int16) (r->x + r->w);
            bc->y1       = (stbtt_int16) (r->y + r->h);
            bc->xadvance =                scale * advance;
            bc->xoff     =       (float)  x0 * recip_h + sub_x;
            bc->yoff     =       (float)  y0 * recip_v + sub_y;
            bc->xoff2    =                (x0 + r->w) * recip_h + sub_x;
            bc->yoff2    =                (y0 + r->h) * recip_v + sub_y;

            if (glyph == 0)
               missing_glyph = j;
         } else if (spc->skip_missing) {
            return_value = 0;
         } else if (r->was_packed && r->w == 0 && r->h == 0 && missing_glyph >= 0) {
            ranges[i].chardata_for_range[j] = ranges[i].chardata_for_range[missing_glyph];
         } else {
            return_value = 0; // if any fail, report failure
         }

         ++k;
      }
   }

   // restore original values
   spc->h_oversample = old_h_over;
   spc->v_oversample = old_v_over;

   return return_value;
}

STBTT_DEF void stbtt_PackFontRangesPackRects(stbtt_pack_context *spc, stbrp_rect *rects, int num_rects)
{
   stbrp_pack_rects((stbrp_context *) spc->pack_info, rects, num_rects);
}

STBTT_DEF int stbtt_PackFontRanges(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, stbtt_pack_range *ranges, int num_ranges)
{
   stbtt_fontinfo info;
   int i,j,n, return_value = 1;
   //stbrp_context *context = (stbrp_context *) spc->pack_info;
   stbrp_rect    *rects;

   // flag all characters as NOT packed
   for (i=0; i < num_ranges; ++i)
      for (j=0; j < ranges[i].num_chars; ++j)
         ranges[i].chardata_for_range[j].x0 =
         ranges[i].chardata_for_range[j].y0 =
         ranges[i].chardata_for_range[j].x1 =
         ranges[i].chardata_for_range[j].y1 = 0;

   n = 0;
   for (i=0; i < num_ranges; ++i)
      n += ranges[i].num_chars;

   rects = (stbrp_rect *) STBTT_malloc(sizeof(*rects) * n, spc->user_allocator_context);
   if (rects == NULL)
      return 0;

   info.userdata = spc->user_allocator_context;
   stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata,font_index));

   n = stbtt_PackFontRangesGatherRects(spc, &info, ranges, num_ranges, rects);

   stbtt_PackFontRangesPackRects(spc, rects, n);

   return_value = stbtt_PackFontRangesRenderIntoRects(spc, &info, ranges, num_ranges, rects);

   STBTT_free(rects, spc->user_allocator_context);
   return return_value;
}

STBTT_DEF int stbtt_PackFontRange(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, float font_size,
            int first_unicode_codepoint_in_range, int num_chars_in_range, stbtt_packedchar *chardata_for_range)
{
   stbtt_pack_range range;
   range.first_unicode_codepoint_in_range = first_unicode_codepoint_in_range;
   range.array_of_unicode_codepoints = NULL;
   range.num_chars                   = num_chars_in_range;
   range.chardata_for_range          = chardata_for_range;
   range.font_size                   = font_size;
   return stbtt_PackFontRanges(spc, fontdata, font_index, &range, 1);
}

STBTT_DEF void stbtt_GetScaledFontVMetrics(const unsigned char *fontdata, int index, float size, float *ascent, float *descent, float *lineGap)
{
   int i_ascent, i_descent, i_lineGap;
   float scale;
   stbtt_fontinfo info;
   stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata, index));
   scale = size > 0 ? stbtt_ScaleForPixelHeight(&info, size) : stbtt_ScaleForMappingEmToPixels(&info, -size);
   stbtt_GetFontVMetrics(&info, &i_ascent, &i_descent, &i_lineGap);
   *ascent  = (float) i_ascent  * scale;
   *descent = (float) i_descent * scale;
   *lineGap = (float) i_lineGap * scale;
}

STBTT_DEF void stbtt_GetPackedQuad(const stbtt_packedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int align_to_integer)
{
   float ipw = 1.0f / pw, iph = 1.0f / ph;
   const stbtt_packedchar *b = chardata + char_index;

   if (align_to_integer) {
      float x = (float) STBTT_ifloor((*xpos + b->xoff) + 0.5f);
      float y = (float) STBTT_ifloor((*ypos + b->yoff) + 0.5f);
      q->x0 = x;
      q->y0 = y;
      q->x1 = x + b->xoff2 - b->xoff;
      q->y1 = y + b->yoff2 - b->yoff;
   } else {
      q->x0 = *xpos + b->xoff;
      q->y0 = *ypos + b->yoff;
      q->x1 = *xpos + b->xoff2;
      q->y1 = *ypos + b->yoff2;
   }

   q->s0 = b->x0 * ipw;
   q->t0 = b->y0 * iph;
   q->s1 = b->x1 * ipw;
   q->t1 = b->y1 * iph;

   *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// sdf computation
//

#define STBTT_min(a,b)  ((a) < (b) ? (a) : (b))
#define STBTT_max(a,b)  ((a) < (b) ? (b) : (a))

static int stbtt__ray_intersect_bezier(float orig[2], float ray[2], float q0[2], float q1[2], float q2[2], float hits[2][2])
{
   float q0perp = q0[1]*ray[0] - q0[0]*ray[1];
   float q1perp = q1[1]*ray[0] - q1[0]*ray[1];
   float q2perp = q2[1]*ray[0] - q2[0]*ray[1];
   float roperp = orig[1]*ray[0] - orig[0]*ray[1];

   float a = q0perp - 2*q1perp + q2perp;
   float b = q1perp - q0perp;
   float c = q0perp - roperp;

   float s0 = 0., s1 = 0.;
   int num_s = 0;

   if (a != 0.0) {
      float discr = b*b - a*c;
      if (discr > 0.0) {
         float rcpna = -1 / a;
         float d = (float) STBTT_sqrt(discr);
         s0 = (b+d) * rcpna;
         s1 = (b-d) * rcpna;
         if (s0 >= 0.0 && s0 <= 1.0)
            num_s = 1;
         if (d > 0.0 && s1 >= 0.0 && s1 <= 1.0) {
            if (num_s == 0) s0 = s1;
            ++num_s;
         }
      }
   } else {
      // 2*b*s + c = 0
      // s = -c / (2*b)
      s0 = c / (-2 * b);
      if (s0 >= 0.0 && s0 <= 1.0)
         num_s = 1;
   }

   if (num_s == 0)
      return 0;
   else {
      float rcp_len2 = 1 / (ray[0]*ray[0] + ray[1]*ray[1]);
      float rayn_x = ray[0] * rcp_len2, rayn_y = ray[1] * rcp_len2;

      float q0d =   q0[0]*rayn_x +   q0[1]*rayn_y;
      float q1d =   q1[0]*rayn_x +   q1[1]*rayn_y;
      float q2d =   q2[0]*rayn_x +   q2[1]*rayn_y;
      float rod = orig[0]*rayn_x + orig[1]*rayn_y;

      float q10d = q1d - q0d;
      float q20d = q2d - q0d;
      float q0rd = q0d - rod;

      hits[0][0] = q0rd + s0*(2.0f - 2.0f*s0)*q10d + s0*s0*q20d;
      hits[0][1] = a*s0+b;

      if (num_s > 1) {
         hits[1][0] = q0rd + s1*(2.0f - 2.0f*s1)*q10d + s1*s1*q20d;
         hits[1][1] = a*s1+b;
         return 2;
      } else {
         return 1;
      }
   }
}

static int equal(float *a, float *b)
{
   return (a[0] == b[0] && a[1] == b[1]);
}

static int stbtt__compute_crossings_x(float x, float y, int nverts, stbtt_vertex *verts)
{
   int i;
   float orig[2], ray[2] = { 1, 0 };
   float y_frac;
   int winding = 0;

   // make sure y never passes through a vertex of the shape
   y_frac = (float) STBTT_fmod(y, 1.0f);
   if (y_frac < 0.01f)
      y += 0.01f;
   else if (y_frac > 0.99f)
      y -= 0.01f;

   orig[0] = x;
   orig[1] = y;

   // test a ray from (-infinity,y) to (x,y)
   for (i=0; i < nverts; ++i) {
      if (verts[i].type == STBTT_vline) {
         int x0 = (int) verts[i-1].x, y0 = (int) verts[i-1].y;
         int x1 = (int) verts[i  ].x, y1 = (int) verts[i  ].y;
         if (y > STBTT_min(y0,y1) && y < STBTT_max(y0,y1) && x > STBTT_min(x0,x1)) {
            float x_inter = (y - y0) / (y1 - y0) * (x1-x0) + x0;
            if (x_inter < x)
               winding += (y0 < y1) ? 1 : -1;
         }
      }
      if (verts[i].type == STBTT_vcurve) {
         int x0 = (int) verts[i-1].x , y0 = (int) verts[i-1].y ;
         int x1 = (int) verts[i  ].cx, y1 = (int) verts[i  ].cy;
         int x2 = (int) verts[i  ].x , y2 = (int) verts[i  ].y ;
         int ax = STBTT_min(x0,STBTT_min(x1,x2)), ay = STBTT_min(y0,STBTT_min(y1,y2));
         int by = STBTT_max(y0,STBTT_max(y1,y2));
         if (y > ay && y < by && x > ax) {
            float q0[2],q1[2],q2[2];
            float hits[2][2];
            q0[0] = (float)x0;
            q0[1] = (float)y0;
            q1[0] = (float)x1;
            q1[1] = (float)y1;
            q2[0] = (float)x2;
            q2[1] = (float)y2;
            if (equal(q0,q1) || equal(q1,q2)) {
               x0 = (int)verts[i-1].x;
               y0 = (int)verts[i-1].y;
               x1 = (int)verts[i  ].x;
               y1 = (int)verts[i  ].y;
               if (y > STBTT_min(y0,y1) && y < STBTT_max(y0,y1) && x > STBTT_min(x0,x1)) {
                  float x_inter = (y - y0) / (y1 - y0) * (x1-x0) + x0;
                  if (x_inter < x)
                     winding += (y0 < y1) ? 1 : -1;
               }
            } else {
               int num_hits = stbtt__ray_intersect_bezier(orig, ray, q0, q1, q2, hits);
               if (num_hits >= 1)
                  if (hits[0][0] < 0)
                     winding += (hits[0][1] < 0 ? -1 : 1);
               if (num_hits >= 2)
                  if (hits[1][0] < 0)
                     winding += (hits[1][1] < 0 ? -1 : 1);
            }
         }
      }
   }
   return winding;
}

static float stbtt__cuberoot( float x )
{
   if (x<0)
      return -(float) STBTT_pow(-x,1.0f/3.0f);
   else
      return  (float) STBTT_pow( x,1.0f/3.0f);
}

// x^3 + a*x^2 + b*x + c = 0
static int stbtt__solve_cubic(float a, float b, float c, float* r)
{
   float s = -a / 3;
   float p = b - a*a / 3;
   float q = a * (2*a*a - 9*b) / 27 + c;
   float p3 = p*p*p;
   float d = q*q + 4*p3 / 27;
   if (d >= 0) {
      float z = (float) STBTT_sqrt(d);
      float u = (-q + z) / 2;
      float v = (-q - z) / 2;
      u = stbtt__cuberoot(u);
      v = stbtt__cuberoot(v);
      r[0] = s + u + v;
      return 1;
   } else {
      float u = (float) STBTT_sqrt(-p/3);
      float v = (float) STBTT_acos(-STBTT_sqrt(-27/p3) * q / 2) / 3; // p3 must be negative, since d is negative
      float m = (float) STBTT_cos(v);
      float n = (float) STBTT_cos(v-3.141592/2)*1.732050808f;
      r[0] = s + u * 2 * m;
      r[1] = s - u * (m + n);
      r[2] = s - u * (m - n);

      //STBTT_assert( STBTT_fabs(((r[0]+a)*r[0]+b)*r[0]+c) < 0.05f);  // these asserts may not be safe at all scales, though they're in bezier t parameter units so maybe?
      //STBTT_assert( STBTT_fabs(((r[1]+a)*r[1]+b)*r[1]+c) < 0.05f);
      //STBTT_assert( STBTT_fabs(((r[2]+a)*r[2]+b)*r[2]+c) < 0.05f);
      return 3;
   }
}

STBTT_DEF unsigned char * stbtt_GetGlyphSDF(const stbtt_fontinfo *info, float scale, int glyph, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff)
{
   float scale_x = scale, scale_y = scale;
   int ix0,iy0,ix1,iy1;
   int w,h;
   unsigned char *data;

   if (scale == 0) return NULL;

   stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale, scale, 0.0f,0.0f, &ix0,&iy0,&ix1,&iy1);

   // if empty, return NULL
   if (ix0 == ix1 || iy0 == iy1)
      return NULL;

   ix0 -= padding;
   iy0 -= padding;
   ix1 += padding;
   iy1 += padding;

   w = (ix1 - ix0);
   h = (iy1 - iy0);

   if (width ) *width  = w;
   if (height) *height = h;
   if (xoff  ) *xoff   = ix0;
   if (yoff  ) *yoff   = iy0;

   // invert for y-downwards bitmaps
   scale_y = -scale_y;

   {
      // distance from singular values (in the same units as the pixel grid)
      const float eps = 1./1024, eps2 = eps*eps;
      int x,y,i,j;
      float *precompute;
      stbtt_vertex *verts;
      int num_verts = stbtt_GetGlyphShape(info, glyph, &verts);
      data = (unsigned char *) STBTT_malloc(w * h, info->userdata);
      precompute = (float *) STBTT_malloc(num_verts * sizeof(float), info->userdata);

      for (i=0,j=num_verts-1; i < num_verts; j=i++) {
         if (verts[i].type == STBTT_vline) {
            float x0 = verts[i].x*scale_x, y0 = verts[i].y*scale_y;
            float x1 = verts[j].x*scale_x, y1 = verts[j].y*scale_y;
            float dist = (float) STBTT_sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
            precompute[i] = (dist < eps) ? 0.0f : 1.0f / dist;
         } else if (verts[i].type == STBTT_vcurve) {
            float x2 = verts[j].x *scale_x, y2 = verts[j].y *scale_y;
            float x1 = verts[i].cx*scale_x, y1 = verts[i].cy*scale_y;
            float x0 = verts[i].x *scale_x, y0 = verts[i].y *scale_y;
            float bx = x0 - 2*x1 + x2, by = y0 - 2*y1 + y2;
            float len2 = bx*bx + by*by;
            if (len2 >= eps2)
               precompute[i] = 1.0f / len2;
            else
               precompute[i] = 0.0f;
         } else
            precompute[i] = 0.0f;
      }

      for (y=iy0; y < iy1; ++y) {
         for (x=ix0; x < ix1; ++x) {
            float val;
            float min_dist = 999999.0f;
            float sx = (float) x + 0.5f;
            float sy = (float) y + 0.5f;
            float x_gspace = (sx / scale_x);
            float y_gspace = (sy / scale_y);

            int winding = stbtt__compute_crossings_x(x_gspace, y_gspace, num_verts, verts); // @OPTIMIZE: this could just be a rasterization, but needs to be line vs. non-tesselated curves so a new path

            for (i=0; i < num_verts; ++i) {
               float x0 = verts[i].x*scale_x, y0 = verts[i].y*scale_y;

               if (verts[i].type == STBTT_vline && precompute[i] != 0.0f) {
                  float x1 = verts[i-1].x*scale_x, y1 = verts[i-1].y*scale_y;

                  float dist,dist2 = (x0-sx)*(x0-sx) + (y0-sy)*(y0-sy);
                  if (dist2 < min_dist*min_dist)
                     min_dist = (float) STBTT_sqrt(dist2);

                  // coarse culling against bbox
                  //if (sx > STBTT_min(x0,x1)-min_dist && sx < STBTT_max(x0,x1)+min_dist &&
                  //    sy > STBTT_min(y0,y1)-min_dist && sy < STBTT_max(y0,y1)+min_dist)
                  dist = (float) STBTT_fabs((x1-x0)*(y0-sy) - (y1-y0)*(x0-sx)) * precompute[i];
                  STBTT_assert(i != 0);
                  if (dist < min_dist) {
                     // check position along line
                     // x' = x0 + t*(x1-x0), y' = y0 + t*(y1-y0)
                     // minimize (x'-sx)*(x'-sx)+(y'-sy)*(y'-sy)
                     float dx = x1-x0, dy = y1-y0;
                     float px = x0-sx, py = y0-sy;
                     // minimize (px+t*dx)^2 + (py+t*dy)^2 = px*px + 2*px*dx*t + t^2*dx*dx + py*py + 2*py*dy*t + t^2*dy*dy
                     // derivative: 2*px*dx + 2*py*dy + (2*dx*dx+2*dy*dy)*t, set to 0 and solve
                     float t = -(px*dx + py*dy) / (dx*dx + dy*dy);
                     if (t >= 0.0f && t <= 1.0f)
                        min_dist = dist;
                  }
               } else if (verts[i].type == STBTT_vcurve) {
                  float x2 = verts[i-1].x *scale_x, y2 = verts[i-1].y *scale_y;
                  float x1 = verts[i  ].cx*scale_x, y1 = verts[i  ].cy*scale_y;
                  float box_x0 = STBTT_min(STBTT_min(x0,x1),x2);
                  float box_y0 = STBTT_min(STBTT_min(y0,y1),y2);
                  float box_x1 = STBTT_max(STBTT_max(x0,x1),x2);
                  float box_y1 = STBTT_max(STBTT_max(y0,y1),y2);
                  // coarse culling against bbox to avoid computing cubic unnecessarily
                  if (sx > box_x0-min_dist && sx < box_x1+min_dist && sy > box_y0-min_dist && sy < box_y1+min_dist) {
                     int num=0;
                     float ax = x1-x0, ay = y1-y0;
                     float bx = x0 - 2*x1 + x2, by = y0 - 2*y1 + y2;
                     float mx = x0 - sx, my = y0 - sy;
                     float res[3] = {0.f,0.f,0.f};
                     float px,py,t,it,dist2;
                     float a_inv = precompute[i];
                     if (a_inv == 0.0) { // if a_inv is 0, it's 2nd degree so use quadratic formula
                        float a = 3*(ax*bx + ay*by);
                        float b = 2*(ax*ax + ay*ay) + (mx*bx+my*by);
                        float c = mx*ax+my*ay;
                        if (STBTT_fabs(a) < eps2) { // if a is 0, it's linear
                           if (STBTT_fabs(b) >= eps2) {
                              res[num++] = -c/b;
                           }
                        } else {
                           float discriminant = b*b - 4*a*c;
                           if (discriminant < 0)
                              num = 0;
                           else {
                              float root = (float) STBTT_sqrt(discriminant);
                              res[0] = (-b - root)/(2*a);
                              res[1] = (-b + root)/(2*a);
                              num = 2; // don't bother distinguishing 1-solution case, as code below will still work
                           }
                        }
                     } else {
                        float b = 3*(ax*bx + ay*by) * a_inv; // could precompute this as it doesn't depend on sample point
                        float c = (2*(ax*ax + ay*ay) + (mx*bx+my*by)) * a_inv;
                        float d = (mx*ax+my*ay) * a_inv;
                        num = stbtt__solve_cubic(b, c, d, res);
                     }
                     dist2 = (x0-sx)*(x0-sx) + (y0-sy)*(y0-sy);
                     if (dist2 < min_dist*min_dist)
                        min_dist = (float) STBTT_sqrt(dist2);

                     if (num >= 1 && res[0] >= 0.0f && res[0] <= 1.0f) {
                        t = res[0], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                     if (num >= 2 && res[1] >= 0.0f && res[1] <= 1.0f) {
                        t = res[1], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                     if (num >= 3 && res[2] >= 0.0f && res[2] <= 1.0f) {
                        t = res[2], it = 1.0f - t;
                        px = it*it*x0 + 2*t*it*x1 + t*t*x2;
                        py = it*it*y0 + 2*t*it*y1 + t*t*y2;
                        dist2 = (px-sx)*(px-sx) + (py-sy)*(py-sy);
                        if (dist2 < min_dist * min_dist)
                           min_dist = (float) STBTT_sqrt(dist2);
                     }
                  }
               }
            }
            if (winding == 0)
               min_dist = -min_dist;  // if outside the shape, value is negative
            val = onedge_value + pixel_dist_scale * min_dist;
            if (val < 0)
               val = 0;
            else if (val > 255)
               val = 255;
            data[(y-iy0)*w+(x-ix0)] = (unsigned char) val;
         }
      }
      STBTT_free(precompute, info->userdata);
      STBTT_free(verts, info->userdata);
   }
   return data;
}

STBTT_DEF unsigned char * stbtt_GetCodepointSDF(const stbtt_fontinfo *info, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff)
{
   return stbtt_GetGlyphSDF(info, scale, stbtt_FindGlyphIndex(info, codepoint), padding, onedge_value, pixel_dist_scale, width, height, xoff, yoff);
}

STBTT_DEF void stbtt_FreeSDF(unsigned char *bitmap, void *userdata)
{
   STBTT_free(bitmap, userdata);
}

//////////////////////////////////////////////////////////////////////////////
//
// font name matching -- recommended not to use this
//

// check if a utf8 string contains a prefix which is the utf16 string; if so return length of matching utf8 string
static stbtt_int32 stbtt__CompareUTF8toUTF16_bigendian_prefix(stbtt_uint8 *s1, stbtt_int32 len1, stbtt_uint8 *s2, stbtt_int32 len2)
{
   stbtt_int32 i=0;

   // convert utf16 to utf8 and compare the results while converting
   while (len2) {
      stbtt_uint16 ch = s2[0]*256 + s2[1];
      if (ch < 0x80) {
         if (i >= len1) return -1;
         if (s1[i++] != ch) return -1;
      } else if (ch < 0x800) {
         if (i+1 >= len1) return -1;
         if (s1[i++] != 0xc0 + (ch >> 6)) return -1;
         if (s1[i++] != 0x80 + (ch & 0x3f)) return -1;
      } else if (ch >= 0xd800 && ch < 0xdc00) {
         stbtt_uint32 c;
         stbtt_uint16 ch2 = s2[2]*256 + s2[3];
         if (i+3 >= len1) return -1;
         c = ((ch - 0xd800) << 10) + (ch2 - 0xdc00) + 0x10000;
         if (s1[i++] != 0xf0 + (c >> 18)) return -1;
         if (s1[i++] != 0x80 + ((c >> 12) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((c >>  6) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((c      ) & 0x3f)) return -1;
         s2 += 2; // plus another 2 below
         len2 -= 2;
      } else if (ch >= 0xdc00 && ch < 0xe000) {
         return -1;
      } else {
         if (i+2 >= len1) return -1;
         if (s1[i++] != 0xe0 + (ch >> 12)) return -1;
         if (s1[i++] != 0x80 + ((ch >> 6) & 0x3f)) return -1;
         if (s1[i++] != 0x80 + ((ch     ) & 0x3f)) return -1;
      }
      s2 += 2;
      len2 -= 2;
   }
   return i;
}

static int stbtt_CompareUTF8toUTF16_bigendian_internal(char *s1, int len1, char *s2, int len2)
{
   return len1 == stbtt__CompareUTF8toUTF16_bigendian_prefix((stbtt_uint8*) s1, len1, (stbtt_uint8*) s2, len2);
}

// returns results in whatever encoding you request... but note that 2-byte encodings
// will be BIG-ENDIAN... use stbtt_CompareUTF8toUTF16_bigendian() to compare
STBTT_DEF const char *stbtt_GetFontNameString(const stbtt_fontinfo *font, int *length, int platformID, int encodingID, int languageID, int nameID)
{
   stbtt_int32 i,count,stringOffset;
   stbtt_uint8 *fc = font->data;
   stbtt_uint32 offset = font->fontstart;
   stbtt_uint32 nm = stbtt__find_table(fc, offset, "name");
   if (!nm) return NULL;

   count = ttUSHORT(fc+nm+2);
   stringOffset = nm + ttUSHORT(fc+nm+4);
   for (i=0; i < count; ++i) {
      stbtt_uint32 loc = nm + 6 + 12 * i;
      if (platformID == ttUSHORT(fc+loc+0) && encodingID == ttUSHORT(fc+loc+2)
          && languageID == ttUSHORT(fc+loc+4) && nameID == ttUSHORT(fc+loc+6)) {
         *length = ttUSHORT(fc+loc+8);
         return (const char *) (fc+stringOffset+ttUSHORT(fc+loc+10));
      }
   }
   return NULL;
}

static int stbtt__matchpair(stbtt_uint8 *fc, stbtt_uint32 nm, stbtt_uint8 *name, stbtt_int32 nlen, stbtt_int32 target_id, stbtt_int32 next_id)
{
   stbtt_int32 i;
   stbtt_int32 count = ttUSHORT(fc+nm+2);
   stbtt_int32 stringOffset = nm + ttUSHORT(fc+nm+4);

   for (i=0; i < count; ++i) {
      stbtt_uint32 loc = nm + 6 + 12 * i;
      stbtt_int32 id = ttUSHORT(fc+loc+6);
      if (id == target_id) {
         // find the encoding
         stbtt_int32 platform = ttUSHORT(fc+loc+0), encoding = ttUSHORT(fc+loc+2), language = ttUSHORT(fc+loc+4);

         // is this a Unicode encoding?
         if (platform == 0 || (platform == 3 && encoding == 1) || (platform == 3 && encoding == 10)) {
            stbtt_int32 slen = ttUSHORT(fc+loc+8);
            stbtt_int32 off = ttUSHORT(fc+loc+10);

            // check if there's a prefix match
            stbtt_int32 matchlen = stbtt__CompareUTF8toUTF16_bigendian_prefix(name, nlen, fc+stringOffset+off,slen);
            if (matchlen >= 0) {
               // check for target_id+1 immediately following, with same encoding & language
               if (i+1 < count && ttUSHORT(fc+loc+12+6) == next_id && ttUSHORT(fc+loc+12) == platform && ttUSHORT(fc+loc+12+2) == encoding && ttUSHORT(fc+loc+12+4) == language) {
                  slen = ttUSHORT(fc+loc+12+8);
                  off = ttUSHORT(fc+loc+12+10);
                  if (slen == 0) {
                     if (matchlen == nlen)
                        return 1;
                  } else if (matchlen < nlen && name[matchlen] == ' ') {
                     ++matchlen;
                     if (stbtt_CompareUTF8toUTF16_bigendian_internal((char*) (name+matchlen), nlen-matchlen, (char*)(fc+stringOffset+off),slen))
                        return 1;
                  }
               } else {
                  // if nothing immediately following
                  if (matchlen == nlen)
                     return 1;
               }
            }
         }

         // @TODO handle other encodings
      }
   }
   return 0;
}

static int stbtt__matches(stbtt_uint8 *fc, stbtt_uint32 offset, stbtt_uint8 *name, stbtt_int32 flags)
{
   stbtt_int32 nlen = (stbtt_int32) STBTT_strlen((char *) name);
   stbtt_uint32 nm,hd;
   if (!stbtt__isfont(fc+offset)) return 0;

   // check italics/bold/underline flags in macStyle...
   if (flags) {
      hd = stbtt__find_table(fc, offset, "head");
      if ((ttUSHORT(fc+hd+44) & 7) != (flags & 7)) return 0;
   }

   nm = stbtt__find_table(fc, offset, "name");
   if (!nm) return 0;

   if (flags) {
      // if we checked the macStyle flags, then just check the family and ignore the subfamily
      if (stbtt__matchpair(fc, nm, name, nlen, 16, -1))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  1, -1))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  3, -1))  return 1;
   } else {
      if (stbtt__matchpair(fc, nm, name, nlen, 16, 17))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  1,  2))  return 1;
      if (stbtt__matchpair(fc, nm, name, nlen,  3, -1))  return 1;
   }

   return 0;
}

static int stbtt_FindMatchingFont_internal(unsigned char *font_collection, char *name_utf8, stbtt_int32 flags)
{
   stbtt_int32 i;
   for (i=0;;++i) {
      stbtt_int32 off = stbtt_GetFontOffsetForIndex(font_collection, i);
      if (off < 0) return off;
      if (stbtt__matches((stbtt_uint8 *) font_collection, off, (stbtt_uint8*) name_utf8, flags))
         return off;
   }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

STBTT_DEF int stbtt_BakeFontBitmap(const unsigned char *data, int offset,
                                float pixel_height, unsigned char *pixels, int pw, int ph,
                                int first_char, int num_chars, stbtt_bakedchar *chardata)
{
   return stbtt_BakeFontBitmap_internal((unsigned char *) data, offset, pixel_height, pixels, pw, ph, first_char, num_chars, chardata);
}

STBTT_DEF int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index)
{
   return stbtt_GetFontOffsetForIndex_internal((unsigned char *) data, index);
}

STBTT_DEF int stbtt_GetNumberOfFonts(const unsigned char *data)
{
   return stbtt_GetNumberOfFonts_internal((unsigned char *) data);
}

STBTT_DEF int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset)
{
   return stbtt_InitFont_internal(info, (unsigned char *) data, offset);
}

STBTT_DEF int stbtt_FindMatchingFont(const unsigned char *fontdata, const char *name, int flags)
{
   return stbtt_FindMatchingFont_internal((unsigned char *) fontdata, (char *) name, flags);
}

STBTT_DEF int stbtt_CompareUTF8toUTF16_bigendian(const char *s1, int len1, const char *s2, int len2)
{
   return stbtt_CompareUTF8toUTF16_bigendian_internal((char *) s1, len1, (char *) s2, len2);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // STB_TRUETYPE_IMPLEMENTATION


// FULL VERSION HISTORY
//
//   1.25 (2021-07-11) many fixes
//   1.24 (2020-02-05) fix warning
//   1.23 (2020-02-02) query SVG data for glyphs; query whole kerning table (but only kern not GPOS)
//   1.22 (2019-08-11) minimize missing-glyph duplication; fix kerning if both 'GPOS' and 'kern' are defined
//   1.21 (2019-02-25) fix warning
//   1.20 (2019-02-07) PackFontRange skips missing codepoints; GetScaleFontVMetrics()
//   1.19 (2018-02-11) OpenType GPOS kerning (horizontal only), STBTT_fmod
//   1.18 (2018-01-29) add missing function
//   1.17 (2017-07-23) make more arguments const; doc fix
//   1.16 (2017-07-12) SDF support
//   1.15 (2017-03-03) make more arguments const
//   1.14 (2017-01-16) num-fonts-in-TTC function
//   1.13 (2017-01-02) support OpenType fonts, certain Apple fonts
//   1.12 (2016-10-25) suppress warnings about casting away const with -Wcast-qual
//   1.11 (2016-04-02) fix unused-variable warning
//   1.10 (2016-04-02) allow user-defined fabs() replacement
//                     fix memory leak if fontsize=0.0
//                     fix warning from duplicate typedef
//   1.09 (2016-01-16) warning fix; avoid crash on outofmem; use alloc userdata for PackFontRanges
//   1.08 (2015-09-13) document stbtt_Rasterize(); fixes for vertical & horizontal edges
//   1.07 (2015-08-01) allow PackFontRanges to accept arrays of sparse codepoints;
//                     allow PackFontRanges to pack and render in separate phases;
//                     fix stbtt_GetFontOFfsetForIndex (never worked for non-0 input?);
//                     fixed an assert() bug in the new rasterizer
//                     replace assert() with STBTT_assert() in new rasterizer
//   1.06 (2015-07-14) performance improvements (~35% faster on x86 and x64 on test machine)
//                     also more precise AA rasterizer, except if shapes overlap
//                     remove need for STBTT_sort
//   1.05 (2015-04-15) fix misplaced definitions for STBTT_STATIC
//   1.04 (2015-04-15) typo in example
//   1.03 (2015-04-12) STBTT_STATIC, fix memory leak in new packing, various fixes
//   1.02 (2014-12-10) fix various warnings & compile issues w/ stb_rect_pack, C++
//   1.01 (2014-12-08) fix subpixel position when oversampling to exactly match
//                        non-oversampled; STBTT_POINT_SIZE for packed case only
//   1.00 (2014-12-06) add new PackBegin etc. API, w/ support for oversampling
//   0.99 (2014-09-18) fix multiple bugs with subpixel rendering (ryg)
//   0.9  (2014-08-07) support certain mac/iOS fonts without an MS platformID
//   0.8b (2014-07-07) fix a warning
//   0.8  (2014-05-25) fix a few more warnings
//   0.7  (2013-09-25) bugfix: subpixel glyph bug fixed in 0.5 had come back
//   0.6c (2012-07-24) improve documentation
//   0.6b (2012-07-20) fix a few more warnings
//   0.6  (2012-07-17) fix warnings; added stbtt_ScaleForMappingEmToPixels,
//                        stbtt_GetFontBoundingBox, stbtt_IsGlyphEmpty
//   0.5  (2011-12-09) bugfixes:
//                        subpixel glyph renderer computed wrong bounding box
//                        first vertex of shape can be off-curve (FreeSans)
//   0.4b (2011-12-03) fixed an error in the font baking example
//   0.4  (2011-12-01) kerning, subpixel rendering (tor)
//                    bugfixes for:
//                        codepoint-to-glyph conversion using table fmt=12
//                        codepoint-to-glyph conversion using table fmt=4
//                        stbtt_GetBakedQuad with non-square texture (Zer)
//                    updated Hello World! sample to use kerning and subpixel
//                    fixed some warnings
//   0.3  (2009-06-24) cmap fmt=12, compound shapes (MM)
//                    userdata, malloc-from-userdata, non-zero fill (stb)
//   0.2  (2009-03-11) Fix unsigned/signed char warnings
//   0.1  (2009-03-09) First public release
//

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/

/* Roboto Regular + Medium, subset to ASCII. Apache License 2.0, (c) Google. */
static const unsigned char font_regular[] = {
  0,1,0,0,0,13,0,128,0,3,0,80,71,68,69,70,0,141,0,128,0,0,33,120,
  0,0,0,28,71,80,79,83,32,222,4,192,0,0,33,148,0,0,9,166,71,83,85,66,
  147,21,130,22,0,0,43,60,0,0,0,54,79,83,47,50,116,180,2,101,0,0,1,88,
  0,0,0,96,99,109,97,112,68,4,66,199,0,0,3,100,0,0,0,108,103,108,121,102,
  187,77,149,43,0,0,4,168,0,0,28,118,104,101,97,100,17,197,145,168,0,0,0,220,
  0,0,0,54,104,104,101,97,14,26,5,164,0,0,1,20,0,0,0,36,104,109,116,120,
  178,194,42,177,0,0,1,184,0,0,1,172,108,111,99,97,146,3,153,137,0,0,3,208,
  0,0,0,216,109,97,120,112,0,119,0,95,0,0,1,56,0,0,0,32,110,97,109,101,
  2,183,10,243,0,0,33,32,0,0,0,56,112,111,115,116,255,109,0,100,0,0,33,88,
  0,0,0,32,0,1,0,0,0,2,35,84,176,31,67,234,95,15,60,245,0,25,8,0,
  0,0,0,0,196,239,245,14,0,0,0,0,230,252,47,130,255,190,254,42,6,237,6,156,
  0,0,0,9,0,2,0,0,0,0,0,0,0,1,0,0,7,108,254,12,0,0,7,47,
  255,190,255,253,6,237,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,107,
  0,1,0,0,0,107,0,63,0,5,0,30,0,3,0,1,0,0,0,0,0,0,0,0,
  0,0,0,0,0,3,0,1,0,4,4,137,1,144,0,5,0,0,5,154,5,51,0,0,
  1,31,5,154,5,51,0,0,3,209,0,102,2,0,0,0,2,0,0,0,0,0,0,0,
  0,0,128,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,71,79,79,71,0,64,
  0,32,32,58,8,98,253,213,0,0,8,98,2,43,0,0,0,1,0,0,0,0,4,58,
  5,176,0,0,0,32,0,3,3,140,0,100,1,252,0,0,2,16,0,161,2,144,0,137,
  4,237,0,119,4,127,0,110,5,220,0,105,4,250,0,101,1,102,0,104,2,189,0,134,
  2,201,0,39,3,114,0,28,4,138,0,78,1,147,0,29,2,54,0,38,2,28,0,144,
  3,77,0,19,4,127,0,115,4,127,0,171,4,127,0,94,4,127,0,95,4,127,0,53,
  4,127,0,154,4,127,0,133,4,127,0,78,4,127,0,112,4,127,0,100,1,240,0,133,
  1,177,0,41,4,17,0,72,4,100,0,152,4,47,0,135,3,200,0,75,7,47,0,98,
  5,56,0,29,4,252,0,169,5,53,0,120,5,64,0,169,4,140,0,169,4,108,0,169,
  5,115,0,122,5,181,0,169,2,45,0,183,4,106,0,53,5,5,0,169,4,79,0,169,
  6,252,0,169,5,181,0,169,5,129,0,119,5,12,0,169,5,129,0,110,4,238,0,169,
  4,192,0,81,4,198,0,50,5,48,0,140,5,24,0,29,7,25,0,61,5,4,0,58,
  4,206,0,15,4,203,0,87,2,31,0,147,3,73,0,41,2,31,0,10,3,88,0,64,
  3,156,0,4,2,121,0,57,4,90,0,109,4,126,0,140,4,48,0,93,4,131,0,95,
  4,62,0,93,2,200,0,61,4,126,0,97,4,104,0,141,1,242,0,142,1,234,255,190,
  4,14,0,141,1,242,0,156,7,4,0,139,4,107,0,141,4,144,0,92,4,126,0,140,
  4,140,0,95,2,182,0,141,4,33,0,95,2,158,0,9,4,105,0,137,3,224,0,33,
  6,3,0,43,3,248,0,42,3,201,0,22,3,248,0,89,2,181,0,64,1,244,0,176,
  2,181,0,20,5,113,0,131,2,253,0,131,4,71,0,97,2,23,0,148,4,69,0,89,
  5,64,0,161,6,63,0,143,2,179,0,139,5,90,0,148,2,103,0,108,2,103,0,90,
  4,147,0,168,0,0,0,2,0,0,0,3,0,0,0,20,0,3,0,1,0,0,0,20,
  0,4,0,88,0,0,0,18,0,16,0,3,0,2,0,126,0,177,0,183,0,215,32,20,
  32,34,32,38,32,58,255,255,0,0,0,32,0,176,0,183,0,215,32,19,32,34,32,38,
  32,57,255,255,255,225,255,176,255,171,255,140,224,81,224,68,224,65,224,47,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,42,0,42,0,73,
  0,97,0,154,0,216,1,45,1,127,1,142,1,177,1,213,1,248,2,17,2,37,2,50,
  2,70,2,86,2,134,2,153,2,195,2,252,3,26,3,73,3,131,3,150,3,220,4,24,
  4,36,4,48,4,69,4,89,4,110,4,165,5,17,5,44,5,100,5,150,5,188,5,211,
  5,232,6,30,6,54,6,68,6,96,6,124,6,140,6,172,6,196,6,251,7,32,7,93,
  7,136,7,193,7,211,7,242,8,8,8,50,8,82,8,106,8,129,8,147,8,162,8,180,
  8,200,8,212,8,227,9,31,9,80,9,126,9,172,9,223,10,1,10,63,10,96,10,125,
  10,164,10,193,10,207,11,2,11,35,11,81,11,127,11,173,11,200,11,254,12,32,12,62,
  12,84,12,116,12,147,12,181,12,204,12,245,13,3,13,45,13,85,13,117,13,148,13,168,
  13,201,13,211,13,221,13,244,14,4,14,25,14,46,14,59,0,5,0,100,0,0,3,40,
  5,176,0,3,0,6,0,9,0,12,0,15,0,0,51,17,33,17,9,2,3,1,17,5,
  17,1,3,33,1,100,2,196,253,154,1,2,1,1,229,1,18,253,168,1,12,228,2,3,
  254,255,5,176,250,80,5,122,253,162,2,94,253,94,253,124,5,8,12,251,16,2,120,253,
  94,2,94,0,0,2,0,161,255,244,1,124,5,176,0,3,0,15,0,0,1,3,35,3,
  3,52,54,51,50,22,21,20,6,35,34,38,1,105,13,167,14,6,55,54,53,57,57,53,
  54,55,5,176,251,235,4,21,250,173,45,62,62,45,43,62,62,0,0,2,0,137,4,19,
  2,36,6,0,0,4,0,9,0,0,1,3,35,17,51,5,3,35,17,51,1,22,30,111,
  141,1,14,30,111,141,5,120,254,155,1,237,136,254,155,1,237,0,0,2,0,119,0,0,
  4,211,5,176,0,27,0,31,0,0,33,19,33,3,35,19,35,53,33,19,33,53,33,19,
  51,3,33,19,51,3,51,21,35,3,51,21,35,3,1,33,19,33,2,174,80,254,248,80,
  143,80,240,1,11,68,254,253,1,30,81,144,81,1,8,81,143,81,203,230,69,224,251,80,
  254,212,1,8,69,254,248,1,154,254,102,1,154,138,1,97,139,1,160,254,96,1,160,254,
  96,139,254,159,138,254,102,2,36,1,97,0,0,1,0,110,255,48,4,18,6,156,0,42,
  0,0,1,20,6,7,21,35,53,38,38,53,51,20,22,51,50,54,53,52,38,39,38,38,
  53,52,54,55,53,51,21,22,22,21,35,52,38,35,34,6,21,20,22,4,22,4,18,209,
  177,149,154,243,186,181,98,134,147,131,160,189,210,193,165,150,168,187,184,128,120,126,119,118,
  1,123,193,1,121,174,201,19,191,190,16,203,228,169,119,127,107,91,131,51,60,198,174,166,
  207,20,219,220,22,237,205,134,173,129,103,95,122,128,191,0,0,5,0,105,255,235,5,131,
  5,197,0,12,0,26,0,30,0,44,0,58,0,0,19,52,54,51,50,22,21,21,20,6,
  32,38,53,51,20,22,51,50,54,53,53,52,38,35,34,6,21,37,1,39,1,3,52,54,
  51,50,22,21,21,20,6,35,34,38,53,51,20,22,51,50,54,53,53,52,38,35,34,6,
  21,105,160,138,140,159,158,254,234,161,139,79,82,80,78,79,81,80,79,3,146,253,57,104,
  2,199,240,160,138,140,159,158,139,140,160,139,79,82,81,78,79,82,80,79,4,152,124,177,
  177,124,77,122,177,177,122,69,110,109,70,77,70,111,111,70,73,251,142,66,4,114,252,66,
  123,177,177,123,78,123,177,177,123,70,110,109,71,78,71,109,109,71,0,3,0,102,255,236,
  4,243,5,196,0,30,0,42,0,52,0,0,33,39,6,6,35,34,38,53,52,54,55,38,
  38,53,52,54,51,50,22,21,20,6,7,7,1,54,53,51,20,7,23,1,20,23,55,54,
  54,53,52,38,35,34,6,3,20,22,51,50,55,1,7,6,6,4,21,96,75,202,100,218,
  252,157,125,75,89,200,165,161,185,108,90,109,1,68,68,166,123,208,252,163,118,107,66,63,
  92,84,88,90,119,140,145,148,121,254,157,42,108,49,115,67,68,232,180,130,179,88,90,158,
  86,170,183,183,124,105,145,67,81,254,124,130,159,255,168,249,4,105,93,147,76,46,85,73,
  59,97,117,252,208,106,154,111,1,169,31,79,134,0,0,1,0,104,4,34,0,254,6,0,
  0,4,0,0,19,21,3,35,19,254,21,129,1,6,0,110,254,144,1,222,0,1,0,134,
  254,42,2,150,6,107,0,15,0,0,19,16,18,18,55,23,6,2,17,16,18,23,7,38,
  2,2,134,166,228,95,39,118,225,225,118,39,95,228,166,2,80,1,34,1,178,1,18,53,
  122,89,254,55,254,117,254,132,254,50,96,112,52,1,18,1,177,0,0,1,0,39,254,42,
  2,55,6,107,0,16,0,0,1,16,2,2,7,39,54,18,17,52,2,38,39,55,22,18,
  18,2,55,166,229,94,39,117,226,112,160,71,39,94,229,166,2,70,254,220,254,78,254,238,
  52,112,89,1,212,1,139,253,1,121,248,58,113,53,254,238,254,79,0,1,0,28,2,98,
  3,86,5,177,0,14,0,0,19,19,37,55,5,3,51,3,37,23,5,19,7,3,3,129,
  201,254,210,47,1,46,9,152,10,1,42,46,254,205,197,124,185,181,2,196,1,20,90,150,
  111,1,88,254,162,111,153,91,254,241,93,1,32,254,231,0,0,1,0,78,0,146,4,52,
  4,182,0,11,0,0,1,33,17,35,17,33,53,33,17,51,17,33,4,52,254,106,185,254,
  105,1,151,185,1,150,2,95,254,51,1,205,174,1,169,254,87,0,0,1,0,29,254,221,
  1,53,0,220,0,8,0,0,37,21,20,6,7,39,54,55,53,1,53,92,83,105,96,3,
  220,149,91,203,68,73,134,140,164,0,0,1,0,38,2,31,2,14,2,183,0,3,0,0,
  1,21,33,53,2,14,254,24,2,183,152,152,0,1,0,144,255,244,1,118,0,210,0,9,
  0,0,55,52,54,50,22,21,20,6,34,38,144,59,112,59,59,112,59,98,47,65,65,47,
  46,64,64,0,0,1,0,19,255,131,3,17,5,176,0,3,0,0,1,1,35,1,3,17,
  253,161,159,2,96,5,176,249,211,6,45,0,0,2,0,115,255,236,4,11,5,196,0,12,
  0,26,0,0,1,16,2,35,34,2,3,53,16,18,32,18,19,39,52,38,35,34,6,7,
  17,20,22,51,50,54,55,4,11,247,212,206,251,4,250,1,162,248,4,186,140,135,130,141,
  2,143,132,135,137,1,2,110,254,154,254,228,1,20,1,87,245,1,101,1,19,254,245,254,
  170,8,246,204,198,238,254,215,244,216,213,239,0,1,0,171,0,0,2,217,5,184,0,6,
  0,0,1,17,35,17,5,53,37,2,217,185,254,139,2,17,5,184,250,72,4,209,136,167,
  200,0,0,1,0,94,0,0,4,51,5,196,0,24,0,0,37,21,33,53,1,54,54,53,
  52,38,35,34,6,21,35,52,18,51,50,22,21,20,2,7,1,4,51,252,71,1,238,122,
  85,133,121,146,144,185,242,233,206,233,158,115,254,122,152,152,133,2,38,138,162,82,107,152,
  166,130,184,1,8,214,175,128,254,253,125,254,89,0,0,1,0,95,255,236,3,250,5,196,
  0,38,0,0,1,53,51,54,54,53,16,35,34,6,21,35,52,54,51,50,22,21,20,6,
  7,22,22,21,20,4,35,34,36,53,51,20,22,51,50,54,16,38,35,1,135,136,143,140,
  254,118,146,185,247,202,197,243,109,120,145,106,254,248,197,190,254,240,185,149,128,127,148,168,
  141,2,155,152,1,142,106,1,0,135,114,167,234,209,203,81,184,51,47,196,99,204,222,211,
  194,115,138,134,1,16,129,0,0,2,0,53,0,0,4,81,5,176,0,10,0,14,0,0,
  19,1,51,17,51,21,35,17,35,17,33,55,33,17,7,53,2,140,197,203,203,185,253,104,
  210,1,198,23,1,191,3,241,252,58,152,254,174,1,82,152,2,203,41,0,0,1,0,154,
  255,236,4,46,5,176,0,28,0,0,1,39,19,33,21,33,3,54,51,50,18,21,20,2,
  35,34,36,39,51,22,51,50,54,53,52,38,35,34,6,1,99,148,73,2,235,253,178,44,
  110,133,201,233,228,233,177,254,254,20,176,35,244,131,144,151,140,93,96,2,181,38,2,213,
  171,254,116,63,254,247,223,209,254,237,199,204,251,178,152,137,187,50,0,2,0,133,255,236,
  4,29,5,178,0,23,0,36,0,0,1,20,2,35,34,38,2,53,53,52,18,36,33,51,
  21,34,6,6,7,54,51,50,22,22,5,20,22,51,50,54,53,52,38,35,34,6,7,4,
  29,231,221,157,208,103,115,1,46,1,25,16,194,215,105,11,116,197,145,185,88,253,34,173,
  109,128,141,130,135,97,158,31,1,219,207,254,224,167,1,1,135,87,205,1,126,245,157,136,
  217,124,131,140,221,61,194,206,187,146,130,206,116,83,0,1,0,78,0,0,4,38,5,176,
  0,6,0,0,1,21,1,35,1,33,53,4,38,253,165,195,2,90,252,236,5,176,104,250,
  184,5,24,152,0,3,0,113,255,236,4,15,5,196,0,22,0,33,0,44,0,0,1,20,
  4,32,36,53,52,54,55,38,38,53,52,54,51,50,22,21,20,6,7,22,22,3,52,38,
  34,6,21,20,22,51,50,54,19,52,38,35,34,6,21,20,22,32,54,4,15,254,245,254,
  120,254,245,131,112,97,113,243,186,187,243,116,97,113,134,219,136,218,134,134,110,109,135,33,
  155,124,127,149,149,1,2,148,1,138,200,214,214,200,122,187,48,47,172,107,191,208,208,191,
  108,171,47,48,188,2,47,109,141,135,115,113,136,136,253,205,122,154,154,122,126,140,140,0,
  0,2,0,100,255,254,3,248,5,196,0,24,0,37,0,0,1,20,2,6,4,35,53,50,
  54,54,55,6,6,35,34,38,38,53,52,18,51,50,22,18,21,37,20,22,51,50,54,55,
  53,52,38,35,34,6,3,248,49,141,254,237,246,221,215,85,5,58,162,95,144,184,88,229,
  223,166,205,93,253,34,131,133,95,155,36,169,111,129,141,3,52,142,254,223,243,148,157,138,
  223,126,69,86,144,223,119,208,1,39,172,254,242,147,96,130,212,114,84,73,199,212,195,0,
  255,255,0,133,255,244,1,108,4,69,0,38,0,15,245,0,0,7,0,15,255,246,3,115,
  255,255,0,41,254,221,1,84,4,69,0,39,0,15,255,222,3,115,0,6,0,13,12,0,
  0,1,0,72,0,196,3,122,4,74,0,6,0,0,37,1,53,1,21,1,5,3,122,252,
  206,3,50,253,144,2,112,196,1,123,145,1,122,196,254,255,253,0,0,2,0,152,1,143,
  3,218,3,207,0,3,0,7,0,0,1,21,33,53,1,21,33,53,3,218,252,190,3,66,
  252,190,3,207,161,161,254,97,161,161,0,1,0,135,0,197,3,221,4,76,0,6,0,0,
  19,1,21,1,53,1,1,135,3,86,252,170,2,150,253,106,4,76,254,134,146,254,133,191,
  1,7,1,2,0,2,0,75,255,244,3,119,5,196,0,23,0,35,0,0,1,35,54,54,
  55,54,54,53,52,38,35,34,6,7,35,54,54,51,50,22,21,20,6,6,3,52,54,51,
  50,22,21,20,6,35,34,38,2,31,186,1,78,94,67,104,110,104,88,138,1,185,2,237,
  173,191,209,158,186,194,56,53,54,56,56,54,53,56,1,154,145,144,97,69,124,102,103,120,
  95,104,169,192,204,176,134,213,172,254,28,45,62,62,45,43,62,62,0,2,0,98,254,59,
  6,217,5,151,0,49,0,62,0,0,1,6,6,35,32,0,19,18,18,36,51,32,0,3,
  14,2,35,34,39,6,35,34,38,55,18,18,51,50,22,23,3,6,22,51,50,54,55,18,
  0,33,32,0,3,2,0,33,50,54,55,1,6,22,51,50,54,55,55,19,38,35,34,6,
  4,241,70,209,93,254,132,254,97,18,12,215,1,119,251,1,129,1,143,17,5,83,177,144,
  188,52,108,160,147,140,18,23,255,155,108,124,56,51,13,92,49,121,131,7,17,254,201,254,
  164,254,197,254,138,15,18,1,79,1,63,88,181,61,253,250,13,81,88,51,116,40,1,46,
  56,64,109,161,254,148,45,44,1,245,1,174,1,18,1,175,248,254,10,254,86,123,242,159,
  170,170,242,197,1,7,1,53,63,43,253,214,146,86,236,169,1,117,1,168,254,56,254,146,
  254,138,254,80,42,36,2,156,141,154,92,106,9,1,246,29,224,0,0,2,0,29,0,0,
  5,30,5,176,0,7,0,10,0,0,51,1,51,1,35,3,33,3,19,33,3,29,2,43,
  169,2,45,198,139,253,159,138,196,1,238,247,5,176,250,80,1,125,254,131,2,27,2,167,
  0,3,0,169,0,0,4,136,5,176,0,14,0,22,0,31,0,0,1,20,4,35,33,17,
  33,50,22,21,20,6,7,22,22,1,17,33,50,54,53,52,37,1,52,38,35,33,17,33,
  50,54,4,136,254,249,218,254,2,1,221,224,251,115,100,123,131,252,226,1,34,124,152,254,
  242,1,54,133,147,254,186,1,61,137,152,1,164,204,216,5,176,185,203,101,157,43,34,190,
  2,243,254,50,124,110,223,5,252,144,122,141,253,244,141,0,0,1,0,120,255,236,4,216,
  5,196,0,27,0,0,1,51,6,0,33,34,0,3,53,16,0,33,50,0,23,35,38,38,
  35,34,2,21,21,20,18,51,50,54,4,24,192,22,254,244,254,250,254,254,201,3,1,59,
  1,14,247,1,10,22,192,22,152,169,193,200,182,194,184,151,1,207,207,254,236,1,107,1,
  45,156,1,50,1,114,254,240,215,152,177,254,228,232,147,215,254,209,172,0,0,2,0,169,
  0,0,4,199,5,176,0,9,0,19,0,0,51,17,33,32,0,17,21,16,0,33,3,17,
  51,50,18,53,53,52,2,35,169,1,156,1,31,1,99,254,155,254,209,201,201,233,236,236,
  215,5,176,254,143,254,196,87,254,196,254,144,5,18,251,139,1,32,239,89,251,1,18,0,
  0,1,0,169,0,0,4,70,5,176,0,11,0,0,51,17,33,21,33,17,33,21,33,17,
  33,21,169,3,147,253,46,2,118,253,138,2,220,5,176,158,254,44,157,253,252,157,0,1,
  0,169,0,0,4,47,5,176,0,9,0,0,51,17,33,21,33,17,33,21,33,17,169,3,
  134,253,59,2,98,253,158,5,176,158,254,15,158,253,125,0,0,1,0,122,255,236,4,221,
  5,196,0,31,0,0,1,17,6,6,35,32,0,17,53,16,0,33,50,4,23,35,38,38,
  35,34,2,7,21,20,18,51,50,54,55,17,33,53,4,221,41,230,244,254,246,254,170,1,
  48,1,26,250,1,3,28,193,20,157,167,201,190,1,228,187,155,138,29,254,175,2,213,253,
  235,60,152,1,109,1,71,113,1,69,1,110,252,194,118,170,254,229,243,120,252,254,229,73,
  33,1,70,156,0,1,0,169,0,0,5,8,5,176,0,11,0,0,33,17,33,17,35,17,
  51,17,33,17,51,17,4,72,253,34,193,193,2,222,192,2,161,253,95,5,176,253,142,2,
  114,250,80,0,0,1,0,183,0,0,1,120,5,176,0,3,0,0,1,17,35,17,1,120,
  193,5,176,250,80,5,176,0,0,1,0,53,255,236,3,204,5,176,0,15,0,0,1,51,
  17,20,4,35,34,36,53,51,20,22,51,50,54,53,3,12,192,254,253,200,201,254,253,193,
  149,118,114,153,5,176,251,249,216,229,208,215,138,128,146,142,0,1,0,169,0,0,5,5,
  5,176,0,11,0,0,33,1,7,17,35,17,51,17,1,51,1,1,4,31,253,253,178,193,
  193,2,135,232,253,196,2,104,2,166,185,254,19,5,176,253,50,2,206,253,125,252,211,0,
  0,1,0,169,0,0,4,28,5,176,0,5,0,0,37,21,33,17,51,17,4,28,252,141,
  193,157,157,5,176,250,237,0,0,1,0,169,0,0,6,82,5,176,0,14,0,0,1,1,
  51,17,35,17,19,1,35,1,19,17,35,17,51,3,126,1,220,248,192,19,254,33,146,254,
  34,19,192,248,1,13,4,163,250,80,2,56,2,97,251,103,4,155,253,157,253,200,5,176,
  0,1,0,169,0,0,5,9,5,176,0,9,0,0,1,17,35,1,17,35,17,51,1,17,
  5,9,194,253,35,193,193,2,224,5,176,250,80,4,99,251,157,5,176,251,154,4,102,0,
  0,2,0,119,255,236,5,10,5,196,0,13,0,27,0,0,1,16,0,33,32,0,17,53,
  16,0,33,32,0,19,7,16,2,35,34,2,17,21,16,18,51,50,18,17,5,10,254,194,
  254,246,254,253,254,184,1,70,1,3,1,8,1,63,3,191,208,187,179,215,217,179,188,205,
  2,170,254,184,254,138,1,118,1,72,92,1,71,1,119,254,143,254,191,10,1,4,1,20,
  254,236,254,252,94,254,250,254,235,1,21,1,6,0,0,2,0,169,0,0,4,193,5,176,
  0,10,0,19,0,0,1,17,35,17,33,50,4,21,20,4,35,1,17,33,50,54,53,52,
  38,35,1,106,193,2,25,248,1,7,254,249,248,254,168,1,88,170,148,148,170,2,59,253,
  197,5,176,250,194,211,230,2,215,253,198,158,124,112,176,0,0,2,0,110,255,10,5,6,
  5,196,0,18,0,32,0,0,5,7,37,6,35,32,0,17,53,16,0,33,32,0,19,21,
  20,2,7,19,16,2,35,34,2,17,21,16,18,51,50,18,17,5,6,130,254,205,72,80,
  254,253,254,184,1,70,1,3,1,8,1,63,3,134,120,63,208,187,179,215,217,179,188,205,
  126,120,244,18,1,118,1,72,92,1,71,1,119,254,143,254,191,104,210,254,205,86,2,185,
  1,4,1,20,254,236,254,252,94,254,250,254,235,1,21,1,6,0,0,2,0,169,0,0,
  4,202,5,176,0,14,0,23,0,0,33,1,33,17,35,17,33,50,4,21,20,6,7,1,
  21,1,17,33,50,54,53,52,38,35,3,252,254,196,254,170,193,1,226,245,1,9,147,130,
  1,86,252,160,1,39,155,157,151,167,2,78,253,178,5,176,224,213,136,203,50,253,150,12,
  5,18,253,217,159,113,126,153,0,1,0,81,255,236,4,115,5,196,0,37,0,0,1,52,
  38,36,38,53,52,36,51,50,0,21,35,52,38,35,34,6,21,20,22,23,22,22,21,20,
  4,35,34,38,38,53,51,20,22,51,50,54,3,177,135,254,80,251,1,15,224,246,1,11,
  192,158,163,154,148,157,177,253,232,254,234,230,134,253,163,192,218,140,151,163,1,112,100,120,
  127,197,168,170,226,254,242,170,122,160,135,101,91,121,50,71,209,170,178,212,100,197,145,149,
  136,125,0,1,0,50,0,0,4,151,5,176,0,7,0,0,19,53,33,21,33,17,35,17,
  50,4,101,254,44,190,5,18,158,158,250,238,5,18,0,1,0,140,255,236,4,170,5,176,
  0,17,0,0,1,51,17,20,4,35,34,36,53,17,51,17,20,22,51,50,54,53,3,234,
  192,254,196,212,223,254,209,191,185,150,152,184,5,176,252,39,246,245,245,246,3,217,252,39,
  171,163,163,171,0,1,0,29,0,0,4,253,5,176,0,6,0,0,1,1,35,1,51,1,
  1,4,253,253,229,171,253,230,209,1,157,1,161,5,176,250,80,5,176,251,80,4,176,0,
  0,1,0,61,0,0,6,237,5,176,0,18,0,0,1,1,35,1,39,7,1,35,1,51,
  19,23,55,1,51,1,23,55,19,6,237,254,160,175,254,212,23,23,254,201,175,254,159,192,
  230,29,40,1,33,161,1,25,41,31,225,5,176,250,80,4,37,111,111,251,219,5,176,252,
  28,193,173,3,248,252,8,177,198,3,227,0,0,1,0,58,0,0,4,206,5,176,0,11,
  0,0,9,2,51,1,1,35,1,1,35,1,1,1,38,1,94,1,94,225,254,52,1,215,
  227,254,153,254,153,227,1,215,254,52,5,176,253,210,2,46,253,47,253,33,2,57,253,199,
  2,223,2,209,0,1,0,15,0,0,4,188,5,176,0,8,0,0,19,1,1,51,1,17,
  35,17,1,236,1,122,1,123,219,254,10,193,254,10,5,176,253,37,2,219,252,112,253,224,
  2,32,3,144,0,1,0,87,0,0,4,122,5,176,0,9,0,0,19,53,33,21,1,33,
  21,33,53,1,100,3,246,252,224,3,64,251,221,3,30,5,18,158,140,251,121,157,144,4,
  130,0,0,1,0,147,254,200,2,11,6,128,0,7,0,0,1,21,35,17,51,21,33,17,
  2,11,191,191,254,136,6,128,152,249,120,152,7,184,0,1,0,41,255,131,3,57,5,176,
  0,3,0,0,5,1,51,1,2,137,253,160,176,2,96,125,6,45,249,211,0,1,0,10,
  254,200,1,132,6,128,0,7,0,0,19,53,33,17,33,53,51,17,10,1,122,254,134,192,
  5,232,152,248,72,152,6,136,0,1,0,64,2,217,3,21,5,176,0,6,0,0,19,1,
  51,1,35,3,3,64,1,43,128,1,42,172,190,191,2,217,2,215,253,41,1,227,254,29,
  0,1,0,4,255,104,3,153,0,0,0,3,0,0,33,21,33,53,3,153,252,107,152,152,
  0,1,0,57,4,218,1,218,6,0,0,3,0,0,1,19,35,1,1,25,193,159,254,254,
  6,0,254,218,1,38,0,2,0,109,255,236,3,234,4,78,0,30,0,40,0,0,33,38,
  39,6,6,35,34,38,53,52,36,51,51,53,52,38,35,34,6,21,35,52,54,51,50,22,
  21,17,20,23,21,37,50,54,55,53,35,4,21,20,22,3,41,18,8,52,156,100,166,200,
  1,8,223,183,115,113,105,129,185,234,196,176,226,38,254,12,100,147,30,172,254,200,104,39,
  75,55,79,186,135,174,181,86,96,115,103,68,116,204,180,182,254,26,150,88,16,142,100,61,
  223,6,194,77,107,0,0,2,0,140,255,236,4,33,6,0,0,15,0,29,0,0,1,20,
  2,35,34,39,7,35,17,51,17,54,51,50,18,21,1,34,6,7,17,22,22,51,50,54,
  53,53,52,38,4,33,220,201,205,111,10,170,186,110,198,204,219,254,43,101,126,35,36,128,
  100,150,131,125,2,17,237,254,200,145,125,6,0,253,197,137,254,208,248,1,140,97,71,254,
  41,72,98,231,161,21,161,235,0,1,0,93,255,236,3,237,4,78,0,29,0,0,37,50,
  54,55,51,6,6,35,34,2,53,53,52,18,51,50,22,23,35,38,38,35,34,6,6,21,
  21,20,22,22,2,62,99,149,7,176,7,252,172,244,237,237,244,190,234,7,176,7,137,111,
  114,129,53,52,129,132,118,92,145,217,1,66,218,42,218,1,66,225,167,100,140,116,178,94,
  42,95,178,115,0,2,0,95,255,236,3,241,6,0,0,15,0,27,0,0,33,39,6,35,
  34,2,39,53,52,18,51,50,23,17,51,17,1,20,22,51,50,55,17,38,35,34,6,21,
  3,71,9,111,198,190,234,2,235,193,191,109,186,253,40,136,150,172,84,83,171,151,137,116,
  136,1,52,233,29,248,1,48,128,2,50,250,0,2,17,161,231,157,1,242,154,235,161,0,
  0,2,0,93,255,236,3,243,4,78,0,22,0,30,0,0,37,6,6,35,34,0,53,53,
  52,54,54,51,50,18,21,21,33,22,22,51,50,54,55,1,34,6,7,33,53,38,38,3,
  229,53,194,160,226,254,241,135,216,122,232,213,253,36,4,169,148,98,136,51,254,193,110,152,
  19,2,29,7,111,189,80,129,1,38,229,42,177,249,131,254,209,227,83,149,209,80,67,2,
  160,160,150,14,108,188,0,1,0,61,0,0,2,203,6,21,0,20,0,0,1,35,17,35,
  17,35,53,51,53,54,54,51,50,23,7,38,35,34,7,21,51,2,135,230,185,171,171,2,
  189,164,61,67,10,42,58,185,3,230,3,172,252,84,3,172,142,121,172,182,16,151,9,197,
  120,0,0,2,0,97,254,85,3,242,4,78,0,26,0,39,0,0,1,34,38,39,55,22,
  51,50,54,53,53,6,35,34,2,53,53,52,18,51,50,23,55,51,17,20,4,1,20,22,
  51,50,55,17,38,38,35,34,6,21,2,40,84,244,73,97,120,170,131,156,110,193,195,230,
  229,198,199,110,9,168,255,0,254,40,135,150,176,82,35,124,97,151,136,254,85,82,106,110,
  146,147,142,95,129,1,56,237,21,248,1,48,140,120,251,221,215,235,3,188,161,231,160,1,
  237,67,89,235,161,0,0,1,0,141,0,0,3,224,6,0,0,18,0,0,1,34,6,7,
  17,35,17,51,17,54,51,50,22,23,17,35,17,52,38,2,78,89,136,39,185,185,123,196,
  159,187,1,186,111,3,178,96,77,252,251,6,0,253,185,149,178,210,253,54,2,199,127,108,
  0,2,0,142,0,0,1,105,5,196,0,11,0,15,0,0,19,52,54,51,50,22,21,20,
  6,35,34,38,23,17,35,17,142,55,54,53,57,57,53,54,55,200,186,5,89,45,62,62,
  45,43,61,61,244,251,198,4,58,0,0,2,255,190,254,75,1,90,5,196,0,11,0,23,
  0,0,19,52,54,51,50,22,21,20,6,35,34,38,23,51,17,16,33,34,39,55,22,51,
  50,55,127,56,53,54,56,56,54,53,56,19,186,254,229,62,53,1,37,48,122,4,5,89,
  45,62,62,45,43,61,61,244,251,69,254,204,17,149,9,139,0,1,0,141,0,0,4,13,
  6,0,0,12,0,0,33,1,7,17,35,17,51,17,55,1,51,1,1,3,51,254,137,117,
  186,186,99,1,80,226,254,90,1,215,1,246,121,254,131,6,0,252,96,118,1,100,254,61,
  253,137,0,1,0,156,0,0,1,86,6,0,0,3,0,0,1,17,35,17,1,86,186,6,
  0,250,0,6,0,0,0,1,0,139,0,0,6,121,4,78,0,32,0,0,1,34,7,17,
  35,17,51,23,54,51,50,22,23,54,54,51,50,22,21,17,35,17,52,38,35,34,6,7,
  17,35,17,52,38,2,62,182,67,186,176,5,117,203,104,163,43,54,173,117,174,189,186,125,
  106,112,128,10,185,126,3,178,153,252,231,4,58,118,138,83,91,78,96,186,205,253,57,2,
  201,136,97,133,95,253,50,2,200,127,107,0,0,1,0,141,0,0,3,224,4,78,0,18,
  0,0,1,34,6,7,17,35,17,51,23,54,51,50,22,23,17,35,17,52,38,2,78,89,
  136,39,185,175,6,123,200,159,187,1,186,111,3,178,96,77,252,251,4,58,135,155,178,210,
  253,54,2,199,127,108,0,2,0,92,255,236,4,53,4,78,0,12,0,26,0,0,19,52,
  0,32,0,23,21,20,0,35,34,0,53,51,20,22,51,50,54,55,53,52,38,35,34,6,
  21,92,1,8,1,198,1,8,3,254,247,227,228,254,247,185,153,155,151,154,1,154,154,153,
  153,2,40,235,1,59,254,203,231,33,235,254,198,1,58,235,161,236,233,161,26,159,239,239,
  159,0,0,2,0,140,254,96,4,31,4,78,0,15,0,27,0,0,5,34,39,17,35,17,
  51,23,54,51,50,18,21,21,20,2,1,34,7,17,22,51,50,54,53,53,52,38,2,123,
  195,114,186,170,9,114,199,205,218,219,254,254,168,84,85,169,150,139,139,20,125,253,247,5,
  218,119,139,254,208,248,21,237,254,200,3,198,149,253,250,147,236,161,21,161,235,0,0,2,
  0,95,254,96,3,240,4,78,0,15,0,27,0,0,1,17,35,17,6,35,34,2,53,53,
  52,18,51,50,23,55,1,20,22,51,50,55,17,38,35,34,6,21,3,240,186,111,187,200,
  229,228,203,192,112,8,253,211,139,150,165,87,87,163,152,139,4,58,250,38,2,4,120,1,
  56,237,21,248,1,48,129,109,253,215,161,236,145,2,19,142,239,162,0,1,0,141,0,0,
  2,152,4,78,0,13,0,0,1,34,7,17,35,17,51,23,54,51,50,23,7,38,2,61,
  181,66,185,180,4,89,168,52,30,1,41,3,156,156,253,0,4,58,124,144,14,172,8,0,
  0,1,0,95,255,236,3,188,4,78,0,36,0,0,1,52,38,39,38,38,53,52,54,51,
  50,22,21,35,52,38,35,34,6,20,22,23,22,22,21,20,6,35,34,38,53,51,22,22,
  51,50,54,3,3,87,157,181,214,219,182,194,218,185,118,109,115,100,93,151,199,195,229,189,
  215,228,186,6,165,86,113,120,1,31,59,95,32,37,141,135,128,188,198,140,67,119,100,118,
  73,34,45,146,133,143,178,220,134,115,87,88,0,1,0,9,255,236,2,87,5,65,0,21,
  0,0,33,6,35,34,38,53,17,35,53,51,17,51,17,51,21,35,17,20,22,51,50,55,
  2,87,63,80,103,146,198,198,185,202,202,72,47,35,52,20,126,163,2,159,142,1,7,254,
  249,142,253,96,83,46,12,0,0,1,0,137,255,236,3,221,4,58,0,17,0,0,33,39,
  6,35,34,38,53,17,51,17,20,22,51,50,55,17,51,17,3,44,4,106,210,159,196,185,
  123,76,209,73,186,107,127,188,216,2,186,253,68,146,99,157,3,20,251,198,0,1,0,33,
  0,0,3,187,4,58,0,6,0,0,1,1,35,1,51,1,1,3,187,254,123,141,254,120,
  190,1,19,1,12,4,58,251,198,4,58,252,195,3,61,0,0,1,0,43,0,0,5,211,
  4,58,0,12,0,0,1,1,35,1,1,35,1,51,19,19,51,1,19,5,211,254,198,150,
  254,249,255,0,151,254,198,185,213,252,149,1,0,209,4,58,251,198,3,51,252,205,4,58,
  252,216,3,40,252,199,3,57,0,1,0,42,0,0,3,203,4,58,0,11,0,0,1,19,
  19,51,1,1,35,3,3,35,1,1,1,10,237,240,217,254,158,1,109,214,250,250,215,1,
  108,254,159,4,58,254,118,1,138,253,234,253,220,1,150,254,106,2,36,2,22,0,0,1,
  0,22,254,75,3,176,4,58,0,16,0,0,5,6,7,7,34,39,39,23,50,54,55,55,
  1,51,1,19,51,1,254,96,194,31,59,45,1,50,94,106,34,40,254,126,202,1,15,251,
  198,167,254,15,1,16,150,4,77,100,110,4,46,252,214,3,42,0,0,1,0,89,0,0,
  3,179,4,58,0,9,0,0,19,53,33,21,1,33,21,33,53,1,97,3,53,253,166,2,
  119,252,166,2,84,3,161,153,131,252,225,152,136,3,25,0,0,1,0,64,254,146,2,159,
  6,61,0,26,0,0,19,53,50,53,53,52,54,55,23,6,6,21,21,20,7,22,21,21,
  20,22,23,7,38,38,53,53,52,64,212,152,204,39,119,90,167,167,90,119,39,204,152,2,
  31,145,242,202,158,249,58,114,37,191,123,198,229,90,88,232,196,123,190,37,115,58,250,157,
  200,244,0,1,0,176,254,242,1,69,5,176,0,3,0,0,1,17,35,17,1,69,149,5,
  176,249,66,6,190,0,0,1,0,20,254,146,2,115,6,61,0,26,0,0,1,21,34,21,
  21,20,6,7,39,54,54,53,53,52,55,38,53,53,52,38,39,55,22,22,21,21,20,2,
  115,212,151,205,39,118,91,182,182,90,119,39,205,151,2,176,145,242,202,157,250,58,115,37,
  190,123,199,235,82,82,236,199,123,191,37,114,58,249,158,204,240,0,0,1,0,131,1,147,
  4,239,3,35,0,24,0,0,1,55,20,6,35,34,38,39,38,38,35,34,6,21,7,52,
  54,51,50,30,2,51,50,54,4,87,152,181,143,87,133,78,51,86,50,76,86,161,181,142,
  86,134,151,73,41,77,95,3,9,1,154,221,70,68,47,52,108,94,2,157,210,71,132,35,
  118,0,0,2,0,131,3,192,2,125,5,197,0,7,0,17,0,0,18,54,50,22,20,6,
  34,38,55,20,22,50,54,53,52,38,34,6,131,151,208,147,147,208,151,124,77,108,73,73,
  108,77,5,44,153,153,214,150,150,107,55,74,74,55,56,76,76,0,0,2,0,97,0,1,
  3,245,4,243,0,11,0,15,0,0,1,33,17,35,17,33,53,33,17,51,17,33,3,21,
  33,53,3,245,254,149,167,254,126,1,130,167,1,107,42,252,189,2,191,254,98,1,158,152,
  1,156,254,100,253,65,151,151,0,1,0,148,2,108,1,121,3,73,0,9,0,0,19,52,
  54,50,22,21,20,6,34,38,148,58,112,59,59,112,58,2,217,47,65,65,47,46,63,63,
  0,1,0,89,0,206,3,222,4,100,0,11,0,0,1,7,1,1,39,1,1,55,1,1,
  23,1,3,222,119,254,181,254,180,119,1,74,254,185,119,1,73,1,72,119,254,185,1,73,
  123,1,82,254,174,123,1,81,1,78,124,254,177,1,79,124,254,178,255,255,0,162,2,139,
  4,140,3,35,0,70,0,106,216,0,76,205,64,0,255,255,0,144,2,139,5,200,3,35,
  0,70,0,106,131,0,102,102,64,0,0,1,0,139,2,24,2,35,3,203,0,12,0,0,
  19,52,54,51,50,22,21,21,20,6,34,38,53,139,109,94,95,110,109,190,109,3,5,86,
  112,113,85,41,86,110,110,86,255,255,0,148,255,244,4,206,0,210,0,34,0,15,4,0,
  0,35,0,15,1,185,0,0,0,3,0,15,3,88,0,0,0,1,0,108,0,153,2,33,
  3,181,0,6,0,0,37,1,53,1,51,1,1,1,147,254,217,1,39,142,254,254,1,2,
  153,1,133,19,1,132,254,114,254,114,0,1,0,90,0,153,2,15,3,181,0,6,0,0,
  19,1,21,1,35,1,1,232,1,39,254,217,142,1,2,254,254,3,181,254,123,19,254,124,
  1,142,1,142,0,1,0,168,2,139,3,235,3,35,0,3,0,0,1,21,33,53,3,235,
  252,189,3,35,152,152,0,0,0,0,0,2,0,30,0,3,0,1,4,9,0,1,0,12,
  0,0,0,3,0,1,4,9,0,2,0,14,0,12,0,82,0,111,0,98,0,111,0,116,
  0,111,0,82,0,101,0,103,0,117,0,108,0,97,0,114,0,3,0,0,0,0,0,0,
  255,106,0,100,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,0,0,0,12,0,0,0,0,0,0,0,2,0,2,0,34,0,59,0,1,0,66,
  0,91,0,1,0,1,0,0,0,10,0,48,0,62,0,4,68,70,76,84,0,26,99,121,
  114,108,0,26,103,114,101,107,0,26,108,97,116,110,0,26,0,4,0,0,0,0,255,255,
  0,1,0,0,0,1,107,101,114,110,0,8,0,0,0,1,0,0,0,1,0,4,0,2,
  0,0,0,2,0,10,2,32,0,1,0,66,0,4,0,0,0,28,0,126,0,132,0,132,
  0,138,0,152,0,158,0,172,0,186,0,196,1,10,1,16,1,22,1,28,1,46,1,60,
  1,74,1,92,1,110,1,148,1,154,1,164,1,206,1,228,1,242,2,6,1,248,2,6,
  2,12,0,1,0,28,0,1,0,3,0,8,0,9,0,16,0,34,0,36,0,38,0,39,
  0,44,0,45,0,49,0,50,0,51,0,53,0,55,0,56,0,58,0,59,0,60,0,71,
  0,76,0,83,0,85,0,87,0,88,0,90,0,92,0,1,0,53,255,216,0,1,0,88,
  0,11,0,3,0,55,0,20,0,56,0,18,0,58,0,22,0,1,0,16,255,32,0,3,
  0,32,255,195,0,85,255,239,0,88,255,223,0,3,0,10,255,230,0,62,255,244,0,94,
  255,239,0,2,0,71,255,238,0,88,255,234,0,17,0,13,255,22,0,15,255,22,0,34,
  255,86,0,43,254,248,0,53,0,20,0,66,255,222,0,68,255,235,0,69,255,235,0,70,
  255,235,0,72,255,235,0,80,255,235,0,82,255,235,0,83,255,230,0,86,255,234,0,87,
  255,232,0,90,255,232,0,103,255,22,0,1,0,88,255,193,0,1,0,88,255,164,0,1,
  0,85,0,14,0,4,0,53,255,213,0,55,255,228,0,56,255,236,0,58,255,221,0,3,
  0,53,255,176,0,55,255,237,0,58,255,208,0,3,0,1,255,216,0,83,255,181,0,88,
  255,199,0,4,0,10,0,20,0,62,0,17,0,83,255,226,0,94,0,19,0,4,0,10,
  0,15,0,62,0,12,0,83,255,235,0,94,0,14,0,9,0,7,255,226,0,10,0,20,
  0,11,255,207,0,62,0,18,0,71,255,234,0,83,255,216,0,85,255,234,0,94,0,19,
  0,102,255,211,0,1,0,88,255,229,0,2,0,43,255,238,0,54,255,238,0,10,0,3,
  0,16,0,8,0,16,0,10,0,20,0,62,0,18,0,68,255,232,0,69,255,232,0,70,
  255,232,0,72,255,232,0,82,255,232,0,94,0,19,0,5,0,68,255,236,0,69,255,236,
  0,70,255,236,0,72,255,236,0,82,255,236,0,3,0,71,0,15,0,85,0,50,0,88,
  0,17,0,1,0,80,255,236,0,3,0,13,255,132,0,15,255,132,0,103,255,132,0,1,
  0,71,0,13,0,2,0,43,255,236,0,54,255,236,0,2,5,80,0,4,0,0,5,164,
  6,116,0,28,0,24,0,0,0,0,0,0,0,0,0,0,0,0,0,18,0,0,0,0,
  0,0,255,227,255,228,0,0,0,0,0,0,0,0,0,17,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,235,0,0,0,0,
  0,0,255,229,255,213,0,0,0,0,0,0,255,233,255,234,0,0,0,0,0,0,255,154,
  255,234,0,0,0,0,0,0,0,0,255,245,0,0,255,244,255,245,0,0,0,0,255,206,
  255,239,255,127,255,162,0,0,0,12,0,0,0,0,0,0,0,0,255,136,255,187,0,0,
  255,169,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,241,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,255,235,255,120,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,152,0,0,0,0,
  0,0,0,0,0,0,0,0,255,237,0,0,0,0,255,237,255,239,0,0,0,0,255,230,
  0,0,0,20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,241,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,189,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,245,
  0,0,0,0,0,0,0,0,255,241,0,0,0,0,0,0,255,241,255,227,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,243,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,242,0,0,0,0,
  0,0,0,0,0,0,0,0,255,243,0,0,0,0,255,241,0,0,0,0,255,241,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,255,149,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,234,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,230,255,225,255,233,255,229,255,233,0,0,0,0,255,216,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,192,0,0,0,0,255,191,255,227,255,216,255,191,255,217,255,162,255,183,255,236,
  255,160,0,17,0,18,255,198,255,226,255,240,0,0,0,13,255,233,0,0,0,17,255,45,
  0,18,255,204,255,160,0,0,255,157,255,228,255,147,255,157,255,161,255,177,255,143,255,184,
  0,0,0,16,0,16,255,140,255,196,255,240,0,0,0,0,255,179,0,0,0,15,255,38,
  0,16,255,24,255,16,0,0,255,240,0,0,0,0,255,240,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,235,255,230,0,0,255,235,255,237,0,13,0,0,255,229,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,230,255,231,0,0,255,235,255,235,0,0,0,0,255,225,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,14,255,210,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,227,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,236,0,0,0,0,255,236,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,237,0,0,0,0,255,236,0,0,0,0,255,216,0,18,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16,0,0,255,133,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,245,0,0,0,0,
  0,0,0,0,0,0,0,0,255,243,0,0,0,0,255,243,0,0,255,118,255,245,0,15,
  0,0,0,0,0,0,0,0,0,0,0,0,255,230,255,225,0,0,0,0,0,0,254,188,
  0,0,0,0,255,56,0,0,0,0,255,191,0,0,0,0,255,212,0,19,0,0,255,123,
  255,202,254,237,255,17,0,0,0,0,0,0,0,0,0,0,0,0,254,176,255,113,0,0,
  255,81,0,0,0,0,0,0,255,197,0,0,255,236,255,195,0,0,255,136,255,206,0,0,
  0,0,0,0,0,0,255,176,0,0,0,0,0,0,0,0,0,0,255,149,0,0,0,0,
  0,0,0,0,0,0,0,0,255,225,0,0,0,0,255,225,255,237,255,213,255,223,0,0,
  0,0,0,14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,133,
  0,0,255,196,0,0,0,0,255,212,255,243,0,0,255,210,255,228,255,181,255,210,255,245,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,31,
  0,0,255,219,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,229,255,201,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  255,232,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,86,0,0,0,0,
  0,0,0,0,0,0,0,1,0,40,0,3,0,8,0,13,0,15,0,34,0,35,0,36,
  0,37,0,38,0,41,0,42,0,43,0,44,0,45,0,46,0,47,0,48,0,49,0,53,
  0,54,0,55,0,56,0,57,0,58,0,59,0,66,0,67,0,68,0,70,0,73,0,78,
  0,79,0,80,0,81,0,83,0,87,0,89,0,90,0,91,0,103,0,1,0,3,0,101,
  0,23,0,0,0,0,0,0,0,0,0,23,0,0,0,0,0,0,0,0,0,27,0,0,
  0,27,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,26,0,17,0,1,0,5,
  0,0,0,0,0,0,0,0,0,10,0,11,0,22,0,0,0,0,0,1,0,21,0,0,
  0,0,0,0,0,13,0,10,0,25,0,24,0,16,0,12,0,15,0,0,0,0,0,0,
  0,0,0,0,0,0,0,6,0,7,0,20,0,0,0,8,0,0,0,0,0,4,0,0,
  0,0,0,0,0,0,0,4,0,4,0,3,0,7,0,0,0,19,0,0,0,0,0,0,
  0,9,0,0,0,18,0,9,0,14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,27,0,1,0,3,0,101,0,18,0,0,0,0,0,0,
  0,0,0,18,0,0,0,0,0,0,0,0,0,20,0,22,0,20,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,6,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,0,
  0,23,0,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,0,14,0,10,0,9,
  0,21,0,19,0,16,0,11,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,7,
  0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,3,
  0,3,0,4,0,3,0,1,0,0,0,12,0,0,0,5,0,8,0,0,0,17,0,8,
  0,13,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,22,0,22,0,0,
  0,20,0,0,0,1,0,0,0,10,0,50,0,52,0,4,68,70,76,84,0,26,99,121,
  114,108,0,36,103,114,101,107,0,36,108,97,116,110,0,36,0,4,0,0,0,0,255,255,
  0,0,0,0,0,0,0,0,0,0,0,0,
};
static const unsigned int font_regular_len = 11124;
static const unsigned char font_medium[] = {
  0,1,0,0,0,13,0,128,0,3,0,80,71,68,69,70,0,141,0,128,0,0,33,28,
  0,0,0,28,71,80,79,83,32,220,1,43,0,0,33,56,0,0,9,166,71,83,85,66,
  147,21,130,22,0,0,42,224,0,0,0,54,79,83,47,50,117,24,2,119,0,0,1,88,
  0,0,0,96,99,109,97,112,68,4,66,199,0,0,3,100,0,0,0,108,103,108,121,102,
  138,187,142,152,0,0,4,168,0,0,28,12,104,101,97,100,17,179,145,175,0,0,0,220,
  0,0,0,54,104,104,101,97,14,8,5,148,0,0,1,20,0,0,0,36,104,109,116,120,
  183,83,38,26,0,0,1,184,0,0,1,172,108,111,99,97,140,64,147,166,0,0,3,208,
  0,0,0,216,109,97,120,112,0,119,0,85,0,0,1,56,0,0,0,32,110,97,109,101,
  4,71,12,0,0,0,32,180,0,0,0,70,112,111,115,116,255,109,0,100,0,0,32,252,
  0,0,0,32,0,1,0,0,0,2,35,84,16,11,109,78,95,15,60,245,0,25,8,0,
  0,0,0,0,196,239,245,14,0,0,0,0,230,252,47,130,255,180,254,49,6,229,6,156,
  0,0,0,9,0,2,0,0,0,0,0,0,0,1,0,0,7,108,254,12,0,0,7,41,
  255,180,255,243,6,229,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,107,
  0,1,0,0,0,107,0,59,0,5,0,24,0,3,0,1,0,0,0,0,0,0,0,0,
  0,0,0,0,0,3,0,1,0,4,4,155,1,244,0,5,0,0,5,154,5,51,0,0,
  1,31,5,154,5,51,0,0,3,209,0,102,2,0,0,0,2,0,0,0,0,0,0,0,
  0,0,128,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,71,79,79,71,0,64,
  0,32,32,58,8,98,253,213,0,0,8,98,2,43,0,0,0,1,0,0,0,0,4,58,
  5,176,0,0,0,32,0,3,3,140,0,100,1,254,0,0,2,37,0,144,2,152,0,101,
  4,226,0,97,4,140,0,101,5,224,0,100,5,29,0,86,1,90,0,83,2,203,0,129,
  2,211,0,40,3,138,0,28,4,118,0,69,1,195,0,29,2,161,0,71,2,60,0,136,
  3,43,0,2,4,140,0,105,4,140,0,168,4,140,0,81,4,140,0,80,4,140,0,53,
  4,140,0,129,4,140,0,118,4,140,0,70,4,140,0,104,4,140,0,94,2,31,0,130,
  1,231,0,47,4,17,0,63,4,122,0,145,4,42,0,129,3,229,0,60,7,41,0,82,
  5,83,0,18,5,13,0,149,5,57,0,102,5,58,0,149,4,134,0,149,4,102,0,149,
  5,115,0,106,5,176,0,149,2,66,0,164,4,113,0,45,5,11,0,149,4,85,0,149,
  7,1,0,149,5,175,0,149,5,134,0,102,5,30,0,149,5,134,0,96,4,255,0,149,
  4,213,0,74,4,220,0,45,5,56,0,126,5,46,0,18,7,11,0,49,5,16,0,41,
  4,224,0,8,4,210,0,80,2,50,0,132,3,88,0,20,2,50,0,13,3,108,0,53,
  3,156,0,3,2,148,0,49,4,85,0,91,4,129,0,125,4,49,0,80,4,133,0,80,
  4,75,0,84,2,214,0,45,4,137,0,82,4,113,0,122,2,12,0,125,2,1,255,180,
  4,45,0,125,2,12,0,140,6,247,0,125,4,115,0,122,4,142,0,80,4,129,0,125,
  4,139,0,80,2,208,0,125,4,34,0,76,2,170,0,8,4,114,0,119,3,246,0,23,
  5,242,0,34,4,7,0,31,3,229,0,12,4,7,0,82,2,175,0,56,2,2,0,174,
  2,175,0,28,5,81,0,118,3,11,0,127,4,75,0,95,2,66,0,142,4,69,0,77,
  5,41,0,157,6,48,0,130,2,204,0,136,5,166,0,140,2,115,0,108,2,105,0,85,
  4,131,0,159,0,0,0,2,0,0,0,3,0,0,0,20,0,3,0,1,0,0,0,20,
  0,4,0,88,0,0,0,18,0,16,0,3,0,2,0,126,0,177,0,183,0,215,32,20,
  32,34,32,38,32,58,255,255,0,0,0,32,0,176,0,183,0,215,32,19,32,34,32,38,
  32,57,255,255,255,225,255,176,255,171,255,140,224,81,224,68,224,65,224,47,0,1,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,42,0,42,0,71,
  0,95,0,149,0,212,1,38,1,119,1,134,1,170,1,207,1,242,2,11,2,32,2,45,
  2,63,2,79,2,129,2,148,2,190,2,246,3,20,3,67,3,124,3,144,3,214,4,14,
  4,26,4,38,4,58,4,78,4,98,4,150,4,253,5,25,5,79,5,129,5,167,5,190,
  5,211,6,10,6,34,6,48,6,76,6,106,6,122,6,155,6,179,6,231,7,12,7,70,
  7,114,7,172,7,190,7,221,7,243,8,20,8,54,8,79,8,102,8,120,8,135,8,153,
  8,173,8,185,8,200,9,1,9,46,9,89,9,134,9,184,9,217,10,22,10,54,10,79,
  10,113,10,143,10,157,10,205,10,237,11,27,11,73,11,119,11,146,11,201,11,235,12,10,
  12,31,12,62,12,94,12,128,12,151,12,192,12,206,12,248,13,30,13,64,13,95,13,117,
  13,150,13,160,13,170,13,193,13,209,13,229,13,249,14,6,0,5,0,100,0,0,3,40,
  5,176,0,3,0,6,0,9,0,12,0,15,0,0,51,17,33,17,9,2,3,1,17,5,
  17,1,3,33,1,100,2,196,253,154,1,2,1,1,229,1,18,253,168,1,12,228,2,3,
  254,255,5,176,250,80,5,122,253,162,2,94,253,94,253,124,5,8,12,251,16,2,120,253,
  94,2,94,0,0,2,0,144,255,242,1,163,5,176,0,3,0,13,0,0,1,3,35,3,
  3,52,54,50,22,21,20,6,34,38,1,151,24,209,24,6,72,130,73,73,130,72,5,176,
  251,253,4,3,250,196,56,75,75,56,55,75,75,0,0,2,0,101,3,245,2,65,6,0,
  0,4,0,9,0,0,1,3,35,17,51,5,3,35,17,51,1,20,36,139,175,1,45,36,
  139,175,5,120,254,125,2,11,136,254,125,2,11,0,0,2,0,97,0,0,4,189,5,176,
  0,27,0,31,0,0,33,19,35,3,35,19,35,53,33,19,35,53,33,19,51,3,51,19,
  51,3,51,21,35,3,51,21,35,3,1,51,19,35,2,132,76,224,77,167,77,232,1,5,
  59,244,1,17,78,167,78,225,77,168,78,208,237,59,221,250,77,254,226,225,58,224,1,154,
  254,102,1,154,158,1,57,159,1,160,254,96,1,160,254,96,159,254,199,158,254,102,2,56,
  1,57,0,1,0,101,255,44,4,38,6,156,0,43,0,0,1,20,6,7,21,35,53,38,
  38,53,51,20,22,51,50,54,53,52,38,39,38,38,53,52,54,55,53,51,21,22,22,21,
  35,52,38,35,34,6,21,20,22,23,22,22,4,38,212,179,159,159,252,243,154,89,113,119,
  112,135,195,220,204,171,159,170,194,242,110,99,106,98,104,141,193,224,1,127,175,205,20,195,
  195,17,210,234,154,111,111,91,85,114,46,67,197,184,169,211,19,219,221,24,240,202,126,146,
  113,89,84,107,51,70,195,0,0,5,0,100,255,235,5,137,5,197,0,12,0,25,0,29,
  0,42,0,55,0,0,19,52,54,51,50,22,21,21,20,6,32,38,53,51,20,22,51,50,
  54,53,53,52,38,34,6,21,37,1,39,1,3,52,54,51,50,22,21,21,20,6,32,38,
  53,51,20,22,51,50,53,53,52,38,35,34,6,21,100,161,147,149,160,159,254,216,162,169,
  70,71,70,67,69,140,69,3,133,253,57,125,2,199,245,162,146,149,160,159,254,216,162,169,
  73,68,138,69,71,70,69,4,152,124,177,177,124,77,122,177,177,122,59,94,93,60,77,61,
  94,94,61,71,251,142,73,4,114,252,61,123,177,177,123,78,123,177,177,123,60,94,169,63,
  60,94,94,60,0,3,0,86,255,236,5,17,5,196,0,29,0,41,0,51,0,0,33,39,
  6,35,34,36,53,52,54,55,38,38,53,52,54,51,50,22,21,20,6,7,7,1,54,53,
  51,16,7,23,1,20,23,55,54,54,53,52,38,35,34,6,3,20,22,51,50,55,1,7,
  6,6,3,247,82,157,214,220,255,0,152,120,69,82,210,173,166,197,109,88,99,1,24,61,
  211,126,214,252,162,103,87,63,54,80,73,77,77,106,124,116,122,107,254,195,31,81,40,97,
  117,233,181,128,173,85,87,156,84,173,196,194,130,104,152,64,72,254,179,121,146,254,239,167,
  254,4,88,83,127,59,41,80,61,52,86,100,252,245,96,131,82,1,119,22,63,111,0,1,
  0,83,3,252,1,11,6,0,0,4,0,0,1,3,35,17,51,1,11,25,159,184,5,131,
  254,121,2,4,0,1,0,129,254,49,2,163,6,95,0,16,0,0,19,16,18,18,55,23,
  6,2,3,21,16,18,23,7,38,2,2,129,164,231,103,48,112,211,2,212,113,48,103,231,
  164,2,80,1,33,1,173,1,14,51,142,85,254,83,254,147,36,254,136,254,77,91,135,51,
  1,14,1,172,0,1,0,40,254,49,2,82,6,95,0,17,0,0,1,16,2,2,7,39,
  54,18,17,53,52,2,38,39,55,22,18,18,2,82,166,236,104,48,113,212,104,151,70,48,
  104,236,166,2,64,254,228,254,82,254,238,51,135,86,1,184,1,120,20,250,1,106,233,57,
  135,51,254,238,254,83,0,1,0,28,2,78,3,117,5,177,0,14,0,0,19,19,37,55,
  5,3,51,3,37,23,5,19,7,3,3,126,206,254,208,54,1,47,15,179,15,1,41,54,
  254,202,200,145,181,177,2,188,1,16,88,170,118,1,89,254,161,115,171,88,254,245,105,1,
  32,254,232,0,0,1,0,69,0,146,4,43,4,182,0,11,0,0,1,33,17,35,17,33,
  53,33,17,51,17,33,4,43,254,131,235,254,130,1,126,235,1,125,2,68,254,78,1,178,
  221,1,149,254,107,0,0,1,0,29,254,184,1,93,0,235,0,9,0,0,37,21,20,6,
  7,39,54,54,53,53,1,93,103,86,131,42,60,235,172,102,218,71,77,67,144,94,181,0,
  0,1,0,71,2,10,2,84,2,205,0,3,0,0,1,21,33,53,2,84,253,243,2,205,
  195,195,0,1,0,136,255,244,1,163,1,1,0,7,0,0,54,54,50,22,20,6,34,38,
  136,74,134,75,75,134,74,179,78,78,114,77,77,0,0,1,0,2,255,131,2,255,5,176,
  0,3,0,0,1,1,35,1,2,255,253,195,192,2,62,5,176,249,211,6,45,0,0,2,
  0,105,255,236,4,34,5,196,0,13,0,26,0,0,1,16,0,35,34,0,3,17,16,0,
  51,50,0,19,39,52,38,35,34,6,7,17,20,22,50,54,55,4,34,254,253,216,211,254,
  249,4,1,6,214,215,1,5,1,242,122,113,110,122,1,123,224,120,1,2,102,254,166,254,
  224,1,24,1,77,1,0,1,91,1,24,254,234,254,169,31,220,175,172,215,254,197,221,183,
  180,216,0,1,0,168,0,0,3,0,5,181,0,6,0,0,1,17,35,17,5,53,37,3,
  0,243,254,155,2,57,5,181,250,75,4,146,122,205,208,0,0,1,0,81,0,0,4,65,
  5,196,0,24,0,0,37,21,33,53,1,54,54,53,52,38,35,34,6,21,35,52,0,51,
  50,22,21,20,2,7,1,4,65,252,44,1,218,120,86,113,104,124,124,243,1,2,236,218,
  239,163,130,254,185,195,195,168,2,4,133,159,74,100,131,151,117,191,1,16,218,182,134,254,
  253,138,254,162,0,1,0,80,255,236,4,22,5,196,0,38,0,0,1,53,51,50,54,53,
  52,35,34,6,21,35,52,36,51,50,4,21,20,6,7,22,22,21,20,4,35,34,36,53,
  51,20,22,51,50,54,53,52,33,1,135,143,125,123,221,97,128,242,1,5,202,209,1,2,
  111,113,134,112,254,232,209,189,254,224,243,130,108,113,129,254,234,2,135,192,125,98,219,110,
  94,173,226,209,205,86,177,51,45,187,108,205,223,208,203,96,120,121,112,239,0,2,0,53,
  0,0,4,89,5,176,0,10,0,14,0,0,19,1,51,17,51,21,35,17,35,17,33,55,
  33,17,7,53,2,116,250,182,182,243,253,139,248,1,125,17,1,216,3,216,252,88,195,254,
  187,1,69,195,2,93,30,0,0,1,0,129,255,236,4,58,5,176,0,28,0,0,1,39,
  19,33,21,33,3,54,51,50,18,21,20,2,35,34,36,39,51,22,51,50,54,53,52,38,
  35,34,6,1,112,193,78,3,14,253,188,39,102,125,207,232,236,243,182,254,233,13,236,28,
  210,118,119,132,127,85,88,2,162,48,2,222,209,254,164,57,254,249,227,202,254,228,211,203,
  219,167,131,125,162,49,0,2,0,118,255,236,4,55,5,183,0,21,0,34,0,0,1,20,
  2,35,34,0,17,53,52,18,36,33,51,21,35,6,6,7,54,51,50,18,5,20,22,51,
  50,54,53,52,38,35,34,6,7,4,55,247,225,220,254,243,140,1,60,1,7,28,25,232,
  223,19,114,184,213,202,253,50,144,99,110,125,119,115,85,131,28,1,221,209,254,224,1,67,
  1,8,79,216,1,116,229,200,2,245,173,121,254,215,137,175,179,166,129,121,177,96,68,0,
  0,1,0,70,0,0,4,55,5,176,0,6,0,0,1,21,1,33,1,33,53,4,55,253,
  186,255,0,2,70,253,15,5,176,134,250,214,4,237,195,0,0,3,0,105,255,236,4,35,
  5,196,0,21,0,33,0,44,0,0,1,20,4,32,36,53,52,54,55,38,38,53,52,54,
  32,22,21,20,6,7,22,22,1,52,38,35,34,6,21,20,22,51,50,54,19,52,38,35,
  34,6,21,20,22,50,54,4,35,254,239,254,106,254,237,127,108,94,109,251,1,132,251,110,
  94,108,128,254,237,109,93,92,110,109,94,93,108,32,129,106,107,126,126,218,124,1,141,203,
  214,215,202,121,184,49,48,168,106,193,210,210,193,106,168,48,50,183,2,34,96,121,117,100,
  98,120,120,253,212,108,133,133,108,110,125,125,0,2,0,94,255,249,4,19,5,196,0,21,
  0,34,0,0,1,20,2,4,33,35,53,51,54,54,55,6,35,34,2,53,52,18,51,50,
  18,21,37,20,22,51,50,54,55,53,52,38,35,34,6,4,19,125,254,200,254,231,20,31,
  252,199,12,122,164,212,207,246,224,240,239,253,59,113,117,79,127,31,143,95,109,120,3,37,
  210,254,141,231,202,3,236,176,129,1,39,192,210,1,42,254,161,241,91,119,185,91,67,95,
  175,181,179,0,255,255,0,130,255,244,1,157,4,82,0,38,0,15,250,0,0,7,0,15,
  255,250,3,81,255,255,0,47,254,184,1,136,4,82,0,39,0,15,255,229,3,81,0,6,
  0,13,18,0,0,1,0,63,0,165,3,133,4,78,0,6,0,0,37,1,53,1,21,5,
  5,3,133,252,186,3,70,253,177,2,79,165,1,116,193,1,116,242,228,225,0,2,0,145,
  1,100,3,240,3,214,0,3,0,7,0,0,1,21,33,53,1,21,33,53,3,240,252,161,
  3,95,252,161,3,214,201,201,254,88,202,202,0,1,0,129,0,165,3,224,4,78,0,6,
  0,0,19,1,21,1,53,37,37,129,3,95,252,161,2,106,253,150,4,78,254,141,194,254,
  140,239,232,228,0,2,0,60,255,244,3,152,5,196,0,23,0,33,0,0,1,35,62,3,
  53,52,35,34,6,7,35,54,54,51,50,22,21,20,6,7,6,6,3,52,54,50,22,21,
  20,6,34,38,2,65,227,2,63,216,46,182,72,119,1,243,2,250,183,201,224,88,164,53,
  37,245,73,130,73,73,130,73,1,173,135,153,212,92,60,193,85,96,189,194,204,182,101,161,
  157,51,118,254,128,56,75,75,56,55,75,75,0,2,0,83,254,59,6,220,5,145,0,48,
  0,58,0,0,1,6,6,35,32,0,19,18,18,36,51,32,0,3,14,2,35,34,39,6,
  35,34,38,55,18,0,51,50,22,23,3,6,22,51,50,54,55,18,0,33,32,0,3,2,
  0,33,50,55,1,2,51,50,54,55,19,38,35,34,4,241,71,209,94,254,124,254,92,17,
  12,218,1,124,255,1,129,1,150,17,5,87,183,150,181,60,104,157,148,149,19,26,1,2,
  163,113,128,59,51,11,79,46,108,124,6,16,254,210,254,177,254,202,254,151,15,17,1,70,
  1,55,193,138,254,15,25,167,43,101,36,45,46,50,233,254,148,45,44,1,231,1,181,1,
  17,1,176,249,254,17,254,100,129,248,162,154,153,244,199,1,4,1,51,64,43,253,204,123,
  74,228,165,1,96,1,152,254,61,254,159,254,143,254,109,73,2,139,254,235,79,98,1,238,
  18,0,0,2,0,18,0,0,5,67,5,176,0,7,0,10,0,0,51,1,51,1,33,3,
  33,3,19,33,3,18,2,38,227,2,40,254,248,119,253,204,118,189,1,167,212,5,176,250,
  80,1,84,254,172,2,31,2,94,0,0,3,0,149,0,0,4,163,5,176,0,14,0,22,
  0,30,0,0,1,20,4,35,33,17,33,50,4,21,20,6,7,22,22,1,17,51,50,54,
  53,52,39,1,52,39,33,17,33,50,54,4,163,254,243,235,253,234,1,242,238,1,12,108,
  104,125,121,252,238,248,119,132,241,1,20,227,254,205,1,26,124,128,1,169,209,216,5,176,
  190,204,99,161,43,33,187,2,202,254,103,107,97,200,5,252,197,224,7,254,57,124,0,1,
  0,102,255,236,4,236,5,196,0,27,0,0,1,51,6,0,33,32,0,17,53,16,0,33,
  32,0,23,35,38,38,35,34,6,7,21,20,18,51,50,54,3,239,252,19,254,229,254,244,
  254,243,254,194,1,68,1,19,1,5,1,20,22,252,16,135,156,172,171,1,155,177,163,137,
  1,218,212,254,230,1,118,1,59,118,1,58,1,119,254,229,218,139,158,251,227,125,218,254,
  245,150,0,2,0,149,0,0,4,210,5,176,0,9,0,19,0,0,51,17,33,32,0,17,
  21,16,0,33,3,17,51,50,18,55,53,52,38,35,149,1,173,1,36,1,108,254,147,254,
  210,166,166,204,208,2,207,196,5,176,254,135,254,197,73,254,197,254,136,4,229,251,229,1,
  0,225,83,236,251,0,0,1,0,149,0,0,4,77,5,176,0,11,0,0,51,17,33,21,
  33,17,33,21,33,17,33,21,149,3,177,253,75,2,86,253,170,2,188,5,176,203,254,109,
  199,254,63,202,0,1,0,149,0,0,4,49,5,176,0,9,0,0,51,17,33,21,33,17,
  33,21,33,17,149,3,156,253,96,2,74,253,182,5,176,203,254,78,202,253,151,0,0,1,
  0,106,255,236,4,240,5,196,0,31,0,0,1,17,6,6,35,32,0,17,53,16,0,33,
  32,0,23,35,38,38,35,34,6,7,21,20,18,51,50,54,55,17,33,53,4,240,46,246,
  244,254,237,254,165,1,62,1,26,1,11,1,9,26,248,19,139,148,176,171,2,200,175,131,
  119,26,254,213,2,235,253,211,62,148,1,110,1,78,95,1,78,1,111,254,250,203,113,149,
  254,232,107,240,254,254,60,28,1,30,191,0,0,1,0,149,0,0,5,25,5,176,0,11,
  0,0,33,17,33,17,35,17,51,17,33,17,51,17,4,28,253,117,252,252,2,139,253,2,
  136,253,120,5,176,253,162,2,94,250,80,0,0,1,0,164,0,0,1,160,5,176,0,3,
  0,0,1,17,35,17,1,160,252,5,176,250,80,5,176,0,0,1,0,45,255,236,3,229,
  5,176,0,15,0,0,1,51,17,20,4,35,34,36,53,51,20,22,51,50,54,53,2,233,
  252,254,241,205,207,254,243,253,124,99,98,126,5,176,252,4,218,238,213,223,126,108,131,123,
  0,1,0,149,0,0,5,24,5,176,0,12,0,0,33,1,7,17,35,17,51,17,55,1,
  33,1,1,3,236,254,75,166,252,252,141,1,170,1,50,253,227,2,59,2,118,176,254,58,
  5,176,253,84,174,1,254,253,124,252,212,0,0,1,0,149,0,0,4,39,5,176,0,5,
  0,0,37,21,33,17,51,17,4,39,252,110,252,202,202,5,176,251,26,0,0,1,0,149,
  0,0,6,107,5,176,0,14,0,0,1,1,33,17,35,17,19,1,35,1,19,17,35,17,
  33,3,128,1,163,1,72,252,24,254,83,181,254,83,25,252,1,71,1,84,4,92,250,80,
  1,224,2,128,251,160,4,97,253,127,254,32,5,176,0,1,0,149,0,0,5,24,5,176,
  0,9,0,0,1,17,35,1,17,35,17,51,1,17,5,24,253,253,118,252,252,2,140,5,
  176,250,80,4,9,251,247,5,176,251,244,4,12,0,0,2,0,102,255,236,5,31,5,196,
  0,13,0,27,0,0,1,16,0,33,32,0,17,53,16,0,33,32,0,17,39,52,2,35,
  34,2,21,21,20,18,51,50,18,53,5,31,254,181,254,240,254,246,254,172,1,82,1,10,
  1,16,1,77,253,186,166,162,189,191,162,168,182,2,179,254,180,254,133,1,123,1,76,73,
  1,76,1,124,254,132,254,180,2,245,1,0,255,0,245,75,245,254,254,1,2,245,0,2,
  0,149,0,0,4,212,5,176,0,10,0,19,0,0,1,17,35,17,33,50,0,21,20,4,
  35,1,17,33,50,54,53,52,38,35,1,145,252,2,45,253,1,21,254,235,253,254,207,1,
  49,147,129,129,147,2,29,253,227,5,176,254,250,201,211,241,2,200,254,3,138,109,102,160,
  0,2,0,96,255,4,5,27,5,196,0,18,0,32,0,0,5,7,37,6,35,32,0,17,
  53,16,0,33,32,0,17,21,20,2,7,3,52,2,35,34,2,21,21,20,18,51,50,18,
  53,5,27,165,254,203,63,68,254,246,254,172,1,82,1,10,1,16,1,77,132,117,4,185,
  167,162,188,190,162,168,182,105,147,245,13,1,123,1,76,73,1,76,1,124,254,132,254,180,
  73,206,254,207,89,2,163,245,1,0,255,0,245,75,245,254,254,1,2,245,0,2,0,149,
  0,0,4,222,5,176,0,14,0,23,0,0,33,1,33,17,35,17,33,50,4,21,20,6,
  7,1,21,1,17,33,50,54,53,52,38,39,3,208,254,220,254,229,252,2,0,251,1,18,
  142,125,1,71,252,179,1,4,136,137,128,141,2,49,253,207,5,176,224,215,146,198,52,253,
  160,13,4,229,254,23,137,104,112,135,1,0,0,1,0,74,255,236,4,138,5,196,0,37,
  0,0,1,52,38,36,36,53,52,36,51,50,0,21,35,52,38,35,34,6,21,20,22,23,
  22,22,21,20,4,35,34,36,38,53,51,20,22,51,50,54,3,141,115,254,97,254,253,1,
  29,231,247,1,22,252,134,142,133,127,152,153,247,237,254,231,237,140,254,251,169,253,183,134,
  132,133,1,120,86,106,129,201,175,176,227,254,249,182,107,137,115,86,82,107,45,72,216,174,
  184,214,102,204,154,138,122,108,0,1,0,45,0,0,4,176,5,176,0,7,0,0,19,53,
  33,21,33,17,35,17,45,4,131,254,59,252,4,229,203,203,251,27,4,229,0,1,0,126,
  255,236,4,190,5,176,0,17,0,0,1,51,17,20,4,35,34,36,53,17,51,17,20,22,
  51,50,54,53,3,193,253,254,194,227,233,254,202,251,159,133,135,157,5,176,252,51,250,253,
  253,250,3,205,252,51,156,145,145,156,0,1,0,18,0,0,5,29,5,176,0,6,0,0,
  1,1,35,1,33,1,1,5,29,253,244,245,253,246,1,21,1,111,1,113,5,176,250,80,
  5,176,251,142,4,114,0,1,0,49,0,0,6,229,5,176,0,12,0,0,1,1,35,1,
  1,35,1,51,19,1,51,1,19,6,229,254,177,243,254,235,254,229,242,254,176,251,226,1,
  22,212,1,19,223,5,176,250,80,4,38,251,218,5,176,251,189,4,67,251,185,4,71,0,
  0,1,0,41,0,0,4,234,5,176,0,11,0,0,9,2,33,1,1,33,1,1,33,1,
  1,1,88,1,50,1,49,1,36,254,72,1,195,254,217,254,199,254,198,254,217,1,195,254,
  72,5,176,253,242,2,14,253,46,253,34,2,23,253,233,2,222,2,210,0,0,1,0,8,
  0,0,4,214,5,176,0,8,0,0,9,2,33,1,17,35,17,1,1,33,1,78,1,79,
  1,24,254,24,254,254,24,5,176,253,79,2,177,252,104,253,232,2,24,3,152,0,0,1,
  0,80,0,0,4,140,5,176,0,9,0,0,19,53,33,21,1,33,21,33,53,1,86,4,
  30,253,14,3,10,251,196,2,242,4,229,203,160,251,186,202,164,4,65,0,0,1,0,132,
  254,187,2,29,6,143,0,7,0,0,1,21,35,17,51,21,33,17,2,29,166,166,254,103,
  6,143,190,249,168,190,7,212,0,1,0,20,255,131,3,100,5,176,0,3,0,0,5,1,
  51,1,2,116,253,160,240,2,96,125,6,45,249,211,0,1,0,13,254,187,1,166,6,143,
  0,7,0,0,19,53,33,17,33,53,51,17,13,1,153,254,103,166,5,209,190,248,44,190,
  6,88,0,1,0,53,2,217,3,54,5,176,0,6,0,0,19,1,51,1,35,3,3,53,
  1,43,172,1,42,206,178,178,2,217,2,215,253,41,1,207,254,49,0,1,0,3,255,64,
  3,153,0,0,0,3,0,0,33,21,33,53,3,153,252,106,192,192,0,1,0,49,4,210,
  2,10,6,0,0,3,0,0,1,19,35,1,1,70,196,202,254,241,6,0,254,210,1,46,
  0,2,0,91,255,236,3,252,4,78,0,29,0,38,0,0,33,38,39,6,35,34,38,53,
  52,36,51,51,53,52,38,35,34,6,21,35,52,54,51,50,22,21,17,20,23,21,36,54,
  55,53,35,6,21,20,22,3,4,18,10,115,170,162,206,1,2,234,153,95,95,87,103,242,
  241,205,182,237,41,254,81,122,25,148,255,88,37,65,122,186,138,173,184,71,85,101,86,62,
  128,204,186,186,254,32,146,87,17,175,81,49,204,6,175,65,88,0,0,2,0,125,255,236,
  4,50,6,0,0,15,0,26,0,0,1,20,2,35,34,39,7,35,17,51,17,54,51,50,
  18,21,1,34,7,17,22,51,50,54,53,52,38,4,50,215,206,189,108,11,220,242,106,178,
  209,214,254,27,155,67,68,156,135,106,104,2,18,239,254,201,138,118,6,0,253,210,124,254,
  205,244,1,97,140,254,68,141,202,170,143,210,0,1,0,80,255,236,3,245,4,78,0,27,
  0,0,37,50,54,55,51,6,6,35,34,2,53,53,52,18,51,50,22,23,35,38,38,35,
  34,6,21,21,20,22,2,58,88,122,5,228,5,255,181,250,242,243,249,196,240,5,228,5,
  112,99,148,99,98,175,103,80,157,221,1,64,223,36,222,1,65,228,179,87,125,219,129,36,
  131,217,0,2,0,80,255,236,4,4,6,0,0,15,0,27,0,0,33,39,6,35,34,2,
  53,53,52,18,51,50,23,17,51,17,1,20,22,51,50,55,17,38,35,34,6,21,3,39,
  11,108,183,195,230,229,198,172,106,243,253,62,114,131,149,69,68,148,132,115,112,132,1,57,
  236,21,247,1,49,119,2,41,250,0,2,17,145,205,134,1,205,130,208,146,0,2,0,84,
  255,236,4,12,4,78,0,20,0,28,0,0,37,6,6,35,34,0,53,53,16,0,51,50,
  18,21,21,33,22,22,51,50,55,1,34,6,7,33,53,38,38,3,250,52,203,162,241,254,
  236,1,27,206,236,227,253,61,10,149,128,167,104,254,198,100,122,18,1,206,5,96,184,76,
  128,1,44,223,41,1,0,1,46,254,212,234,103,125,165,134,2,86,140,124,18,95,151,0,
  0,1,0,45,0,0,2,215,6,21,0,20,0,0,1,35,17,35,17,35,53,51,53,52,
  54,51,50,23,7,38,35,34,7,21,51,2,161,219,244,165,165,204,176,67,70,6,42,52,
  171,2,219,3,135,252,121,3,135,179,103,181,191,18,190,8,168,107,0,2,0,82,254,86,
  4,13,4,78,0,26,0,38,0,0,1,34,38,39,55,22,51,50,54,53,53,6,35,34,
  2,39,53,52,18,51,50,23,55,51,17,20,4,1,20,22,51,50,55,17,38,35,34,6,
  21,2,34,89,244,72,115,114,162,121,141,106,174,198,232,2,233,201,186,105,10,220,254,239,
  254,73,120,131,150,68,67,149,132,121,254,86,81,98,145,133,133,127,73,118,1,53,232,29,
  247,1,49,127,107,251,228,219,237,3,187,145,205,128,1,217,124,208,146,0,0,1,0,122,
  0,0,3,249,6,0,0,17,0,0,1,34,7,17,35,17,51,17,54,51,50,22,23,17,
  35,17,52,38,2,71,147,72,242,242,118,183,159,192,1,243,98,3,136,131,252,251,6,0,
  253,195,139,182,218,253,66,2,187,113,92,0,0,2,0,125,0,0,1,145,5,213,0,7,
  0,11,0,0,18,54,50,22,20,6,34,38,5,17,35,17,125,73,130,73,73,130,73,1,
  2,243,5,140,73,73,112,73,73,226,251,198,4,58,0,2,255,180,254,75,1,134,5,213,
  0,7,0,19,0,0,18,54,50,22,20,6,34,38,23,51,17,16,33,34,39,53,22,51,
  50,53,114,73,130,73,73,130,73,21,243,254,188,68,62,46,42,123,5,140,73,73,112,73,
  73,226,251,102,254,171,17,191,8,141,0,1,0,125,0,0,4,55,6,0,0,12,0,0,
  33,1,7,17,35,17,51,17,55,1,33,1,1,3,30,254,191,109,243,243,77,1,43,1,
  36,254,109,1,190,1,210,112,254,158,6,0,252,138,96,1,80,254,62,253,136,0,0,1,
  0,140,0,0,1,127,6,0,0,3,0,0,1,17,35,17,1,127,243,6,0,250,0,6,
  0,0,0,1,0,125,0,0,6,122,4,78,0,30,0,0,1,34,7,17,35,17,51,23,
  54,51,50,23,54,51,50,22,23,17,35,17,52,38,35,34,6,7,17,35,17,52,38,2,
  63,147,61,242,228,7,115,198,215,81,119,214,165,190,1,243,105,90,83,105,20,243,104,3,
  136,120,252,240,4,58,113,133,165,165,184,212,253,62,2,190,120,82,90,72,253,26,2,188,
  114,90,0,1,0,122,0,0,3,249,4,78,0,17,0,0,1,34,7,17,35,17,51,23,
  54,51,50,22,23,17,35,17,52,38,2,71,145,73,243,229,8,119,194,156,188,1,243,97,
  3,136,132,252,252,4,58,124,144,180,217,253,63,2,190,112,90,0,0,2,0,80,255,236,
  4,62,4,78,0,13,0,27,0,0,19,52,0,51,50,0,23,21,20,0,35,34,0,53,
  51,20,22,51,50,54,53,53,52,38,35,34,6,21,80,1,10,236,235,1,9,4,254,247,
  237,238,254,246,242,125,137,134,125,125,136,135,125,2,39,235,1,60,254,204,230,34,236,254,
  198,1,58,236,146,209,209,146,21,143,213,213,143,0,0,2,0,125,254,96,4,49,4,78,
  0,15,0,27,0,0,5,34,39,17,35,17,51,23,54,51,50,18,21,21,20,2,1,34,
  7,17,22,51,50,54,53,53,52,38,2,140,177,108,242,223,10,109,183,206,217,218,254,239,
  150,65,67,150,130,116,117,20,117,253,255,5,218,109,129,254,208,248,20,236,254,198,3,156,
  125,254,35,127,211,145,20,146,207,0,0,2,0,80,254,96,4,3,4,78,0,15,0,27,
  0,0,1,17,35,17,6,35,34,2,53,53,52,18,51,50,23,55,1,20,22,51,50,55,
  17,38,35,34,6,21,4,3,243,106,170,202,226,224,206,180,107,13,254,24,116,131,146,69,
  70,143,131,118,4,58,250,38,1,253,113,1,58,237,20,247,1,48,126,106,253,216,145,210,
  124,1,234,118,211,145,0,1,0,125,0,0,2,181,4,78,0,13,0,0,1,34,7,17,
  35,17,51,23,54,51,50,23,7,38,2,81,169,57,242,232,6,86,158,49,37,2,50,3,
  100,128,253,28,4,58,122,142,13,229,8,0,0,1,0,76,255,236,3,202,4,78,0,37,
  0,0,1,52,38,39,38,38,53,52,54,51,50,22,21,35,52,38,35,34,6,21,20,22,
  23,22,22,21,20,6,35,34,38,53,51,22,22,51,50,54,2,220,85,142,176,217,224,193,
  203,231,243,96,95,92,88,88,134,188,211,238,199,223,234,236,4,139,80,96,101,1,38,52,
  77,30,37,147,137,133,195,198,146,63,99,82,54,52,64,29,41,145,145,144,184,228,134,100,
  80,74,0,1,0,8,255,236,2,115,5,65,0,21,0,0,37,6,35,34,38,53,17,35,
  53,51,17,51,17,51,21,35,17,20,22,51,50,55,2,115,70,90,121,159,179,179,242,191,
  191,63,50,42,43,4,24,137,167,2,107,179,1,7,254,249,179,253,163,73,43,10,0,1,
  0,119,255,236,3,247,4,58,0,17,0,0,33,39,6,35,34,38,39,17,51,17,20,22,
  51,50,55,17,51,17,3,18,6,106,198,158,198,1,243,103,69,174,64,243,106,126,186,213,
  2,191,253,67,117,85,125,3,10,251,198,0,0,1,0,23,0,0,3,219,4,58,0,6,
  0,0,1,1,35,1,51,19,19,3,219,254,137,211,254,134,251,234,228,4,58,251,198,4,
  58,252,251,3,5,0,0,1,0,34,0,0,5,205,4,58,0,12,0,0,1,1,35,3,
  3,35,1,51,19,19,51,19,19,5,205,254,216,200,232,228,199,254,216,237,175,221,184,225,
  172,4,58,251,198,2,230,253,26,4,58,253,30,2,226,253,20,2,236,0,0,1,0,31,
  0,0,3,233,4,58,0,11,0,0,1,19,19,33,1,1,33,3,3,33,1,1,1,56,
  202,206,1,13,254,182,1,86,254,243,216,215,254,242,1,86,254,182,4,58,254,156,1,100,
  253,236,253,218,1,114,254,142,2,38,2,20,0,1,0,12,254,75,3,215,4,58,0,17,
  0,0,5,14,2,35,34,39,53,23,50,54,55,55,1,33,19,19,33,2,40,22,76,133,
  104,49,68,46,90,94,27,35,254,132,1,7,229,220,1,3,164,59,126,88,17,188,3,65,
  81,94,4,53,253,33,2,223,0,1,0,82,0,0,3,192,4,58,0,9,0,0,19,53,
  33,21,1,33,21,33,53,1,93,3,78,253,214,2,63,252,146,2,38,3,118,196,153,253,
  34,195,159,2,215,0,0,1,0,56,254,152,2,146,6,61,0,26,0,0,19,53,50,53,
  53,52,54,55,23,6,6,21,21,20,7,22,21,21,20,22,23,7,38,38,53,53,52,56,
  194,154,206,48,104,76,172,172,76,104,48,206,154,2,18,178,226,198,158,249,58,140,35,176,
  114,194,229,90,90,229,194,114,176,35,141,58,250,158,198,226,0,1,0,174,254,242,1,86,
  5,176,0,3,0,0,1,17,35,17,1,86,168,5,176,249,66,6,190,0,0,1,0,28,
  254,152,2,118,6,61,0,26,0,0,1,21,34,21,21,20,6,7,39,54,54,53,53,52,
  55,38,53,53,52,38,39,55,22,22,21,21,20,2,118,194,153,207,48,104,75,182,182,75,
  104,48,207,153,2,196,178,226,198,158,250,58,141,35,176,114,195,232,86,86,232,195,114,176,
  35,140,58,249,158,198,226,0,0,1,0,118,1,132,4,220,3,48,0,23,0,0,1,55,
  20,6,35,34,46,2,35,34,6,21,35,52,54,51,50,22,23,22,51,50,54,4,33,187,
  187,144,78,123,151,68,38,67,77,193,185,145,83,136,71,94,74,67,84,3,18,1,173,226,
  57,134,34,104,84,175,216,70,65,90,110,0,0,2,0,127,3,175,2,140,5,197,0,9,
  0,19,0,0,18,54,51,50,22,20,6,35,34,38,55,20,22,50,54,53,52,38,34,6,
  127,156,109,107,153,153,107,109,156,139,75,102,70,70,102,75,5,38,159,159,220,155,155,110,
  53,71,71,53,55,73,73,0,0,2,0,95,0,1,3,243,4,252,0,11,0,15,0,0,
  1,33,17,35,17,33,53,33,17,51,17,33,3,21,33,53,3,243,254,169,215,254,154,1,
  102,215,1,87,37,252,176,2,188,254,124,1,132,199,1,121,254,135,253,66,196,196,0,1,
  0,142,2,70,1,170,3,82,0,11,0,0,19,52,54,51,50,22,21,20,6,35,34,38,
  142,74,68,67,75,75,67,68,74,2,203,57,78,78,57,56,77,77,0,1,0,77,0,215,
  3,236,4,134,0,11,0,0,1,7,1,1,39,1,1,55,1,1,23,1,3,236,148,254,
  196,254,197,148,1,60,254,196,148,1,59,1,60,148,254,196,1,108,149,1,66,254,190,149,
  1,66,1,67,149,254,190,1,66,149,254,189,255,255,0,158,2,110,4,151,3,50,0,70,
  0,106,223,0,76,205,64,0,255,255,0,130,2,110,5,207,3,50,0,70,0,106,132,0,
  102,102,64,0,0,1,0,136,2,7,2,69,3,219,0,12,0,0,19,52,54,51,50,22,
  21,21,20,6,34,38,53,136,120,101,104,120,119,206,120,3,3,95,121,121,95,39,94,119,
  119,94,255,255,0,140,255,244,5,41,1,1,0,34,0,15,4,0,0,35,0,15,1,205,
  0,0,0,3,0,15,3,134,0,0,0,1,0,108,0,139,2,52,3,170,0,6,0,0,
  37,1,53,1,51,3,19,1,140,254,224,1,32,168,247,247,139,1,134,19,1,134,254,112,
  254,113,0,1,0,85,0,138,2,28,3,169,0,6,0,0,19,1,21,1,35,19,3,252,
  1,32,254,224,167,247,247,3,169,254,122,19,254,122,1,144,1,143,0,1,0,159,2,110,
  3,239,3,50,0,3,0,0,1,21,33,53,3,239,252,176,3,50,196,196,0,0,0,2,
  0,30,0,3,0,1,4,9,0,1,0,26,0,0,0,3,0,1,4,9,0,2,0,14,
  0,26,0,82,0,111,0,98,0,111,0,116,0,111,0,32,0,77,0,101,0,100,0,105,
  0,117,0,109,0,82,0,101,0,103,0,117,0,108,0,97,0,114,0,0,0,3,0,0,
  0,0,0,0,255,106,0,100,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,1,0,0,0,12,0,0,0,0,0,0,0,2,0,2,0,34,0,59,
  0,1,0,66,0,91,0,1,0,1,0,0,0,10,0,48,0,62,0,4,68,70,76,84,
  0,26,99,121,114,108,0,26,103,114,101,107,0,26,108,97,116,110,0,26,0,4,0,0,
  0,0,255,255,0,1,0,0,0,1,107,101,114,110,0,8,0,0,0,1,0,0,0,1,
  0,4,0,2,0,0,0,2,0,10,2,32,0,1,0,66,0,4,0,0,0,28,0,126,
  0,132,0,132,0,138,0,152,0,158,0,172,0,186,0,196,1,10,1,16,1,22,1,28,
  1,46,1,60,1,74,1,92,1,110,1,148,1,154,1,164,1,206,1,228,1,242,2,6,
  1,248,2,6,2,12,0,1,0,28,0,1,0,3,0,8,0,9,0,16,0,34,0,36,
  0,38,0,39,0,44,0,45,0,49,0,50,0,51,0,53,0,55,0,56,0,58,0,59,
  0,60,0,71,0,76,0,83,0,85,0,87,0,88,0,90,0,92,0,1,0,53,255,196,
  0,1,0,88,0,11,0,3,0,55,0,20,0,56,0,38,0,58,0,22,0,1,0,16,
  255,8,0,3,0,32,255,175,0,85,255,239,0,88,255,223,0,3,0,10,255,230,0,62,
  255,244,0,94,255,239,0,2,0,71,255,238,0,88,255,234,0,17,0,13,254,238,0,15,
  254,238,0,34,255,64,0,43,255,48,0,53,0,20,0,66,255,222,0,68,255,235,0,69,
  255,235,0,70,255,235,0,72,255,235,0,80,255,235,0,82,255,235,0,83,255,230,0,86,
  255,234,0,87,255,232,0,90,255,232,0,103,254,238,0,1,0,88,255,193,0,1,0,88,
  255,204,0,1,0,85,0,14,0,4,0,53,255,223,0,55,255,228,0,56,255,236,0,58,
  255,221,0,3,0,53,255,206,0,55,255,237,0,58,255,208,0,3,0,1,255,196,0,83,
  255,191,0,88,255,209,0,4,0,10,0,20,0,62,0,17,0,83,255,226,0,94,0,19,
  0,4,0,10,0,15,0,62,0,12,0,83,255,235,0,94,0,14,0,9,0,7,255,226,
  0,10,0,20,0,11,255,207,0,62,0,18,0,71,255,234,0,83,255,216,0,85,255,234,
  0,94,0,19,0,102,255,211,0,1,0,88,255,229,0,2,0,43,255,238,0,54,255,238,
  0,10,0,3,0,16,0,8,0,16,0,10,0,20,0,62,0,18,0,68,255,232,0,69,
  255,232,0,70,255,232,0,72,255,232,0,82,255,232,0,94,0,19,0,5,0,68,255,236,
  0,69,255,236,0,70,255,236,0,72,255,236,0,82,255,236,0,3,0,71,0,20,0,85,
  0,50,0,88,0,17,0,1,0,80,255,226,0,3,0,13,255,132,0,15,255,132,0,103,
  255,132,0,1,0,71,0,13,0,2,0,43,255,236,0,54,255,236,0,2,5,80,0,4,
  0,0,5,164,6,116,0,28,0,24,0,0,0,0,0,0,0,0,0,0,0,0,0,18,
  0,0,0,0,0,0,255,227,255,228,0,0,0,0,0,0,0,0,0,17,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,235,
  0,0,0,0,0,0,255,171,255,213,0,0,0,0,0,0,255,233,255,234,0,0,0,0,
  0,0,255,134,255,234,0,0,0,0,0,0,0,0,255,235,255,208,255,244,255,245,0,0,
  0,0,255,206,255,239,255,136,255,106,0,0,0,12,0,0,0,0,0,0,0,0,255,136,
  255,217,0,0,255,179,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,241,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,255,235,255,168,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,176,
  0,0,0,0,0,0,0,0,0,0,0,0,255,237,0,0,0,0,255,237,255,239,0,0,
  0,0,255,230,0,0,0,20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,241,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,239,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,245,0,0,0,0,0,0,0,0,255,241,0,0,0,0,0,0,255,241,255,227,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,243,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,242,
  0,0,0,0,0,0,0,0,0,0,0,0,255,243,0,0,0,0,255,241,0,0,0,0,
  255,241,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,
  0,0,255,89,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,234,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,255,230,255,225,0,0,255,229,255,233,0,0,
  0,0,255,216,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,255,92,0,0,0,0,255,191,255,227,255,216,255,191,255,217,255,106,
  255,193,255,236,255,160,0,17,0,18,255,198,255,226,255,240,0,0,0,13,255,233,0,0,
  0,17,255,25,0,18,255,104,255,160,0,0,255,167,255,228,255,167,255,48,255,191,255,136,
  255,88,255,174,0,0,0,16,0,16,255,180,255,196,255,240,0,0,0,0,255,179,0,0,
  0,15,254,254,0,16,254,240,255,40,0,0,255,240,0,0,0,0,255,240,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,255,235,255,230,0,0,255,235,255,237,0,13,
  0,0,255,229,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,255,230,255,231,0,0,255,235,255,235,0,0,
  0,0,255,225,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,14,255,100,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,255,227,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,255,236,0,0,0,0,255,216,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,255,237,0,0,0,0,255,220,0,0,0,0,
  255,226,0,18,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16,
  0,0,255,83,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,245,
  0,0,0,0,0,0,0,0,0,0,0,0,255,243,0,0,0,0,255,243,0,0,255,78,
  255,245,0,15,0,0,0,0,0,0,0,0,0,0,0,0,255,220,255,205,0,0,0,0,
  0,0,254,108,0,0,0,0,255,72,0,0,0,0,255,192,0,0,0,0,255,242,0,19,
  0,0,255,133,255,232,255,51,254,233,0,0,0,0,0,0,0,0,0,0,0,0,254,224,
  255,163,0,0,255,50,0,0,0,0,0,0,255,197,0,0,255,236,255,165,0,0,255,136,
  255,206,0,0,0,0,0,0,0,0,255,164,0,0,0,0,0,0,0,0,0,0,255,219,
  0,0,0,0,0,0,0,0,0,0,0,0,255,225,0,0,0,0,255,225,255,237,255,213,
  255,223,0,0,0,0,0,14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,113,0,0,255,196,0,0,0,0,255,212,255,243,0,0,255,210,255,228,255,181,
  255,210,255,245,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,255,41,0,0,255,99,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,255,229,255,201,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,255,232,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,254,227,
  0,0,0,0,0,0,0,0,0,0,0,1,0,40,0,3,0,8,0,13,0,15,0,34,
  0,35,0,36,0,37,0,38,0,41,0,42,0,43,0,44,0,45,0,46,0,47,0,48,
  0,49,0,53,0,54,0,55,0,56,0,57,0,58,0,59,0,66,0,67,0,68,0,70,
  0,73,0,78,0,79,0,80,0,81,0,83,0,87,0,89,0,90,0,91,0,103,0,1,
  0,3,0,101,0,23,0,0,0,0,0,0,0,0,0,23,0,0,0,0,0,0,0,0,
  0,27,0,0,0,27,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,26,0,17,
  0,1,0,5,0,0,0,0,0,0,0,0,0,10,0,11,0,22,0,0,0,0,0,1,
  0,21,0,0,0,0,0,0,0,13,0,10,0,25,0,24,0,16,0,12,0,15,0,0,
  0,0,0,0,0,0,0,0,0,0,0,6,0,7,0,20,0,0,0,8,0,0,0,0,
  0,4,0,0,0,0,0,0,0,0,0,4,0,4,0,3,0,7,0,0,0,19,0,0,
  0,0,0,0,0,9,0,0,0,18,0,9,0,14,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,27,0,1,0,3,0,101,0,18,0,0,
  0,0,0,0,0,0,0,18,0,0,0,0,0,0,0,0,0,20,0,22,0,20,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,2,0,0,0,0,0,0,0,2,
  0,0,0,0,0,23,0,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,0,14,
  0,10,0,9,0,21,0,19,0,16,0,11,0,15,0,0,0,0,0,0,0,0,0,0,
  0,0,0,7,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,
  0,0,0,3,0,3,0,4,0,3,0,1,0,0,0,12,0,0,0,5,0,8,0,0,
  0,17,0,8,0,13,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,22,
  0,22,0,0,0,20,0,0,0,1,0,0,0,10,0,50,0,52,0,4,68,70,76,84,
  0,26,99,121,114,108,0,36,103,114,101,107,0,36,108,97,116,110,0,36,0,4,0,0,
  0,0,255,255,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const unsigned int font_medium_len = 11032;

/* ------------------------------------------------------------------ utilities */
#define ARRLEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CLAMP(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
#define CL01(x) ((x) < 0.f ? 0.f : (x) > 1.f ? 1.f : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
#define PI_F 3.14159265358979f

static inline float smoothf(float a, float b, float x) {
	float d = b - a;
	if (d == 0.f) return x < a ? 0.f : 1.f;
	float t = (x - a) / d;
	t = CL01(t);
	return t * t * (3.f - 2.f * t);
}

static void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "snapdesk: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}
static void *xmalloc(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}
static void *xcalloc(size_t n, size_t m) {
	void *p = calloc(n ? n : 1, m ? m : 1);
	if (!p) die("out of memory (%zu bytes)", n * m);
	return p;
}
static void *xrealloc(void *p, size_t n) {
	p = realloc(p, n ? n : 1);
	if (!p) die("out of memory (%zu bytes)", n);
	return p;
}

static inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
	uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u + 0x9e3779b9u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	h *= 2246822519u;
	return h ^ (h >> 15);
}
static inline float hashf(uint32_t x, uint32_t y, uint32_t seed) {
	return (float)(hash2(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}
/* smooth value noise on a lattice, result in 0..1 */
static float vnoise(float x, float y, uint32_t seed) {
	float fx = floorf(x), fy = floorf(y);
	int ix = (int)fx, iy = (int)fy;
	float tx = x - fx, ty = y - fy;
	tx = tx * tx * (3.f - 2.f * tx);
	ty = ty * ty * (3.f - 2.f * ty);
	float a = hashf((uint32_t)ix, (uint32_t)iy, seed), b = hashf((uint32_t)ix + 1, (uint32_t)iy, seed);
	float c = hashf((uint32_t)ix, (uint32_t)iy + 1, seed), d = hashf((uint32_t)ix + 1, (uint32_t)iy + 1, seed);
	return LERP(LERP(a, b, tx), LERP(c, d, tx), ty);
}
static float fbm(float x, float y, uint32_t seed, int oct) {
	float s = 0, a = 0.5f, n = 0;
	for (int i = 0; i < oct; i++) {
		s += a * vnoise(x, y, seed + (uint32_t)i * 101u);
		n += a;
		a *= 0.5f;
		x *= 2.03f;
		y *= 2.03f;
	}
	return s / n;
}

/* ------------------------------------------------------------------ thread pool */
typedef void (*RowFn)(void *ctx, int a, int b);
static struct {
	SDL_Thread *th[32];
	int n;
	SDL_mutex *mu;
	SDL_cond *cv, *done;
	RowFn fn;
	void *ctx;
	int total, chunk, busy, gen, quit;
	SDL_atomic_t next;
} POOL;

static void pool_run_chunks(void) {
	for (;;) {
		int a = SDL_AtomicAdd(&POOL.next, POOL.chunk);
		if (a >= POOL.total) break;
		int b = a + POOL.chunk;
		if (b > POOL.total) b = POOL.total;
		POOL.fn(POOL.ctx, a, b);
	}
}
static int pool_worker(void *arg) {
	(void)arg;
	int seen = 0;
	SDL_LockMutex(POOL.mu);
	for (;;) {
		while (!POOL.quit && POOL.gen == seen) SDL_CondWait(POOL.cv, POOL.mu);
		if (POOL.quit) break;
		seen = POOL.gen;
		SDL_UnlockMutex(POOL.mu);
		pool_run_chunks();
		SDL_LockMutex(POOL.mu);
		if (--POOL.busy == 0) SDL_CondSignal(POOL.done);
	}
	SDL_UnlockMutex(POOL.mu);
	return 0;
}
static void pool_init(void) {
	int cpus = SDL_GetCPUCount();
	if (cpus < 1) cpus = 1;
	if (cpus > 32) cpus = 32;
	POOL.mu = SDL_CreateMutex();
	POOL.cv = SDL_CreateCond();
	POOL.done = SDL_CreateCond();
	POOL.n = cpus - 1;
	for (int i = 0; i < POOL.n; i++) POOL.th[i] = SDL_CreateThread(pool_worker, "worker", NULL);
}
static void pool_quit(void) {
	SDL_LockMutex(POOL.mu);
	POOL.quit = 1;
	SDL_CondBroadcast(POOL.cv);
	SDL_UnlockMutex(POOL.mu);
	for (int i = 0; i < POOL.n; i++) SDL_WaitThread(POOL.th[i], NULL);
	POOL.n = 0;
}
/* run fn over [0,n) in chunks on all cores. fn must not call parallel() itself. */
static void parallel(RowFn fn, void *ctx, int n) {
	if (n <= 0) return;
	if (POOL.n == 0 || n < 4) {
		fn(ctx, 0, n);
		return;
	}
	int ways = POOL.n + 1;
	int chunk = n / (ways * 6);
	if (chunk < 1) chunk = 1;
	SDL_LockMutex(POOL.mu);
	POOL.fn = fn;
	POOL.ctx = ctx;
	POOL.total = n;
	POOL.chunk = chunk;
	SDL_AtomicSet(&POOL.next, 0);
	POOL.busy = POOL.n;
	POOL.gen++;
	SDL_CondBroadcast(POOL.cv);
	SDL_UnlockMutex(POOL.mu);
	pool_run_chunks();
	SDL_LockMutex(POOL.mu);
	while (POOL.busy > 0) SDL_CondWait(POOL.done, POOL.mu);
	SDL_UnlockMutex(POOL.mu);
}

/* ------------------------------------------------------------------ float images */
typedef struct {
	int w, h;
	float *p; /* RGB, gamma encoded (sRGB-ish), nominal range 0..1 */
} Img;

static Img img_new(int w, int h) {
	Img i;
	i.w = w;
	i.h = h;
	i.p = (float *)xmalloc((size_t)w * h * 3 * sizeof(float));
	return i;
}
static void img_free(Img *i) {
	free(i->p);
	i->p = NULL;
	i->w = i->h = 0;
}
static Img img_clone(const Img *s) {
	Img i = img_new(s->w, s->h);
	memcpy(i.p, s->p, (size_t)s->w * s->h * 3 * sizeof(float));
	return i;
}
/* replace *dst by a copy of *s, reusing the allocation when the size matches */
static void img_assign(Img *dst, const Img *s) {
	if (dst->p && (dst->w != s->w || dst->h != s->h)) img_free(dst);
	if (!dst->p) *dst = img_new(s->w, s->h);
	memcpy(dst->p, s->p, (size_t)s->w * s->h * 3 * sizeof(float));
}

static inline void bilin(const Img *im, float x, float y, float *o) {
	x -= 0.5f;
	y -= 0.5f;
	float fx0 = floorf(x), fy0 = floorf(y);
	int x0 = (int)fx0, y0 = (int)fy0;
	float fx = x - fx0, fy = y - fy0;
	int x1 = x0 + 1, y1 = y0 + 1;
	x0 = CLAMP(x0, 0, im->w - 1);
	x1 = CLAMP(x1, 0, im->w - 1);
	y0 = CLAMP(y0, 0, im->h - 1);
	y1 = CLAMP(y1, 0, im->h - 1);
	const float *a = im->p + ((size_t)y0 * im->w + x0) * 3, *b = im->p + ((size_t)y0 * im->w + x1) * 3;
	const float *c = im->p + ((size_t)y1 * im->w + x0) * 3, *d = im->p + ((size_t)y1 * im->w + x1) * 3;
	for (int k = 0; k < 3; k++) {
		float t = a[k] + (b[k] - a[k]) * fx, u = c[k] + (d[k] - c[k]) * fx;
		o[k] = t + (u - t) * fy;
	}
}

/* ---- area-average downscale (exact box weights), from u8 or float sources ---- */
typedef struct {
	const uint8_t *s8;
	const float *sf;
	int sw, sh, tw, th;
	int *st, *cnt;
	float *wt;
	int *vst, *vcnt;
	float *vwt;
	float *tmp;
	float *dst;
	int vmax, hmax;
} RsCtx;

static void rs_weights(int si, int to, int **st, int **cnt, float **wt, int *maxc) {
	float s = (float)si / (float)to;
	int mc = (int)ceilf(s) + 2;
	*st = (int *)xmalloc(sizeof(int) * to);
	*cnt = (int *)xmalloc(sizeof(int) * to);
	*wt = (float *)xmalloc(sizeof(float) * to * mc);
	*maxc = mc;
	for (int o = 0; o < to; o++) {
		float a = o * s, b = (o + 1) * s;
		int i0 = (int)floorf(a), i1 = (int)ceilf(b) - 1;
		if (i1 >= si) i1 = si - 1;
		if (i0 < 0) i0 = 0;
		int n = i1 - i0 + 1;
		if (n > mc) n = mc;
		float sum = 0;
		for (int i = 0; i < n; i++) {
			float lo = MAX((float)(i0 + i), a), hi = MIN((float)(i0 + i + 1), b);
			float w = hi - lo;
			if (w < 0) w = 0;
			(*wt)[o * mc + i] = w;
			sum += w;
		}
		if (sum <= 0) {
			(*wt)[o * mc] = 1;
			sum = 1;
			n = 1;
		}
		for (int i = 0; i < n; i++) (*wt)[o * mc + i] /= sum;
		(*st)[o] = i0;
		(*cnt)[o] = n;
	}
}
static void rs_h_job(void *v, int y0, int y1) {
	RsCtx *c = (RsCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *d = c->tmp + (size_t)y * c->tw * 3;
		for (int x = 0; x < c->tw; x++) {
			float r = 0, g = 0, b = 0;
			const float *w = c->wt + (size_t)x * c->hmax;
			for (int i = 0; i < c->cnt[x]; i++) {
				size_t idx = ((size_t)y * c->sw + c->st[x] + i) * 3;
				if (c->s8) {
					r += w[i] * c->s8[idx];
					g += w[i] * c->s8[idx + 1];
					b += w[i] * c->s8[idx + 2];
				} else {
					r += w[i] * c->sf[idx];
					g += w[i] * c->sf[idx + 1];
					b += w[i] * c->sf[idx + 2];
				}
			}
			float k = c->s8 ? (1.f / 255.f) : 1.f;
			d[x * 3] = r * k;
			d[x * 3 + 1] = g * k;
			d[x * 3 + 2] = b * k;
		}
	}
}
static void rs_v_job(void *v, int y0, int y1) {
	RsCtx *c = (RsCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst + (size_t)y * c->tw * 3;
		const float *w = c->vwt + (size_t)y * c->vmax;
		for (int x = 0; x < c->tw * 3; x++) {
			float s = 0;
			for (int i = 0; i < c->vcnt[y]; i++) s += w[i] * c->tmp[((size_t)(c->vst[y] + i) * c->tw) * 3 + x];
			d[x] = s;
		}
	}
}
static Img resize_generic(const uint8_t *s8, const float *sf, int sw, int sh, int tw, int th) {
	Img out = img_new(tw, th);
	if (tw >= sw && th >= sh) { /* no downscale needed: convert / copy (sizes are equal here) */
		size_t n = (size_t)sw * sh * 3;
		if (tw == sw && th == sh) {
			if (s8)
				for (size_t i = 0; i < n; i++) out.p[i] = s8[i] * (1.f / 255.f);
			else
				memcpy(out.p, sf, n * sizeof(float));
			return out;
		}
		/* upscale (rare): bilinear */
		Img src;
		src.w = sw;
		src.h = sh;
		src.p = (float *)xmalloc(n * sizeof(float));
		if (s8)
			for (size_t i = 0; i < n; i++) src.p[i] = s8[i] * (1.f / 255.f);
		else
			memcpy(src.p, sf, n * sizeof(float));
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
				bilin(&src, (x + 0.5f) * sw / tw, (y + 0.5f) * sh / th, out.p + ((size_t)y * tw + x) * 3);
		free(src.p);
		return out;
	}
	if (tw > sw) tw = sw;
	if (th > sh) th = sh;
	RsCtx c;
	memset(&c, 0, sizeof c);
	c.s8 = s8;
	c.sf = sf;
	c.sw = sw;
	c.sh = sh;
	c.tw = out.w;
	c.th = out.h;
	rs_weights(sw, out.w, &c.st, &c.cnt, &c.wt, &c.hmax);
	rs_weights(sh, out.h, &c.vst, &c.vcnt, &c.vwt, &c.vmax);
	c.tmp = (float *)xmalloc((size_t)out.w * sh * 3 * sizeof(float));
	c.dst = out.p;
	parallel(rs_h_job, &c, sh);
	parallel(rs_v_job, &c, out.h);
	free(c.tmp);
	free(c.st);
	free(c.cnt);
	free(c.wt);
	free(c.vst);
	free(c.vcnt);
	free(c.vwt);
	return out;
}
static Img resize_u8(const uint8_t *s, int sw, int sh, int tw, int th) { return resize_generic(s, NULL, sw, sh, tw, th); }
static Img resize_f(const Img *s, int tw, int th) { return resize_generic(NULL, s->p, s->w, s->h, tw, th); }

/* ------------------------------------------------------------------ colour helpers */
static float g2l_t[1025], l2g_t[1025];
static void init_luts(void) {
	for (int i = 0; i <= 1024; i++) {
		float v = i / 1024.f;
		g2l_t[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
		float lin = v * v;
		l2g_t[i] = lin <= 0.0031308f ? 12.92f * lin : 1.055f * powf(lin, 1.f / 2.4f) - 0.055f;
	}
}
static inline float to_lin(float v) {
	v = CL01(v) * 1024.f;
	int i = (int)v;
	if (i >= 1024) return g2l_t[1024];
	float f = v - i;
	return g2l_t[i] + (g2l_t[i + 1] - g2l_t[i]) * f;
}
static inline float to_gam(float l) {
	if (l <= 0.f) return 0.f;
	float t = sqrtf(l) * 1024.f;
	if (t >= 1024.f) return 1.f;
	int i = (int)t;
	float f = t - i;
	return l2g_t[i] + (l2g_t[i + 1] - l2g_t[i]) * f;
}
static inline float lum3(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
/* 4096-entry 1D LUT lookup with interpolation (input clamped to 0..1) */
#define LUTN 4096
static inline float lut_at(const float *t, float v) {
	v = CL01(v) * (LUTN - 1);
	int i = (int)v;
	if (i >= LUTN - 1) return t[LUTN - 1];
	float f = v - i;
	return t[i] + (t[i + 1] - t[i]) * f;
}

/* ------------------------------------------------------------------ blurs & guided filter */
typedef struct {
	const float *src;
	float *dst;
	int w, h, ch, r;
} BoxCtx;

static void box_h_job(void *v, int y0, int y1) {
	BoxCtx *c = (BoxCtx *)v;
	int w = c->w, ch = c->ch, r = c->r;
	double inv = 1.0 / (2 * r + 1);
	for (int y = y0; y < y1; y++) {
		const float *s = c->src + (size_t)y * w * ch;
		float *d = c->dst + (size_t)y * w * ch;
		for (int k = 0; k < ch; k++) {
			double sum = 0;
			for (int i = -r; i <= r; i++) sum += s[CLAMP(i, 0, w - 1) * ch + k];
			for (int x = 0; x < w; x++) {
				d[x * ch + k] = (float)(sum * inv);
				int xa = x + r + 1;
				if (xa >= w) xa = w - 1;
				int xr = x - r;
				if (xr < 0) xr = 0;
				sum += (double)s[xa * ch + k] - (double)s[xr * ch + k];
			}
		}
	}
}
#define VB 16
static void box_v_job(void *v, int b0, int b1) {
	BoxCtx *c = (BoxCtx *)v;
	int w = c->w, h = c->h, ch = c->ch, r = c->r;
	double inv = 1.0 / (2 * r + 1);
	for (int b = b0; b < b1; b++) {
		int x0 = b * VB, x1 = MIN(x0 + VB, w), n = (x1 - x0) * ch;
		double acc[VB * 3];
		for (int k = 0; k < n; k++) acc[k] = 0;
		for (int i = -r; i <= r; i++) {
			const float *s = c->src + ((size_t)CLAMP(i, 0, h - 1) * w + x0) * ch;
			for (int k = 0; k < n; k++) acc[k] += s[k];
		}
		for (int y = 0; y < h; y++) {
			float *d = c->dst + ((size_t)y * w + x0) * ch;
			for (int k = 0; k < n; k++) d[k] = (float)(acc[k] * inv);
			const float *sa = c->src + ((size_t)MIN(y + r + 1, h - 1) * w + x0) * ch;
			const float *sr = c->src + ((size_t)MAX(y - r, 0) * w + x0) * ch;
			for (int k = 0; k < n; k++) acc[k] += (double)sa[k] - (double)sr[k];
		}
	}
}
/* box blur of radius r (window 2r+1), clamped edges. src may equal dst. tmp: w*h*ch floats. */
static void box_blur(const float *src, float *dst, int w, int h, int ch, int r, float *tmp) {
	if (r <= 0) {
		if (src != dst) memcpy(dst, src, (size_t)w * h * ch * sizeof(float));
		return;
	}
	BoxCtx c = {src, tmp, w, h, ch, r};
	parallel(box_h_job, &c, h);
	c.src = tmp;
	c.dst = dst;
	parallel(box_v_job, &c, (w + VB - 1) / VB);
}
/* gaussian-like blur (3 box passes), in place. sigma in pixels. */
static void blur_f(float *buf, int w, int h, int ch, float sigma) {
	if (sigma < 0.6f) return;
	float wi = sqrtf(4.f * sigma * sigma + 1.f);
	int r = (int)((wi - 1.f) * 0.5f + 0.5f);
	if (r < 1) r = 1;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *tmp = (float *)xmalloc((size_t)w * h * ch * sizeof(float));
	for (int i = 0; i < 3; i++) box_blur(buf, buf, w, h, ch, r, tmp);
	free(tmp);
}
/* edge-aware smoothing of a 1-channel plane (self-guided filter, He et al.) */
static void guided_self(const float *I, float *out, int w, int h, int r, float eps) {
	size_t n = (size_t)w * h;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *m = (float *)xmalloc(n * sizeof(float)), *a = (float *)xmalloc(n * sizeof(float));
	float *tmp = (float *)xmalloc(n * sizeof(float));
	box_blur(I, m, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) a[i] = I[i] * I[i];
	box_blur(a, a, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) {
		float var = a[i] - m[i] * m[i];
		if (var < 0) var = 0;
		float aa = var / (var + eps);
		a[i] = aa;
		m[i] = m[i] * (1.f - aa);
	}
	box_blur(a, a, w, h, 1, r, tmp);
	box_blur(m, m, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) out[i] = a[i] * I[i] + m[i];
	free(m);
	free(a);
	free(tmp);
}

typedef struct {
	const Img *im;
	float *L;
} LumCtx;
static void lum_job(void *v, int y0, int y1) {
	LumCtx *c = (LumCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		float *l = c->L + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) l[x] = lum3(p[0], p[1], p[2]);
	}
}
static float *lum_plane(const Img *im) {
	float *L = (float *)xmalloc((size_t)im->w * im->h * sizeof(float));
	LumCtx c = {im, L};
	parallel(lum_job, &c, im->h);
	return L;
}

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

/* ------------------------------------------------------------------ shared helpers */
static stbtt_fontinfo font_reg, font_med; /* initialised in fonts_init() */

typedef struct {
	Img *im;
	const float *a, *b;
	float k[16];
	const float *lut0, *lut1, *lut2;
} Job;

/* multiply rgb by (Ynew/Yold), keeping hue; gains limited */
static void ratio_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		const float *yo = j->a + (size_t)y * w, *yn = j->b + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float o = yo[x], n = yn[x];
			if (o > 0.004f) {
				float k = n / o;
				k = CLAMP(k, 0.f, 4.f);
				p[0] *= k;
				p[1] *= k;
				p[2] *= k;
			} else {
				float d = n - o;
				p[0] += d;
				p[1] += d;
				p[2] += d;
			}
		}
	}
}
static void apply_ratio(Img *im, const float *Yold, const float *Ynew) {
	Job j = {im, Yold, Ynew, {0}, NULL, NULL, NULL};
	parallel(ratio_job, &j, im->h);
}
static void clamp_job(void *v, int y0, int y1) {
	Img *im = (Img *)v;
	size_t n = (size_t)im->w * 3;
	for (int y = y0; y < y1; y++) {
		float *p = im->p + (size_t)y * n;
		for (size_t i = 0; i < n; i++) p[i] = CL01(p[i]);
	}
}
static void img_clamp(Img *im) { parallel(clamp_job, im, im->h); }

/* saturation scale about luminance; k = 1 neutral */
static void sat_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	float s = j->k[0];
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float L = lum3(p[0], p[1], p[2]);
			p[0] = L + (p[0] - L) * s;
			p[1] = L + (p[1] - L) * s;
			p[2] = L + (p[2] - L) * s;
		}
	}
}
static void img_saturate(Img *im, float s) {
	if (fabsf(s - 1.f) < 1e-4f) return;
	Job j = {im, NULL, NULL, {s}, NULL, NULL, NULL};
	parallel(sat_job, &j, im->h);
}

/* grain: value noise on a lattice tied to image size so preview == export */
static void grain_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	float amt = j->k[0], cell = j->k[1];
	uint32_t seed = (uint32_t)j->k[2];
	float inv = 1.f / cell;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float n = vnoise(x * inv, y * inv, seed) + 0.5f * vnoise(x * inv * 2.1f + 17.f, y * inv * 2.1f + 5.f, seed + 7) - 0.75f;
			float L = lum3(p[0], p[1], p[2]);
			float wgt = 1.f - 0.8f * (2.f * L - 1.f) * (2.f * L - 1.f);
			float d = n * amt * wgt;
			p[0] += d;
			p[1] += d;
			p[2] += d;
		}
	}
}
static void add_grain(Img *im, float amount, uint32_t seed) {
	if (amount <= 0) return;
	float cell = MAX(1.f, MIN(im->w, im->h) / 1100.f);
	Job j = {im, NULL, NULL, {amount * 0.55f, cell, (float)seed}, NULL, NULL, NULL};
	parallel(grain_job, &j, im->h);
}

/* radial darkening/brightening: strength -1..1, size 0..1 (inner radius), used by filters */
static void vig_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w, h = j->im->h;
	float st = j->k[0], size = j->k[1];
	float cx = w * 0.5f, cy = h * 0.5f, dmax = sqrtf(cx * cx + cy * cy);
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float dx = x - cx, dy = y - cy;
			float d = sqrtf(dx * dx + dy * dy) / dmax;
			float t = smoothf(size, 1.05f, d);
			float g = 1.f + st * t * (st < 0 ? 1.f : 0.6f);
			g = MAX(g, 0.f);
			p[0] *= g;
			p[1] *= g;
			p[2] *= g;
		}
	}
}
static void add_vignette(Img *im, float strength, float size) {
	if (fabsf(strength) < 1e-3f) return;
	Job j = {im, NULL, NULL, {strength, size}, NULL, NULL, NULL};
	parallel(vig_job, &j, im->h);
}

/* the S curve used all over: contrast -1..1 about mid-grey */
static inline float scurve(float v, float c) {
	if (c > 0) {
		float s = v * v * (3.f - 2.f * v);
		s = s * s * (3.f - 2.f * s);
		return v + c * 0.9f * (s - v);
	}
	return 0.5f + (v - 0.5f) * (1.f + c * 0.75f);
}

/* ------------------------------------------------------------------ Tune Image */
typedef struct {
	Img *im;
	const float *tone, *hs, *base;
	float amb, sat, warm;
} TuneCtx;
static void tune_a_job(void *v, int y0, int y1) {
	TuneCtx *c = (TuneCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float r = lut_at(c->tone, p[0]), g = lut_at(c->tone, p[1]), b = lut_at(c->tone, p[2]);
			float k = lut_at(c->hs, lum3(r, g, b));
			p[0] = r * k;
			p[1] = g * k;
			p[2] = b * k;
		}
	}
}
static void tune_b_job(void *v, int y0, int y1) {
	TuneCtx *c = (TuneCtx *)v;
	int w = c->im->w;
	float amb = c->amb, sat = c->sat, warm = c->warm;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *bs = c->base ? c->base + (size_t)y * w : NULL;
		for (int x = 0; x < w; x++, p += 3) {
			float r = p[0], g = p[1], b = p[2];
			float L = lum3(r, g, b);
			float sboost = 1.f;
			if (bs) {
				float Lb = bs[x], d = L - Lb;
				float Lb2 = Lb + amb * 0.40f * (0.5f - Lb);
				float L2 = Lb2 + d * (1.f + amb * 1.3f);
				if (L > 0.004f) {
					float k = CLAMP(L2 / L, 0.15f, 4.f);
					r *= k;
					g *= k;
					b *= k;
				} else {
					float dd = L2 - L;
					r += dd;
					g += dd;
					b += dd;
				}
				sboost = 1.f + 0.22f * amb;
				L = lum3(r, g, b);
			}
			if (sat != 0.f || sboost != 1.f) {
				float mx = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b));
				float sx = mx > 0.001f ? (mx - mn) / mx : 0.f;
				float f = sat >= 0.f ? 1.f + sat * (1.f - 0.55f * sx) : 1.f + sat;
				f *= sboost;
				r = L + (r - L) * f;
				g = L + (g - L) * f;
				b = L + (b - L) * f;
			}
			if (warm != 0.f) {
				r *= 1.f + 0.20f * warm;
				b *= 1.f - 0.20f * warm;
				g *= 1.f + 0.02f * warm;
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_tune(const Layer *L, Img *im) {
	float br = L->p[TU_BRI] / 100.f, co = L->p[TU_CON] / 100.f, sa = L->p[TU_SAT] / 100.f;
	float am = L->p[TU_AMB] / 100.f, hi = L->p[TU_HIG] / 100.f, sh = L->p[TU_SHA] / 100.f, wa = L->p[TU_WAR] / 100.f;
	float *tone = (float *)xmalloc(LUTN * sizeof(float)), *hs = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		if (br != 0.f) v = powf(v, exp2f(-br * 1.0f));
		tone[i] = scurve(v, co);
		float l = v;
		float k = 1.f + sh * 1.1f * powf(1.f - l, 3.f) + hi * 0.65f * l * l * l;
		hs[i] = MAX(k, 0.f);
	}
	TuneCtx c = {im, tone, hs, NULL, am, sa, wa};
	parallel(tune_a_job, &c, im->h);
	float *base = NULL;
	if (am != 0.f) {
		float *Y = lum_plane(im);
		base = (float *)xmalloc((size_t)im->w * im->h * sizeof(float));
		int r = MAX(2, (int)(MIN(im->w, im->h) * 0.03f));
		guided_self(Y, base, im->w, im->h, r, 0.02f);
		free(Y);
	}
	c.base = base;
	parallel(tune_b_job, &c, im->h);
	free(base);
	free(tone);
	free(hs);
}

/* ------------------------------------------------------------------ Details (structure, sharpening) */
typedef struct {
	Img *im;
	const float *Y, *b1, *b2, *b3;
	float st, sh;
	float *Ynew;
} DetCtx;
static void det_job(void *v, int y0, int y1) {
	DetCtx *c = (DetCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		for (int x = 0; x < w; x++) {
			size_t i = (size_t)y * w + x;
			float Y = c->Y[i], d = 0.f;
			if (c->b1) {
				float d1 = Y - c->b1[i], d2 = c->b1[i] - c->b2[i];
				float t = 2.f * Y - 1.f;
				float wg = 1.f - 0.6f * t * t;
				d += c->st * (1.5f * d1 + 0.9f * d2) * wg;
			}
			if (c->b3) d += c->sh * 1.8f * (Y - c->b3[i]);
			c->Ynew[i] = Y + d;
		}
	}
}
static void apply_details(const Layer *L, Img *im) {
	float st = L->p[DE_STR] / 100.f, sh = L->p[DE_SHP] / 100.f;
	if (st == 0.f && sh == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *Yn = (float *)xmalloc(n * sizeof(float));
	float *b1 = NULL, *b2 = NULL, *b3 = NULL;
	if (st != 0.f) {
		b1 = (float *)xmalloc(n * sizeof(float));
		b2 = (float *)xmalloc(n * sizeof(float));
		memcpy(b1, Y, n * sizeof(float));
		memcpy(b2, Y, n * sizeof(float));
		blur_f(b1, w, h, 1, MAX(1.f, m * 0.0035f));
		blur_f(b2, w, h, 1, MAX(2.f, m * 0.014f));
	}
	if (sh > 0.f) {
		b3 = (float *)xmalloc(n * sizeof(float));
		memcpy(b3, Y, n * sizeof(float));
		blur_f(b3, w, h, 1, MAX(1.f, m * 0.0012f));
	}
	DetCtx c = {im, Y, b1, b2, b3, st, sh, Yn};
	parallel(det_job, &c, h);
	apply_ratio(im, Y, Yn);
	img_clamp(im);
	free(Y);
	free(Yn);
	free(b1);
	free(b2);
	free(b3);
}

/* ------------------------------------------------------------------ Curves */
static void pchip_lut(const float (*pt)[2], int n, float *out, int N) {
	float d[MAXCP], mm[MAXCP], hh[MAXCP];
	if (n < 2) {
		for (int i = 0; i < N; i++) out[i] = i / (float)(N - 1);
		return;
	}
	for (int k = 0; k < n - 1; k++) {
		hh[k] = MAX(pt[k + 1][0] - pt[k][0], 1e-5f);
		d[k] = (pt[k + 1][1] - pt[k][1]) / hh[k];
	}
	mm[0] = d[0];
	mm[n - 1] = d[n - 2];
	for (int k = 1; k < n - 1; k++) mm[k] = (d[k - 1] * d[k] <= 0.f) ? 0.f : 2.f * d[k - 1] * d[k] / (d[k - 1] + d[k]);
	/* keep end slopes monotone */
	for (int k = 0; k < n - 1; k++) {
		if (d[k] == 0.f) {
			mm[k] = 0;
			mm[k + 1] = 0;
		} else {
			float a = mm[k] / d[k], b = mm[k + 1] / d[k], s = a * a + b * b;
			if (s > 9.f) {
				float t = 3.f / sqrtf(s);
				mm[k] = t * a * d[k];
				mm[k + 1] = t * b * d[k];
			}
		}
	}
	int seg = 0;
	for (int i = 0; i < N; i++) {
		float x = i / (float)(N - 1);
		while (seg < n - 2 && x > pt[seg + 1][0]) seg++;
		float y;
		if (x <= pt[0][0])
			y = pt[0][1];
		else if (x >= pt[n - 1][0])
			y = pt[n - 1][1];
		else {
			float t = (x - pt[seg][0]) / hh[seg], t2 = t * t, t3 = t2 * t;
			y = (2 * t3 - 3 * t2 + 1) * pt[seg][1] + (t3 - 2 * t2 + t) * hh[seg] * mm[seg] + (-2 * t3 + 3 * t2) * pt[seg + 1][1] + (t3 - t2) * hh[seg] * mm[seg + 1];
		}
		out[i] = CL01(y);
	}
}
static void curve_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			p[0] = lut_at(j->lut0, p[0]);
			p[1] = lut_at(j->lut1, p[1]);
			p[2] = lut_at(j->lut2, p[2]);
		}
	}
}
static void curves_luts(const Layer *L, float *lut[3]) {
	float *m = (float *)xmalloc(LUTN * sizeof(float)), *c = (float *)xmalloc(LUTN * sizeof(float));
	pchip_lut(L->cp[0], L->ncp[0], m, LUTN);
	for (int ch = 0; ch < 3; ch++) {
		pchip_lut(L->cp[1 + ch], L->ncp[1 + ch], c, LUTN);
		for (int i = 0; i < LUTN; i++) lut[ch][i] = lut_at(m, c[i]);
	}
	free(m);
	free(c);
}
static void apply_curves(const Layer *L, Img *im) {
	float *l[3];
	for (int i = 0; i < 3; i++) l[i] = (float *)xmalloc(LUTN * sizeof(float));
	curves_luts(L, l);
	Job j = {im, NULL, NULL, {0}, l[0], l[1], l[2]};
	parallel(curve_job, &j, im->h);
	for (int i = 0; i < 3; i++) free(l[i]);
}

/* ------------------------------------------------------------------ White balance */
static void wb_gains(float temp, float tint, float g[3]) {
	float a = temp / 100.f * 0.75f, b = -tint / 100.f * 0.5f;
	g[0] = exp2f(a);
	g[1] = exp2f(b);
	g[2] = exp2f(-a);
	float k = 1.f / lum3(g[0], g[1], g[2]);
	g[0] *= k;
	g[1] *= k;
	g[2] *= k;
}
static void wb_job(void *v, int y0, int y1) {
	Job *j = (Job *)v;
	int w = j->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = j->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			p[0] = to_gam(to_lin(p[0]) * j->k[0]);
			p[1] = to_gam(to_lin(p[1]) * j->k[1]);
			p[2] = to_gam(to_lin(p[2]) * j->k[2]);
		}
	}
}
static void apply_wb(const Layer *L, Img *im) {
	if (L->p[WB_TEMP] == 0.f && L->p[WB_TINT] == 0.f) return;
	Job j = {im, NULL, NULL, {0}, NULL, NULL, NULL};
	wb_gains(L->p[WB_TEMP], L->p[WB_TINT], j.k);
	parallel(wb_job, &j, im->h);
}
/* temperature/tint (slider units) that make the given sample neutral */
static void wb_from_sample(float r, float g, float b, float *temp, float *tint) {
	r = MAX(to_lin(r), 1e-4f);
	g = MAX(to_lin(g), 1e-4f);
	b = MAX(to_lin(b), 1e-4f);
	float a = 0.5f * log2f(b / r);
	float bb = 0.5f * (log2f(r) + log2f(b)) - log2f(g);
	*temp = CLAMP(a / 0.75f * 100.f, -100.f, 100.f);
	*tint = CLAMP(-bb / 0.5f * 100.f, -100.f, 100.f);
}

/* ------------------------------------------------------------------ Geometry: crop / rotate / perspective */
static void apply_crop(const Layer *L, Img *im) {
	int w = im->w, h = im->h;
	int x0 = (int)roundf(L->cr[0] * w), y0 = (int)roundf(L->cr[1] * h);
	int x1 = (int)roundf(L->cr[2] * w), y1 = (int)roundf(L->cr[3] * h);
	x0 = CLAMP(x0, 0, w - 2);
	y0 = CLAMP(y0, 0, h - 2);
	x1 = CLAMP(x1, x0 + 2, w);
	y1 = CLAMP(y1, y0 + 2, h);
	if (x0 == 0 && y0 == 0 && x1 == w && y1 == h) return;
	Img o = img_new(x1 - x0, y1 - y0);
	for (int y = y0; y < y1; y++) memcpy(o.p + (size_t)(y - y0) * o.w * 3, im->p + ((size_t)y * w + x0) * 3, (size_t)o.w * 3 * sizeof(float));
	img_free(im);
	*im = o;
}
static Img rot90cw(const Img *s) {
	Img o = img_new(s->h, s->w);
	for (int y = 0; y < s->h; y++)
		for (int x = 0; x < s->w; x++) memcpy(o.p + ((size_t)x * o.w + (s->h - 1 - y)) * 3, s->p + ((size_t)y * s->w + x) * 3, 3 * sizeof(float));
	return o;
}
static void flip_h(Img *im) {
	for (int y = 0; y < im->h; y++) {
		float *r = im->p + (size_t)y * im->w * 3;
		for (int x = 0; x < im->w / 2; x++)
			for (int k = 0; k < 3; k++) {
				float t = r[x * 3 + k];
				r[x * 3 + k] = r[(im->w - 1 - x) * 3 + k];
				r[(im->w - 1 - x) * 3 + k] = t;
			}
	}
}
static void flip_v(Img *im) {
	size_t rb = (size_t)im->w * 3;
	float *t = (float *)xmalloc(rb * sizeof(float));
	for (int y = 0; y < im->h / 2; y++) {
		float *a = im->p + y * rb, *b = im->p + (size_t)(im->h - 1 - y) * rb;
		memcpy(t, a, rb * sizeof(float));
		memcpy(a, b, rb * sizeof(float));
		memcpy(b, t, rb * sizeof(float));
	}
	free(t);
}
typedef struct {
	const Img *in;
	Img *out;
	float a, b, c, d; /* source offset from centre = [a b; c d] * (out offset from centre) */
} WarpCtx;
static void warp_job(void *v, int y0, int y1) {
	WarpCtx *c = (WarpCtx *)v;
	float cx = c->in->w * 0.5f, cy = c->in->h * 0.5f, ox = c->out->w * 0.5f, oy = c->out->h * 0.5f;
	for (int y = y0; y < y1; y++) {
		float *d = c->out->p + (size_t)y * c->out->w * 3;
		for (int x = 0; x < c->out->w; x++, d += 3) {
			float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
			bilin(c->in, cx + c->a * dx + c->b * dy, cy + c->c * dx + c->d * dy, d);
		}
	}
}
static void apply_rotate(const Layer *L, Img *im) {
	for (int i = 0; i < (L->rot & 3); i++) {
		Img t = rot90cw(im);
		img_free(im);
		*im = t;
	}
	if (L->flipx) flip_h(im);
	if (L->flipy) flip_v(im);
	float ang = L->p[RO_ANGLE];
	if (fabsf(ang) < 0.01f) return;
	float th = ang * PI_F / 180.f, cs = cosf(th), sn = sinf(th);
	float w = (float)im->w, h = (float)im->h;
	float s = MIN(w / (w * fabsf(cs) + h * fabsf(sn)), h / (w * fabsf(sn) + h * fabsf(cs)));
	Img o = img_new(im->w, im->h);
	WarpCtx c = {im, &o, s * cs, s * sn, -s * sn, s * cs};
	parallel(warp_job, &c, o.h);
	img_free(im);
	*im = o;
}
typedef struct {
	const Img *in;
	Img *out;
	float kv, kh, cs, sn, fs;
} PersCtx;
static void persp_map(const PersCtx *c, float x, float y, float w, float h, float *sx, float *sy) {
	float m = MAX(w, h) * 0.5f, u = (x - w * 0.5f) / m, v = (y - h * 0.5f) / m;
	float ur = u * c->cs - v * c->sn, vr = u * c->sn + v * c->cs;
	float den = 1.f - c->kv * vr - c->kh * ur;
	if (den < 0.25f) den = 0.25f;
	float f = c->fs / den;
	*sx = w * 0.5f + ur * f * m;
	*sy = h * 0.5f + vr * f * m;
}
static void pers_job(void *v, int y0, int y1) {
	PersCtx *c = (PersCtx *)v;
	float w = (float)c->out->w, h = (float)c->out->h;
	for (int y = y0; y < y1; y++) {
		float *d = c->out->p + (size_t)y * c->out->w * 3;
		for (int x = 0; x < c->out->w; x++, d += 3) {
			float sx, sy;
			persp_map(c, x + 0.5f, y + 0.5f, w, h, &sx, &sy);
			bilin(c->in, sx, sy, d);
		}
	}
}
static void apply_persp(const Layer *L, Img *im) {
	float vt = L->p[PE_VERT], hz = L->p[PE_HORZ], rt = L->p[PE_ROT], sc = L->p[PE_SCALE];
	if (vt == 0.f && hz == 0.f && rt == 0.f && sc == 100.f) return;
	float th = rt * PI_F / 180.f;
	PersCtx c = {im, NULL, vt / 100.f * 0.45f, hz / 100.f * 0.45f, cosf(th), sinf(th), 1.f};
	float w = (float)im->w, h = (float)im->h, fmax_ = 0.f;
	for (int i = 0; i <= 64; i++) {
		float t = i / 64.f, sx, sy;
		float pts[4][2] = {{t * w, 0}, {t * w, h}, {0, t * h}, {w, t * h}};
		for (int k = 0; k < 4; k++) {
			persp_map(&c, pts[k][0], pts[k][1], w, h, &sx, &sy);
			float rx = fabsf(sx - w * 0.5f) / (w * 0.5f), ry = fabsf(sy - h * 0.5f) / (h * 0.5f);
			fmax_ = MAX(fmax_, MAX(rx, ry));
		}
	}
	float autof = 1.f / MAX(1.f, fmax_);
	c.fs = autof * 100.f / MAX(sc, 1.f);
	Img o = img_new(im->w, im->h);
	c.out = &o;
	parallel(pers_job, &c, o.h);
	img_free(im);
	*im = o;
}

/* ------------------------------------------------------------------ Selective (control points) */
typedef struct {
	Img *im;
	const float *Y, *Bl;
	int x0, x1, y0;
	float px, py, R, cr, cg, cb, cL;
	float br, co, sa, st;
} SelCtx;
static void sel_job(void *v, int a, int b) {
	SelCtx *c = (SelCtx *)v;
	int w = c->im->w;
	float inv = 1.f / c->R;
	for (int yy = a; yy < b; yy++) {
		int y = c->y0 + yy;
		float *p = c->im->p + ((size_t)y * w + c->x0) * 3;
		for (int x = c->x0; x < c->x1; x++, p += 3) {
			float dx = x + 0.5f - c->px, dy = y + 0.5f - c->py;
			float d = sqrtf(dx * dx + dy * dy) * inv;
			if (d >= 1.f) continue;
			float sp = 1.f - smoothf(0.f, 1.f, d);
			float dc = fabsf(p[0] - c->cr) + fabsf(p[1] - c->cg) + fabsf(p[2] - c->cb);
			float sim = expf(-dc * dc / (2.f * 0.2f * 0.2f));
			float wg = sp * (0.12f + 0.88f * sim);
			if (wg < 0.002f) continue;
			float r = p[0], g = p[1], bl = p[2];
			if (c->br != 0.f) {
				float k = exp2f(c->br * wg * 0.9f);
				r *= k;
				g *= k;
				bl *= k;
			}
			if (c->co != 0.f) {
				float k = 1.f + c->co * wg * 0.9f;
				r = c->cL + (r - c->cL) * k;
				g = c->cL + (g - c->cL) * k;
				bl = c->cL + (bl - c->cL) * k;
			}
			float L = lum3(r, g, bl);
			if (c->sa != 0.f) {
				float k = 1.f + c->sa * wg;
				r = L + (r - L) * k;
				g = L + (g - L) * k;
				bl = L + (bl - L) * k;
			}
			if (c->st != 0.f && c->Y) {
				size_t i = (size_t)y * w + x;
				float dL = c->st * wg * 1.7f * (c->Y[i] - c->Bl[i]);
				if (L > 0.004f) {
					float k = CLAMP((L + dL) / L, 0.f, 4.f);
					r *= k;
					g *= k;
					bl *= k;
				} else {
					r += dL;
					g += dL;
					bl += dL;
				}
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(bl);
		}
	}
}
static void apply_selective(const Layer *L, Img *im) {
	if (L->nsel == 0) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	float *Y = NULL, *Bl = NULL;
	int needs = 0;
	for (int i = 0; i < L->nsel; i++)
		if (L->sel[i].v[3] != 0.f) needs = 1;
	if (needs) {
		Y = lum_plane(im);
		Bl = (float *)xmalloc((size_t)w * h * sizeof(float));
		memcpy(Bl, Y, (size_t)w * h * sizeof(float));
		blur_f(Bl, w, h, 1, MAX(1.f, m * 0.004f));
	}
	SelCtx c[MAXSEL];
	for (int i = 0; i < L->nsel; i++) { /* sample all centre colours first (from the untouched image) */
		const SelPt *s = &L->sel[i];
		SelCtx *q = &c[i];
		q->px = s->x * w;
		q->py = s->y * h;
		float sr = 0, sg = 0, sb = 0;
		int n = 0;
		for (int dy = -2; dy <= 2; dy++)
			for (int dx = -2; dx <= 2; dx++) {
				int xx = CLAMP((int)q->px + dx, 0, w - 1), yy = CLAMP((int)q->py + dy, 0, h - 1);
				const float *p = im->p + ((size_t)yy * w + xx) * 3;
				sr += p[0];
				sg += p[1];
				sb += p[2];
				n++;
			}
		q->cr = sr / n;
		q->cg = sg / n;
		q->cb = sb / n;
		q->cL = lum3(q->cr, q->cg, q->cb);
	}
	for (int i = 0; i < L->nsel; i++) {
		const SelPt *s = &L->sel[i];
		SelCtx *q = &c[i];
		q->im = im;
		q->Y = Y;
		q->Bl = Bl;
		q->R = MAX(4.f, s->rad * m);
		q->x0 = MAX(0, (int)(q->px - q->R));
		q->x1 = MIN(w, (int)(q->px + q->R) + 1);
		q->y0 = MAX(0, (int)(q->py - q->R));
		int y1 = MIN(h, (int)(q->py + q->R) + 1);
		q->br = s->v[0] / 100.f;
		q->co = s->v[1] / 100.f;
		q->sa = s->v[2] / 100.f;
		q->st = s->v[3] / 100.f;
		if (q->br == 0 && q->co == 0 && q->sa == 0 && q->st == 0) continue;
		if (y1 > q->y0 && q->x1 > q->x0) parallel(sel_job, q, y1 - q->y0);
	}
	free(Y);
	free(Bl);
}

/* ------------------------------------------------------------------ Brush (dodge/burn, exposure, temperature, saturation) */
struct BrushRT {
	int w, h;
	float *map[4];
	int s_idx, p_idx;
	float carry;
};
static void brt_free(BrushRT *rt) {
	for (int i = 0; i < 4; i++) free(rt->map[i]);
	free(rt);
}
static void brt_stamp(BrushRT *rt, const Stroke *s, float cx, float cy, float R) {
	int x0 = MAX(0, (int)(cx - R)), x1 = MIN(rt->w - 1, (int)(cx + R) + 1);
	int y0 = MAX(0, (int)(cy - R)), y1 = MIN(rt->h - 1, (int)(cy + R) + 1);
	float hard = CLAMP(s->hard, 0.f, 0.98f), inv = 1.f / R;
	int erase = s->mode >= 4;
	if (!erase && !rt->map[s->mode]) rt->map[s->mode] = (float *)xcalloc((size_t)rt->w * rt->h, sizeof(float));
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float t = sqrtf(dx * dx + dy * dy) * inv;
			if (t >= 1.f) continue;
			float wgt = 1.f;
			if (t > hard) {
				float u = (t - hard) / (1.f - hard);
				wgt = 1.f - u * u * (3.f - 2.f * u);
			}
			float a = wgt * s->flow;
			size_t i = (size_t)y * rt->w + x;
			if (erase) {
				for (int m = 0; m < 4; m++)
					if (rt->map[m]) rt->map[m][i] *= 1.f - a;
			} else
				rt->map[s->mode][i] += (s->amt - rt->map[s->mode][i]) * a;
		}
}
static void brt_raster(BrushRT *rt, const Stroke *strokes, int nstr, const Pt *pts) {
	float m = (float)MIN(rt->w, rt->h);
	for (int s = rt->s_idx; s < nstr; s++) {
		const Stroke *st = &strokes[s];
		int p0 = (s == rt->s_idx) ? rt->p_idx : 0;
		float R = MAX(1.5f, st->rad * m), spacing = MAX(1.f, R * 0.2f);
		for (int p = p0; p < st->n; p++) {
			Pt a = pts[st->start + p];
			float ax = a.x * rt->w, ay = a.y * rt->h;
			if (p == 0) {
				brt_stamp(rt, st, ax, ay, R);
				rt->carry = 0;
				continue;
			}
			Pt b = pts[st->start + p - 1];
			float bx = b.x * rt->w, by = b.y * rt->h, dx = ax - bx, dy = ay - by;
			float len = sqrtf(dx * dx + dy * dy);
			if (len < 1e-4f) continue;
			float t = spacing - rt->carry;
			while (t <= len) {
				brt_stamp(rt, st, bx + dx * (t / len), by + dy * (t / len), R);
				t += spacing;
			}
			rt->carry = len - (t - spacing);
		}
		rt->s_idx = s;
		rt->p_idx = st->n;
		if (s < nstr - 1) {
			rt->s_idx = s + 1;
			rt->p_idx = 0;
			rt->carry = 0;
		}
	}
}
typedef struct {
	Img *im;
	const float *map;
	int mode;
} BrushCtx;
static void brush_job(void *v, int y0, int y1) {
	BrushCtx *c = (BrushCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *m = c->map + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float A = m[x];
			if (fabsf(A) < 0.003f) continue;
			float r = p[0], g = p[1], b = p[2];
			switch (c->mode) {
			case 0: { /* dodge & burn: gamma curve */
				float e = exp2f(-A * 1.1f);
				r = powf(CL01(r), e);
				g = powf(CL01(g), e);
				b = powf(CL01(b), e);
			} break;
			case 1: { /* exposure in linear light */
				float k = exp2f(A * 1.6f);
				r = to_gam(to_lin(r) * k);
				g = to_gam(to_lin(g) * k);
				b = to_gam(to_lin(b) * k);
			} break;
			case 2:
				r *= 1.f + 0.25f * A;
				b *= 1.f - 0.25f * A;
				break;
			default: {
				float L = lum3(r, g, b), k = 1.f + A;
				r = L + (r - L) * k;
				g = L + (g - L) * k;
				b = L + (b - L) * k;
			}
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_brush(Layer *L, Img *im) {
	if (L->nstr == 0) return;
	BrushRT *rt = L->rt;
	if (rt && (rt->w != im->w || rt->h != im->h)) {
		brt_free(rt);
		rt = L->rt = NULL;
	}
	if (!rt) {
		rt = L->rt = (BrushRT *)xcalloc(1, sizeof(BrushRT));
		rt->w = im->w;
		rt->h = im->h;
	}
	brt_raster(rt, L->str, L->nstr, L->pts);
	for (int m = 0; m < 4; m++) {
		if (!rt->map[m]) continue;
		BrushCtx c = {im, rt->map[m], m};
		parallel(brush_job, &c, im->h);
	}
}

/* ------------------------------------------------------------------ Healing (patch copy with colour match) */
static void heal_spot(Img *im, float cx, float cy, float R) {
	int w = im->w, h = im->h;
	float Rout = R * 1.45f;
	enum { NA = 24 };
	float rr[2] = {R * 1.15f, R * 1.32f};
	float T[2 * NA][3], S[2 * NA][3];
	float offs[2 * NA][2];
	for (int k = 0; k < 2; k++)
		for (int a = 0; a < NA; a++) {
			float an = a * (2.f * PI_F / NA) + k * 0.13f;
			offs[k * NA + a][0] = cosf(an) * rr[k];
			offs[k * NA + a][1] = sinf(an) * rr[k];
		}
	for (int i = 0; i < 2 * NA; i++) bilin(im, cx + offs[i][0], cy + offs[i][1], T[i]);
	float best = 1e30f, bsx = 0, bsy = 0, bmean[3] = {0, 0, 0};
	int found = 0;
	static const float dist[3] = {2.4f, 3.5f, 5.0f};
	for (int di = 0; di < 3; di++)
		for (int ai = 0; ai < 20; ai++) {
			float an = ai * (2.f * PI_F / 20.f) + di * 0.31f;
			float sx = cx + cosf(an) * dist[di] * R, sy = cy + sinf(an) * dist[di] * R;
			if (sx - Rout < 0 || sy - Rout < 0 || sx + Rout >= w || sy + Rout >= h) continue;
			float ssd = 0, mean[3] = {0, 0, 0}, mt[3] = {0, 0, 0};
			for (int i = 0; i < 2 * NA; i++) {
				bilin(im, sx + offs[i][0], sy + offs[i][1], S[i]);
				for (int k = 0; k < 3; k++) {
					mean[k] += S[i][k];
					mt[k] += T[i][k];
				}
			}
			for (int k = 0; k < 3; k++) {
				mean[k] /= 2 * NA;
				mt[k] /= 2 * NA;
			}
			for (int i = 0; i < 2 * NA; i++)
				for (int k = 0; k < 3; k++) {
					float d = (T[i][k] - mt[k]) - (S[i][k] - mean[k]);
					ssd += d * d;
				}
			ssd *= 1.f + 0.04f * di;
			if (ssd < best) {
				best = ssd;
				bsx = sx;
				bsy = sy;
				found = 1;
				for (int k = 0; k < 3; k++) bmean[k] = mt[k] - mean[k];
			}
		}
	int x0 = MAX(0, (int)(cx - R) - 1), x1 = MIN(w - 1, (int)(cx + R) + 1);
	int y0 = MAX(0, (int)(cy - R) - 1), y1 = MIN(h - 1, (int)(cy + R) + 1);
	float mt[3] = {0, 0, 0};
	for (int i = 0; i < 2 * NA; i++)
		for (int k = 0; k < 3; k++) mt[k] += T[i][k] / (2 * NA);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			if (d >= R) continue;
			float a = 1.f - smoothf(R * 0.55f, R, d);
			float s[3];
			float *p = im->p + ((size_t)y * w + x) * 3;
			if (found) {
				bilin(im, bsx + dx, bsy + dy, s);
				for (int k = 0; k < 3; k++) s[k] += bmean[k];
			} else
				for (int k = 0; k < 3; k++) s[k] = mt[k];
			for (int k = 0; k < 3; k++) p[k] = CL01(LERP(p[k], s[k], a));
		}
}
static void apply_heal(const Layer *L, Img *im) {
	float m = (float)MIN(im->w, im->h);
	for (int i = 0; i < L->nspots; i++) heal_spot(im, L->spots[i].x * im->w, L->spots[i].y * im->h, MAX(3.f, L->spots[i].r * m));
}

/* ------------------------------------------------------------------ Vignette */
typedef struct {
	Img *im;
	float cx, cy, og, ig, inner, feather, round, dmax;
} VigCtx;
static void vign_job(void *v, int y0, int y1) {
	VigCtx *c = (VigCtx *)v;
	int w = c->im->w, h = c->im->h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float dx = x + 0.5f - c->cx, dy = y + 0.5f - c->cy;
			float e = sqrtf((dx / (w * 0.5f)) * (dx / (w * 0.5f)) + (dy / (h * 0.5f)) * (dy / (h * 0.5f))) * 0.7071f;
			float ci = sqrtf(dx * dx + dy * dy) / c->dmax;
			float d = LERP(e, ci, c->round);
			float t = smoothf(c->inner, c->inner + c->feather, d);
			float g = LERP(c->ig, c->og, t);
			p[0] = CL01(p[0] * g);
			p[1] = CL01(p[1] * g);
			p[2] = CL01(p[2] * g);
		}
	}
}
static inline float bright_gain(float b) { return b < 0 ? 1.f + b * 0.96f : 1.f + b * 0.9f; }
static void apply_vignette(const Layer *L, Img *im) {
	if (L->p[VG_OUTER] == 0.f && L->p[VG_INNER] == 0.f) return;
	VigCtx c;
	c.im = im;
	c.cx = L->cx * im->w;
	c.cy = L->cy * im->h;
	c.og = bright_gain(L->p[VG_OUTER] / 100.f);
	c.ig = bright_gain(L->p[VG_INNER] / 100.f);
	c.inner = L->p[VG_SIZE] / 100.f * 0.85f;
	c.feather = 0.08f + L->p[VG_FEATHER] / 100.f * 0.9f;
	c.round = (L->p[VG_ROUND] + 100.f) / 200.f;
	c.dmax = 0.5f * sqrtf((float)im->w * im->w + (float)im->h * im->h);
	parallel(vign_job, &c, im->h);
}

/* ------------------------------------------------------------------ Lens blur */
typedef struct {
	float *mask;
	int w, h;
	float cx, cy, a, b, cs, sn, tw;
	int linear;
	const Img *im;
	Img *out;
	float wk[4];
	int lvl;
} LensCtx;
static void lens_mask_job(void *v, int y0, int y1) {
	LensCtx *c = (LensCtx *)v;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->cx, dy = y + 0.5f - c->cy;
			float u = dx * c->cs + dy * c->sn, vv = -dx * c->sn + dy * c->cs, d;
			if (c->linear)
				d = fabsf(vv) / c->b;
			else
				d = sqrtf(u * u / (c->a * c->a) + vv * vv / (c->b * c->b));
			c->mask[(size_t)y * c->w + x] = smoothf(1.f, 1.f + c->tw, d);
		}
}
static void lens_acc_job(void *v, int y0, int y1) {
	LensCtx *c = (LensCtx *)v;
	for (int y = y0; y < y1; y++) {
		float *o = c->out->p + (size_t)y * c->w * 3;
		const float *s = c->im->p + (size_t)y * c->w * 3;
		const float *m = c->mask + (size_t)y * c->w;
		for (int x = 0; x < c->w; x++) {
			float f = m[x] * 3.f;
			float wgt = 1.f - fabsf(f - (float)c->lvl);
			if (wgt <= 0.f) continue;
			o[x * 3] += wgt * s[x * 3];
			o[x * 3 + 1] += wgt * s[x * 3 + 1];
			o[x * 3 + 2] += wgt * s[x * 3 + 2];
		}
	}
}
static void apply_lens(const Layer *L, Img *im) {
	float bl = L->p[LN_BLUR] / 100.f, vg = L->p[LN_VIG] / 100.f;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	if (bl > 0.f) {
		LensCtx c;
		memset(&c, 0, sizeof c);
		c.w = w;
		c.h = h;
		c.cx = L->cx * w;
		c.cy = L->cy * h;
		float rf = (0.08f + L->p[LN_SIZE] / 100.f * 0.55f) * m;
		c.a = rf * 1.35f;
		c.b = rf * 0.85f;
		float th = L->p[LN_ANGLE] * PI_F / 180.f;
		c.cs = cosf(th);
		c.sn = sinf(th);
		c.tw = 0.12f + L->p[LN_TRANS] / 100.f * 1.1f;
		c.linear = (int)L->p[LN_SHAPE] == 1;
		c.mask = (float *)xmalloc((size_t)w * h * sizeof(float));
		parallel(lens_mask_job, &c, h);
		Img out = img_new(w, h), tmp = img_new(w, h);
		memset(out.p, 0, (size_t)w * h * 3 * sizeof(float));
		c.im = im;
		c.out = &out;
		float Rmax = bl * 0.03f * m;
		for (int lvl = 0; lvl <= 3; lvl++) {
			c.lvl = lvl;
			if (lvl == 0) {
				parallel(lens_acc_job, &c, h);
				continue;
			}
			memcpy(tmp.p, im->p, (size_t)w * h * 3 * sizeof(float));
			/* bright pixels bloom a little, like real bokeh */
			size_t n = (size_t)w * h * 3;
			for (size_t i = 0; i < n; i++) tmp.p[i] += 0.6f * MAX(0.f, tmp.p[i] - 0.85f);
			blur_f(tmp.p, w, h, 3, MAX(0.8f, Rmax * lvl / 3.f * 0.7f));
			c.im = &tmp;
			parallel(lens_acc_job, &c, h);
			c.im = im;
		}
		img_free(&tmp);
		free(c.mask);
		img_free(im);
		*im = out;
	}
	if (vg > 0.f) add_vignette(im, -vg * 0.7f, 0.35f);
	img_clamp(im);
}

/* ------------------------------------------------------------------ Portrait (skin smoothing, face brightening) */
typedef struct {
	const Img *im;
	float *mask;
} SkinCtx;
static void skin_job(void *v, int y0, int y1) {
	SkinCtx *c = (SkinCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float r = p[0] * 255.f, g = p[1] * 255.f, b = p[2] * 255.f;
			float Y = 0.299f * r + 0.587f * g + 0.114f * b;
			float cb = 128.f - 0.168736f * r - 0.331264f * g + 0.5f * b;
			float cr = 128.f + 0.5f * r - 0.418688f * g - 0.081312f * b;
			float m = smoothf(70.f, 82.f, cb) * (1.f - smoothf(122.f, 134.f, cb)) * smoothf(126.f, 138.f, cr) * (1.f - smoothf(168.f, 180.f, cr)) * smoothf(30.f, 70.f, Y);
			if (r < g || r < b * 0.95f) m *= 0.3f;
			c->mask[(size_t)y * w + x] = m;
		}
	}
}
typedef struct {
	Img *im;
	const Img *sm;
	const float *mask;
	float amt, bri, warm;
} PortCtx;
static void port_job(void *v, int y0, int y1) {
	PortCtx *c = (PortCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *s = c->sm->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, s += 3) {
			float m = c->mask[(size_t)y * w + x];
			if (m < 0.003f) continue;
			float a = m * c->amt;
			float r = LERP(p[0], s[0] + 0.12f * (p[0] - s[0]), a);
			float g = LERP(p[1], s[1] + 0.12f * (p[1] - s[1]), a);
			float b = LERP(p[2], s[2] + 0.12f * (p[2] - s[2]), a);
			float k = 1.f + c->bri * m * 0.45f;
			r *= k;
			g *= k;
			b *= k;
			r *= 1.f + c->warm * m * 0.12f;
			b *= 1.f - c->warm * m * 0.12f;
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_portrait(const Layer *L, Img *im) {
	float sm = L->p[PO_SMOOTH] / 100.f, br = L->p[PO_BRIGHT] / 100.f, wa = L->p[PO_WARM] / 100.f;
	if (sm == 0.f && br == 0.f && wa == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *mask = (float *)xmalloc(n * sizeof(float));
	SkinCtx sc = {im, mask};
	parallel(skin_job, &sc, h);
	blur_f(mask, w, h, 1, MAX(1.f, m * 0.004f));
	Img smooth = img_new(w, h);
	if (sm > 0.f) {
		float *pl = (float *)xmalloc(n * sizeof(float)), *po = (float *)xmalloc(n * sizeof(float));
		int r = MAX(2, (int)(m * 0.007f));
		float eps = 0.0015f + 0.02f * sm * sm;
		for (int ch = 0; ch < 3; ch++) {
			for (size_t i = 0; i < n; i++) pl[i] = im->p[i * 3 + ch];
			guided_self(pl, po, w, h, r, eps);
			for (size_t i = 0; i < n; i++) smooth.p[i * 3 + ch] = po[i];
		}
		free(pl);
		free(po);
	} else
		memcpy(smooth.p, im->p, n * 3 * sizeof(float));
	PortCtx c = {im, &smooth, mask, MIN(1.f, sm * 1.15f), br, wa};
	parallel(port_job, &c, h);
	img_free(&smooth);
	free(mask);
}

/* ------------------------------------------------------------------ Text */
static const float TEXT_COL[7][3] = {{1, 1, 1}, {0.05f, 0.05f, 0.05f}, {0.93f, 0.2f, 0.2f}, {1, 0.86f, 0.15f}, {0.15f, 0.8f, 0.72f}, {0.25f, 0.5f, 0.95f}, {1, 0.45f, 0.7f}};
static float *text_mask(const char *s, float px, int *ow, int *oh, int pad) {
	float sc = stbtt_ScaleForPixelHeight(&font_med, px);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&font_med, &asc, &desc, &gap);
	float width = 0;
	for (const char *t = s; *t; t++) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&font_med, (unsigned char)*t, &adv, &lsb);
		width += adv * sc;
		if (t[1]) width += stbtt_GetCodepointKernAdvance(&font_med, (unsigned char)*t, (unsigned char)t[1]) * sc;
	}
	int W = (int)ceilf(width) + pad * 2 + 2, H = (int)ceilf((asc - desc) * sc) + pad * 2 + 2;
	float *m = (float *)xcalloc((size_t)W * H, sizeof(float));
	float xp = (float)pad;
	int base = pad + (int)ceilf(asc * sc);
	for (const char *t = s; *t; t++) {
		int cp = (unsigned char)*t, x0, y0, x1, y1, adv, lsb;
		stbtt_GetCodepointBitmapBox(&font_med, cp, sc, sc, &x0, &y0, &x1, &y1);
		int bw = x1 - x0, bh = y1 - y0;
		if (bw > 0 && bh > 0) {
			unsigned char *bm = (unsigned char *)xmalloc((size_t)bw * bh);
			stbtt_MakeCodepointBitmap(&font_med, bm, bw, bh, bw, sc, sc, cp);
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++) {
					int dx = (int)xp + x0 + x, dy = base + y0 + y;
					if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
					float a = bm[y * bw + x] / 255.f;
					float *d = &m[(size_t)dy * W + dx];
					*d = *d + a - *d * a;
				}
			free(bm);
		}
		stbtt_GetCodepointHMetrics(&font_med, cp, &adv, &lsb);
		xp += adv * sc;
		if (t[1]) xp += stbtt_GetCodepointKernAdvance(&font_med, cp, (unsigned char)t[1]) * sc;
	}
	*ow = W;
	*oh = H;
	return m;
}
static inline float mask_at(const float *m, int W, int H, float x, float y) {
	x -= 0.5f;
	y -= 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	float v[4];
	for (int i = 0; i < 4; i++) {
		int xx = x0 + (i & 1), yy = y0 + (i >> 1);
		v[i] = (xx < 0 || yy < 0 || xx >= W || yy >= H) ? 0.f : m[(size_t)yy * W + xx];
	}
	return LERP(LERP(v[0], v[1], fx), LERP(v[2], v[3], fx), fy);
}
static void dilate(float *m, int W, int H, int r) {
	if (r < 1) return;
	float *t = (float *)xmalloc((size_t)W * H * sizeof(float));
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float mx = 0;
			for (int k = -r; k <= r; k++) mx = MAX(mx, m[(size_t)y * W + CLAMP(x + k, 0, W - 1)]);
			t[(size_t)y * W + x] = mx;
		}
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float mx = 0;
			for (int k = -r; k <= r; k++) mx = MAX(mx, t[(size_t)CLAMP(y + k, 0, H - 1) * W + x]);
			m[(size_t)y * W + x] = mx;
		}
	free(t);
}
static void apply_text(const Layer *L, Img *im) {
	if (!L->text[0]) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	float px = MAX(6.f, L->p[TX_SIZE] / 100.f * 0.34f * m);
	int pad = (int)ceilf(px * 0.3f), W, H;
	float *mk = text_mask(L->text, px, &W, &H, pad);
	int style = (int)L->p[TX_STYLE];
	const float *col = TEXT_COL[CLAMP((int)L->p[TX_COLOR], 0, 6)];
	float opac = L->p[TX_OPAC] / 100.f;
	float lumc = lum3(col[0], col[1], col[2]);
	float alt[3] = {lumc > 0.5f ? 0.05f : 1.f, lumc > 0.5f ? 0.05f : 1.f, lumc > 0.5f ? 0.05f : 1.f};
	float *under = NULL;
	if (style == 1) {
		under = (float *)xmalloc((size_t)W * H * sizeof(float));
		memcpy(under, mk, (size_t)W * H * sizeof(float));
		dilate(under, W, H, MAX(1, (int)(px * 0.07f)));
	} else if (style == 2) {
		under = (float *)xmalloc((size_t)W * H * sizeof(float));
		memcpy(under, mk, (size_t)W * H * sizeof(float));
		blur_f(under, W, H, 1, MAX(1.f, px * 0.06f));
		for (size_t i = 0; i < (size_t)W * H; i++) under[i] = CL01(under[i] * 1.6f);
	}
	float th = L->p[TX_ROT] * PI_F / 180.f, cs = cosf(th), sn = sinf(th);
	float cx = L->cx * w, cy = L->cy * h, hw = W * 0.5f, hh = H * 0.5f;
	float ext = (fabsf(cs) * hw + fabsf(sn) * hh) + 2, eyt = (fabsf(sn) * hw + fabsf(cs) * hh) + 2;
	int x0 = MAX(0, (int)(cx - ext)), x1 = MIN(w - 1, (int)(cx + ext)), y0 = MAX(0, (int)(cy - eyt)), y1 = MIN(h - 1, (int)(cy + eyt));
	float plate_r = px * 0.35f;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float u = dx * cs + dy * sn + hw, v = -dx * sn + dy * cs + hh;
			float *p = im->p + ((size_t)y * w + x) * 3;
			if (style == 3) {
				float qx = fabsf(u - hw) - (hw - plate_r * 0.6f), qy = fabsf(v - hh) - (hh - plate_r * 0.6f);
				float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
				float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - plate_r * 0.6f;
				float pa = CL01(0.5f - dd) * opac;
				if (pa > 0) {
					p[0] = LERP(p[0], col[0], pa * 0.92f);
					p[1] = LERP(p[1], col[1], pa * 0.92f);
					p[2] = LERP(p[2], col[2], pa * 0.92f);
				}
				float a = mask_at(mk, W, H, u, v) * opac;
				p[0] = LERP(p[0], alt[0], a);
				p[1] = LERP(p[1], alt[1], a);
				p[2] = LERP(p[2], alt[2], a);
				continue;
			}
			if (under) {
				float a;
				if (style == 2)
					a = mask_at(under, W, H, u - px * 0.05f, v - px * 0.06f) * 0.7f * opac;
				else
					a = mask_at(under, W, H, u, v) * opac;
				p[0] = LERP(p[0], alt[0] * (style == 2 ? 0.f : 1.f), a);
				p[1] = LERP(p[1], alt[1] * (style == 2 ? 0.f : 1.f), a);
				p[2] = LERP(p[2], alt[2] * (style == 2 ? 0.f : 1.f), a);
			}
			float a = mask_at(mk, W, H, u, v) * opac;
			p[0] = LERP(p[0], col[0], a);
			p[1] = LERP(p[1], col[1], a);
			p[2] = LERP(p[2], col[2], a);
		}
	free(mk);
	free(under);
}

/* ------------------------------------------------------------------ Frame */
static const float FRAME_COL[6][3] = {{0.98f, 0.98f, 0.98f}, {0.03f, 0.03f, 0.03f}, {0.5f, 0.5f, 0.5f}, {0.96f, 0.93f, 0.84f}, {0.09f, 0.11f, 0.16f}, {0.78f, 0.7f, 0.55f}};
/* border sizes (left/right, top, bottom) for a frame layer */
static void frame_geom(const Layer *L, int w, int h, int *bx, int *bt, int *bb) {
	int st = (int)L->p[FR_STYLE];
	int b = (int)(L->p[FR_WIDTH] / 100.f * 0.16f * MIN(w, h));
	*bx = *bt = *bb = b;
	if (st == 1) *bb = b * 3; /* polaroid: heavy bottom */
	if (st == 3) *bx = *bt = *bb = 0; /* inset is painted over the photo */
}
static void apply_frame(const Layer *L, Img *im) {
	float wd = L->p[FR_WIDTH] / 100.f, cn = L->p[FR_CORNER] / 100.f;
	if (wd <= 0.f && cn <= 0.f) return;
	int st = (int)L->p[FR_STYLE];
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	int bx, bt, bb;
	frame_geom(L, w, h, &bx, &bt, &bb);
	int bsz = (int)(wd * 0.16f * m);
	const float *col = FRAME_COL[CLAMP((int)L->p[FR_COLOR], 0, 5)];
	float lc = lum3(col[0], col[1], col[2]);
	float line = lc > 0.5f ? 0.12f : 0.9f;
	Img o = img_new(w + 2 * bx, h + bt + bb);
	float rad = cn * 0.12f * m;
	for (int y = 0; y < o.h; y++)
		for (int x = 0; x < o.w; x++) {
			float *d = o.p + ((size_t)y * o.w + x) * 3;
			int sx = x - bx, sy = y - bt;
			float a = 0.f;
			if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
				a = 1.f;
				if (rad > 0.5f) {
					float qx = fabsf(sx + 0.5f - w * 0.5f) - (w * 0.5f - rad), qy = fabsf(sy + 0.5f - h * 0.5f) - (h * 0.5f - rad);
					float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
					float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - rad;
					a = CL01(0.5f - dd);
				}
			}
			float c3[3] = {col[0], col[1], col[2]};
			if (st == 2 && a < 1.f) { /* double: a thin keyline inside the border */
				float ex = MIN(MIN(x, o.w - 1 - x), MIN(y, o.h - 1 - y)) + 0.5f;
				float q = fabsf(ex - bsz * 0.42f) - MAX(1.f, bsz * 0.035f);
				float k = CL01(0.5f - q) * 0.9f;
				for (int i = 0; i < 3; i++) c3[i] = LERP(c3[i], line, k);
			}
			if (st == 3) { /* inset: a border painted on the photo's own edge */
				float ex = MIN(MIN(sx, w - 1 - sx), MIN(sy, h - 1 - sy)) + 0.5f;
				a = 1.f - CL01(bsz + 0.5f - ex);
				if (rad > 0.5f) {
					float qx = fabsf(sx + 0.5f - w * 0.5f) - (w * 0.5f - rad), qy = fabsf(sy + 0.5f - h * 0.5f) - (h * 0.5f - rad);
					float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
					float dd = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - rad;
					a = MIN(a, CL01(0.5f - dd));
				}
			}
			const float *s = im->p + ((size_t)CLAMP(sy, 0, h - 1) * w + CLAMP(sx, 0, w - 1)) * 3;
			for (int k = 0; k < 3; k++) d[k] = LERP(c3[k], s[k], a);
		}
	img_free(im);
	*im = o;
}

/* ------------------------------------------------------------------ colour grades (Vintage / Retrolux / Grainy Film / Noir) */
typedef struct {
	float mul[3], fade, con, sat, sh[3], hi[3];
} Grade;
static const Grade VINTAGE[9] = {
	{{1.06f, 1.00f, 0.86f}, 0.07f, 0.12f, 0.85f, {0.02f, 0.00f, -0.02f}, {0.04f, 0.02f, -0.04f}},
	{{0.96f, 1.00f, 1.06f}, 0.03f, 0.22f, 0.95f, {-0.03f, 0.02f, 0.05f}, {0.06f, 0.02f, -0.05f}},
	{{1.12f, 0.97f, 0.74f}, 0.05f, 0.15f, 0.35f, {0.03f, 0.01f, -0.02f}, {0.05f, 0.03f, -0.03f}},
	{{0.95f, 1.03f, 0.93f}, 0.10f, 0.05f, 0.80f, {0.00f, 0.03f, 0.02f}, {0.03f, 0.04f, -0.02f}},
	{{1.03f, 1.00f, 0.94f}, 0.09f, 0.18f, 0.90f, {0.00f, 0.02f, 0.05f}, {0.05f, 0.01f, -0.03f}},
	{{0.94f, 1.02f, 1.10f}, 0.04f, 0.28f, 1.05f, {0.02f, 0.03f, 0.06f}, {0.08f, 0.02f, -0.06f}},
	{{1.10f, 1.00f, 0.82f}, 0.02f, 0.18f, 1.10f, {0.04f, 0.00f, -0.03f}, {0.08f, 0.05f, -0.05f}},
	{{1.05f, 0.97f, 1.00f}, 0.11f, 0.06f, 0.72f, {0.04f, 0.00f, 0.02f}, {0.03f, 0.00f, 0.03f}},
	{{1.04f, 1.00f, 0.93f}, 0.12f, 0.20f, 0.00f, {0.01f, 0.01f, 0.00f}, {0.03f, 0.02f, -0.01f}},
};
static const Grade RETRO[6] = {
	{{1.10f, 0.98f, 0.80f}, 0.10f, 0.25f, 1.15f, {0.05f, 0.00f, -0.04f}, {0.10f, 0.06f, -0.06f}},
	{{0.92f, 1.06f, 1.00f}, 0.09f, 0.20f, 0.90f, {-0.02f, 0.05f, 0.04f}, {0.07f, 0.05f, -0.05f}},
	{{1.00f, 0.94f, 1.12f}, 0.07f, 0.28f, 1.05f, {0.05f, 0.00f, 0.08f}, {0.08f, 0.02f, -0.02f}},
	{{1.08f, 1.02f, 0.90f}, 0.14f, 0.12f, 0.65f, {0.04f, 0.02f, 0.00f}, {0.05f, 0.03f, 0.00f}},
	{{0.98f, 1.00f, 1.04f}, 0.12f, 0.30f, 1.20f, {0.00f, 0.04f, 0.08f}, {0.10f, 0.03f, -0.04f}},
	{{1.12f, 0.92f, 0.86f}, 0.08f, 0.22f, 1.25f, {0.06f, -0.02f, 0.02f}, {0.09f, 0.04f, -0.06f}},
};
static const Grade FILMS[6] = {
	{{1.00f, 1.00f, 1.00f}, 0.00f, 0.10f, 1.00f, {0, 0, 0}, {0, 0, 0}},
	{{1.06f, 1.01f, 0.90f}, 0.02f, 0.12f, 1.05f, {0.02f, 0.00f, -0.02f}, {0.04f, 0.02f, -0.03f}},
	{{0.94f, 1.00f, 1.07f}, 0.02f, 0.12f, 1.00f, {-0.02f, 0.01f, 0.04f}, {0.00f, 0.01f, 0.03f}},
	{{1.02f, 1.00f, 0.96f}, 0.12f, 0.00f, 0.75f, {0.02f, 0.02f, 0.02f}, {0.02f, 0.02f, 0.01f}},
	{{1.00f, 1.00f, 1.00f}, 0.03f, 0.25f, 0.00f, {0, 0, 0}, {0, 0, 0}},
	{{1.02f, 1.00f, 1.02f}, 0.00f, 0.32f, 1.30f, {0, 0, 0.02f}, {0.03f, 0.01f, -0.02f}},
};
typedef struct {
	Img *im;
	const Grade *g;
	float strength;
} GradeCtx;
static void grade_job(void *v, int y0, int y1) {
	GradeCtx *c = (GradeCtx *)v;
	const Grade *g = c->g;
	int w = c->im->w;
	float s = c->strength;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float o[3] = {p[0], p[1], p[2]}, q[3];
			for (int k = 0; k < 3; k++) {
				float t = o[k] * g->mul[k];
				t = g->fade + t * (1.f - g->fade);
				q[k] = scurve(CL01(t), g->con);
			}
			float L = lum3(q[0], q[1], q[2]);
			float ws = (1.f - L) * (1.f - L) * 0.16f, wh = L * L * 0.16f;
			for (int k = 0; k < 3; k++) {
				q[k] = L + (q[k] - L) * g->sat;
				q[k] += g->sh[k] * ws * 6.f + g->hi[k] * wh * 6.f;
				p[k] = CL01(LERP(o[k], q[k], s));
			}
		}
	}
}
static void apply_grade(Img *im, const Grade *g, float strength) {
	if (strength <= 0.f) return;
	GradeCtx c = {im, g, CL01(strength)};
	parallel(grade_job, &c, im->h);
}
static void tone_brightness(Img *im, float br) {
	if (br == 0.f) return;
	float e = exp2f(-br);
	size_t n = (size_t)im->w * im->h * 3;
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) lut[i] = powf(i / (float)(LUTN - 1), e);
	for (size_t i = 0; i < n; i++) im->p[i] = lut_at(lut, im->p[i]);
	free(lut);
}

static void apply_vintage(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[VI_STYLE], 0, 8);
	apply_grade(im, &VINTAGE[st], L->p[VI_STR] / 100.f);
	tone_brightness(im, L->p[VI_BRI] / 100.f * 0.8f);
	img_saturate(im, 1.f + L->p[VI_SAT] / 100.f);
	add_vignette(im, -L->p[VI_VIG] / 100.f * 0.85f, 0.35f);
	add_grain(im, 0.10f * L->p[VI_STR] / 100.f, 31);
	img_clamp(im);
}

/* procedural light leaks and scratches; deterministic in normalised coordinates */
typedef struct {
	Img *im;
	float leak, scr;
	uint32_t seed;
	float lx, ly, lr, col[3], m;
} RetCtx;
static void retro_job(void *v, int y0, int y1) {
	RetCtx *c = (RetCtx *)v;
	int w = c->im->w, h = c->im->h;
	int nscr = (int)(c->scr * 40.f);
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		float fy = (float)y / h;
		for (int x = 0; x < w; x++, p += 3) {
			float fx = (float)x / w;
			if (c->leak > 0.f) {
				float dx = (fx - c->lx) * (float)w / c->m, dy = (fy - c->ly) * (float)h / c->m;
				float d = sqrtf(dx * dx + dy * dy) / c->lr;
				float a = expf(-d * d * 1.6f) * c->leak;
				float n = 0.85f + 0.3f * vnoise(fx * 6.f, fy * 6.f, c->seed + 3);
				a *= n;
				for (int k = 0; k < 3; k++) p[k] = 1.f - (1.f - p[k]) * (1.f - CL01(c->col[k] * a));
			}
			for (int s = 0; s < nscr; s++) {
				float sx = hashf(s, 1, c->seed), y0s = hashf(s, 2, c->seed) * 1.2f - 0.1f, len = 0.1f + hashf(s, 3, c->seed) * 0.5f;
				sx += (fy - y0s) * (hashf(s, 4, c->seed) - 0.5f) * 0.02f;
				float dxp = fabsf(fx - sx) * w / c->m;
				if (fy < y0s || fy > y0s + len || dxp > 0.0018f) continue;
				float a = (1.f - dxp / 0.0018f) * 0.45f * (0.5f + 0.5f * vnoise(fy * 90.f, (float)s, c->seed));
				float t = (hashf(s, 5, c->seed) > 0.4f) ? 1.f : 0.f;
				for (int k = 0; k < 3; k++) p[k] = LERP(p[k], t, a);
			}
		}
	}
}
static void apply_retro(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[RE_STYLE], 0, 5);
	uint32_t seed = (uint32_t)L->p[RE_SEED];
	apply_grade(im, &RETRO[st], L->p[RE_STR] / 100.f);
	tone_brightness(im, L->p[RE_BRI] / 100.f * 0.8f);
	img_saturate(im, 1.f + L->p[RE_SAT] / 100.f);
	RetCtx c;
	memset(&c, 0, sizeof c);
	c.im = im;
	c.leak = L->p[RE_LEAK] / 100.f * 0.9f;
	c.scr = L->p[RE_SCR] / 100.f;
	c.seed = seed * 7919u + 13u;
	c.m = (float)MIN(im->w, im->h);
	int side = (int)(hash2(seed, 9, 5) % 4);
	c.lx = (side == 0) ? -0.05f : (side == 1) ? 1.05f : 0.2f + 0.6f * hashf(seed, 10, 1);
	c.ly = (side == 2) ? -0.05f : (side == 3) ? 1.05f : 0.2f + 0.6f * hashf(seed, 11, 1);
	c.lr = 0.35f + 0.35f * hashf(seed, 12, 1);
	static const float LC[5][3] = {{1.0f, 0.45f, 0.1f}, {1.0f, 0.2f, 0.3f}, {1.0f, 0.75f, 0.2f}, {0.9f, 0.3f, 0.6f}, {1.0f, 0.55f, 0.2f}};
	const float *lc = LC[hash2(seed, 13, 2) % 5];
	c.col[0] = lc[0];
	c.col[1] = lc[1];
	c.col[2] = lc[2];
	if (c.leak > 0.f || c.scr > 0.f) parallel(retro_job, &c, im->h);
	add_vignette(im, -0.35f * L->p[RE_STR] / 100.f, 0.4f);
	add_grain(im, 0.12f, seed + 5);
	img_clamp(im);
}
static void apply_grain(const Layer *L, Img *im) {
	int st = CLAMP((int)L->p[GR_STYLE], 0, 5);
	apply_grade(im, &FILMS[st], L->p[GR_STR] / 100.f);
	img_saturate(im, 1.f + L->p[GR_SAT] / 100.f);
	add_grain(im, L->p[GR_GRAIN] / 100.f * 1.3f, 77);
	img_clamp(im);
}

/* ------------------------------------------------------------------ Black & White / Noir */
static const float BW_W[6][3] = {{0.30f, 0.59f, 0.11f}, {0.80f, 0.27f, -0.07f}, {0.62f, 0.45f, -0.07f}, {0.45f, 0.58f, -0.03f}, {0.10f, 0.80f, 0.10f}, {-0.05f, 0.20f, 0.85f}};
static const float BW_T[5][2][3] = {
	{{1, 1, 1}, {1, 1, 1}},
	{{0.95f, 0.82f, 0.65f}, {1.05f, 0.97f, 0.80f}},
	{{0.85f, 0.95f, 1.10f}, {0.96f, 1.00f, 1.06f}},
	{{1.00f, 0.90f, 0.70f}, {1.08f, 0.98f, 0.72f}},
	{{0.93f, 0.88f, 1.05f}, {1.00f, 0.98f, 0.98f}},
};
typedef struct {
	Img *im;
	const float *w;
	const float (*tone)[3];
	const float *lut;
	float wash;
} BwCtx;
static void bw_job(void *v, int y0, int y1) {
	BwCtx *c = (BwCtx *)v;
	int W = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * W * 3;
		for (int x = 0; x < W; x++, p += 3) {
			float g = CL01(c->w[0] * p[0] + c->w[1] * p[1] + c->w[2] * p[2]);
			g = lut_at(c->lut, g);
			for (int k = 0; k < 3; k++) p[k] = CL01(g * LERP(c->tone[0][k], c->tone[1][k], g));
		}
	}
}
static void apply_bw(const Layer *L, Img *im) {
	int f = CLAMP((int)L->p[BW_FILTER], 0, 5), t = CLAMP((int)L->p[BW_TONE], 0, 4);
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	float br = L->p[BW_BRI] / 100.f, co = L->p[BW_CON] / 100.f;
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		if (br != 0.f) v = powf(v, exp2f(-br));
		lut[i] = scurve(v, co);
	}
	BwCtx c = {im, BW_W[f], BW_T[t], lut, 0};
	parallel(bw_job, &c, im->h);
	free(lut);
	add_grain(im, L->p[BW_GRAIN] / 100.f * 1.2f, 99);
	img_clamp(im);
}
static void apply_noir(const Layer *L, Img *im) {
	static const float W3[3] = {0.30f, 0.59f, 0.11f};
	static const float STY[4][3] = {{0.45f, 0.00f, 0.0f}, {0.15f, 0.12f, 0.15f}, {0.85f, -0.03f, 0.04f}, {0.20f, 0.18f, 0.2f}}; /* contrast, lift, gamma bias */
	int st = CLAMP((int)L->p[NO_STYLE], 0, 3);
	float wash = L->p[NO_WASH] / 100.f, br = L->p[NO_BRI] / 100.f;
	float *lut = (float *)xmalloc(LUTN * sizeof(float));
	for (int i = 0; i < LUTN; i++) {
		float v = i / (float)(LUTN - 1);
		v = powf(v, exp2f(-br + STY[st][2] * 2.f));
		v = scurve(v, STY[st][0]);
		v = wash * 0.28f + v * (1.f - wash * 0.38f);
		lut[i] = CL01(v);
	}
	BwCtx c = {im, W3, BW_T[0], lut, 0};
	parallel(bw_job, &c, im->h);
	free(lut);
	add_vignette(im, -L->p[NO_VIG] / 100.f * 0.95f, 0.3f);
	add_grain(im, L->p[NO_GRAIN] / 100.f * 1.3f, 123);
	img_clamp(im);
}

/* ------------------------------------------------------------------ local-contrast filters: Drama, Tonal Contrast, HDR Scape */
static void apply_drama(const Layer *L, Img *im) {
	static const float SG[4][4] = {{1.5f, 0.20f, 0.00f, 0.10f}, {2.6f, 0.32f, 0.00f, 0.28f}, {1.9f, 0.25f, 0.07f, 0.06f}, {2.2f, 0.25f, -0.08f, 0.28f}};
	int st = CLAMP((int)L->p[DR_STYLE], 0, 3);
	float f = L->p[DR_STR] / 100.f, sat = L->p[DR_SAT] / 100.f;
	if (f <= 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *base = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, base, w, h, MAX(3, (int)(MIN(w, h) * 0.035f)), 0.03f);
	for (size_t i = 0; i < n; i++) {
		float b = base[i], d = Y[i] - b;
		float b2 = 0.5f + (b - 0.5f) * (1.f - SG[st][1] * f) + SG[st][2] * f;
		float v = b2 + d * (1.f + SG[st][0] * f);
		Yn[i] = scurve(CL01(v), SG[st][3] * f);
	}
	apply_ratio(im, Y, Yn);
	img_saturate(im, (1.f - 0.28f * f) * (sat >= 0 ? 1.f + sat : 1.f + sat));
	img_clamp(im);
	free(Y);
	free(base);
	free(Yn);
}
static void apply_tonal(const Layer *L, Img *im) {
	float hi = L->p[TC_HIGH] / 100.f, mi = L->p[TC_MID] / 100.f, lo = L->p[TC_LOW] / 100.f;
	float psh = L->p[TC_PSH] / 100.f, phi = L->p[TC_PHI] / 100.f;
	if (hi == 0 && mi == 0 && lo == 0) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *base = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, base, w, h, MAX(2, (int)(MIN(w, h) * 0.018f)), 0.008f);
	for (size_t i = 0; i < n; i++) {
		float b = base[i], d = Y[i] - b;
		float wH = smoothf(0.5f, 0.95f, b), wL = 1.f - smoothf(0.05f, 0.5f, b), wM = 1.f - fabsf(2.f * b - 1.f);
		wM = wM * wM * (3.f - 2.f * wM);
		float g = 2.4f * (hi * wH + mi * wM + lo * wL);
		float prot = 1.f - psh * (1.f - smoothf(0.0f, 0.3f, Y[i])) - phi * smoothf(0.7f, 1.f, Y[i]);
		Yn[i] = Y[i] + d * g * MAX(prot, 0.f);
	}
	apply_ratio(im, Y, Yn);
	img_clamp(im);
	free(Y);
	free(base);
	free(Yn);
}
static void apply_hdr(const Layer *L, Img *im) {
	static const float ST[4][4] = {{1.6f, 1.2f, 0.18f, 0.35f}, {1.1f, 0.8f, -0.05f, 0.30f}, {1.5f, 1.7f, 0.10f, 0.30f}, {2.6f, 1.6f, 0.22f, 0.45f}};
	int st = CLAMP((int)L->p[HD_STYLE], 0, 3);
	float f = L->p[HD_STR] / 100.f, br = L->p[HD_BRI] / 100.f, sat = L->p[HD_SAT] / 100.f;
	if (f <= 0.f && br == 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	float m = (float)MIN(w, h);
	size_t n = (size_t)w * h;
	float *Y = lum_plane(im), *b1 = (float *)xmalloc(n * sizeof(float)), *b2 = (float *)xmalloc(n * sizeof(float)), *Yn = (float *)xmalloc(n * sizeof(float));
	guided_self(Y, b1, w, h, MAX(4, (int)(m * 0.07f)), 0.03f);
	guided_self(Y, b2, w, h, MAX(2, (int)(m * 0.012f)), 0.006f);
	for (size_t i = 0; i < n; i++) {
		float base = b1[i] + (0.5f - b1[i]) * ST[st][3] * f + br * 0.25f;
		float mid = (b2[i] - b1[i]) * (1.f + ST[st][0] * f);
		float fine = (Y[i] - b2[i]) * (1.f + ST[st][1] * f);
		Yn[i] = CL01(base + mid + fine);
	}
	apply_ratio(im, Y, Yn);
	img_saturate(im, (1.f + ST[st][2] * f) * (sat >= 0 ? 1.f + sat : 1.f + sat));
	img_clamp(im);
	free(Y);
	free(b1);
	free(b2);
	free(Yn);
}

/* ------------------------------------------------------------------ Glamour glow */
typedef struct {
	Img *im;
	const Img *bl;
	float glow, k0, k1, k2;
} GlowCtx;
static void glow_job(void *v, int y0, int y1) {
	GlowCtx *c = (GlowCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *b = c->bl->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, b += 3) {
			float g[3] = {b[0], b[1], b[2]};
			g[0] *= c->k0;
			g[1] *= c->k1;
			g[2] *= c->k2;
			for (int k = 0; k < 3; k++) {
				float gl = CL01(g[k] * (1.f + 0.5f * c->glow));
				float s = 1.f - (1.f - p[k]) * (1.f - gl * c->glow * 0.6f);
				p[k] = LERP(s, g[k], 0.12f * c->glow);
			}
		}
	}
}
static void apply_glow(const Layer *L, Img *im) {
	static const float TN[4][3] = {{1, 1, 1}, {1.08f, 1.0f, 0.88f}, {0.92f, 0.99f, 1.08f}, {1.04f, 1.04f, 1.04f}};
	int st = CLAMP((int)L->p[GL_STYLE], 0, 3);
	float gl = L->p[GL_GLOW] / 100.f * (st == 3 ? 1.35f : 1.f), sat = L->p[GL_SAT] / 100.f, wa = L->p[GL_WARM] / 100.f;
	if (gl > 0.f) {
		Img bl = img_clone(im);
		blur_f(bl.p, bl.w, bl.h, 3, MAX(1.5f, MIN(im->w, im->h) * 0.018f));
		GlowCtx c = {im, &bl, gl, TN[st][0], TN[st][1], TN[st][2]};
		parallel(glow_job, &c, im->h);
		img_free(&bl);
	}
	img_saturate(im, 1.f + sat);
	if (wa != 0.f) {
		size_t n = (size_t)im->w * im->h;
		for (size_t i = 0; i < n; i++) {
			im->p[i * 3] *= 1.f + 0.18f * wa;
			im->p[i * 3 + 2] *= 1.f - 0.18f * wa;
		}
	}
	img_clamp(im);
}

/* ------------------------------------------------------------------ Grunge */
typedef struct {
	Img *im;
	float tex, m, tint[3];
	uint32_t seed;
	int style;
} GrungeCtx;
static void grunge_job(void *v, int y0, int y1) {
	GrungeCtx *c = (GrungeCtx *)v;
	int w = c->im->w;
	float m = c->m;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float fx = x / m, fy = y / m;
			float t = 0.45f * fbm(fx * 4.f, fy * 4.f, c->seed, 4) + 0.35f * fbm(fx * 16.f + 9.f, fy * 16.f, c->seed + 11, 3) + 0.2f * fbm(fx * 90.f, fy * 90.f, c->seed + 23, 2);
			float cr = vnoise(fx * 3.f + 40.f, fy * 3.f, c->seed + 5);
			float crack = (cr > 0.48f && cr < 0.5f) ? 1.f : 0.f;
			t = CL01((t - 0.5f) * 2.2f + 0.5f - crack * 0.4f);
			float d = (t - 0.5f) * c->tex * 1.5f;
			for (int k = 0; k < 3; k++) {
				float v0 = p[k];
				float soft = v0 + d * (v0 < 0.5f ? 2.f * v0 + 0.3f : 2.f * (1.f - v0) + 0.3f);
				p[k] = CL01(soft * LERP(1.f, c->tint[k], c->tex * 0.8f));
			}
		}
	}
}
static void apply_grunge(const Layer *L, Img *im) {
	static const float TINT[5][3] = {{0.95f, 0.92f, 0.84f}, {0.86f, 0.95f, 1.0f}, {0.92f, 1.0f, 0.88f}, {1.0f, 0.86f, 0.78f}, {0.9f, 0.9f, 0.92f}};
	int st = CLAMP((int)L->p[GU_STYLE], 0, 4);
	GrungeCtx c;
	memset(&c, 0, sizeof c);
	c.im = im;
	c.tex = L->p[GU_TEX] / 100.f;
	c.m = (float)MIN(im->w, im->h);
	c.seed = (uint32_t)L->p[GU_SEED] * 31u + (uint32_t)st * 7u + 1u;
	for (int k = 0; k < 3; k++) c.tint[k] = TINT[st][k];
	tone_brightness(im, L->p[GU_BRI] / 100.f * 0.8f);
	size_t n = (size_t)im->w * im->h * 3;
	float co = L->p[GU_CON] / 100.f;
	if (co != 0.f)
		for (size_t i = 0; i < n; i++) im->p[i] = scurve(CL01(im->p[i]), co);
	img_saturate(im, 1.f + L->p[GU_SAT] / 100.f);
	if (c.tex > 0.f) parallel(grunge_job, &c, im->h);
	add_vignette(im, -0.5f * c.tex, 0.4f);
	img_clamp(im);
}


/* ================================================================== EFFECTS: Bloom, Dehaze, Color Mixer, Split Toning,
 *                                                       Denoise, Chromatic Aberration, Light Streaks, Gradient Filter */

/* ---- colour space helpers ---- */
static void hue_rgb(float h, float *r, float *g, float *b) {
	h = fmodf(h, 360.f);
	if (h < 0) h += 360.f;
	float x = h / 60.f, f = x - floorf(x);
	switch ((int)x % 6) {
	case 0: *r = 1; *g = f; *b = 0; break;
	case 1: *r = 1 - f; *g = 1; *b = 0; break;
	case 2: *r = 0; *g = 1; *b = f; break;
	case 3: *r = 0; *g = 1 - f; *b = 1; break;
	case 4: *r = f; *g = 0; *b = 1; break;
	default: *r = 1; *g = 0; *b = 1 - f; break;
	}
}
static void rgb2hsv(float r, float g, float b, float *h, float *s, float *v) {
	float mx_ = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b)), d = mx_ - mn;
	*v = mx_;
	*s = mx_ > 1e-6f ? d / mx_ : 0.f;
	if (d < 1e-6f) {
		*h = 0;
		return;
	}
	float hh;
	if (mx_ == r) hh = (g - b) / d + (g < b ? 6.f : 0.f);
	else if (mx_ == g) hh = (b - r) / d + 2.f;
	else hh = (r - g) / d + 4.f;
	*h = hh * 60.f;
}
static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b) {
	float cr, cg, cb;
	hue_rgb(h, &cr, &cg, &cb);
	*r = v * (1.f - s + s * cr);
	*g = v * (1.f - s + s * cg);
	*b = v * (1.f - s + s * cb);
}

/* downscaled copy so that heavy blurs cost the same at preview and export size */
static Img small_copy(const Img *im, int maxdim) {
	int m = MAX(im->w, im->h);
	if (m <= maxdim) return img_clone(im);
	float s = (float)maxdim / m;
	return resize_f(im, MAX(2, (int)(im->w * s + 0.5f)), MAX(2, (int)(im->h * s + 0.5f)));
}
/* soft-knee highlight extraction, result is linear light * mask */
typedef struct { Img *im; float t0, t1; } HlCtx;
static void hl_job(void *v, int y0, int y1) {
	HlCtx *c = (HlCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float m = 0.5f * MAX(p[0], MAX(p[1], p[2])) + 0.5f * lum3(p[0], p[1], p[2]);
			float k = smoothf(c->t0, c->t1, m);
			p[0] = to_lin(p[0]) * k;
			p[1] = to_lin(p[1]) * k;
			p[2] = to_lin(p[2]) * k;
		}
	}
}
static void extract_highlights(Img *im, float thr) {
	float t0 = thr * 0.92f, t1 = MIN(1.05f, t0 + 0.30f);
	HlCtx c = {im, t0, t1};
	parallel(hl_job, &c, im->h);
}
typedef struct { Img *im; const Img *add; float amt, tint[3]; } AddCtx;
static void add_light_job(void *v, int y0, int y1) {
	AddCtx *c = (AddCtx *)v;
	int w = c->im->w, h = c->im->h;
	float sx = (float)c->add->w / w, sy = (float)c->add->h / h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float b[3];
			bilin(c->add, (x + 0.5f) * sx, (y + 0.5f) * sy, b);
			for (int k = 0; k < 3; k++) p[k] = CL01(to_gam(to_lin(p[k]) + c->amt * b[k] * c->tint[k]));
		}
	}
}
static void add_light(Img *im, const Img *light, float amt, const float *tint) {
	AddCtx c = {im, light, amt, {tint[0], tint[1], tint[2]}};
	parallel(add_light_job, &c, im->h);
}

/* ---- Bloom ---- */
static void apply_bloom(const Layer *L, Img *im) {
	float amt = L->p[BL_AMT] / 100.f, thr = L->p[BL_THR] / 100.f, rad = L->p[BL_RAD] / 100.f, wm = L->p[BL_WARM] / 100.f;
	if (amt <= 0.f) return;
	Img sm = small_copy(im, 1100);
	extract_highlights(&sm, thr);
	float m = (float)MIN(sm.w, sm.h);
	float s1 = MAX(1.2f, m * (0.004f + 0.03f * rad));
	static const float SC[3] = {1.f, 2.6f, 6.f}, WT[3] = {0.45f, 0.35f, 0.45f};
	size_t n = (size_t)sm.w * sm.h * 3;
	Img acc = img_new(sm.w, sm.h), t = img_new(sm.w, sm.h);
	memset(acc.p, 0, n * sizeof(float));
	for (int i = 0; i < 3; i++) {
		memcpy(t.p, sm.p, n * sizeof(float));
		blur_f(t.p, t.w, t.h, 3, s1 * SC[i]);
		for (size_t k = 0; k < n; k++) acc.p[k] += WT[i] * t.p[k];
	}
	float tint[3] = {1.f + 0.3f * wm, 1.f, 1.f - 0.3f * wm};
	add_light(im, &acc, amt * 1.0f, tint);
	img_free(&sm);
	img_free(&acc);
	img_free(&t);
}

/* ---- Light streaks (anamorphic) ---- */
typedef struct { const Img *src; Img *dst; float dx, dy, decay; int N; } StrCtx;
static void streak_job(void *v, int y0, int y1) {
	StrCtx *c = (StrCtx *)v;
	int w = c->dst->w;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, d += 3) {
			float r = 0, g = 0, b = 0, ws = 0;
			for (int i = -c->N; i <= c->N; i++) {
				float wt = expf(-fabsf((float)i) * c->decay);
				float s[3];
				bilin(c->src, x + 0.5f + i * c->dx, y + 0.5f + i * c->dy, s);
				r += wt * s[0];
				g += wt * s[1];
				b += wt * s[2];
				ws += wt;
			}
			d[0] = r / ws;
			d[1] = g / ws;
			d[2] = b / ws;
		}
	}
}
static void apply_streak(const Layer *L, Img *im) {
	static const float TINT[4][3] = {{1, 1, 1}, {1.0f, 0.72f, 0.45f}, {0.45f, 0.78f, 1.0f}, {1.0f, 0.45f, 0.9f}};
	float amt = L->p[ST_AMT] / 100.f, thr = L->p[ST_THR] / 100.f, len = L->p[ST_LEN] / 100.f, ang = L->p[ST_ANG] * PI_F / 180.f;
	if (amt <= 0.f || len <= 0.f) return;
	Img sm = small_copy(im, 560);
	extract_highlights(&sm, thr);
	Img out = img_new(sm.w, sm.h);
	int N = 26;
	float half = len * 0.5f * (float)MIN(sm.w, sm.h) * 1.4f;
	StrCtx c = {&sm, &out, cosf(ang) * half / N, sinf(ang) * half / N, 2.6f / N, N};
	parallel(streak_job, &c, out.h);
	/* the streak averages energy away, so scale up (length compensates) */
	size_t n = (size_t)out.w * out.h * 3;
	float gain = 1.1f + 1.6f * len;
	for (size_t i = 0; i < n; i++) out.p[i] *= gain;
	add_light(im, &out, amt * 1.1f, TINT[CLAMP((int)L->p[ST_TINT], 0, 3)]);
	img_free(&sm);
	img_free(&out);
}

/* ---- Dehaze ---- */
typedef struct { Img *im; const float *V; float amt, A, sat; } DhCtx;
static void dehaze_job(void *v, int y0, int y1) {
	DhCtx *c = (DhCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *V = c->V + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float ratio = CL01(V[x] / c->A);
			if (c->amt >= 0.f) {
				float t = MAX(1.f - c->amt * 0.92f * ratio, 0.18f);
				for (int k = 0; k < 3; k++) p[k] = CL01((p[k] - c->A) / t + c->A);
			} else {
				float k2 = -c->amt * 0.55f * (1.f - 0.4f * ratio);
				for (int k = 0; k < 3; k++) p[k] = LERP(p[k], c->A, k2);
			}
		}
	}
}
static void apply_dehaze(const Layer *L, Img *im) {
	float amt = L->p[DH_AMT] / 100.f, sat = L->p[DH_SAT] / 100.f;
	if (amt == 0.f && sat == 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	if (amt != 0.f) {
		float *mn = (float *)xmalloc(n * sizeof(float)), *V = (float *)xmalloc(n * sizeof(float));
		for (size_t i = 0; i < n; i++) mn[i] = MIN(im->p[i * 3], MIN(im->p[i * 3 + 1], im->p[i * 3 + 2]));
		guided_self(mn, V, w, h, MAX(3, (int)(MIN(w, h) * 0.035f)), 0.01f);
		/* airlight = high percentile of the smoothed dark channel */
		int bins[512] = {0};
		for (size_t i = 0; i < n; i += 3) bins[(int)(CL01(V[i]) * 511.f)]++;
		size_t tot = (n + 2) / 3, acc = 0;
		float A = 0.6f;
		for (int b = 511; b >= 0; b--) {
			acc += bins[b];
			if (acc >= tot / 500 + 1) {
				A = (b + 1) / 512.f;
				break;
			}
		}
		A = CLAMP(A, 0.25f, 0.98f);
		DhCtx c = {im, V, amt, A, sat};
		parallel(dehaze_job, &c, h);
		free(mn);
		free(V);
	}
	/* dehazing flattens colour a little, so the saturation slider is applied on top */
	img_saturate(im, 1.f + sat + (amt > 0.f ? 0.15f * amt : 0.f));
	img_clamp(im);
}

/* ---- Color mixer (8 hue bands: hue / saturation / luminance) ---- */
static const float MIX_CENTER[8] = {0, 30, 60, 120, 180, 225, 275, 320};
static const char *MIX_NAMES = "Red|Orange|Yellow|Green|Aqua|Blue|Purple|Magenta";
typedef struct { Img *im; float dH[360], dS[360], dL[360]; } MixCtx;
static void mix_job(void *v, int y0, int y1) {
	MixCtx *c = (MixCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float h, s, vv;
			rgb2hsv(CL01(p[0]), CL01(p[1]), CL01(p[2]), &h, &s, &vv);
			float sw = smoothf(0.03f, 0.22f, s) * smoothf(0.02f, 0.10f, vv);
			if (sw <= 0.f) continue;
			int hi = (int)h % 360;
			if (hi < 0) hi += 360;
			float nh = h + c->dH[hi] * sw, ns = CL01(s * (1.f + c->dS[hi] * sw));
			float r, g, b;
			hsv2rgb(nh, ns, vv, &r, &g, &b);
			float k = 1.f + c->dL[hi] * sw;
			p[0] = CL01(r * k);
			p[1] = CL01(g * k);
			p[2] = CL01(b * k);
		}
	}
}
static void apply_mixer(const Layer *L, Img *im) {
	int any = 0;
	for (int i = 0; i < 8; i++)
		for (int k = 0; k < 3; k++)
			if (L->mix[i][k] != 0.f) any = 1;
	if (!any) return;
	MixCtx *c = (MixCtx *)xmalloc(sizeof(MixCtx));
	c->im = im;
	for (int hdeg = 0; hdeg < 360; hdeg++) {
		float w[8], sum = 0;
		for (int i = 0; i < 8; i++) {
			float d = fabsf((float)hdeg - MIX_CENTER[i]);
			d = MIN(d, 360.f - d);
			float W = 52.f;
			float cs = d >= W ? 0.f : cosf(d / W * PI_F * 0.5f);
			w[i] = cs * cs;
			sum += w[i];
		}
		float dh = 0, ds = 0, dl = 0;
		if (sum > 1e-6f)
			for (int i = 0; i < 8; i++) {
				float wn = w[i] / sum;
				dh += wn * L->mix[i][0] / 100.f * 35.f;
				ds += wn * L->mix[i][1] / 100.f;
				dl += wn * L->mix[i][2] / 100.f * 0.6f;
			}
		c->dH[hdeg] = dh;
		c->dS[hdeg] = ds;
		c->dL[hdeg] = dl;
	}
	parallel(mix_job, c, im->h);
	free(c);
}

/* ---- Split toning ---- */
typedef struct { Img *im; float cs[3], cm[3], ch[3], ss, sm, sh, bal, blend; } SplitCtx;
static void split_job(void *v, int y0, int y1) {
	SplitCtx *c = (SplitCtx *)v;
	int w = c->im->w;
	float Ls = lum3(c->cs[0], c->cs[1], c->cs[2]), Lh = lum3(c->ch[0], c->ch[1], c->ch[2]), Lm = lum3(c->cm[0], c->cm[1], c->cm[2]);
	float mid = 0.5f + 0.3f * c->bal, ww = 0.1f + 0.44f * c->blend;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float L = lum3(p[0], p[1], p[2]);
			float wh = smoothf(mid - ww, mid + ww, L), wsh = 1.f - wh;
			float t = (L - mid) / (0.18f + 0.3f * c->blend), wm = expf(-t * t);
			wh *= 1.f - 0.5f * wm;
			wsh *= 1.f - 0.5f * wm;
			for (int k = 0; k < 3; k++) p[k] = CL01(p[k] + (c->cs[k] - Ls) * wsh * c->ss * 0.55f + (c->cm[k] - Lm) * wm * c->sm * 0.55f + (c->ch[k] - Lh) * wh * c->sh * 0.55f);
		}
	}
}
static void apply_split(const Layer *L, Img *im) {
	SplitCtx c;
	c.im = im;
	hue_rgb(L->p[SP_SHUE], &c.cs[0], &c.cs[1], &c.cs[2]);
	hue_rgb(L->p[SP_MHUE], &c.cm[0], &c.cm[1], &c.cm[2]);
	hue_rgb(L->p[SP_HHUE], &c.ch[0], &c.ch[1], &c.ch[2]);
	c.sm = L->p[SP_MSTR] / 100.f;
	c.blend = L->p[SP_BLEND] / 100.f;
	c.ss = L->p[SP_SSTR] / 100.f;
	c.sh = L->p[SP_HSTR] / 100.f;
	c.bal = L->p[SP_BAL] / 100.f;
	if (c.ss <= 0.f && c.sh <= 0.f && c.sm <= 0.f) return;
	parallel(split_job, &c, im->h);
}

/* ---- Denoise (edge-aware luminance + guided chroma smoothing) ---- */
static void guided_cross(const float *G, const float *I, float *out, int w, int h, int r, float eps) {
	size_t n = (size_t)w * h;
	if (r > w / 2 - 1) r = MAX(1, w / 2 - 1);
	if (r > h / 2 - 1) r = MAX(1, h / 2 - 1);
	float *mG = (float *)xmalloc(n * 4), *mI = (float *)xmalloc(n * 4), *a = (float *)xmalloc(n * 4), *b = (float *)xmalloc(n * 4), *tmp = (float *)xmalloc(n * 4);
	box_blur(G, mG, w, h, 1, r, tmp);
	box_blur(I, mI, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) a[i] = G[i] * I[i];
	box_blur(a, a, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) b[i] = G[i] * G[i];
	box_blur(b, b, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) {
		float cov = a[i] - mG[i] * mI[i], var = b[i] - mG[i] * mG[i];
		if (var < 0) var = 0;
		float aa = cov / (var + eps);
		a[i] = aa;
		b[i] = mI[i] - aa * mG[i];
	}
	box_blur(a, a, w, h, 1, r, tmp);
	box_blur(b, b, w, h, 1, r, tmp);
	for (size_t i = 0; i < n; i++) out[i] = a[i] * G[i] + b[i];
	free(mG);
	free(mI);
	free(a);
	free(b);
	free(tmp);
}
static void apply_denoise(const Layer *L, Img *im) {
	float lum = L->p[DN_LUM] / 100.f, col = L->p[DN_COL] / 100.f, det = L->p[DN_DET] / 100.f;
	if (lum <= 0.f && col <= 0.f) return;
	int w = im->w, h = im->h;
	size_t n = (size_t)w * h;
	float m = (float)MIN(w, h);
	float *Y = (float *)xmalloc(n * 4), *cb = (float *)xmalloc(n * 4), *cr = (float *)xmalloc(n * 4), *t = (float *)xmalloc(n * 4);
	for (size_t i = 0; i < n; i++) {
		float r = im->p[i * 3], g = im->p[i * 3 + 1], b = im->p[i * 3 + 2];
		Y[i] = 0.299f * r + 0.587f * g + 0.114f * b;
		cb[i] = b - Y[i];
		cr[i] = r - Y[i];
	}
	if (col > 0.f) {
		int r = MAX(2, (int)(m * (0.004f + 0.012f * col)));
		float eps = 0.0008f + 0.01f * (1.f - col);
		guided_cross(Y, cb, t, w, h, r, eps);
		memcpy(cb, t, n * 4);
		guided_cross(Y, cr, t, w, h, r, eps);
		memcpy(cr, t, n * 4);
	}
	if (lum > 0.f) {
		int r = MAX(1, (int)(m * (0.0015f + 0.0035f * lum)));
		float eps = (0.0004f + 0.012f * lum * lum) * (1.f - det * 0.75f);
		guided_self(Y, t, w, h, r, eps);
		for (size_t i = 0; i < n; i++) Y[i] = LERP(Y[i], t[i], MIN(1.f, lum * 1.15f));
	}
	for (size_t i = 0; i < n; i++) {
		float r = Y[i] + cr[i], b = Y[i] + cb[i];
		float g = (Y[i] - 0.299f * r - 0.114f * b) / 0.587f;
		im->p[i * 3] = CL01(r);
		im->p[i * 3 + 1] = CL01(g);
		im->p[i * 3 + 2] = CL01(b);
	}
	free(Y);
	free(cb);
	free(cr);
	free(t);
}

/* ---- Chromatic aberration (red/blue fringes, positive or negative) ---- */
typedef struct { const Img *src; Img *dst; float k, bias; } CaCtx;
static void ca_job(void *v, int y0, int y1) {
	CaCtx *c = (CaCtx *)v;
	int w = c->dst->w, h = c->dst->h;
	float cx = w * 0.5f, cy = h * 0.5f, rmax2 = cx * cx + cy * cy;
	for (int y = y0; y < y1; y++) {
		float *d = c->dst->p + (size_t)y * w * 3;
		const float *s = c->src->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, d += 3, s += 3) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float r2 = (dx * dx + dy * dy) / rmax2;
			float k = c->k * LERP(1.f, r2, c->bias);
			float a[3], b[3];
			bilin(c->src, cx + dx * (1.f + k), cy + dy * (1.f + k), a);
			bilin(c->src, cx + dx * (1.f - k), cy + dy * (1.f - k), b);
			d[0] = a[0];
			d[1] = s[1];
			d[2] = b[2];
		}
	}
}
static void apply_chroma(const Layer *L, Img *im) {
	float amt = L->p[CA_AMT] / 100.f;
	if (amt == 0.f) return;
	Img out = img_new(im->w, im->h);
	CaCtx c = {im, &out, amt * 0.012f, L->p[CA_EDGE] / 100.f};
	parallel(ca_job, &c, im->h);
	img_free(im);
	*im = out;
}

/* ---- Gradient filter ---- */
typedef struct { Img *im; float cx, cy, nx, ny, f, br, co, sa, wa; } GradCtx;
static void grad_job(void *v, int y0, int y1) {
	GradCtx *c = (GradCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float d = (x + 0.5f - c->cx) * c->nx + (y + 0.5f - c->cy) * c->ny;
			float m = 1.f - smoothf(-c->f, c->f, d);
			if (m < 0.002f) continue;
			float r = p[0], g = p[1], b = p[2];
			if (c->br != 0.f) {
				float k = exp2f(c->br * m * 1.3f);
				r *= k;
				g *= k;
				b *= k;
			}
			if (c->co != 0.f) {
				float k = 1.f + c->co * m * 0.9f;
				r = 0.5f + (r - 0.5f) * k;
				g = 0.5f + (g - 0.5f) * k;
				b = 0.5f + (b - 0.5f) * k;
			}
			if (c->sa != 0.f) {
				float Lm = lum3(r, g, b), k = 1.f + c->sa * m;
				r = Lm + (r - Lm) * k;
				g = Lm + (g - Lm) * k;
				b = Lm + (b - Lm) * k;
			}
			if (c->wa != 0.f) {
				r *= 1.f + 0.22f * c->wa * m;
				b *= 1.f - 0.22f * c->wa * m;
			}
			p[0] = CL01(r);
			p[1] = CL01(g);
			p[2] = CL01(b);
		}
	}
}
static void apply_grad(const Layer *L, Img *im) {
	GradCtx c;
	c.im = im;
	c.cx = L->cx * im->w;
	c.cy = L->cy * im->h;
	float th = L->p[GD_ANG] * PI_F / 180.f;
	c.nx = sinf(th);
	c.ny = cosf(th);
	c.f = (0.004f + L->p[GD_FEA] / 100.f * 0.5f) * (float)MIN(im->w, im->h);
	c.br = L->p[GD_BRI] / 100.f;
	c.co = L->p[GD_CON] / 100.f;
	c.sa = L->p[GD_SAT] / 100.f;
	c.wa = L->p[GD_WARM] / 100.f;
	if (c.br == 0.f && c.co == 0.f && c.sa == 0.f && c.wa == 0.f) return;
	parallel(grad_job, &c, im->h);
}
/* ------------------------------------------------------------------ Halation */
static void apply_halation(const Layer *L, Img *im) {
	float amt = L->p[HA_AMT] / 100.f, thr = L->p[HA_THR] / 100.f, rad = L->p[HA_RAD] / 100.f;
	if (amt <= 0.f) return;
	static const float TINT[4][3] = {{1.f, 0.10f, 0.03f}, {1.f, 0.32f, 0.04f}, {1.f, 0.28f, 0.42f}, {0.95f, 0.08f, 0.75f}};
	const float *tn = TINT[CLAMP((int)L->p[HA_TINT], 0, 3)];
	Img sm = small_copy(im, 1000);
	extract_highlights(&sm, thr);
	/* keep only the bright energy as a single channel, then spread it widely */
	size_t n = (size_t)sm.w * sm.h;
	float *l = (float *)xmalloc(n * 4);
	for (size_t i = 0; i < n; i++) l[i] = lum3(sm.p[i * 3], sm.p[i * 3 + 1], sm.p[i * 3 + 2]);
	float m = (float)MIN(sm.w, sm.h), s = MAX(2.f, m * (0.012f + 0.07f * rad));
	blur_f(l, sm.w, sm.h, 1, s);
	float *l2 = (float *)xmalloc(n * 4);
	memcpy(l2, l, n * 4);
	blur_f(l2, sm.w, sm.h, 1, s * 2.4f);
	Img acc = img_new(sm.w, sm.h);
	for (size_t i = 0; i < n; i++) {
		float v = 0.6f * l[i] + 0.7f * l2[i];
		for (int k = 0; k < 3; k++) acc.p[i * 3 + k] = v;
	}
	add_light(im, &acc, amt * 0.5f, tn);
	/* halation also warms and slightly lifts the core highlights */
	free(l);
	free(l2);
	img_free(&sm);
	img_free(&acc);
}

/* ------------------------------------------------------------------ Double Exposure */
static char dx_path[1024];
static Img dx_img;
static int dx_ok = 0;
static int dx_load(const char *path) {
	if (dx_ok && !strcmp(dx_path, path)) return 1;
	int w, h, n;
	uint8_t *px = stbi_load(path, &w, &h, &n, 3);
	if (!px) return 0;
	if (dx_ok) img_free(&dx_img);
	int mx = MAX(w, h);
	if (mx > 2400) {
		float s = 2400.f / mx;
		dx_img = resize_u8(px, w, h, MAX(2, (int)(w * s)), MAX(2, (int)(h * s)));
	} else
		dx_img = resize_u8(px, w, h, w, h);
	free(px);
	snprintf(dx_path, sizeof dx_path, "%s", path);
	dx_ok = 1;
	return 1;
}
static float blend_ch(int mode, float a, float b) {
	switch (mode) {
	case 1: return a + b;
	case 2: return 1.f - (1.f - a) * (1.f - b);
	case 3: return a * b;
	case 4: return a < 0.5f ? 2.f * a * b : 1.f - 2.f * (1.f - a) * (1.f - b);
	case 5: return b < 0.5f ? a - (1.f - 2.f * b) * a * (1.f - a) : a + (2.f * b - 1.f) * (sqrtf(a) - a);
	case 6: return MAX(a, b);
	case 7: return MIN(a, b);
	case 8: return fabsf(a - b);
	}
	return b;
}
typedef struct { Img *im; float cx, cy, c, s, inv, opac, bri; int mode; } DxCtx;
static void dx_job(void *v, int y0, int y1) {
	DxCtx *c = (DxCtx *)v;
	int w = c->im->w, h = c->im->h;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3) {
			float px = x + 0.5f - w * 0.5f, py = y + 0.5f - h * 0.5f;
			float rx = (px * c->c + py * c->s) * c->inv + c->cx, ry = (-px * c->s + py * c->c) * c->inv + c->cy;
			float W = (float)dx_img.w, H = (float)dx_img.h;
			float e = MIN(MIN(rx, W - rx), MIN(ry, H - ry));
			float cov = CL01(e / c->inv + 0.5f);
			if (cov <= 0.f) continue;
			float b[3];
			bilin(&dx_img, CLAMP(rx, 0.f, W - 1.f), CLAMP(ry, 0.f, H - 1.f), b);
			float a = c->opac * cov;
			for (int k = 0; k < 3; k++) {
				float t = CL01(blend_ch(c->mode, p[k], CL01(b[k] + c->bri)));
				p[k] = LERP(p[k], t, a);
			}
		}
	}
}
static void apply_double(const Layer *L, Img *im) {
	if (!L->path[0] || !dx_load(L->path)) return;
	float W = (float)dx_img.w, H = (float)dx_img.h;
	float cover = MAX(im->w / W, im->h / H);
	float sc = cover * L->p[DX_SCALE] / 100.f, a = L->p[DX_ROT] * PI_F / 180.f;
	DxCtx c = {im, W * 0.5f, H * 0.5f, cosf(a), sinf(a), 1.f / sc, L->p[DX_OPAC] / 100.f, L->p[DX_BRI] / 200.f, (int)L->p[DX_BLEND]};
	if (c.opac <= 0.f) return;
	parallel(dx_job, &c, im->h);
}

/* ------------------------------------------------------------------ Expand */
static void expand_geom(const Layer *L, int w, int h, int *l, int *t, int *r, int *b) {
	*l = (int)(L->p[EX_L] / 100.f * w);
	*t = (int)(L->p[EX_T] / 100.f * h);
	*r = (int)(L->p[EX_R] / 100.f * w);
	*b = (int)(L->p[EX_B] / 100.f * h);
	static const float AS[6] = {0, 1.f, 0.8f, 1.5f, 16.f / 9.f, 9.f / 16.f};
	int ai = CLAMP((int)L->p[EX_ASP], 0, 5);
	if (ai > 0) { /* grow the short side evenly until the aspect fits */
		float ar = AS[ai];
		int tw = w, th = h;
		if ((float)w / h < ar) tw = (int)(h * ar + 0.5f);
		else th = (int)(w / ar + 0.5f);
		*l = (tw - w) / 2;
		*r = tw - w - *l;
		*t = (th - h) / 2;
		*b = th - h - *t;
	}
}
static int refl(int i, int n) {
	if (n <= 1) return 0;
	int p = 2 * (n - 1);
	i %= p;
	if (i < 0) i += p;
	return i < n ? i : p - i;
}
static void apply_expand(const Layer *L, Img *im) {
	int w = im->w, h = im->h, l, t, r, b;
	expand_geom(L, w, h, &l, &t, &r, &b);
	if (l + t + r + b == 0) return;
	int W = w + l + r, H = h + t + b, fill = (int)L->p[EX_FILL];
	Img o = img_new(W, H);
	static const float SOL[3][3] = {{1, 1, 1}, {0, 0, 0}, {0.5f, 0.5f, 0.5f}};
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float *d = o.p + ((size_t)y * W + x) * 3;
			int sx = x - l, sy = y - t;
			if (fill >= 3) {
				if (sx >= 0 && sy >= 0 && sx < w && sy < h) memcpy(d, im->p + ((size_t)sy * w + sx) * 3, 12);
				else memcpy(d, SOL[fill - 3], 12);
			} else
				memcpy(d, im->p + ((size_t)refl(sy, h) * w + refl(sx, w)) * 3, 12);
		}
	if (fill <= 2 && fill != 1) {
		/* Smart: sharp mirror near the photo, melting into blur away from it. Blur: all blur. */
		Img sm = small_copy(&o, 700);
		float s = MAX(2.f, MIN(sm.w, sm.h) * 0.09f);
		blur_f(sm.p, sm.w, sm.h, 3, s);
		float m = (float)MIN(W, H), fd = m * 0.2f;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int sx = x - l, sy = y - t;
				float dx = sx < 0 ? -sx : (sx >= w ? sx - w + 1 : 0), dy = sy < 0 ? -sy : (sy >= h ? sy - h + 1 : 0);
				float dd = sqrtf(dx * dx + dy * dy);
				if (dd <= 0.f) continue;
				float k = fill == 2 ? 1.f : smoothf(0.f, fd, dd);
				float bl[3];
				bilin(&sm, (x + 0.5f) * sm.w / W, (y + 0.5f) * sm.h / H, bl);
				float *d = o.p + ((size_t)y * W + x) * 3;
				float nz = (hashf(x, y, 77u) - 0.5f) * 0.02f;
				for (int c = 0; c < 3; c++) d[c] = CL01(LERP(d[c], bl[c], k) + nz);
			}
		img_free(&sm);
	} else if (fill == 1) {
		/* Mirror: leave the reflection untouched */
	}
	img_free(im);
	*im = o;
}
/* ------------------------------------------------------------------ dispatcher */
/* apply one layer to *im (which may be reallocated/resized by geometry tools) */
static void apply_layer_raw(Layer *L, Img *im) {
	switch (L->tool) {
	case T_TUNE: apply_tune(L, im); break;
	case T_DETAILS: apply_details(L, im); break;
	case T_CURVES: apply_curves(L, im); break;
	case T_WB: apply_wb(L, im); break;
	case T_CROP: apply_crop(L, im); break;
	case T_ROTATE: apply_rotate(L, im); break;
	case T_PERSP: apply_persp(L, im); break;
	case T_SELECTIVE: apply_selective(L, im); break;
	case T_BRUSH: apply_brush(L, im); break;
	case T_HEAL: apply_heal(L, im); break;
	case T_PORTRAIT: apply_portrait(L, im); break;
	case T_VIGNETTE: apply_vignette(L, im); break;
	case T_LENS: apply_lens(L, im); break;
	case T_TEXT: apply_text(L, im); break;
	case T_FRAME: apply_frame(L, im); break;
	case T_BW: apply_bw(L, im); break;
	case T_DRAMA: apply_drama(L, im); break;
	case T_TONAL: apply_tonal(L, im); break;
	case T_HDR: apply_hdr(L, im); break;
	case T_GLOW: apply_glow(L, im); break;
	case T_VINTAGE: apply_vintage(L, im); break;
	case T_RETRO: apply_retro(L, im); break;
	case T_GRAIN: apply_grain(L, im); break;
	case T_NOIR: apply_noir(L, im); break;
	case T_GRUNGE: apply_grunge(L, im); break;
	case T_BLOOM: apply_bloom(L, im); break;
	case T_DEHAZE: apply_dehaze(L, im); break;
	case T_MIXER: apply_mixer(L, im); break;
	case T_SPLIT: apply_split(L, im); break;
	case T_DENOISE: apply_denoise(L, im); break;
	case T_CHROMA: apply_chroma(L, im); break;
	case T_STREAK: apply_streak(L, im); break;
	case T_GRAD: apply_grad(L, im); break;
	case T_HALATION: apply_halation(L, im); break;
	case T_DOUBLE: apply_double(L, im); break;
	case T_EXPAND: apply_expand(L, im); break;
	}
}
/* per-layer mask: strokes paint where the effect shows (red overlay in the UI) */
static inline int maskable(int tool) { return tool != T_CROP && tool != T_ROTATE && tool != T_PERSP && tool != T_FRAME && tool != T_EXPAND && tool != T_TEXT; }
static const float *layer_mask(Layer *L, int w, int h) {
	BrushRT *rt = L->mrt;
	if (rt && (rt->w != w || rt->h != h)) {
		brt_free(rt);
		rt = L->mrt = NULL;
	}
	if (!rt) {
		rt = L->mrt = (BrushRT *)xcalloc(1, sizeof(BrushRT));
		rt->w = w;
		rt->h = h;
	}
	brt_raster(rt, L->mstr, L->nmstr, L->mpts);
	return rt->map[0];
}
typedef struct { Img *im; const Img *before; const float *map; int inv, full; } MaskCtx;
static void mask_job(void *v, int y0, int y1) {
	MaskCtx *c = (MaskCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		float *p = c->im->p + (size_t)y * w * 3;
		const float *q = c->before->p + (size_t)y * w * 3;
		for (int x = 0; x < w; x++, p += 3, q += 3) {
			float a = c->full ? 1.f : (c->map ? CL01(c->map[(size_t)y * w + x]) : 0.f);
			if (c->inv) a = 1.f - a;
			for (int k = 0; k < 3; k++) p[k] = LERP(q[k], p[k], a);
		}
	}
}
static void apply_layer(Layer *L, Img *im) {
	if (!L->mask_on || !maskable(L->tool)) {
		apply_layer_raw(L, im);
		return;
	}
	Img before = img_clone(im);
	apply_layer_raw(L, im);
	if (im->w == before.w && im->h == before.h) {
		MaskCtx c = {im, &before, layer_mask(L, im->w, im->h), L->mask_inv, 0};
		parallel(mask_job, &c, im->h);
	}
	img_free(&before);
}
/* output size of a layer for a given input size (used for the 100% zoom / export size) */
static void layer_outdims(const Layer *L, int *w, int *h) {
	switch (L->tool) {
	case T_CROP: {
		int x0 = (int)roundf(L->cr[0] * *w), y0 = (int)roundf(L->cr[1] * *h), x1 = (int)roundf(L->cr[2] * *w), y1 = (int)roundf(L->cr[3] * *h);
		x0 = CLAMP(x0, 0, *w - 2);
		y0 = CLAMP(y0, 0, *h - 2);
		x1 = CLAMP(x1, x0 + 2, *w);
		y1 = CLAMP(y1, y0 + 2, *h);
		*w = x1 - x0;
		*h = y1 - y0;
	} break;
	case T_ROTATE:
		if (L->rot & 1) {
			int t = *w;
			*w = *h;
			*h = t;
		}
		break;
	case T_FRAME: {
		int bx, bt, bb;
		frame_geom(L, *w, *h, &bx, &bt, &bb);
		*w += 2 * bx;
		*h += bt + bb;
	} break;
	case T_EXPAND: {
		int l, t, r, b;
		expand_geom(L, *w, *h, &l, &t, &r, &b);
		*w += l + r;
		*h += t + b;
	} break;
	}
}

/* ================================================================== document, pipeline, history, I/O */
#define PROXY_DEFAULT 1600
#define HMAX 200

typedef struct {
	char path[1024];
	uint8_t *rgb; /* source pixels, EXIF-oriented, RGB8 */
	int w, h;
	Img proxy; /* float preview-resolution copy of the source */
	int proxy_max;
	uint32_t proxy_gen;
	int loaded;
} Doc;
static Doc doc;

static Layer stack[MAXLAYERS];
static int nlayers = 0;
static int active = -1; /* layer being edited, -1 = none */

/* ---- display mip chain (RGB packed 0x00RRGGBB) ---- */
#define MAXMIP 9
typedef struct {
	int n, w[MAXMIP], h[MAXMIP];
	uint32_t *px[MAXMIP];
} Mips;
static Mips disp, odisp; /* edited result / original (for compare) */
static float histo[4][256];
static double last_render_ms = 0;
static int render_serial = 0; /* bumps when `out` changes */

typedef struct {
	const Img *im;
	uint32_t *dst;
	uint32_t seed;
} CvtCtx;
static void cvt_job(void *v, int y0, int y1) {
	CvtCtx *c = (CvtCtx *)v;
	int w = c->im->w;
	for (int y = y0; y < y1; y++) {
		const float *p = c->im->p + (size_t)y * w * 3;
		uint32_t *d = c->dst + (size_t)y * w;
		for (int x = 0; x < w; x++, p += 3) {
			float dth = hashf(x, y, c->seed) - 0.5f;
			int r = (int)(p[0] * 255.f + 0.5f + dth), g = (int)(p[1] * 255.f + 0.5f + dth), b = (int)(p[2] * 255.f + 0.5f + dth);
			r = CLAMP(r, 0, 255);
			g = CLAMP(g, 0, 255);
			b = CLAMP(b, 0, 255);
			d[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
		}
	}
}
typedef struct {
	const uint32_t *s;
	uint32_t *d;
	int sw, sh, dw, dh;
} MipCtx;
static void mip_job(void *v, int y0, int y1) {
	MipCtx *c = (MipCtx *)v;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->dw; x++) {
			int x0 = MIN(x * 2, c->sw - 1), x1 = MIN(x * 2 + 1, c->sw - 1), ya = MIN(y * 2, c->sh - 1), yb = MIN(y * 2 + 1, c->sh - 1);
			uint32_t q[4] = {c->s[(size_t)ya * c->sw + x0], c->s[(size_t)ya * c->sw + x1], c->s[(size_t)yb * c->sw + x0], c->s[(size_t)yb * c->sw + x1]};
			uint32_t r = 0, g = 0, b = 0;
			for (int i = 0; i < 4; i++) {
				r += (q[i] >> 16) & 255;
				g += (q[i] >> 8) & 255;
				b += q[i] & 255;
			}
			c->d[(size_t)y * c->dw + x] = ((r >> 2) << 16) | ((g >> 2) << 8) | (b >> 2);
		}
}
static void mips_free(Mips *m) {
	for (int i = 0; i < m->n; i++) free(m->px[i]);
	m->n = 0;
}
static void mips_from_img(Mips *m, const Img *im) {
	mips_free(m);
	m->w[0] = im->w;
	m->h[0] = im->h;
	m->px[0] = (uint32_t *)xmalloc((size_t)im->w * im->h * 4);
	CvtCtx c = {im, m->px[0], 1234u};
	parallel(cvt_job, &c, im->h);
	m->n = 1;
	while (m->n < MAXMIP && MIN(m->w[m->n - 1], m->h[m->n - 1]) > 48) {
		int i = m->n;
		m->w[i] = (m->w[i - 1] + 1) / 2;
		m->h[i] = (m->h[i - 1] + 1) / 2;
		m->px[i] = (uint32_t *)xmalloc((size_t)m->w[i] * m->h[i] * 4);
		MipCtx mc = {m->px[i - 1], m->px[i], m->w[i - 1], m->h[i - 1], m->w[i], m->h[i]};
		parallel(mip_job, &mc, m->h[i]);
		m->n++;
	}
}
static void compute_histogram(const Mips *m) {
	memset(histo, 0, sizeof histo);
	if (!m->n) return;
	const uint32_t *p = m->px[MIN(1, m->n - 1)];
	size_t n = (size_t)m->w[MIN(1, m->n - 1)] * m->h[MIN(1, m->n - 1)];
	for (size_t i = 0; i < n; i++) {
		uint32_t c = p[i];
		int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
		histo[0][r]++;
		histo[1][g]++;
		histo[2][b]++;
		histo[3][(int)(0.2126f * r + 0.7152f * g + 0.0722f * b + 0.5f)]++;
	}
}

/* ---- the render pipeline with prefix cache ---- */
static Img pf, out;
static int pf_ok = 0, pf_k = -1, out_ok = 0;
static uint64_t pf_sig = 0, out_sig = 0;
static int need_render = 1;

static inline int is_geom(int tool) { return tool == T_CROP || tool == T_ROTATE || tool == T_PERSP || tool == T_FRAME; }
/* while a layer is being edited: its own crop is shown uncropped, and geometry layers above it are
 * hidden so that on-canvas coordinates stay valid (they come back when the edit is committed) */
static inline int eff_visible(int i) {
	if (!stack[i].visible) return 0;
	if (i == active && stack[i].tool == T_CROP) return 0;
	if (active >= 0 && i > active && is_geom(stack[i].tool)) return 0;
	return 1;
}
static uint64_t stack_sig(int K) {
	uint64_t h = 1469598103934665603ULL;
#define MIX(x) h = (h ^ (uint64_t)(x)) * 1099511628211ULL
	MIX(doc.proxy_gen);
	MIX(K);
	for (int i = 0; i < K; i++) {
		MIX(stack[i].ver);
		MIX(eff_visible(i));
		MIX(stack[i].tool);
	}
#undef MIX
	return h;
}
static void pipeline_invalidate(void) {
	pf_ok = 0;
	out_ok = 0;
	need_render = 1;
}
static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

static void pipeline_render(void) {
	if (!doc.loaded) return;
	double t0 = now_ms();
	int n = nlayers;
	int K = active >= 0 ? active : n;
	uint64_t sigK = stack_sig(K);
	if (!(pf_ok && pf_k == K && pf_sig == sigK)) {
		if (out_ok && K == n && out_sig == sigK)
			img_assign(&pf, &out);
		else {
			img_assign(&pf, &doc.proxy);
			for (int i = 0; i < K; i++)
				if (eff_visible(i)) apply_layer(&stack[i], &pf);
		}
		pf_k = K;
		pf_sig = sigK;
		pf_ok = 1;
	}
	uint64_t sigN = stack_sig(n);
	if (!(out_ok && out_sig == sigN)) {
		img_assign(&out, &pf);
		for (int i = K; i < n; i++)
			if (eff_visible(i)) apply_layer(&stack[i], &out);
		out_sig = sigN;
		out_ok = 1;
		mips_from_img(&disp, &out);
		compute_histogram(&disp);
		render_serial++;
	}
	need_render = 0;
	last_render_ms = now_ms() - t0;
}

/* final size of the exported image (computed from the stack, no pixels touched) */
static void full_dims(int *w, int *h) {
	*w = doc.w;
	*h = doc.h;
	for (int i = 0; i < nlayers; i++)
		if (stack[i].visible) layer_outdims(&stack[i], w, h);
}

/* ---- history (snapshots of the whole stack; layers are small) ---- */
typedef struct {
	Layer *ls;
	int n;
} Snap;
static Snap hist[HMAX + 1];
static int hn = 0, hpos = -1;
static void snap_take(Snap *s) {
	s->n = nlayers;
	s->ls = nlayers ? (Layer *)xmalloc(sizeof(Layer) * nlayers) : NULL;
	for (int i = 0; i < nlayers; i++) layer_copy(&s->ls[i], &stack[i]);
}
static void snap_free(Snap *s) {
	for (int i = 0; i < s->n; i++) layer_free(&s->ls[i]);
	free(s->ls);
	s->ls = NULL;
	s->n = 0;
}
static void stack_clear(void) {
	for (int i = 0; i < nlayers; i++) layer_free(&stack[i]);
	nlayers = 0;
}
static void snap_restore(const Snap *s) {
	stack_clear();
	for (int i = 0; i < s->n; i++) layer_copy(&stack[i], &s->ls[i]);
	nlayers = s->n;
	need_render = 1;
}
static void hist_reset(void) {
	for (int i = 0; i < hn; i++) snap_free(&hist[i]);
	hn = 0;
	hpos = -1;
}
static void hist_push(void) {
	for (int j = hpos + 1; j < hn; j++) snap_free(&hist[j]);
	hn = hpos + 1;
	if (hn >= HMAX) {
		snap_free(&hist[0]);
		memmove(&hist[0], &hist[1], sizeof(Snap) * (hn - 1));
		hn--;
	}
	snap_take(&hist[hn]);
	hpos = hn;
	hn++;
}
static int hist_can_undo(void) { return hpos > 0; }
static int hist_can_redo(void) { return hpos >= 0 && hpos < hn - 1; }

/* ---- edit sessions (a tool being edited on the active layer) ---- */
static int edit_dirty = 0;
static Layer edit_backup;
static int edit_has_backup = 0, edit_is_new = 0;

static void layer_remove(int i) {
	layer_free(&stack[i]);
	memmove(&stack[i], &stack[i + 1], sizeof(Layer) * (nlayers - i - 1));
	nlayers--;
	memset(&stack[nlayers], 0, sizeof(Layer));
	if (active == i)
		active = -1;
	else if (active > i)
		active--;
	need_render = 1;
}
static void edit_begin(int idx, int is_new) {
	if (edit_has_backup) layer_free(&edit_backup);
	layer_copy(&edit_backup, &stack[idx]);
	edit_has_backup = 1;
	edit_is_new = is_new;
	edit_dirty = 0;
	active = idx;
	need_render = 1;
}
/* leave the tool: commit keeps edits (and records an undo step), !commit rolls them back */
static void edit_end(int commit) {
	if (active < 0) return;
	int i = active, changed = 0;
	if (!commit) {
		if (edit_is_new)
			layer_remove(i);
		else if (edit_has_backup) {
			layer_free(&stack[i]);
			layer_copy(&stack[i], &edit_backup);
			layer_bump(&stack[i]);
		}
	} else if (!layer_has_data(&stack[i]) || (edit_is_new && !stack[i].touched)) {
		changed = edit_dirty && !edit_is_new;
		layer_remove(i);
	} else
		changed = edit_dirty;
	active = -1;
	edit_dirty = 0;
	need_render = 1;
	if (changed) hist_push();
}
static Layer *layer_new(int tool) {
	if (nlayers >= MAXLAYERS) return NULL;
	layer_init(&stack[nlayers], tool);
	return &stack[nlayers++];
}
static void select_tool(int tool) {
	if (active >= 0 && stack[active].tool == tool) return;
	edit_end(1);
	for (int i = nlayers - 1; i >= 0; i--)
		if (stack[i].tool == tool) {
			edit_begin(i, 0);
			return;
		}
	Layer *L = layer_new(tool);
	if (L) {
		edit_begin(nlayers - 1, 1);
		if (TD[tool].group[0] == 'F' || TD[tool].group[0] == 'E') { /* filters / effects show their default look */
			stack[active].touched = 1;
			edit_dirty = 1;
		}
	}
}
static void select_layer(int i) {
	if (i < 0 || i >= nlayers) return;
	if (active == i) return;
	edit_end(1);
	if (i >= nlayers) return;
	edit_begin(i, 0);
}
static void touch(Layer *L) {
	L->touched = 1;
	edit_dirty = 1;
	layer_bump(L);
	need_render = 1;
}

/* ---- looks (preset bundles of layers) ---- */
typedef struct {
	int tool, n;
	struct {
		int idx;
		float v;
	} a[6];
} LookPart;
typedef struct {
	const char *name;
	int np;
	LookPart part[3];
} Look;
static const Look LOOKS[] = {
	{"Pop", 2, {{T_TUNE, 3, {{TU_CON, 18}, {TU_SAT, 28}, {TU_AMB, 22}}}, {T_DETAILS, 1, {{DE_STR, 20}}}}},
	{"Fine Art", 2, {{T_TUNE, 4, {{TU_AMB, 35}, {TU_SHA, 25}, {TU_HIG, -20}, {TU_SAT, -12}}}, {T_DETAILS, 1, {{DE_STR, 30}}}}},
	{"Punch", 2, {{T_TUNE, 3, {{TU_CON, 25}, {TU_SAT, 20}, {TU_AMB, 10}}}, {T_DRAMA, 2, {{DR_STYLE, 0}, {DR_STR, 35}}}}},
	{"Soft Glow", 2, {{T_GLOW, 2, {{GL_GLOW, 55}, {GL_SAT, 8}}}, {T_TUNE, 2, {{TU_BRI, 8}, {TU_WAR, 8}}}}},
	{"Morning", 2, {{T_TUNE, 4, {{TU_BRI, 12}, {TU_WAR, 22}, {TU_SHA, 18}, {TU_CON, -8}}}, {T_GLOW, 2, {{GL_STYLE, 1}, {GL_GLOW, 30}}}}},
	{"Moody", 2, {{T_TUNE, 4, {{TU_BRI, -12}, {TU_CON, 15}, {TU_SAT, -18}, {TU_SHA, -10}}}, {T_VIGNETTE, 1, {{VG_OUTER, -45}}}}},
	{"Faded", 1, {{T_VINTAGE, 3, {{VI_STYLE, 3}, {VI_STR, 60}, {VI_VIG, 20}}}}},
	{"Cinema", 2, {{T_VINTAGE, 3, {{VI_STYLE, 1}, {VI_STR, 85}, {VI_VIG, 25}}}, {T_TUNE, 1, {{TU_CON, 10}}}}},
	{"HDR", 1, {{T_HDR, 2, {{HD_STYLE, 0}, {HD_STR, 60}}}}},
	{"Drama", 1, {{T_DRAMA, 2, {{DR_STYLE, 1}, {DR_STR, 60}}}}},
	{"Retro", 1, {{T_RETRO, 2, {{RE_STYLE, 0}, {RE_STR, 75}}}}},
	{"Film", 1, {{T_GRAIN, 3, {{GR_STYLE, 1}, {GR_GRAIN, 45}, {GR_STR, 65}}}}},
	{"Silver", 1, {{T_BW, 3, {{BW_FILTER, 2}, {BW_CON, 30}, {BW_GRAIN, 10}}}}},
	{"Noir", 1, {{T_NOIR, 3, {{NO_STYLE, 0}, {NO_WASH, 15}, {NO_VIG, 45}}}}},
	{"Portrait", 2, {{T_PORTRAIT, 2, {{PO_SMOOTH, 45}, {PO_BRIGHT, 25}}}, {T_TUNE, 2, {{TU_AMB, 12}, {TU_WAR, 6}}}}},
	{"Dreamy", 2, {{T_BLOOM, 3, {{BL_AMT, 70}, {BL_THR, 45}, {BL_RAD, 55}}}, {T_TUNE, 2, {{TU_WAR, 10}, {TU_SAT, 12}}}}},
	{"Teal & Orange", 2, {{T_SPLIT, 4, {{SP_SHUE, 195}, {SP_SSTR, 55}, {SP_HHUE, 32}, {SP_HSTR, 45}}}, {T_TUNE, 2, {{TU_CON, 14}, {TU_SAT, 8}}}}},
	{"Clear Air", 2, {{T_DEHAZE, 2, {{DH_AMT, 55}, {DH_SAT, 12}}}, {T_DETAILS, 1, {{DE_STR, 20}}}}},
	{"Anamorphic", 3, {{T_STREAK, 3, {{ST_AMT, 60}, {ST_THR, 65}, {ST_TINT, 2}}}, {T_BLOOM, 2, {{BL_AMT, 30}, {BL_THR, 60}}}, {T_VIGNETTE, 1, {{VG_OUTER, -35}}}}},
	{"Cool Matte", 2, {{T_TUNE, 3, {{TU_WAR, -20}, {TU_SAT, -10}, {TU_CON, 8}}}, {T_VINTAGE, 3, {{VI_STYLE, 1}, {VI_STR, 40}, {VI_VIG, 0}}}}},
};
#define NLOOKS ((int)(sizeof(LOOKS) / sizeof(LOOKS[0])))
/* build the layers of a look into out[] (up to 3); returns count */
static int look_build(const Look *lk, Layer *out_l) {
	for (int i = 0; i < lk->np; i++) {
		layer_init(&out_l[i], lk->part[i].tool);
		for (int j = 0; j < lk->part[i].n; j++) out_l[i].p[lk->part[i].a[j].idx] = lk->part[i].a[j].v;
		out_l[i].touched = 1;
	}
	return lk->np;
}
/* ---- custom looks (saved from the current edit stack, stored in the user's pref dir) ---- */
#define MAXCL 16
#define CL_LAYERS 8
typedef struct {
	char name[32];
	int n;
	Layer ls[CL_LAYERS];
} CLook;
static CLook clooks[MAXCL];
static int ncl = 0;
static int lt_serial = -1, lt_next = 0;
static int look_total(void) { return NLOOKS + ncl; }
static const char *look_name(int i) { return i < NLOOKS ? LOOKS[i].name : clooks[i - NLOOKS].name; }
static int look_layers(int i, Layer *o) {
	if (i < NLOOKS) return look_build(&LOOKS[i], o);
	const CLook *c = &clooks[i - NLOOKS];
	for (int k = 0; k < c->n; k++) {
		layer_copy(&o[k], &c->ls[k]);
		o[k].touched = 1;
		o[k].visible = 1;
	}
	return c->n;
}
static int look_saveable(const Layer *L) {
	switch (L->tool) {
	case T_CROP: case T_ROTATE: case T_PERSP: case T_EXPAND: case T_BRUSH: case T_HEAL: case T_SELECTIVE: case T_TEXT: case T_DOUBLE: return 0;
	}
	return !L->mask_on;
}
static char *looks_file(char *buf, size_t cap) {
	char *pd = SDL_GetPrefPath("snapdesk", "snapdesk");
	if (!pd) return NULL;
	snprintf(buf, cap, "%slooks.txt", pd);
	SDL_free(pd);
	return buf;
}
static void looks_save_file(void) {
	char path[1100];
	if (!looks_file(path, sizeof path)) return;
	FILE *f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < ncl; i++) {
		fprintf(f, "LOOK %s\n", clooks[i].name);
		for (int k = 0; k < clooks[i].n; k++) {
			const Layer *L = &clooks[i].ls[k];
			char tn[64];
			snprintf(tn, sizeof tn, "%s", TD[L->tool].name);
			for (char *c = tn; *c; c++)
				if (*c == ' ') *c = '_';
			fprintf(f, "L %s", tn);
			for (int j = 0; j < MAXP; j++) fprintf(f, " %g", L->p[j]);
			fprintf(f, " %g %g\n", L->cx, L->cy);
			if (L->tool == T_MIXER) {
				fprintf(f, "M");
				for (int b = 0; b < 8; b++)
					for (int j = 0; j < 3; j++) fprintf(f, " %g", L->mix[b][j]);
				fprintf(f, "\n");
			}
			for (int ch = 0; ch < 4; ch++)
				if (L->ncp[ch] > 1) {
					fprintf(f, "C %d %d", ch, L->ncp[ch]);
					for (int j = 0; j < L->ncp[ch]; j++) fprintf(f, " %g %g", L->cp[ch][j][0], L->cp[ch][j][1]);
					fprintf(f, "\n");
				}
		}
		fprintf(f, "END\n");
	}
	fclose(f);
}
static void looks_load_file(void) {
	char path[1100], ln[1024];
	ncl = 0;
	if (!looks_file(path, sizeof path)) return;
	FILE *f = fopen(path, "r");
	if (!f) return;
	CLook *cur = NULL;
	while (fgets(ln, sizeof ln, f)) {
		size_t n = strlen(ln);
		while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r')) ln[--n] = 0;
		if (!strncmp(ln, "LOOK ", 5)) {
			if (ncl >= MAXCL) break;
			cur = &clooks[ncl++];
			memset(cur, 0, sizeof *cur);
			snprintf(cur->name, sizeof cur->name, "%.31s", ln + 5);
		} else if (cur && !strncmp(ln, "L ", 2) && cur->n < CL_LAYERS) {
			char tn[64];
			if (sscanf(ln + 2, "%63s", tn) != 1) continue;
			for (char *c = tn; *c; c++)
				if (*c == '_') *c = ' ';
			int t = -1;
			for (int i = 0; i < T_COUNT; i++)
				if (!strcmp(TD[i].name, tn)) t = i;
			if (t < 0) continue;
			Layer *L = &cur->ls[cur->n++];
			layer_init(L, t);
			const char *s = ln + 2 + strlen(tn) + 1;
			for (int j = 0; j < MAXP; j++) {
				int used = 0;
				float v;
				if (sscanf(s, "%f%n", &v, &used) == 1) {
					L->p[j] = v;
					s += used;
				}
			}
			sscanf(s, "%f %f", &L->cx, &L->cy);
		} else if (cur && cur->n && !strncmp(ln, "M ", 2)) {
			Layer *L = &cur->ls[cur->n - 1];
			const char *s = ln + 2;
			for (int b = 0; b < 8; b++)
				for (int j = 0; j < 3; j++) {
					int used = 0;
					float v;
					if (sscanf(s, "%f%n", &v, &used) == 1) {
						L->mix[b][j] = v;
						s += used;
					}
				}
		} else if (cur && cur->n && !strncmp(ln, "C ", 2)) {
			Layer *L = &cur->ls[cur->n - 1];
			int ch, cnt, used = 0;
			if (sscanf(ln + 2, "%d %d%n", &ch, &cnt, &used) == 2 && ch >= 0 && ch < 4 && cnt > 1 && cnt <= MAXCP) {
				const char *s = ln + 2 + used;
				L->ncp[ch] = cnt;
				for (int j = 0; j < cnt; j++) {
					int u2 = 0;
					sscanf(s, "%f %f%n", &L->cp[ch][j][0], &L->cp[ch][j][1], &u2);
					s += u2;
				}
			}
		}
	}
	fclose(f);
	lt_serial = -1;
}
/* returns 1 on success; *skipped counts layers that cannot be stored (geometry, painted data...) */
static int look_save_current(const char *name, int *skipped) {
	CLook c;
	memset(&c, 0, sizeof c);
	snprintf(c.name, sizeof c.name, "%.31s", name);
	*skipped = 0;
	for (int i = 0; i < nlayers && c.n < CL_LAYERS; i++) {
		if (!stack[i].visible) continue;
		if (!look_saveable(&stack[i])) {
			(*skipped)++;
			continue;
		}
		layer_copy(&c.ls[c.n], &stack[i]);
		c.ls[c.n].visible = 1;
		c.n++;
	}
	if (!c.n) return 0;
	int slot = -1;
	for (int i = 0; i < ncl; i++)
		if (!strcmp(clooks[i].name, c.name)) slot = i;
	if (slot < 0) {
		if (ncl >= MAXCL) return -1;
		slot = ncl++;
	}
	clooks[slot] = c;
	looks_save_file();
	lt_serial = -1;
	return 1;
}
static void look_delete_custom(int i) {
	i -= NLOOKS;
	if (i < 0 || i >= ncl) return;
	for (int k = i; k < ncl - 1; k++) clooks[k] = clooks[k + 1];
	ncl--;
	looks_save_file();
	lt_serial = -1;
}
static void look_apply(int i) {
	if (i < 0 || i >= look_total()) return;
	edit_end(1);
	Layer tmp[CL_LAYERS];
	int n = look_layers(i, tmp);
	for (int k = 0; k < n; k++) {
		if (nlayers >= MAXLAYERS) break;
		stack[nlayers++] = tmp[k];
	}
	need_render = 1;
	hist_push();
}

/* ---- loading ---- */
static int exif_orientation(const char *path) {
	FILE *f = stbi__fopen(path, "rb");
	if (!f) return 1;
	unsigned char *b = (unsigned char *)xmalloc(131072);
	size_t n = fread(b, 1, 131072, f);
	fclose(f);
	int res = 1;
	if (n > 12 && b[0] == 0xFF && b[1] == 0xD8) {
		size_t pos = 2;
		while (pos + 4 < n && b[pos] == 0xFF) {
			int mk = b[pos + 1];
			size_t len = ((size_t)b[pos + 2] << 8) | b[pos + 3];
			if (mk == 0xDA) break;
			if (mk == 0xE1 && pos + 10 < n && memcmp(b + pos + 4, "Exif\0\0", 6) == 0) {
				const unsigned char *t = b + pos + 10;
				size_t tl = n - (pos + 10);
				int le = t[0] == 'I';
#define R16(o) (le ? (t[o] | (t[(o) + 1] << 8)) : ((t[o] << 8) | t[(o) + 1]))
#define R32(o) (le ? (uint32_t)(t[o] | (t[(o) + 1] << 8) | (t[(o) + 2] << 16) | ((uint32_t)t[(o) + 3] << 24)) : (uint32_t)(((uint32_t)t[o] << 24) | (t[(o) + 1] << 16) | (t[(o) + 2] << 8) | t[(o) + 3]))
				if (tl > 16) {
					uint32_t ifd = R32(4);
					if (ifd + 2 < tl) {
						int cnt = R16(ifd);
						for (int i = 0; i < cnt && ifd + 2 + (size_t)(i + 1) * 12 <= tl; i++) {
							size_t e = ifd + 2 + (size_t)i * 12;
							if (R16(e) == 0x0112) {
								res = R16(e + 8);
								break;
							}
						}
					}
				}
#undef R16
#undef R32
				break;
			}
			pos += 2 + len;
		}
	}
	free(b);
	return (res >= 1 && res <= 8) ? res : 1;
}
static void orient_u8(uint8_t **rgb, int *w, int *h, int o) {
	if (o <= 1) return;
	int sw = *w, sh = *h, swap = o >= 5;
	int dw = swap ? sh : sw, dh = swap ? sw : sh;
	uint8_t *d = (uint8_t *)xmalloc((size_t)dw * dh * 3), *s = *rgb;
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			int nx, ny;
			switch (o) {
			case 2: nx = sw - 1 - x; ny = y; break;
			case 3: nx = sw - 1 - x; ny = sh - 1 - y; break;
			case 4: nx = x; ny = sh - 1 - y; break;
			case 5: nx = y; ny = x; break;
			case 6: nx = sh - 1 - y; ny = x; break;
			case 7: nx = sh - 1 - y; ny = sw - 1 - x; break;
			default: nx = y; ny = sw - 1 - x; break;
			}
			memcpy(d + ((size_t)ny * dw + nx) * 3, s + ((size_t)y * sw + x) * 3, 3);
		}
	free(s);
	*rgb = d;
	*w = dw;
	*h = dh;
}
static void proxy_build(void) {
	if (!doc.rgb) return;
	img_free(&doc.proxy);
	int tw = doc.w, th = doc.h;
	if (doc.proxy_max > 0 && MAX(tw, th) > doc.proxy_max) {
		float s = (float)doc.proxy_max / (float)MAX(tw, th);
		tw = MAX(1, (int)(doc.w * s + 0.5f));
		th = MAX(1, (int)(doc.h * s + 0.5f));
	}
	doc.proxy = resize_u8(doc.rgb, doc.w, doc.h, tw, th);
	doc.proxy_gen++;
	mips_from_img(&odisp, &doc.proxy);
	pipeline_invalidate();
}
static char g_err[256];
static int doc_load(const char *path) {
	int w, h, n;
	uint8_t *px = stbi_load(path, &w, &h, &n, 3);
	if (!px) {
		snprintf(g_err, sizeof g_err, "Cannot open: %s", stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
		return 0;
	}
	int o = exif_orientation(path);
	orient_u8(&px, &w, &h, o);
	if (doc.rgb) free(doc.rgb);
	edit_end(0);
	stack_clear();
	hist_reset();
	doc.rgb = px;
	doc.w = w;
	doc.h = h;
	snprintf(doc.path, sizeof doc.path, "%s", path);
	doc.loaded = 1;
	proxy_build();
	hist_push();
	return 1;
}

/* ---- export ---- */
static int ends_with_ci(const char *s, const char *suf) {
	size_t a = strlen(s), b = strlen(suf);
	if (b > a) return 0;
	for (size_t i = 0; i < b; i++)
		if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
	return 1;
}
static int export_image(const char *path, int quality, int scale_pct, char *msg, size_t msgcap) {
	if (!doc.loaded) return 0;
	int sw = doc.w, sh = doc.h;
	Img im;
	if (scale_pct < 100) {
		sw = MAX(1, doc.w * scale_pct / 100);
		sh = MAX(1, doc.h * scale_pct / 100);
	}
	im = resize_u8(doc.rgb, doc.w, doc.h, sw, sh);
	for (int i = 0; i < nlayers; i++) {
		if (!stack[i].visible) continue;
		Layer c;
		layer_copy(&c, &stack[i]);
		apply_layer(&c, &im);
		layer_free(&c);
	}
	uint8_t *rgb = (uint8_t *)xmalloc((size_t)im.w * im.h * 3);
	for (int y = 0; y < im.h; y++)
		for (int x = 0; x < im.w; x++) {
			float d = hashf(x, y, 4321u) - 0.5f;
			const float *p = im.p + ((size_t)y * im.w + x) * 3;
			for (int k = 0; k < 3; k++) {
				int v = (int)(p[k] * 255.f + 0.5f + d);
				rgb[((size_t)y * im.w + x) * 3 + k] = (uint8_t)CLAMP(v, 0, 255);
			}
		}
	int ok;
	if (ends_with_ci(path, ".png"))
		ok = stbi_write_png(path, im.w, im.h, 3, rgb, im.w * 3);
	else if (ends_with_ci(path, ".bmp"))
		ok = stbi_write_bmp(path, im.w, im.h, 3, rgb);
	else if (ends_with_ci(path, ".tga"))
		ok = stbi_write_tga(path, im.w, im.h, 3, rgb);
	else
		ok = stbi_write_jpg(path, im.w, im.h, 3, rgb, CLAMP(quality, 1, 100));
	if (msg) {
		if (ok)
			snprintf(msg, msgcap, "Saved %dx%d  %s", im.w, im.h, path);
		else
			snprintf(msg, msgcap, "Could not write %s", path);
	}
	free(rgb);
	img_free(&im);
	return ok;
}

/* ---- native file dialogs (Win32 common dialog / zenity / kdialog); returns 1 ok, 0 cancelled, -1 unavailable */
static int native_dialog(int save, const char *start, char *outp, int cap) {
#ifdef _WIN32
	wchar_t wf[1024] = {0};
	if (start) MultiByteToWideChar(CP_UTF8, 0, start, -1, wf, 1024);
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof ofn);
	ofn.lStructSize = sizeof ofn;
	ofn.lpstrFile = wf;
	ofn.nMaxFile = 1024;
	ofn.lpstrFilter = save ? L"JPEG (*.jpg)\0*.jpg\0PNG (*.png)\0*.png\0BMP (*.bmp)\0*.bmp\0" : L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.tga;*.gif;*.psd\0All files\0*.*\0";
	ofn.lpstrDefExt = save ? L"jpg" : NULL;
	ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok) return 0;
	WideCharToMultiByte(CP_UTF8, 0, wf, -1, outp, cap, NULL, NULL);
	return 1;
#else
	static int have = -1; /* 0 none, 1 zenity, 2 kdialog */
	if (have < 0) {
		have = 0;
		if (system("command -v zenity >/dev/null 2>&1") == 0)
			have = 1;
		else if (system("command -v kdialog >/dev/null 2>&1") == 0)
			have = 2;
	}
	if (!have) return -1;
	char q[1100];
	size_t k = 0;
	const char *s = start ? start : "";
	for (; *s && k < sizeof q - 6; s++) {
		if (*s == '\'') {
			memcpy(q + k, "'\\''", 4);
			k += 4;
		} else
			q[k++] = *s;
	}
	q[k] = 0;
	char cmd[1400];
	if (have == 1)
		snprintf(cmd, sizeof cmd, "zenity --file-selection %s --filename='%s' 2>/dev/null", save ? "--save --confirm-overwrite" : "", q);
	else
		snprintf(cmd, sizeof cmd, "kdialog %s '%s' 2>/dev/null", save ? "--getsavefilename" : "--getopenfilename", q);
	FILE *p = popen(cmd, "r");
	if (!p) return -1;
	char line[1100] = {0};
	if (!fgets(line, sizeof line, p)) line[0] = 0;
	int rc = pclose(p);
	(void)rc;
	size_t l = strlen(line);
	while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
	if (!l) return 0;
	snprintf(outp, cap, "%s", line);
	return 1;
#endif
}

/* ================================================================== software UI rasteriser */
static uint32_t *fb = NULL;
static int FW = 0, FH = 0;
static int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0; /* clip rect [x0,x1) [y0,y1) */

#define C_BG 0x17181a
#define C_PANEL 0x212325
#define C_PANEL2 0x2a2d30
#define C_PANEL3 0x34383c
#define C_LINE 0x383c40
#define C_TEXT 0xe8eaed
#define C_DIM 0x9aa0a6
#define C_FAINT 0x636a70
#define C_ACC 0x35c4b6
#define C_ACCD 0x23867d
#define C_WARN 0xef6c5d
#define C_G_ADJ 0x4f9dff
#define C_G_GEO 0x55c27a
#define C_G_RET 0xf2a03d
#define C_G_FIL 0xb77cf0

typedef struct { int x0, y0, x1, y1; } Clip;
static Clip clip_get(void) { Clip c = {cx0_, cy0_, cx1_, cy1_}; return c; }
static void clip_set(Clip c) { cx0_ = c.x0; cy0_ = c.y0; cx1_ = c.x1; cy1_ = c.y1; }
static Clip clip_push(int x, int y, int w, int h) {
	Clip o = clip_get();
	cx0_ = MAX(cx0_, MAX(0, x));
	cy0_ = MAX(cy0_, MAX(0, y));
	cx1_ = MIN(cx1_, MIN(FW, x + w));
	cy1_ = MIN(cy1_, MIN(FH, y + h));
	if (cx1_ < cx0_) cx1_ = cx0_;
	if (cy1_ < cy0_) cy1_ = cy0_;
	return o;
}
static void clip_reset(void) { cx0_ = 0; cy0_ = 0; cx1_ = FW; cy1_ = FH; }

static inline uint32_t blend_px(uint32_t d, uint32_t c, int a) { /* a: 0..256 */
	if (a >= 256) return c;
	if (a <= 0) return d;
	uint32_t rb = (((c & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
	uint32_t g = (((c & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
	return rb | g;
}
static inline void put_px(int x, int y, uint32_t c, int a) {
	if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_) return;
	uint32_t *p = &fb[(size_t)y * FW + x];
	*p = blend_px(*p, c, a);
}
static inline int a256(float alpha) { return (int)(CL01(alpha) * 256.f + 0.5f); }

static void fill_rect(int x, int y, int w, int h, uint32_t c, float alpha) {
	int x0 = MAX(x, cx0_), y0 = MAX(y, cy0_), x1 = MIN(x + w, cx1_), y1 = MIN(y + h, cy1_);
	int a = a256(alpha);
	if (a <= 0) return;
	for (int j = y0; j < y1; j++) {
		uint32_t *p = &fb[(size_t)j * FW];
		if (a >= 256)
			for (int i = x0; i < x1; i++) p[i] = c;
		else
			for (int i = x0; i < x1; i++) p[i] = blend_px(p[i], c, a);
	}
}
static void frame_rect(int x, int y, int w, int h, uint32_t c, float alpha) {
	fill_rect(x, y, w, 1, c, alpha);
	fill_rect(x, y + h - 1, w, 1, c, alpha);
	fill_rect(x, y + 1, 1, h - 2, c, alpha);
	fill_rect(x + w - 1, y + 1, 1, h - 2, c, alpha);
}
static float sdf_rr(float px, float py, float cx, float cy, float hw, float hh, float r) {
	float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
	float ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
	return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - r;
}
static void rrect(float x, float y, float w, float h, float r, uint32_t c, float alpha) {
	int x0 = MAX((int)floorf(x), cx0_), y0 = MAX((int)floorf(y), cy0_), x1 = MIN((int)ceilf(x + w), cx1_), y1 = MIN((int)ceilf(y + h), cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float d = sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r);
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void rrect_line(float x, float y, float w, float h, float r, float t, uint32_t c, float alpha) {
	int x0 = MAX((int)floorf(x) - 1, cx0_), y0 = MAX((int)floorf(y) - 1, cy0_), x1 = MIN((int)ceilf(x + w) + 1, cx1_), y1 = MIN((int)ceilf(y + h) + 1, cy1_);
	float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
	r = MIN(r, MIN(hw, hh));
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float d = fabsf(sdf_rr(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r) + t * 0.5f) - t * 0.5f;
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void disc(float cx, float cy, float r, uint32_t c, float alpha) {
	int x0 = MAX((int)floorf(cx - r - 1), cx0_), y0 = MAX((int)floorf(cy - r - 1), cy0_), x1 = MIN((int)ceilf(cx + r + 1), cx1_), y1 = MIN((int)ceilf(cy + r + 1), cy1_);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
			float cov = CL01(r - sqrtf(dx * dx + dy * dy) + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void ring(float cx, float cy, float r, float t, uint32_t c, float alpha) {
	float ro = r + t * 0.5f;
	int x0 = MAX((int)floorf(cx - ro - 1), cx0_), y0 = MAX((int)floorf(cy - ro - 1), cy0_), x1 = MIN((int)ceilf(cx + ro + 1), cx1_), y1 = MIN((int)ceilf(cy + ro + 1), cy1_);
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
			float d = fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f;
			float cov = CL01(0.5f - d);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void ellipse_ring(float cx, float cy, float a, float b, float ang, float t, uint32_t c, float alpha, int dashed) {
	int N = MAX(48, (int)(MAX(a, b) * 0.6f));
	float cs = cosf(ang), sn = sinf(ang), px = 0, py = 0;
	for (int i = 0; i <= N; i++) {
		float u = i * 2.f * PI_F / N;
		float ex = cosf(u) * a, ey = sinf(u) * b;
		float x = cx + ex * cs - ey * sn, y = cy + ex * sn + ey * cs;
		if (i > 0 && (!dashed || (i & 2) == 0)) {
			/* inline segment */
			float dx = x - px, dy = y - py, len2 = dx * dx + dy * dy;
			int bx0 = MAX((int)floorf(MIN(px, x) - t - 1), cx0_), bx1 = MIN((int)ceilf(MAX(px, x) + t + 1), cx1_);
			int by0 = MAX((int)floorf(MIN(py, y) - t - 1), cy0_), by1 = MIN((int)ceilf(MAX(py, y) + t + 1), cy1_);
			for (int j = by0; j < by1; j++)
				for (int k = bx0; k < bx1; k++) {
					float qx = k + 0.5f - px, qy = j + 0.5f - py;
					float h = len2 > 0 ? CL01((qx * dx + qy * dy) / len2) : 0.f;
					float ddx = qx - dx * h, ddy = qy - dy * h;
					float cov = CL01(t * 0.5f - sqrtf(ddx * ddx + ddy * ddy) + 0.5f);
					if (cov > 0.f) put_px(k, j, c, a256(cov * alpha));
				}
		}
		px = x;
		py = y;
	}
}
static void line_aa(float x0, float y0, float x1, float y1, float t, uint32_t c, float alpha) {
	float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
	int bx0 = MAX((int)floorf(MIN(x0, x1) - t - 1), cx0_), bx1 = MIN((int)ceilf(MAX(x0, x1) + t + 1), cx1_);
	int by0 = MAX((int)floorf(MIN(y0, y1) - t - 1), cy0_), by1 = MIN((int)ceilf(MAX(y0, y1) + t + 1), cy1_);
	for (int j = by0; j < by1; j++)
		for (int i = bx0; i < bx1; i++) {
			float qx = i + 0.5f - x0, qy = j + 0.5f - y0;
			float h = len2 > 0 ? CL01((qx * dx + qy * dy) / len2) : 0.f;
			float ddx = qx - dx * h, ddy = qy - dy * h;
			float cov = CL01(t * 0.5f - sqrtf(ddx * ddx + ddy * ddy) + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, float alpha) {
	int bx0 = MAX((int)floorf(MIN(x0, MIN(x1, x2))) - 1, cx0_), bx1 = MIN((int)ceilf(MAX(x0, MAX(x1, x2))) + 1, cx1_);
	int by0 = MAX((int)floorf(MIN(y0, MIN(y1, y2))) - 1, cy0_), by1 = MIN((int)ceilf(MAX(y0, MAX(y1, y2))) + 1, cy1_);
	float ax[3] = {x0, x1, x2}, ay[3] = {y0, y1, y2};
	float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	float sg = area < 0 ? -1.f : 1.f;
	for (int j = by0; j < by1; j++)
		for (int i = bx0; i < bx1; i++) {
			float px = i + 0.5f, py = j + 0.5f, m = 1e9f;
			for (int k = 0; k < 3; k++) {
				int n = (k + 1) % 3;
				float ex = ax[n] - ax[k], ey = ay[n] - ay[k], l = sqrtf(ex * ex + ey * ey) + 1e-6f;
				float d = sg * (ex * (py - ay[k]) - ey * (px - ax[k])) / l;
				m = MIN(m, d);
			}
			float cov = CL01(m + 0.5f);
			if (cov > 0.f) put_px(i, j, c, a256(cov * alpha));
		}
}
static void soft_shadow(int x, int y, int w, int h, int r, float alpha) {
	for (int i = r; i >= 1; i--) {
		float a = alpha * (1.f - (float)i / (r + 1)) * (1.f - (float)i / (r + 1)) * 0.5f;
		rrect((float)(x - i), (float)(y - i + r / 3), (float)(w + 2 * i), (float)(h + 2 * i), (float)(i + 2), 0x000000, a);
	}
}

/* ---- fonts ---- */
#define NSZ 8
static const int FSZ[NSZ] = {10, 11, 12, 13, 14, 16, 20, 30};
typedef struct {
	uint8_t *bm;
	int w, h, xo, yo, ok;
	float adv;
} Glyph;
static Glyph gcache[2][NSZ][96];
static float fsc[2][NSZ];
static int fasc[2][NSZ];
static stbtt_fontinfo *FI[2];
static void fonts_init(void) {
	if (!stbtt_InitFont(&font_reg, font_regular, 0) || !stbtt_InitFont(&font_med, font_medium, 0)) die("font init failed");
	FI[0] = &font_reg;
	FI[1] = &font_med;
	for (int w = 0; w < 2; w++)
		for (int s = 0; s < NSZ; s++) {
			fsc[w][s] = stbtt_ScaleForPixelHeight(FI[w], (float)FSZ[s]);
			int a, d, g;
			stbtt_GetFontVMetrics(FI[w], &a, &d, &g);
			fasc[w][s] = (int)roundf(a * fsc[w][s]);
		}
}
static int size_idx(int px) {
	int b = 0, bd = 1000;
	for (int i = 0; i < NSZ; i++)
		if (abs(FSZ[i] - px) < bd) {
			bd = abs(FSZ[i] - px);
			b = i;
		}
	return b;
}
static Glyph *get_glyph(int wt, int si, int ch) {
	if (ch < 32 || ch > 126) ch = '?';
	Glyph *g = &gcache[wt][si][ch - 32];
	if (g->ok) return g;
	stbtt_fontinfo *f = FI[wt];
	float sc = fsc[wt][si];
	int x0, y0, x1, y1, adv, lsb;
	stbtt_GetCodepointBitmapBox(f, ch, sc, sc, &x0, &y0, &x1, &y1);
	stbtt_GetCodepointHMetrics(f, ch, &adv, &lsb);
	g->w = x1 - x0;
	g->h = y1 - y0;
	g->xo = x0;
	g->yo = y0;
	g->adv = adv * sc;
	g->ok = 1;
	if (g->w > 0 && g->h > 0) {
		g->bm = (uint8_t *)xmalloc((size_t)g->w * g->h);
		stbtt_MakeCodepointBitmap(f, g->bm, g->w, g->h, g->w, sc, sc, ch);
	}
	return g;
}
static float text_width(const char *s, int px, int wt) {
	int si = size_idx(px);
	float w = 0;
	for (; *s; s++) {
		Glyph *g = get_glyph(wt, si, (unsigned char)*s);
		w += g->adv;
		if (s[1]) w += stbtt_GetCodepointKernAdvance(FI[wt], (unsigned char)*s, (unsigned char)s[1]) * fsc[wt][si];
	}
	return w;
}
/* draws text with its caps vertically centred on cy; returns the advance */
static float text(float x, float cy, const char *s, int px, int wt, uint32_t col, float alpha) {
	int si = size_idx(px);
	float capc = 0.355f * FSZ[si];
	int base = (int)roundf(cy + capc);
	float xp = x;
	for (; *s; s++) {
		Glyph *g = get_glyph(wt, si, (unsigned char)*s);
		if (g->bm) {
			int gx = (int)floorf(xp + 0.5f) + g->xo, gy = base + g->yo;
			for (int j = 0; j < g->h; j++) {
				int yy = gy + j;
				if (yy < cy0_ || yy >= cy1_) continue;
				for (int i = 0; i < g->w; i++) {
					int xx = gx + i;
					if (xx < cx0_ || xx >= cx1_) continue;
					int a = g->bm[j * g->w + i];
					if (!a) continue;
					uint32_t *p = &fb[(size_t)yy * FW + xx];
					*p = blend_px(*p, col, (int)(a * alpha * 256.f / 255.f));
				}
			}
		}
		xp += g->adv;
		if (s[1]) xp += stbtt_GetCodepointKernAdvance(FI[wt], (unsigned char)*s, (unsigned char)s[1]) * fsc[wt][si];
	}
	return xp - x;
}
static void text_r(float xr, float cy, const char *s, int px, int wt, uint32_t col, float alpha) { text(xr - text_width(s, px, wt), cy, s, px, wt, col, alpha); }
static void text_c(float xc, float cy, const char *s, int px, int wt, uint32_t col, float alpha) { text(xc - text_width(s, px, wt) * 0.5f, cy, s, px, wt, col, alpha); }
/* fits text into maxw by trimming with ".." */
static void text_fit(float x, float cy, const char *s, float maxw, int px, int wt, uint32_t col, float alpha) {
	if (text_width(s, px, wt) <= maxw) {
		text(x, cy, s, px, wt, col, alpha);
		return;
	}
	char buf[160];
	size_t n = strlen(s);
	if (n > sizeof buf - 3) n = sizeof buf - 3;
	while (n > 1) {
		memcpy(buf, s, n);
		buf[n] = '.';
		buf[n + 1] = '.';
		buf[n + 2] = 0;
		if (text_width(buf, px, wt) <= maxw) break;
		n--;
	}
	text(x, cy, buf, px, wt, col, alpha);
}

/* ---- small vector icons ---- */
static void icon_eye(float cx, float cy, float s, uint32_t c, float a, int on) {
	ellipse_ring(cx, cy, s, s * 0.58f, 0, 1.4f, c, a, 0);
	if (on) disc(cx, cy, s * 0.32f, c, a);
	else line_aa(cx - s, cy + s * 0.7f, cx + s, cy - s * 0.7f, 1.4f, c, a);
}
static void icon_x(float cx, float cy, float s, uint32_t c, float a) {
	line_aa(cx - s, cy - s, cx + s, cy + s, 1.6f, c, a);
	line_aa(cx - s, cy + s, cx + s, cy - s, 1.6f, c, a);
}
static void icon_check(float cx, float cy, float s, uint32_t c, float a) {
	line_aa(cx - s, cy, cx - s * 0.25f, cy + s * 0.7f, 2.f, c, a);
	line_aa(cx - s * 0.25f, cy + s * 0.7f, cx + s, cy - s * 0.65f, 2.f, c, a);
}
static void icon_undo(float cx, float cy, float s, uint32_t c, float a, int flip) {
	float f = flip ? -1.f : 1.f, r = s * 0.7f, ccy = cy + s * 0.15f;
	float t0 = -2.7f, t1 = 1.0f, px = 0, py = 0;
	int N = 16;
	for (int i = 0; i <= N; i++) {
		float u = t0 + (t1 - t0) * i / N;
		float x = cx + f * r * cosf(u), y = ccy + r * sinf(u);
		if (i) line_aa(px, py, x, y, 1.7f, c, a);
		px = x;
		py = y;
	}
	float sx = cx + f * r * cosf(t0), sy = ccy + r * sinf(t0);
	float dx = f * sinf(t0), dy = -cosf(t0);
	float tx = sx + dx * s * 0.5f, ty = sy + dy * s * 0.5f, ex = -dy, ey = dx;
	tri(tx, ty, sx + ex * s * 0.42f, sy + ey * s * 0.42f, sx - ex * s * 0.42f, sy - ey * s * 0.42f, c, a);
}
static void icon_split(float cx, float cy, float s, uint32_t c, float a) { /* compare icon: two overlapping squares */
	rrect_line(cx - s, cy - s * 0.7f, s * 1.3f, s * 1.4f, 2, 1.5f, c, a);
	rrect_line(cx - s * 0.3f, cy - s * 0.7f, s * 1.3f, s * 1.4f, 2, 1.5f, c, a);
}

/* ================================================================== UI state & widgets */
static int mx = 0, my = 0;
static int mdx = 0, mdy = 0;            /* mouse delta since last frame */
static int m_down[3], m_press[3], m_rel[3];
static int m_dbl = 0, wheel = 0, wheel_used = 0;
static int mod_shift = 0, mod_ctrl = 0, mod_alt = 0;
static int act_id = 0;                  /* widget currently being dragged */
static int modal = 0, in_modal_draw = 0;
static int quit_flag = 0, dirty = 1;
static float ui_scale = 1.f;
static int sel_param = 0;               /* parameter picked for keyboard / scrub */
static int sel_pt = 0;                  /* selective: current control point */
static int wb_pick = 0;
static int curve_ch = 0;
static char toast_msg[300];
static uint32_t toast_until = 0;
static uint32_t hud_until = 0;
static char hud_name[48];
static float hud_val = 0;
static int hud_is_choice = 0;
static char hud_text[48];

enum { MODAL_NONE, MODAL_EXPORT, MODAL_OPENPATH, MODAL_HELP, MODAL_SAVELOOK, MODAL_INFO };

static void toast(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(toast_msg, sizeof toast_msg, fmt, ap);
	va_end(ap);
	toast_until = SDL_GetTicks() + 3500;
	dirty = 1;
}

static int mouse_in(int x, int y, int w, int h) {
	if (modal && !in_modal_draw) return 0;
	if (mx < x || my < y || mx >= x + w || my >= y + h) return 0;
	return mx >= cx0_ && my >= cy0_ && mx < cx1_ && my < cy1_;
}

/* ---- text input field (single active field) ---- */
static int tf_id = 0, tf_enter = 0, tf_kind = 0; /* kind: 0 text, 1 number */
static char tf_buf[512];
static void tf_begin(int id, const char *init, int kind) {
	tf_id = id;
	tf_kind = kind;
	tf_enter = 0;
	snprintf(tf_buf, sizeof tf_buf, "%s", init);
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_StartTextInput();
}
static void tf_end(void) {
	tf_id = 0;
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_StopTextInput();
}
static void tf_append(const char *s) {
	size_t n = strlen(tf_buf);
	for (; *s && n < sizeof tf_buf - 1; s++) {
		unsigned char c = (unsigned char)*s;
		if (c < 32) continue;
		if (tf_kind == 1 && !(isdigit(c) || c == '-' || c == '.')) continue;
		tf_buf[n++] = (char)c;
	}
	tf_buf[n] = 0;
}

/* returns 1 if the text changed this frame */
static int text_field(int id, int x, int y, int w, int h, char *dst, int cap, const char *hint) {
	int changed = 0;
	int hov = mouse_in(x, y, w, h);
	if (m_press[0]) {
		if (hov && tf_id != id) tf_begin(id, dst, 0);
		else if (!hov && tf_id == id) tf_end();
	}
	rrect((float)x, (float)y, (float)w, (float)h, 6, C_BG, 1);
	rrect_line((float)x, (float)y, (float)w, (float)h, 6, 1.2f, tf_id == id ? C_ACC : C_LINE, 1);
	const char *show = tf_id == id ? tf_buf : dst;
	Clip c = clip_push(x + 8, y, w - 16, h);
	float tw = text_width(show, 13, 0);
	float ox = tw > w - 20 ? (w - 20 - tw) : 0.f;
	if (!show[0] && tf_id != id) text((float)x + 9, y + h * 0.5f, hint ? hint : "", 13, 0, C_FAINT, 1);
	else text(x + 9 + ox, y + h * 0.5f, show, 13, 0, C_TEXT, 1);
	if (tf_id == id && ((SDL_GetTicks() / 500) & 1) == 0) fill_rect((int)(x + 9 + ox + tw + 1), y + 6, 1, h - 12, C_ACC, 1);
	clip_set(c);
	if (tf_id == id) {
		if (strcmp(dst, tf_buf) != 0) {
			snprintf(dst, cap, "%s", tf_buf);
			changed = 1;
		}
		if (tf_enter) {
			tf_end();
			tf_enter = 0;
			changed |= 2;
		}
	}
	return changed;
}

/* ---- buttons ---- */
enum { BS_NORMAL, BS_ACCENT, BS_GHOST, BS_DANGER };
static int button(int id, int x, int y, int w, int h, const char *label, int style, int on) {
	int hov = mouse_in(x, y, w, h), res = 0;
	if (hov && m_press[0]) act_id = id;
	if (act_id == id && m_rel[0]) {
		if (hov) res = 1;
		act_id = 0;
	}
	uint32_t bg = C_PANEL2, fg = C_TEXT;
	float a = 1.f;
	if (style == BS_ACCENT) {
		bg = C_ACC;
		fg = 0x0b2a27;
	} else if (style == BS_GHOST) {
		bg = C_PANEL3;
		a = 0.f;
	} else if (style == BS_DANGER) {
		bg = C_WARN;
		fg = 0x2a0d09;
	}
	if (on) {
		bg = C_ACC;
		fg = 0x0b2a27;
		a = 1.f;
	}
	if (hov && !on) {
		if (style == BS_GHOST) a = 1.f;
		else bg = style == BS_NORMAL ? C_PANEL3 : bg;
	}
	if (act_id == id && hov) a = 1.f;
	if (a > 0.f) rrect((float)x, (float)y, (float)w, (float)h, 6, bg, a);
	if (style == BS_ACCENT && hov) rrect((float)x, (float)y, (float)w, (float)h, 6, 0xffffff, 0.15f);
	if (label && label[0]) text_c(x + w * 0.5f, y + h * 0.5f, label, 13, 1, fg, 1);
	return res;
}
static int chip_row(int id, int x, int *py, int w, const char *list, float *val, int extra_h) {
	int n = choice_count(list), cur = (int)(*val + 0.5f), changed = 0;
	int cx = x, cy = *py;
	for (int i = 0; i < n; i++) {
		char lb[40];
		choice_label(list, i, lb, sizeof lb);
		int tw = (int)text_width(lb, 12, 1) + 18;
		if (tw < 30) tw = 30;
		if (cx + tw > x + w) {
			cx = x;
			cy += 28;
		}
		int hov = mouse_in(cx, cy, tw, 24);
		int on = i == cur;
		if (hov && m_press[0]) {
			act_id = id + i;
		}
		if (act_id == id + i && m_rel[0]) {
			act_id = 0;
			if (hov && !on) {
				*val = (float)i;
				changed = 1;
				cur = i;
				on = 1;
			}
		}
		rrect((float)cx, (float)cy, (float)tw, 24, 12, on ? C_ACC : (hov ? C_PANEL3 : C_PANEL2), 1);
		text_c(cx + tw * 0.5f, cy + 12, lb, 12, 1, on ? 0x0b2a27 : C_TEXT, 1);
		cx += tw + 5;
	}
	*py = cy + 28 + extra_h;
	return changed;
}

/* slider: returns bit0 = value changed, bit1 = user interacted (pick this param) */
static float sl_start_v, sl_start_x;
static int slider(int id, int x, int y, int w, const char *label, float *v, float mn, float mxv, float def, int dec, int sel) {
	int h = 40, res = 0;
	int hov = mouse_in(x, y, w, h);
	float old = *v;
	int tx = x + 3, tw = w - 6, ty = y + 29;
	int on_track = hov && my >= y + 16;
	int on_val = hov && my < y + 18 && mx > x + w - 70;
	if (hov && m_press[0]) {
		res |= 2;
		if (m_dbl && on_val) {
			char b[32];
			snprintf(b, sizeof b, dec ? "%.1f" : "%.0f", *v);
			tf_begin(id, b, 1);
		} else if (m_dbl) {
			*v = def;
		} else if (on_track) {
			act_id = id;
			sl_start_v = *v;
			sl_start_x = (float)mx;
		}
	}
	if (hov && m_press[2]) {
		*v = def;
		res |= 2;
	}
	if (act_id == id) {
		if (m_down[0]) {
			float t;
			if (mod_shift)
				*v = sl_start_v + ((float)mx - sl_start_x) / tw * (mxv - mn) * 0.2f;
			else {
				t = CL01(((float)mx - tx) / (float)tw);
				*v = mn + t * (mxv - mn);
			}
			res |= 2;
		} else
			act_id = 0;
	}
	if (hov && wheel && !wheel_used && act_id != id) {
		float step = dec ? 0.1f : 1.f;
		if (mxv - mn > 150) step *= 1.f;
		*v += wheel * step * (mod_shift ? 5.f : 1.f);
		wheel_used = 1;
		res |= 2;
	}
	if (tf_id == id && tf_enter) {
		*v = (float)atof(tf_buf);
		tf_end();
		tf_enter = 0;
		res |= 2;
	}
	if (*v < mn) *v = mn;
	if (*v > mxv) *v = mxv;
	if (dec == 0) *v = roundf(*v);
	else *v = roundf(*v * 10.f) / 10.f;
	if (*v != old) res |= 1;
	/* draw */
	uint32_t lc = sel ? C_ACC : C_TEXT;
	if (sel) rrect((float)x - 4, (float)y + 1, 2.5f, (float)h - 4, 1.2f, C_ACC, 1);
	text((float)x + 2, y + 10, label, 13, sel ? 1 : 0, lc, sel ? 1.f : 0.88f);
	if (tf_id == id) {
		int fw = 64;
		rrect((float)(x + w - fw), (float)y + 1, (float)fw, 20, 5, C_BG, 1);
		rrect_line((float)(x + w - fw), (float)y + 1, (float)fw, 20, 5, 1.2f, C_ACC, 1);
		text_r((float)(x + w - 6), y + 11, tf_buf, 13, 0, C_TEXT, 1);
	} else {
		char b[32];
		float vv = *v;
		if (dec) snprintf(b, sizeof b, "%.1f", vv);
		else if (mn < 0 && vv > 0) snprintf(b, sizeof b, "+%d", (int)vv);
		else snprintf(b, sizeof b, "%d", (int)vv);
		text_r((float)(x + w - 2), y + 10, b, 13, 1, (*v != def) ? C_ACC : C_DIM, 1);
	}
	float t = (mxv > mn) ? (*v - mn) / (mxv - mn) : 0.f;
	float t0 = (mn < 0 && mxv > 0) ? (0.f - mn) / (mxv - mn) : 0.f;
	float kx = tx + t * tw;
	rrect((float)tx, (float)ty - 1.5f, (float)tw, 3.f, 1.5f, C_PANEL3, 1);
	float fx0 = MIN(kx, tx + t0 * tw), fx1 = MAX(kx, tx + t0 * tw);
	if (fx1 - fx0 > 0.5f) rrect(fx0, (float)ty - 1.5f, fx1 - fx0, 3.f, 1.5f, sel ? C_ACC : C_ACCD, 1);
	if (mn < 0 && mxv > 0) fill_rect((int)(tx + t0 * tw), ty - 4, 1, 8, C_FAINT, 1);
	float kr = (act_id == id || on_track) ? 7.5f : 6.f;
	disc(kx, (float)ty, kr + 1.5f, 0x000000, 0.25f);
	disc(kx, (float)ty, kr, (act_id == id) ? 0xffffff : C_ACC, 1);
	return res;
}

static uint32_t group_col(const char *g) {
	if (g[0] == 'A') return C_G_ADJ;
	if (g[0] == 'G') return C_G_GEO;
	if (g[0] == 'R') return C_G_RET;
	if (g[0] == 'E') return 0x4fd1c5;
	return C_G_FIL;
}
static const char *fmt_param(const Layer *L, int i, char *buf, int cap) {
	const PDef *d = &TD[L->tool].p[i];
	if (d->ch) {
		choice_label(d->ch, CLAMP((int)L->p[i], 0, choice_max(d)), buf, cap);
		return buf;
	}
	if (d->dec) snprintf(buf, cap, "%.1f", L->p[i]);
	else if (d->mn < 0 && L->p[i] > 0) snprintf(buf, cap, "+%d", (int)L->p[i]);
	else snprintf(buf, cap, "%d", (int)L->p[i]);
	return buf;
}
static void hud_show(const Layer *L, int i) {
	const PDef *d = &TD[L->tool].p[i];
	snprintf(hud_name, sizeof hud_name, "%s", d->name);
	fmt_param(L, i, hud_text, sizeof hud_text);
	hud_until = SDL_GetTicks() + 1100;
	dirty = 1;
}

/* ================================================================== view / canvas geometry */
static int cvx, cvy, cvw, cvh; /* canvas rectangle */
static float zoom = 1.f, vcx = 0, vcy = 0;
static int view_fit = 1, last_ow = 0, last_oh = 0;
static int show_looks = 1, looks_h = 100;
static int compare_hold = 0;

static float fit_zoom(void) {
	if (!out.w || !out.h) return 1.f;
	return MIN((cvw - 40) / (float)out.w, (cvh - 40) / (float)out.h);
}
static void view_do_fit(void) {
	zoom = fit_zoom();
	if (zoom < 0.01f) zoom = 0.01f;
	vcx = out.w * 0.5f;
	vcy = out.h * 0.5f;
}
static void img2scr(float ix, float iy, float *sx, float *sy) {
	*sx = cvx + cvw * 0.5f + (ix - vcx) * zoom;
	*sy = cvy + cvh * 0.5f + (iy - vcy) * zoom;
}
static void scr2img(float sx, float sy, float *ix, float *iy) {
	*ix = vcx + (sx - cvx - cvw * 0.5f) / zoom;
	*iy = vcy + (sy - cvy - cvh * 0.5f) / zoom;
}
static void view_zoom_at(float factor, float sx, float sy) {
	float ix, iy;
	scr2img(sx, sy, &ix, &iy);
	float nz = CLAMP(zoom * factor, fit_zoom() * 0.3f, 48.f);
	zoom = nz;
	vcx = ix - (sx - cvx - cvw * 0.5f) / zoom;
	vcy = iy - (sy - cvy - cvh * 0.5f) / zoom;
	view_fit = 0;
}
static void view_clamp(void) {
	float hw = cvw * 0.5f / zoom, hh = cvh * 0.5f / zoom;
	vcx = CLAMP(vcx, -hw * 0.5f, out.w + hw * 0.5f);
	vcy = CLAMP(vcy, -hh * 0.5f, out.h + hh * 0.5f);
}
static float full_scale(void) { /* proxy px -> full-res px ratio of the displayed image */
	int fw, fh;
	full_dims(&fw, &fh);
	return out.w ? (float)out.w / (float)fw : 1.f;
}

typedef struct {
	const Mips *m;
	int x0, y0, w, h;
	float cx, cy, zoom;
} DrawCtx;
static void canvas_job(void *v, int ya, int yb) {
	DrawCtx *c = (DrawCtx *)v;
	const Mips *m = c->m;
	int lev = 0;
	float s = 1.f / c->zoom;
	while (s >= 1.9f && lev < m->n - 1) {
		s *= 0.5f;
		lev++;
	}
	int W = m->w[lev], H = m->h[lev];
	float lsc = 1.f / (float)(1 << lev);
	const uint32_t *px = m->px[lev];
	int nearest_ = c->zoom >= 5.f;
	for (int y = ya; y < yb; y++) {
		uint32_t *d = &fb[(size_t)(c->y0 + y) * FW + c->x0];
		float iy = (c->cy + (y + 0.5f - c->h * 0.5f) / c->zoom) * lsc;
		if (iy < 0 || iy >= H) continue;
		for (int x = 0; x < c->w; x++) {
			float ix = (c->cx + (x + 0.5f - c->w * 0.5f) / c->zoom) * lsc;
			if (ix < 0 || ix >= W) continue;
			if (nearest_) {
				d[x] = px[(size_t)(int)iy * W + (int)ix];
				continue;
			}
			float fx = ix - 0.5f, fy = iy - 0.5f;
			int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
			int ax = (int)((fx - x0) * 256.f), ay = (int)((fy - y0) * 256.f);
			int x1 = MIN(x0 + 1, W - 1), y1 = MIN(y0 + 1, H - 1);
			x0 = MAX(x0, 0);
			y0 = MAX(y0, 0);
			uint32_t p00 = px[(size_t)y0 * W + x0], p10 = px[(size_t)y0 * W + x1], p01 = px[(size_t)y1 * W + x0], p11 = px[(size_t)y1 * W + x1];
			uint32_t t0 = blend_px(p00, p10, ax), t1 = blend_px(p01, p11, ax);
			d[x] = blend_px(t0, t1, ay);
		}
	}
}
static void draw_mips(const Mips *m, int x0, int y0, int w, int h, float cx, float cy, float z) {
	if (!m->n) return;
	DrawCtx c = {m, x0, y0, w, h, cx, cy, z};
	Clip o = clip_push(x0, y0, w, h);
	/* only rows/cols inside the clip are valid; canvas rect is always inside fb */
	if (x0 >= 0 && y0 >= 0 && x0 + w <= FW && y0 + h <= FH) parallel(canvas_job, &c, h);
	clip_set(o);
}

/* ================================================================== actions */
static char export_path[1024] = "";
static float export_q = 92, export_scale = 100;
static char open_path[1024] = "";
static int export_busy = 0;
#define ID_EXPORT_PATH 9001
#define ID_OPEN_PATH 9002
#define ID_LOOKNAME 9004
static char look_name_buf[32];
#define ID_TEXT_FIELD 9003

static void path_split_name(const char *p, char *dir, int dcap, char *base, int bcap) {
	const char *sl = strrchr(p, '/');
	const char *bs = strrchr(p, '\\');
	if (bs && (!sl || bs > sl)) sl = bs;
	const char *name = sl ? sl + 1 : p;
	int dl = sl ? (int)(sl - p + 1) : 0;
	if (dl >= dcap) dl = dcap - 1;
	memcpy(dir, p, dl);
	dir[dl] = 0;
	snprintf(base, bcap, "%s", name);
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
}
static void default_export_path(void) {
	char dir[1024], base[256];
	path_split_name(doc.path, dir, sizeof dir, base, sizeof base);
	snprintf(export_path, sizeof export_path, "%s%s_edit.jpg", dir, base);
}
static void select_none(void) { edit_end(1); }
static int load_file(const char *p) {
	if (!doc_load(p)) {
		toast("%s", g_err);
		return 0;
	}
	view_fit = 1;
	active = -1;
	sel_param = 0;
	sel_pt = 0;
	wb_pick = 0;
	default_export_path();
	char dir[1024], base[256];
	path_split_name(p, dir, sizeof dir, base, sizeof base);
	toast("Opened %s  (%d x %d)", base, doc.w, doc.h);
	return 1;
}
static void do_open(void) {
	char p[1024];
	snprintf(p, sizeof p, "%s", doc.loaded ? doc.path : "");
	int r = native_dialog(0, p, p, sizeof p);
	if (r == 1)
		load_file(p);
	else if (r < 0) {
		modal = MODAL_OPENPATH;
		snprintf(open_path, sizeof open_path, "%s", doc.loaded ? doc.path : "");
		tf_begin(ID_OPEN_PATH, open_path, 0);
	}
	dirty = 1;
}
static void do_export_dialog(void) {
	if (!doc.loaded) return;
	edit_end(1);
	if (!export_path[0]) default_export_path();
	modal = MODAL_EXPORT;
	tf_end();
	dirty = 1;
}
static void do_undo(void) {
	if (active >= 0 && edit_dirty) {
		layer_free(&stack[active]);
		layer_copy(&stack[active], &edit_backup);
		layer_bump(&stack[active]);
		edit_dirty = 0;
		need_render = 1;
		return;
	}
	edit_end(1);
	if (hist_can_undo()) {
		hpos--;
		snap_restore(&hist[hpos]);
	}
}
static void do_redo(void) {
	edit_end(1);
	if (hist_can_redo()) {
		hpos++;
		snap_restore(&hist[hpos]);
	}
}
static void layer_reset(Layer *L) {
	switch (L->tool) {
	case T_CURVES:
		for (int c = 0; c < 4; c++) curves_reset(L, c);
		break;
	case T_SELECTIVE: L->nsel = 0; sel_pt = 0; break;
	case T_BRUSH:
		L->nstr = 0;
		L->npts = 0;
		if (L->rt) {
			brt_free(L->rt);
			L->rt = NULL;
		}
		break;
	case T_HEAL: L->nspots = 0; break;
	case T_TEXT: L->text[0] = 0; break;
	case T_CROP:
		L->cr[0] = L->cr[1] = 0;
		L->cr[2] = L->cr[3] = 1;
		L->rot = 0;
		L->p[CR_ASPECT] = 0;
		break;
	case T_ROTATE:
		L->rot = 0;
		L->flipx = L->flipy = 0;
		layer_params_default(L);
		break;
	case T_VIGNETTE:
	case T_LENS:
		L->cx = L->cy = 0.5f;
		layer_params_default(L);
		break;
	case T_GRAD:
		L->cx = 0.5f;
		L->cy = 0.38f;
		layer_params_default(L);
		break;
	case T_MIXER: memset(L->mix, 0, sizeof L->mix); break;
	default: layer_params_default(L);
	}
	touch(L);
}
static void do_delete_layer(int i) {
	if (i < 0 || i >= nlayers) return;
	if (active == i) {
		active = -1;
		edit_dirty = 0;
	}
	layer_remove(i);
	hist_push();
}
static void do_move_layer(int i, int dir) {
	int j = i + dir;
	if (i < 0 || j < 0 || i >= nlayers || j >= nlayers) return;
	Layer t = stack[i];
	stack[i] = stack[j];
	stack[j] = t;
	if (active == i) active = j;
	else if (active == j) active = i;
	layer_bump(&stack[i]);
	layer_bump(&stack[j]);
	need_render = 1;
	hist_push();
}

/* ---- auto adjustments computed from the layer's input image (pf) ---- */
static void auto_tune(Layer *L) {
	if (!pf.p) return;
	int bins[256] = {0};
	size_t n = (size_t)pf.w * pf.h, cnt = 0;
	double sum = 0;
	for (size_t i = 0; i < n; i += 7) {
		float Y = CL01(lum3(pf.p[i * 3], pf.p[i * 3 + 1], pf.p[i * 3 + 2]));
		bins[(int)(Y * 255.f)]++;
		sum += Y;
		cnt++;
	}
	float mean = (float)(sum / MAX(cnt, 1)), lo = 0, hi = 1;
	size_t acc = 0;
	for (int i = 0; i < 256; i++) {
		acc += bins[i];
		if (acc >= cnt * 0.01) {
			lo = i / 255.f;
			break;
		}
	}
	acc = 0;
	for (int i = 255; i >= 0; i--) {
		acc += bins[i];
		if (acc >= cnt * 0.01) {
			hi = i / 255.f;
			break;
		}
	}
	L->p[TU_BRI] = roundf(CLAMP((0.47f - mean) * 190.f, -50.f, 50.f));
	L->p[TU_CON] = roundf(CLAMP((0.9f - (hi - lo)) * 90.f, -10.f, 35.f));
	L->p[TU_SHA] = roundf(CLAMP((0.10f - lo) * 220.f, 0.f, 45.f));
	L->p[TU_HIG] = roundf(CLAMP((hi - 0.93f) * -400.f, -45.f, 0.f));
	L->p[TU_AMB] = 18;
	L->p[TU_SAT] = 10;
	touch(L);
}
static void auto_wb(Layer *L) {
	if (!pf.p) return;
	double r = 0, g = 0, b = 0;
	size_t n = (size_t)pf.w * pf.h, cnt = 0;
	for (size_t i = 0; i < n; i += 5) {
		float R = pf.p[i * 3], G = pf.p[i * 3 + 1], B = pf.p[i * 3 + 2];
		float Y = lum3(R, G, B);
		if (Y < 0.08f || Y > 0.95f) continue;
		r += R;
		g += G;
		b += B;
		cnt++;
	}
	if (!cnt) return;
	float t, ti;
	wb_from_sample((float)(r / cnt), (float)(g / cnt), (float)(b / cnt), &t, &ti);
	L->p[WB_TEMP] = roundf(t);
	L->p[WB_TINT] = roundf(ti);
	touch(L);
}

/* ================================================================== looks thumbnails */
#define LT_W 92
#define LT_H 58
static uint32_t *lthumb[64];
static int lt_have[64];
static Img lt_base;
static int looks_scroll = 0;
static void looks_tick(void) {
	if (!out.p || !out_ok || look_total() > 64) return;
	if (lt_serial != render_serial) {
		if (m_down[0]) return;
		/* cover-crop `out` to LT_W:LT_H */
		float ta = (float)LT_W / LT_H, sa = (float)out.w / out.h;
		int cw = out.w, ch = out.h, ox = 0, oy = 0;
		if (sa > ta) {
			cw = (int)(out.h * ta);
			ox = (out.w - cw) / 2;
		} else {
			ch = (int)(out.w / ta);
			oy = (out.h - ch) / 2;
		}
		Img crop = img_new(MAX(cw, 2), MAX(ch, 2));
		for (int y = 0; y < crop.h; y++) memcpy(crop.p + (size_t)y * crop.w * 3, out.p + ((size_t)(y + oy) * out.w + ox) * 3, (size_t)crop.w * 3 * sizeof(float));
		/* render looks on 2x thumbs for better filter behaviour, shown at 1x */
		img_free(&lt_base);
		lt_base = resize_f(&crop, LT_W * 2, LT_H * 2);
		img_free(&crop);
		for (int i = 0; i < 64; i++) lt_have[i] = 0;
		lt_serial = render_serial;
		lt_next = 0;
	}
	for (int k = 0; k < 2 && lt_next < look_total(); k++, lt_next++) {
		int i = lt_next;
		Img t = img_clone(&lt_base);
		Layer ls[CL_LAYERS];
		int n = look_layers(i, ls);
		for (int j = 0; j < n; j++) {
			apply_layer(&ls[j], &t);
			if (i >= NLOOKS) layer_free(&ls[j]);
		}
		if (!lthumb[i]) lthumb[i] = (uint32_t *)xmalloc(LT_W * LT_H * 4);
		for (int y = 0; y < LT_H; y++)
			for (int x = 0; x < LT_W; x++) {
				float rr = 0, gg = 0, bb = 0;
				for (int q = 0; q < 4; q++) {
					const float *p = t.p + ((size_t)(y * 2 + (q >> 1)) * t.w + x * 2 + (q & 1)) * 3;
					rr += p[0];
					gg += p[1];
					bb += p[2];
				}
				lthumb[i][y * LT_W + x] = ((uint32_t)CLAMP((int)(rr * 63.75f), 0, 255) << 16) | ((uint32_t)CLAMP((int)(gg * 63.75f), 0, 255) << 8) | (uint32_t)CLAMP((int)(bb * 63.75f), 0, 255);
			}
		lt_have[i] = 1;
		img_free(&t);
		dirty = 1;
	}
}
static void draw_looks(int x, int y, int w, int h) {
	fill_rect(x, y, w, h, C_PANEL, 1);
	fill_rect(x, y, w, 1, C_LINE, 1);
	text((float)x + 12, y + 14, "LOOKS", 11, 1, C_DIM, 1);
	text((float)x + 12, y + 28, "click to apply", 10, 0, C_FAINT, 1);
	text((float)x + 12, y + 42, "right-click: delete", 9, 0, C_FAINT, 1);
	int tx0 = x + 96, tw = LT_W + 8, total = look_total() * tw;
	int view = w - 96 - 8;
	int maxs = MAX(0, total - view);
	if (mouse_in(x, y, w, h) && wheel && !wheel_used) {
		looks_scroll -= wheel * 60;
		wheel_used = 1;
	}
	looks_scroll = CLAMP(looks_scroll, 0, maxs);
	Clip c = clip_push(tx0, y, view, h);
	for (int i = 0; i < look_total(); i++) {
		int bx = tx0 - looks_scroll + i * tw, by = y + 8;
		if (bx + LT_W < tx0 || bx > tx0 + view) continue;
		int hov = mouse_in(bx, by, LT_W, h - 12);
		if (hov && m_press[0]) act_id = 7000 + i;
		if (act_id == 7000 + i && m_rel[0]) {
			act_id = 0;
			if (hov && doc.loaded) look_apply(i);
		}
		if (hov && m_press[2] && i >= NLOOKS) {
			look_delete_custom(i);
			toast("Look deleted");
			break;
		}
		if (lt_have[i] && lthumb[i]) {
			for (int j = 0; j < LT_H; j++) memcpy(&fb[(size_t)(by + j) * FW + bx], &lthumb[i][j * LT_W], LT_W * 4);
		} else
			fill_rect(bx, by, LT_W, LT_H, C_PANEL2, 1);
		if (hov) rrect_line((float)bx - 1, (float)by - 1, LT_W + 2, LT_H + 2, 3, 2, C_ACC, 1);
		if (i >= NLOOKS) disc((float)bx + 9, (float)by + 9, 3.5f, C_ACC, 1);
		text_c(bx + LT_W * 0.5f, by + LT_H + 11, look_name(i), 12, hov ? 1 : 0, hov ? C_TEXT : C_DIM, 1);
	}
	clip_set(c);
	if (maxs > 0) {
		int sx = tx0, sw = view;
		fill_rect(sx, y + h - 4, sw, 2, C_PANEL2, 1);
		fill_rect(sx + (int)((float)looks_scroll / total * sw), y + h - 4, (int)((float)view / total * sw), 2, C_FAINT, 1);
	}
}

/* ================================================================== histogram */
static void draw_histogram(int x, int y, int w, int h) {
	rrect((float)x, (float)y, (float)w, (float)h, 8, C_BG, 1);
	Clip c = clip_push(x, y, w, h);
	float mxv = 1.f;
	for (int k = 0; k < 4; k++)
		for (int i = 2; i < 254; i++) mxv = MAX(mxv, histo[k][i]);
	for (int i = 0; i < 256; i++) {
		float bx = x + 6 + (w - 12) * i / 255.f;
		int bw = MAX(1, (int)((w - 12) / 256.f + 0.99f));
		for (int k = 0; k < 4; k++) {
			float v = sqrtf(MIN(histo[k][i], mxv) / mxv);
			int bh = (int)(v * (h - 14));
			static const uint32_t col[4] = {0xff5a4d, 0x4dd06a, 0x4d8dff, 0xdddddd};
			if (bh <= 0) continue;
			fill_rect((int)bx, y + h - 6 - bh, bw, bh, col[k], k == 3 ? 0.30f : 0.32f);
		}
	}
	clip_set(c);
	rrect_line((float)x, (float)y, (float)w, (float)h, 8, 1, C_LINE, 1);
}

/* ================================================================== tool panel */
static int panel_scroll = 0, panel_content = 0;
static void hint_text(int x, int *y, int w, const char *s) {
	char line[200];
	int n = 0;
	const char *p = s;
	while (*p) {
		const char *wend = p;
		while (*wend && *wend != ' ') wend++;
		int wl = (int)(wend - p);
		char tmp[200];
		snprintf(tmp, sizeof tmp, "%.*s%s%.*s", n, line, n ? " " : "", wl, p);
		if (n && text_width(tmp, 12, 0) > w) {
			line[n] = 0;
			text((float)x, *y + 8, line, 12, 0, C_DIM, 1);
			*y += 16;
			n = 0;
		}
		if (n) line[n++] = ' ';
		if (n + wl < (int)sizeof line - 2) {
			memcpy(line + n, p, wl);
			n += wl;
		}
		p = wend;
		while (*p == ' ') p++;
	}
	if (n) {
		line[n] = 0;
		text((float)x, *y + 8, line, 12, 0, C_DIM, 1);
		*y += 16;
	}
	*y += 4;
}

static void generic_params(Layer *L, int x, int *py, int w) {
	const ToolDef *d = &TD[L->tool];
	for (int i = 0; i < d->np; i++) {
		const PDef *pd = &d->p[i];
		if (pd->ch) {
			int sel = (sel_param == i);
			text((float)x + 2, *py + 8, pd->name, 13, sel ? 1 : 0, sel ? C_ACC : C_TEXT, sel ? 1.f : 0.88f);
			*py += 20;
			if (chip_row(5000 + i * 32, x, py, w, pd->ch, &L->p[i], 2)) {
				touch(L);
				sel_param = i;
				hud_show(L, i);
			}
			continue;
		}
		int r = slider(4000 + i, x, *py, w, pd->name, &L->p[i], pd->mn, pd->mx, pd->def, pd->dec, sel_param == i);
		if (r & 2) sel_param = i;
		if (r & 1) touch(L);
		*py += 42;
	}
}

/* crop helpers */
static float crop_ratio(const Layer *L) {
	static const float R[] = {0, 0, 1.f, 0.8f, 0.75f, 2.f / 3.f, 9.f / 16.f, 1.25f, 4.f / 3.f, 1.5f, 16.f / 9.f};
	int a = CLAMP((int)L->p[CR_ASPECT], 0, 10);
	float r = a == 0 ? 0.f : a == 1 ? (float)out.w / MAX(out.h, 1) : R[a];
	if (r > 0 && (L->rot & 1)) r = 1.f / r;
	return r;
}
static void crop_conform(Layer *L) {
	float R = crop_ratio(L);
	if (R <= 0) return;
	float W = (float)out.w, H = (float)out.h;
	float x0 = L->cr[0] * W, y0 = L->cr[1] * H, x1 = L->cr[2] * W, y1 = L->cr[3] * H;
	float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, w = x1 - x0, h = y1 - y0;
	float area = w * h;
	float nw = sqrtf(area * R), nh = nw / R;
	if (nw > W) {
		nw = W;
		nh = nw / R;
	}
	if (nh > H) {
		nh = H;
		nw = nh * R;
	}
	cx = CLAMP(cx, nw * 0.5f, W - nw * 0.5f);
	cy = CLAMP(cy, nh * 0.5f, H - nh * 0.5f);
	L->cr[0] = (cx - nw * 0.5f) / W;
	L->cr[2] = (cx + nw * 0.5f) / W;
	L->cr[1] = (cy - nh * 0.5f) / H;
	L->cr[3] = (cy + nh * 0.5f) / H;
}

/* ---- per-layer mask ---- */
static int mask_edit = 0, mask_erase = 0;
static float mask_size = 30.f, mask_hard = 40.f;
static void mask_clear(Layer *L) {
	L->nmpts = L->nmstr = 0;
	if (L->mrt) {
		brt_free(L->mrt);
		L->mrt = NULL;
	}
	touch(L);
}
static void mask_section(Layer *L, int x, int *py, int w) {
	if (!maskable(L->tool)) return;
	*py += 6;
	fill_rect(x, *py, w, 1, C_LINE, 1);
	*py += 10;
	text((float)x, *py + 8, "MASK", 11, 1, C_DIM, 1);
	if (button(4950, x + w - 64, *py - 2, 64, 22, L->mask_on ? "On" : "Off", L->mask_on ? BS_ACCENT : BS_NORMAL, 0)) {
		L->mask_on ^= 1;
		if (!L->mask_on) mask_edit = 0;
		touch(L);
	}
	*py += 28;
	if (!L->mask_on) {
		hint_text(x, py, w, "Limit this effect to part of the photo by painting where it shows.");
		return;
	}
	int bw = (w - 10) / 3;
	if (button(4951, x, *py, bw, 24, mask_edit ? "Painting" : "Paint", mask_edit ? BS_ACCENT : BS_NORMAL, 0)) {
		mask_edit ^= 1;
		mask_erase = 0;
	}
	if (button(4952, x + bw + 5, *py, bw, 24, "Erase", BS_NORMAL, mask_edit && mask_erase)) {
		mask_edit = 1;
		mask_erase = !mask_erase;
	}
	if (button(4953, x + 2 * (bw + 5), *py, bw, 24, "Invert", BS_NORMAL, L->mask_inv)) {
		L->mask_inv ^= 1;
		touch(L);
	}
	*py += 32;
	if (button(4954, x, *py, (w - 5) / 2, 24, "Reveal all", BS_NORMAL, 0)) {
		Stroke *s = layer_add_mstroke(L);
		s->mode = 0;
		s->amt = 1.f;
		s->rad = 2.f;
		s->hard = 0.98f;
		s->flow = 1.f;
		layer_add_mpt(L, 0.5f, 0.5f);
		s->n = 1;
		touch(L);
	}
	if (button(4955, x + (w - 5) / 2 + 5, *py, (w - 5) / 2, 24, "Clear", BS_NORMAL, 0)) mask_clear(L);
	*py += 32;
	int r = slider(4956, x, *py, w, "Brush size", &mask_size, 1, 100, 30, 0, 0);
	(void)r;
	*py += 42;
	slider(4957, x, *py, w, "Brush hardness", &mask_hard, 0, 100, 40, 0, 0);
	*py += 42;
	hint_text(x, py, w, "Red shows where the effect is visible. Paint on the photo; Erase removes it again.");
}
static void panel_tool(int x, int y, int w, int h) {
	Layer *L = &stack[active];
	const ToolDef *d = &TD[L->tool];
	Clip c0 = clip_push(x - 6, y, w + 12, h);
	int content_start = y - panel_scroll;
	int py = content_start + 2;
	/* title row */
	disc(x + 6, py + 12, 5, group_col(d->group), 1);
	text((float)x + 18, py + 12, d->name, 16, 1, C_TEXT, 1);
	int bx = x + w;
	if (button(4900, bx - 56, py + 2, 56, 22, "Reset", BS_NORMAL, 0)) {
		layer_reset(L);
		hud_until = 0;
	}
	bx -= 62;
	if (L->tool == T_TUNE && button(4901, bx - 50, py + 2, 50, 22, "Auto", BS_NORMAL, 0)) auto_tune(L);
	if (L->tool == T_WB && button(4901, bx - 50, py + 2, 50, 22, "Auto", BS_NORMAL, 0)) auto_wb(L);
	py += 34;

	switch (L->tool) {
	case T_CURVES: {
		static const char *chn[4] = {"RGB", "R", "G", "B"};
		static const uint32_t chc[4] = {0xdddddd, 0xff6a5d, 0x58d673, 0x5d9bff};
		int tx = x;
		for (int i = 0; i < 4; i++) {
			int tw = i == 0 ? 54 : 36;
			if (button(4910 + i, tx, py, tw, 24, chn[i], BS_NORMAL, curve_ch == i)) curve_ch = i;
			disc(tx + tw - 9, py + 5, 2.5f, chc[i], 1);
			tx += tw + 5;
		}
		py += 32;
		int S = w;
		int gx = x, gy = py;
		rrect((float)gx, (float)gy, (float)S, (float)S, 8, C_BG, 1);
		/* histogram backdrop */
		{
			float mxv = 1.f;
			int hk = curve_ch == 0 ? 3 : curve_ch - 1;
			for (int i = 2; i < 254; i++) mxv = MAX(mxv, histo[hk][i]);
			for (int i = 0; i < 256; i++) {
				float v = sqrtf(MIN(histo[hk][i], mxv) / mxv);
				int bh = (int)(v * (S - 12));
				fill_rect(gx + 6 + (int)((S - 12) * i / 255.f), gy + S - 6 - bh, MAX(1, (S - 12) / 256 + 1), bh, 0xffffff, 0.10f);
			}
		}
		for (int i = 1; i < 4; i++) {
			fill_rect(gx + 6 + (S - 12) * i / 4, gy + 6, 1, S - 12, C_LINE, 1);
			fill_rect(gx + 6, gy + 6 + (S - 12) * i / 4, S - 12, 1, C_LINE, 1);
		}
		line_aa((float)gx + 6, (float)gy + S - 6, (float)gx + S - 6, (float)gy + 6, 1, C_FAINT, 0.5f);
		float lut[256];
		pchip_lut(L->cp[curve_ch], L->ncp[curve_ch], lut, 256);
		uint32_t cc = chc[curve_ch];
		float ppx = 0, ppy = 0;
		for (int i = 0; i < 256; i += 2) {
			float px_ = gx + 6 + (S - 12) * i / 255.f, py_ = gy + S - 6 - (S - 12) * lut[i];
			if (i) line_aa(ppx, ppy, px_, py_, 2.f, cc, 1);
			ppx = px_;
			ppy = py_;
		}
		int n = L->ncp[curve_ch];
		static int drag_pt = -1;
		float mxn = (mx - (gx + 6)) / (float)(S - 12), myn = 1.f - (my - (gy + 6)) / (float)(S - 12);
		int hov_pt = -1;
		for (int i = 0; i < n; i++) {
			float qx = gx + 6 + (S - 12) * L->cp[curve_ch][i][0], qy = gy + S - 6 - (S - 12) * L->cp[curve_ch][i][1];
			if (mouse_in(gx, gy, S, S) && (mx - qx) * (mx - qx) + (my - qy) * (my - qy) < 100) hov_pt = i;
		}
		if (mouse_in(gx, gy, S, S) && m_press[0]) {
			if (hov_pt >= 0 && m_dbl && hov_pt > 0 && hov_pt < n - 1) {
				for (int k = hov_pt; k < n - 1; k++) {
					L->cp[curve_ch][k][0] = L->cp[curve_ch][k + 1][0];
					L->cp[curve_ch][k][1] = L->cp[curve_ch][k + 1][1];
				}
				L->ncp[curve_ch]--;
				touch(L);
			} else if (hov_pt >= 0) {
				drag_pt = hov_pt;
				act_id = 4950;
			} else if (n < MAXCP) {
				float nx = CL01(mxn);
				int at = 0;
				while (at < n && L->cp[curve_ch][at][0] < nx) at++;
				if (at == 0) at = 1;
				if (at >= n) at = n - 1;
				for (int k = n; k > at; k--) {
					L->cp[curve_ch][k][0] = L->cp[curve_ch][k - 1][0];
					L->cp[curve_ch][k][1] = L->cp[curve_ch][k - 1][1];
				}
				L->cp[curve_ch][at][0] = nx;
				L->cp[curve_ch][at][1] = CL01(myn);
				L->ncp[curve_ch]++;
				drag_pt = at;
				act_id = 4950;
				touch(L);
			}
		}
		if (mouse_in(gx, gy, S, S) && m_press[2] && hov_pt > 0 && hov_pt < n - 1) {
			for (int k = hov_pt; k < n - 1; k++) {
				L->cp[curve_ch][k][0] = L->cp[curve_ch][k + 1][0];
				L->cp[curve_ch][k][1] = L->cp[curve_ch][k + 1][1];
			}
			L->ncp[curve_ch]--;
			touch(L);
		}
		if (act_id == 4950) {
			if (m_down[0] && drag_pt >= 0 && drag_pt < L->ncp[curve_ch]) {
				float lo = drag_pt == 0 ? 0.f : L->cp[curve_ch][drag_pt - 1][0] + 0.01f;
				float hi = drag_pt == L->ncp[curve_ch] - 1 ? 1.f : L->cp[curve_ch][drag_pt + 1][0] - 0.01f;
				float nx = (drag_pt == 0 || drag_pt == L->ncp[curve_ch] - 1) ? L->cp[curve_ch][drag_pt][0] : CLAMP(mxn, lo, hi);
				float ny = CL01(myn);
				if (nx != L->cp[curve_ch][drag_pt][0] || ny != L->cp[curve_ch][drag_pt][1]) {
					L->cp[curve_ch][drag_pt][0] = nx;
					L->cp[curve_ch][drag_pt][1] = ny;
					touch(L);
				}
			} else
				act_id = 0;
		}
		for (int i = 0; i < L->ncp[curve_ch]; i++) {
			float qx = gx + 6 + (S - 12) * L->cp[curve_ch][i][0], qy = gy + S - 6 - (S - 12) * L->cp[curve_ch][i][1];
			disc(qx, qy, (i == hov_pt || (act_id == 4950 && i == drag_pt)) ? 7.f : 5.5f, 0x000000, 0.35f);
			disc(qx, qy, (i == hov_pt || (act_id == 4950 && i == drag_pt)) ? 6.f : 4.5f, i == hov_pt ? 0xffffff : cc, 1);
		}
		rrect_line((float)gx, (float)gy, (float)S, (float)S, 8, 1, C_LINE, 1);
		py += S + 8;
		static const char *pn[4] = {"Contrast", "Lift", "Fade", "Flat"};
		int cxp = x;
		for (int i = 0; i < 4; i++) {
			int tw = (int)text_width(pn[i], 12, 1) + 20;
			if (button(4920 + i, cxp, py, tw, 24, pn[i], BS_NORMAL, 0)) {
				float (*P)[2] = L->cp[curve_ch];
				int np_ = 2;
				if (i == 0) {
					float t[4][2] = {{0, 0}, {0.25f, 0.18f}, {0.75f, 0.83f}, {1, 1}};
					memcpy(P, t, sizeof t);
					np_ = 4;
				} else if (i == 1) {
					float t[3][2] = {{0, 0}, {0.4f, 0.52f}, {1, 1}};
					memcpy(P, t, sizeof t);
					np_ = 3;
				} else if (i == 2) {
					float t[2][2] = {{0, 0.09f}, {1, 0.93f}};
					memcpy(P, t, sizeof t);
				} else {
					float t[3][2] = {{0, 0.1f}, {0.5f, 0.5f}, {1, 0.9f}};
					memcpy(P, t, sizeof t);
					np_ = 3;
				}
				L->ncp[curve_ch] = np_;
				touch(L);
			}
			cxp += tw + 5;
		}
		py += 34;
		hint_text(x, &py, w, "Click the curve to add a point, drag to move, double-click or right-click a point to remove it.");
	} break;

	case T_SELECTIVE: {
		hint_text(x, &py, w, "Click the photo to place a control point (up to 8). Drag a point to move it. Wheel changes its reach. Right-click removes it.");
		if (L->nsel == 0) {
			hint_text(x, &py, w, "No points yet.");
			break;
		}
		sel_pt = CLAMP(sel_pt, 0, L->nsel - 1);
		SelPt *s = &L->sel[sel_pt];
		static const char *nm[4] = {"Brightness", "Contrast", "Saturation", "Structure"};
		for (int i = 0; i < 4; i++) {
			int r = slider(4040 + i, x, py, w, nm[i], &s->v[i], -100, 100, 0, 0, sel_param == i);
			if (r & 2) sel_param = i;
			if (r & 1) touch(L);
			py += 42;
		}
		float rad = s->rad * 100.f;
		int r = slider(4050, x, py, w, "Reach", &rad, 2, 60, 15, 0, sel_param == 4);
		if (r & 2) sel_param = 4;
		if (r & 1) {
			s->rad = rad / 100.f;
			touch(L);
		}
		py += 46;
		char b[40];
		snprintf(b, sizeof b, "Remove point %d", sel_pt + 1);
		if (button(4960, x, py, w, 26, b, BS_NORMAL, 0)) {
			for (int k = sel_pt; k < L->nsel - 1; k++) L->sel[k] = L->sel[k + 1];
			L->nsel--;
			sel_pt = MAX(0, sel_pt - 1);
			touch(L);
		}
		py += 34;
	} break;

	case T_CROP: {
		float before = L->p[CR_ASPECT];
		text((float)x + 2, py + 8, "Aspect ratio", 13, 0, C_TEXT, 1);
		py += 20;
		if (chip_row(5000, x, &py, w, TD[T_CROP].p[0].ch, &L->p[CR_ASPECT], 4)) {
			if (before != L->p[CR_ASPECT]) crop_conform(L);
			touch(L);
		}
		if (button(4930, x, py, 120, 26, "Swap orientation", BS_NORMAL, 0)) {
			L->rot ^= 1;
			crop_conform(L);
			touch(L);
		}
		if (button(4931, x + 126, py, 70, 26, "Full", BS_NORMAL, 0)) {
			L->cr[0] = L->cr[1] = 0;
			L->cr[2] = L->cr[3] = 1;
			crop_conform(L);
			touch(L);
		}
		py += 36;
		int cw = (int)roundf((L->cr[2] - L->cr[0]) * out.w), ch = (int)roundf((L->cr[3] - L->cr[1]) * out.h);
		float fs = full_scale();
		char b[80];
		snprintf(b, sizeof b, "Result: %d x %d px", (int)(cw / fs + 0.5f), (int)(ch / fs + 0.5f));
		text((float)x + 2, py + 8, b, 13, 1, C_ACC, 1);
		py += 24;
		hint_text(x, &py, w, "Drag the corners or edges, drag inside to move. Enter applies the crop.");
	} break;

	case T_ROTATE: {
		int bw = (w - 10) / 2;
		if (button(4940, x, py, bw, 28, "Rotate left", BS_NORMAL, 0)) {
			L->rot = (L->rot + 3) & 3;
			touch(L);
			view_fit = 1;
		}
		if (button(4941, x + bw + 10, py, bw, 28, "Rotate right", BS_NORMAL, 0)) {
			L->rot = (L->rot + 1) & 3;
			touch(L);
			view_fit = 1;
		}
		py += 34;
		if (button(4942, x, py, bw, 28, "Flip horizontal", BS_NORMAL, L->flipx)) {
			L->flipx ^= 1;
			touch(L);
		}
		if (button(4943, x + bw + 10, py, bw, 28, "Flip vertical", BS_NORMAL, L->flipy)) {
			L->flipy ^= 1;
			touch(L);
		}
		py += 40;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag left or right on the photo to straighten.");
	} break;

	case T_WB: {
		generic_params(L, x, &py, w);
		if (button(4944, x, py, w, 28, wb_pick ? "Click a neutral grey on the photo..." : "Pick neutral point", BS_NORMAL, wb_pick)) wb_pick = !wb_pick;
		py += 36;
	} break;

	case T_TEXT: {
		text((float)x + 2, py + 8, "Text", 13, 0, C_TEXT, 1);
		py += 18;
		int ch = text_field(ID_TEXT_FIELD, x, py, w, 30, L->text, sizeof L->text, "Type your text here");
		if (ch & 1) touch(L);
		py += 40;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag the text on the photo to position it.");
	} break;

	case T_BRUSH: {
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Paint on the photo. Wheel changes brush size, Ctrl+wheel zooms. Strength sets how strong new strokes are; use Eraser to remove strokes.");
	} break;
	case T_HEAL:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Click (or drag) over blemishes and dust spots. Wheel changes the size.");
		break;
	case T_VIGNETTE:
	case T_LENS:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag on the photo to move the centre.");
		break;
	case T_GRAD:
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Drag on the photo to place the line (the effect applies above it). Wheel rotates it.");
		break;
	case T_MIXER: {
		static int mix_ch = 0;
		static const uint32_t band[8] = {0xff4a4a, 0xff9a3c, 0xf2d83a, 0x4fd16a, 0x35d0c8, 0x4a86ff, 0x9a64f0, 0xf064c8};
		int cxp = x, cyp = py;
		for (int i = 0; i < 8; i++) {
			char lb[16];
			choice_label(MIX_NAMES, i, lb, sizeof lb);
			int tw = (int)text_width(lb, 12, 1) + 24;
			if (cxp + tw > x + w) {
				cxp = x;
				cyp += 28;
			}
			int hov = mouse_in(cxp, cyp, tw, 24), on = i == mix_ch;
			if (hov && m_press[0]) mix_ch = i;
			rrect((float)cxp, (float)cyp, (float)tw, 24, 12, on ? C_PANEL3 : (hov ? C_PANEL2 : C_PANEL2), 1);
			if (on) rrect_line((float)cxp, (float)cyp, (float)tw, 24, 12, 1.6f, band[i], 1);
			disc(cxp + 11.f, cyp + 12.f, 4.5f, band[i], 1);
			text(cxp + 20.f, cyp + 12.f, lb, 12, 1, C_TEXT, 1);
			cxp += tw + 5;
		}
		py = cyp + 36;
		static const char *nm[3] = {"Hue", "Saturation", "Luminance"};
		for (int k = 0; k < 3; k++) {
			int r = slider(4070 + k, x, py, w, nm[k], &L->mix[mix_ch][k], -100, 100, 0, 0, sel_param == k);
			if (r & 2) sel_param = k;
			if (r & 1) touch(L);
			py += 42;
		}
		hint_text(x, &py, w, "Pick a colour band, then shift its hue, saturation or brightness. Each band blends smoothly into its neighbours.");
	} break;
	case T_DOUBLE: {
		char bn[64], dir[8];
		path_split_name(L->path, dir, sizeof dir, bn, sizeof bn);
		if (button(4960, x, py, w, 30, L->path[0] ? bn : "Choose second photo...", L->path[0] ? BS_NORMAL : BS_ACCENT, 0)) {
			char p[1024];
			snprintf(p, sizeof p, "%s", L->path[0] ? L->path : doc.path);
			int r = native_dialog(0, p, p, sizeof p);
			if (r == 1) {
				snprintf(L->path, sizeof L->path, "%s", p);
				touch(L);
			} else if (r < 0)
				toast("No file dialog here - drop the second photo onto the window");
		}
		py += 38;
		generic_params(L, x, &py, w);
		hint_text(x, &py, w, "Tip: drop an image file onto the window to use it as the second exposure.");
	} break;
	default:
		generic_params(L, x, &py, w);
		if (L->tool != T_PERSP && L->tool != T_PORTRAIT && L->tool != T_FRAME) hint_text(x, &py, w, "Tip: drag up/down on the photo to pick a parameter, left/right to change it.");
		break;
	}
	mask_section(L, x, &py, w);
	panel_content = py - content_start + 8;
	clip_set(c0);
}

/* ================================================================== edit stack */
static int stack_scroll = 0;
static void draw_stack(int x, int y, int w, int h) {
	fill_rect(x, y, w, h, C_PANEL, 1);
	fill_rect(x, y, w, 1, C_LINE, 1);
	text((float)x + 14, y + 16, "EDIT STACK", 11, 1, C_DIM, 1);
	char b[32];
	snprintf(b, sizeof b, "%d", nlayers);
	text((float)x + 14 + text_width("EDIT STACK", 11, 1) + 8, y + 16, b, 11, 1, C_FAINT, 1);
	if (button(4980, x + w - 148, y + 5, 62, 20, "Revert", BS_NORMAL, 0) && nlayers) {
		edit_end(0);
		stack_clear();
		hist_push();
		need_render = 1;
		toast("Reverted all edits  (Ctrl+Z to undo)");
	}
	if (button(4981, x + w - 80, y + 5, 70, 20, "Save Look", BS_NORMAL, 0)) {
		if (!nlayers) toast("Nothing to save - make some edits first");
		else {
			edit_end(1);
			snprintf(look_name_buf, sizeof look_name_buf, "My Look %d", ncl + 1);
			modal = MODAL_SAVELOOK;
			tf_begin(ID_LOOKNAME, look_name_buf, 0);
		}
	}
	int rowh = 30, top = y + 30, vis = h - 34;
	int total = nlayers * rowh;
	if (mouse_in(x, y, w, h) && wheel && !wheel_used) {
		stack_scroll -= wheel * 30;
		wheel_used = 1;
	}
	stack_scroll = CLAMP(stack_scroll, 0, MAX(0, total - vis));
	Clip c = clip_push(x, top, w, vis);
	if (nlayers == 0) text((float)x + 14, top + 18, "No edits yet.", 12, 0, C_FAINT, 1);
	int do_del = -1, do_up = -1, do_dn = -1, do_vis = -1, do_sel = -1;
	for (int k = 0; k < nlayers; k++) {
		int i = nlayers - 1 - k; /* newest on top */
		int ry = top - stack_scroll + k * rowh;
		if (ry + rowh < top || ry > top + vis) continue;
		Layer *L = &stack[i];
		int isact = i == active;
		int hov = mouse_in(x + 6, ry, w - 12, rowh - 2);
		if (isact) rrect((float)x + 6, (float)ry, (float)w - 12, (float)rowh - 2, 6, C_PANEL3, 1);
		else if (hov) rrect((float)x + 6, (float)ry, (float)w - 12, (float)rowh - 2, 6, C_PANEL2, 1);
		if (isact) fill_rect(x + 6, ry + 6, 3, rowh - 14, C_ACC, 1);
		/* eye */
		int ex = x + 24;
		int hoe = mouse_in(ex - 11, ry + 2, 22, rowh - 6);
		icon_eye((float)ex, ry + rowh * 0.5f - 1, 7, L->visible ? C_TEXT : C_FAINT, hoe ? 1.f : 0.85f, L->visible);
		if (hoe && m_press[0]) do_vis = i;
		/* name */
		int nx = x + 42, nw = w - 42 - 78;
		uint32_t gc = group_col(TD[L->tool].group);
		disc((float)nx + 3, ry + rowh * 0.5f - 1, 3.2f, gc, L->visible ? 1.f : 0.4f);
		if (L->mask_on) {
			rrect((float)nx + nw - 16, ry + 7, 16, 14, 4, C_ACC, 0.85f);
			text_c(nx + nw - 8.f, ry + 14.f, "M", 10, 1, 0x0b2a27, 1);
			nw -= 20;
		}
		text_fit((float)nx + 12, ry + rowh * 0.5f - 1, TD[L->tool].name, (float)nw - 12, 13, isact ? 1 : 0, L->visible ? C_TEXT : C_FAINT, 1);
		/* buttons */
		int bx = x + w - 74;
		int hu = mouse_in(bx, ry + 3, 20, rowh - 8), hd = mouse_in(bx + 22, ry + 3, 20, rowh - 8), hx = mouse_in(bx + 46, ry + 3, 24, rowh - 8);
		float cy = ry + rowh * 0.5f - 1;
		tri(bx + 10, cy - 4, bx + 5, cy + 3, bx + 15, cy + 3, C_DIM, hu ? 1.f : 0.5f);
		tri(bx + 32, cy + 4, bx + 27, cy - 3, bx + 37, cy - 3, C_DIM, hd ? 1.f : 0.5f);
		icon_x((float)bx + 58, cy, 4.2f, hx ? C_WARN : C_DIM, hx ? 1.f : 0.6f);
		if (m_press[0]) {
			if (hu) do_up = i;
			else if (hd) do_dn = i;
			else if (hx) do_del = i;
			else if (!hoe && hov) do_sel = i;
		}
	}
	clip_set(c);
	if (do_vis >= 0) {
		stack[do_vis].visible ^= 1;
		layer_bump(&stack[do_vis]);
		need_render = 1;
		hist_push();
	} else if (do_up >= 0)
		do_move_layer(do_up, 1);
	else if (do_dn >= 0)
		do_move_layer(do_dn, -1);
	else if (do_del >= 0)
		do_delete_layer(do_del);
	else if (do_sel >= 0)
		select_layer(do_sel);
}

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
static void center_tool(Layer *L, int in_canvas) {
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
	if (in_canvas && wheel && !mod_ctrl && !wheel_used && L->tool == T_LENS) {
		L->p[LN_SIZE] = CLAMP(L->p[LN_SIZE] + wheel * 3.f, 0.f, 100.f);
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
	} else if (L->tool == T_LENS) {
		float rf = (0.08f + L->p[LN_SIZE] / 100.f * 0.55f) * m * zoom, th = L->p[LN_ANGLE] * PI_F / 180.f;
		if ((int)L->p[LN_SHAPE] == 0)
			ellipse_ring(sx, sy, rf * 1.35f, rf * 0.85f, th, 1.4f, 0xffffff, 0.85f, 1);
		else {
			float nx = -sinf(th), ny = cosf(th), tx = cosf(th), ty = sinf(th), L2 = 3000.f;
			for (int s = -1; s <= 1; s += 2) line_aa(sx + nx * rf * 0.85f * s - tx * L2, sy + ny * rf * 0.85f * s - ty * L2, sx + nx * rf * 0.85f * s + tx * L2, sy + ny * rf * 0.85f * s + ty * L2, 1.4f, 0xffffff, 0.8f);
		}
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
	case T_VIGNETTE: case T_LENS: case T_TEXT: case T_GRAD: center_tool(L, in_canvas); break;
	case T_CURVES: case T_MIXER: break;
	default: scrub_tool(L, in_canvas); break;
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
		int size_tool = active >= 0 && (stack[active].tool == T_BRUSH || stack[active].tool == T_HEAL || stack[active].tool == T_SELECTIVE || stack[active].tool == T_LENS || stack[active].tool == T_GRAD);
		if (!size_tool || mod_ctrl) {
			view_zoom_at(powf(1.18f, (float)wheel), (float)mx, (float)my);
			wheel_used = 1;
		}
	}
	/* image */
	const Mips *m = &disp;
	float z = zoom, cx = vcx, cy = vcy;
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
