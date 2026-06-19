// pep_codec.h — reference image-preprocessing library for the PEP harness.
//
// PEP (github.com/ENDESGA/PEP) is a palette compressor: pep_compress keeps only
// the first <=256 distinct colors and collapses everything else to palette
// index 0. Photographic input must therefore be reduced to <=256 colors BEFORE
// it is handed to pep_compress. This library performs that pre-processing:
//
//     load image (jpg/png/tga/bmp/gif via stb_image)
//       -> deterministic median-cut quantization to <= max_colors
//       -> a fixed-format .qbin (quantized RGBA) the timing harness consumes
//
// Quantization is the build-stage / pre-processing step; it is EXCLUDED from the
// timed compression measurement. The timed step is pep_compress() alone, called
// by the harness on the .qbin pixels.
//
// This translation unit pulls in stb_image / stb_image_write and depends on
// nothing from pep.h, so a single object is shared by both the reference and
// optimized harnesses.
#ifndef PEP_CODEC_H
#define PEP_CODEC_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
	int      width;
	int      height;
	uint8_t* rgba;    // width*height*4, RGBA8, owned by the struct
	int      colors;  // distinct palette colors after quantization (<=256)
} pepc_image;

// Load any stb-supported image as RGBA8. Returns 1 on success, 0 on failure.
int pepc_load( const char* path, pepc_image* out );

// Deterministic median-cut quantization to at most max_colors (clamped 2..256),
// in place. Returns the number of palette colors produced, or 0 on error.
int pepc_quantize( pepc_image* img, int max_colors );

void pepc_free( pepc_image* img );

// Fixed-format quantized-image container (build artifact under build/):
//   "PQB1" | u32 width | u32 height | u32 colors | width*height*4 RGBA bytes
// All integers little-endian. Returns 1 on success.
int pepc_write_qbin( const char* path, const pepc_image* img );
int pepc_read_qbin( const char* path, pepc_image* out );

// FNV-1a 64-bit digest — used by the harness/comparator to record and compare
// byte-identity of produced artifacts.
uint64_t pepc_fnv1a( const void* data, size_t n );

// FNV-1a 64-bit of an entire file's bytes. Returns 1 on success.
int pepc_fnv1a_file( const char* path, uint64_t* out_digest );

#endif // PEP_CODEC_H
