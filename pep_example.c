// pep_example.c
//
// Simple example for ENDESGA's PEP (Prediction-Encoded Pixels):
//   https://github.com/ENDESGA/PEP
//
//   1. load an image (jpg / png / tga / bmp / gif ... via stb_image)
//   2. quantize it to <= 256 colors  (PEP is a palette format: it keeps only
//      the first 256 distinct colors, so photographic input must be reduced
//      first or every extra color collapses to palette index 0)
//   3. recompress the quantized pixels with pep_compress() -- this step is timed
//   4. save the result as a .pep file
//   5. write PNG previews (quantized input + PEP round-trip) so the result is
//      verifiable by eye, and report compression metrics
//
// build:   gcc -O2 -I. pep_example.c -o pep_example -lm
// usage:   ./pep_example <input-image> [num_colors=256] [output.pep]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define PEP_IMPLEMENTATION
#include "src/pep.h"

// ---- timing helper -------------------------------------------------------

static double now_ms( void )
{
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return t.tv_sec * 1000.0 + t.tv_nsec / 1.0e6;
}

// ---- median-cut color quantization --------------------------------------
//
// Reduces an RGBA image to at most `max_colors` colors. Each pixel is mapped
// to the average color of the median-cut box it ends up in, giving an O(area)
// mapping with no per-pixel nearest-color search.

typedef struct { uint8_t r, g, b, a; uint32_t pos; } qpx;
typedef struct { uint32_t begin, end; } qbox;        // [begin, end) into px[]

static uint8_t box_longest_axis( const qpx* px, qbox b )
{
	uint8_t lo[4] = { 255, 255, 255, 255 }, hi[4] = { 0, 0, 0, 0 };
	for( uint32_t i = b.begin; i < b.end; i++ )
	{
		const uint8_t c[4] = { px[i].r, px[i].g, px[i].b, px[i].a };
		for( int k = 0; k < 4; k++ )
		{
			if( c[k] < lo[k] ) lo[k] = c[k];
			if( c[k] > hi[k] ) hi[k] = c[k];
		}
	}
	uint8_t axis = 0; int best = -1;
	for( int k = 0; k < 4; k++ )
	{
		int range = hi[k] - lo[k];
		if( range > best ) { best = range; axis = (uint8_t)k; }
	}
	return axis;
}

static uint8_t g_axis;
static int qcmp( const void* a, const void* b )
{
	const uint8_t* x = &((const qpx*)a)->r;
	const uint8_t* y = &((const qpx*)b)->r;
	return (int)x[g_axis] - (int)y[g_axis];
}

// Quantize in place. Fills out_rgba (area*4) with the quantized image and
// returns the number of palette colors actually produced.
static int quantize( const uint8_t* rgba, uint32_t area, int max_colors,
                     uint8_t* out_rgba )
{
	qpx* px = (qpx*)malloc( (size_t)area * sizeof( qpx ) );
	for( uint32_t i = 0; i < area; i++ )
	{
		px[i].r = rgba[i*4+0]; px[i].g = rgba[i*4+1];
		px[i].b = rgba[i*4+2]; px[i].a = rgba[i*4+3];
		px[i].pos = i;
	}

	qbox* boxes = (qbox*)malloc( (size_t)max_colors * sizeof( qbox ) );
	int nboxes = 1;
	boxes[0].begin = 0; boxes[0].end = area;

	while( nboxes < max_colors )
	{
		// pick the box with the most pixels that is still splittable
		int target = -1; uint32_t best = 1;
		for( int i = 0; i < nboxes; i++ )
		{
			uint32_t cnt = boxes[i].end - boxes[i].begin;
			if( cnt > best ) { best = cnt; target = i; }
		}
		if( target < 0 ) break;

		qbox b = boxes[target];
		g_axis = box_longest_axis( px, b );
		qsort( px + b.begin, b.end - b.begin, sizeof( qpx ), qcmp );
		uint32_t mid = b.begin + ( b.end - b.begin ) / 2;
		if( mid == b.begin ) mid++;           // guarantee forward progress

		boxes[target].end = mid;
		boxes[nboxes].begin = mid;
		boxes[nboxes].end = b.end;
		nboxes++;
	}

	// average each box -> palette color, then paint every pixel in the box
	for( int i = 0; i < nboxes; i++ )
	{
		uint64_t sr = 0, sg = 0, sb = 0, sa = 0;
		uint32_t cnt = boxes[i].end - boxes[i].begin;
		if( !cnt ) continue;
		for( uint32_t j = boxes[i].begin; j < boxes[i].end; j++ )
		{
			sr += px[j].r; sg += px[j].g; sb += px[j].b; sa += px[j].a;
		}
		uint8_t r = (uint8_t)( sr / cnt ), g = (uint8_t)( sg / cnt );
		uint8_t b8 = (uint8_t)( sb / cnt ), a = (uint8_t)( sa / cnt );
		for( uint32_t j = boxes[i].begin; j < boxes[i].end; j++ )
		{
			uint32_t pos = px[j].pos;
			out_rgba[pos*4+0] = r; out_rgba[pos*4+1] = g;
			out_rgba[pos*4+2] = b8; out_rgba[pos*4+3] = a;
		}
	}

	free( boxes );
	free( px );
	return nboxes;
}

// -------------------------------------------------------------------------

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		fprintf( stderr, "usage: %s <input-image> [num_colors=256] [output.pep]\n", argv[0] );
		return 1;
	}
	const char* in_path  = argv[1];
	int num_colors       = ( argc > 2 ) ? atoi( argv[2] ) : 256;
	if( num_colors < 2 )   num_colors = 2;
	if( num_colors > 256 ) num_colors = 256;

	char out_path[1024];
	if( argc > 3 ) snprintf( out_path, sizeof out_path, "%s", argv[3] );
	else           snprintf( out_path, sizeof out_path, "%s.pep", in_path );

	// 1. load -------------------------------------------------------------
	int w, h, n;
	uint8_t* rgba = stbi_load( in_path, &w, &h, &n, 4 );
	if( !rgba ) { fprintf( stderr, "failed to load '%s': %s\n", in_path, stbi_failure_reason() ); return 1; }
	uint32_t area = (uint32_t)w * (uint32_t)h;
	printf( "loaded   %s  %dx%d  (%u px, %d src channels)\n", in_path, w, h, area, n );

	// 2. quantize to <= num_colors ---------------------------------------
	uint8_t* quant = (uint8_t*)malloc( (size_t)area * 4 );
	double qt0 = now_ms();
	int palette_colors = quantize( rgba, area, num_colors, quant );
	double qt1 = now_ms();
	printf( "quantize requested %d colors -> %d boxes   (%.2f ms)\n",
	        num_colors, palette_colors, qt1 - qt0 );

	// 3. compress (timed) -------------------------------------------------
	double t0 = now_ms();
	pep p = pep_compress( (const uint32_t*)quant, (uint16_t)w, (uint16_t)h, pep_rgba, pep_8bit );
	double t1 = now_ms();
	if( p.bytes == NULL ) { fprintf( stderr, "pep_compress failed\n" ); return 1; }

	// 4. save -------------------------------------------------------------
	if( !pep_save( &p, out_path ) ) { fprintf( stderr, "pep_save failed\n" ); return 1; }

	// 5. round-trip + previews -------------------------------------------
	uint32_t* back = pep_decompress( &p, pep_rgba, 0, 0 );
	char prev_q[1100], prev_r[1100];
	snprintf( prev_q, sizeof prev_q, "%s.quantized.png", out_path );
	snprintf( prev_r, sizeof prev_r, "%s.roundtrip.png", out_path );
	stbi_write_png( prev_q, w, h, 4, quant, w * 4 );
	if( back ) stbi_write_png( prev_r, w, h, 4, back, w * 4 );

	// ---- metrics --------------------------------------------------------
	FILE* f = fopen( in_path, "rb" );
	long src_size = 0;
	if( f ) { fseek( f, 0, SEEK_END ); src_size = ftell( f ); fclose( f ); }
	uint64_t raw_size = (uint64_t)area * 4;          // uncompressed RGBA
	uint64_t pep_size = p.bytes_size;
	double mpix = area / 1.0e6;

	printf( "\n=== PEP compression ===\n" );
	printf( "  palette size     : %u colors\n", p.palette_size );
	printf( "  chosen model     : %u\n", p.model );
	printf( "  output file      : %s\n", out_path );
	printf( "  --- timing ---\n" );
	printf( "  compress time    : %.3f ms\n", t1 - t0 );
	printf( "  throughput       : %.2f Mpix/s   (%.2f MB/s raw RGBA)\n",
	        mpix / ( ( t1 - t0 ) / 1000.0 ),
	        ( raw_size / 1.0e6 ) / ( ( t1 - t0 ) / 1000.0 ) );
	printf( "  --- size ---\n" );
	printf( "  raw RGBA         : %llu bytes\n", (unsigned long long)raw_size );
	printf( "  PEP              : %llu bytes  (%.1fx vs raw RGBA, %.3f bpp)\n",
	        (unsigned long long)pep_size,
	        (double)raw_size / (double)pep_size,
	        ( pep_size * 8.0 ) / area );
	if( src_size )
		printf( "  source file      : %ld bytes  (%s is %.2fx the source)\n",
		        src_size, "PEP", (double)pep_size / (double)src_size );
	printf( "  previews         : %s , %s\n", prev_q, back ? prev_r : "(roundtrip failed)" );

	free( back );
	pep_free( &p );
	free( quant );
	stbi_image_free( rgba );
	return 0;
}
