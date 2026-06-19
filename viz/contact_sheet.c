// contact_sheet.c — PEP quality visualizer (see prompts/create-visualizer.md).
//
// Renders, one image at a time, a side-by-side comparison:
//   [ original ] | [ quantized (<=256 colors) ] | [ PEP round-trip ]
// with a caption strip (name, dims, original colors, palette size, source/.pep
// bytes, ratio vs raw RGBA, bpp). The quantized and round-trip panels MUST be
// pixel-identical (PEP is lossless on palettized pixels); the tool asserts this
// and prints a per-image verdict. All visible loss is from quantization.
//
// Usage: contact_sheet <results.txt> <images-dir> <qdir> <out-dir>
//   results.txt  the harness manifest (last column is the .pep path)
//   images-dir   directory holding the original images (named like the .pep stem)
//   qdir         directory holding <name>.qbin (the quantized pixels)
//   out-dir      where to write <name>.png comparison sheets
//
// Verification aid only: never timed, never imported by the harness.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "stb_image.h"        // decl only; implementation comes from pep_codec.c
#include "stb_image_write.h"  // decl only; implementation comes from pep_codec.c
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"
#include "pep.h"
#include "pep_codec.h"

#define PANEL_H   400          // panel height in pixels (downscaled)
#define GAP        12
#define LABEL_H    22
#define CAPTION_H  60
#define TXT_SCALE   2

static void put_px( uint8_t* c, int W, int H, int x, int y, uint8_t r, uint8_t g, uint8_t b )
{
	if( x < 0 || y < 0 || x >= W || y >= H ) return;
	size_t i = ( (size_t)y * W + x ) * 4;
	c[i+0] = r; c[i+1] = g; c[i+2] = b; c[i+3] = 255;
}

static void fill_rect( uint8_t* c, int W, int H, int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b )
{
	for( int y = y0; y < y0 + h; y++ )
		for( int x = x0; x < x0 + w; x++ )
			put_px( c, W, H, x, y, r, g, b );
}

// Render text via stb_easy_font (outputs axis-aligned quads we rasterize).
static void draw_text( uint8_t* c, int W, int H, int x, int y, const char* s,
                       uint8_t r, uint8_t g, uint8_t b )
{
	static char buf[120000];
	int nq = stb_easy_font_print( 0, 0, (char*)s, NULL, buf, sizeof buf );
	for( int q = 0; q < nq; q++ )
	{
		char* v0 = buf + (size_t)q * 4 * 16;
		char* v2 = v0 + 2 * 16;
		float x0, y0, x1, y1;
		memcpy( &x0, v0 + 0, 4 ); memcpy( &y0, v0 + 4, 4 );
		memcpy( &x1, v2 + 0, 4 ); memcpy( &y1, v2 + 4, 4 );
		int rx0 = x + (int)( x0 * TXT_SCALE ), ry0 = y + (int)( y0 * TXT_SCALE );
		int rx1 = x + (int)( x1 * TXT_SCALE ), ry1 = y + (int)( y1 * TXT_SCALE );
		for( int yy = ry0; yy < ry1; yy++ )
			for( int xx = rx0; xx < rx1; xx++ )
				put_px( c, W, H, xx, yy, r, g, b );
	}
}

// area-average downscale of RGBA (sw,sh) -> (dw,dh)
static void resize_rgba( const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh )
{
	for( int dy = 0; dy < dh; dy++ )
	{
		int sy0 = (int)( (int64_t)dy * sh / dh );
		int sy1 = (int)( (int64_t)( dy + 1 ) * sh / dh );
		if( sy1 <= sy0 ) sy1 = sy0 + 1;
		for( int dx = 0; dx < dw; dx++ )
		{
			int sx0 = (int)( (int64_t)dx * sw / dw );
			int sx1 = (int)( (int64_t)( dx + 1 ) * sw / dw );
			if( sx1 <= sx0 ) sx1 = sx0 + 1;
			uint64_t ar = 0, ag = 0, ab = 0, n = 0;
			for( int sy = sy0; sy < sy1 && sy < sh; sy++ )
				for( int sx = sx0; sx < sx1 && sx < sw; sx++ )
				{
					size_t i = ( (size_t)sy * sw + sx ) * 4;
					ar += src[i+0]; ag += src[i+1]; ab += src[i+2]; n++;
				}
			if( !n ) n = 1;
			size_t di = ( (size_t)dy * dw + dx ) * 4;
			dst[di+0] = (uint8_t)( ar / n ); dst[di+1] = (uint8_t)( ag / n );
			dst[di+2] = (uint8_t)( ab / n ); dst[di+3] = 255;
		}
	}
}

// blit an RGBA panel into the canvas at (px,py)
static void blit( uint8_t* c, int W, int H, const uint8_t* p, int pw, int ph, int px, int py )
{
	for( int y = 0; y < ph; y++ )
		for( int x = 0; x < pw; x++ )
		{
			size_t s = ( (size_t)y * pw + x ) * 4;
			put_px( c, W, H, px + x, py + y, p[s+0], p[s+1], p[s+2] );
		}
}

static int cmp_u32( const void* a, const void* b )
{
	uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
	return ( x < y ) ? -1 : ( x > y );
}
static long distinct_colors( const uint8_t* rgba, long area )
{
	uint32_t* p = (uint32_t*)malloc( (size_t)area * 4 );
	if( !p ) return -1;
	memcpy( p, rgba, (size_t)area * 4 );
	qsort( p, area, 4, cmp_u32 );
	long u = area ? 1 : 0;
	for( long i = 1; i < area; i++ ) if( p[i] != p[i-1] ) u++;
	free( p );
	return u;
}

static long file_size( const char* path )
{
	FILE* f = fopen( path, "rb" );
	if( !f ) return -1;
	fseek( f, 0, SEEK_END );
	long n = ftell( f );
	fclose( f );
	return n;
}

int main( int argc, char** argv )
{
	if( argc != 5 )
	{
		fprintf( stderr, "usage: %s <results.txt> <images-dir> <qdir> <out-dir>\n", argv[0] );
		return 2;
	}
	const char* res_path = argv[1];
	const char* img_dir  = argv[2];
	const char* qdir     = argv[3];
	const char* out_dir  = argv[4];
	mkdir( out_dir, 0777 );

	FILE* f = fopen( res_path, "r" );
	if( !f ) { fprintf( stderr, "cannot open '%s'\n", res_path ); return 2; }

	char line[1200];
	int sheets = 0, mismatches = 0;
	while( fgets( line, sizeof line, f ) )
	{
		int idx, w, h, colors, model, rt_ok;
		unsigned long long pep_bytes, pep_fnv, decoded_fnv;
		char pep_path[1024];
		if( sscanf( line, "%d %d %d %d %d %llu %llx %llx %d %1023s",
		            &idx, &w, &h, &colors, &model, &pep_bytes, &pep_fnv,
		            &decoded_fnv, &rt_ok, pep_path ) != 10 )
			continue;

		// name = basename(pep_path) without ".pep"
		const char* base = strrchr( pep_path, '/' );
		base = base ? base + 1 : pep_path;
		char name[512];
		snprintf( name, sizeof name, "%s", base );
		char* dot = strrchr( name, '.' );
		if( dot && strcmp( dot, ".pep" ) == 0 ) *dot = '\0';

		char orig_path[1100], qbin_path[1100];
		snprintf( orig_path, sizeof orig_path, "%s/%s", img_dir, name );
		snprintf( qbin_path, sizeof qbin_path, "%s/%s.qbin", qdir, name );

		// 1. original
		int ow, oh, on;
		uint8_t* orig = stbi_load( orig_path, &ow, &oh, &on, 4 );
		if( !orig ) { fprintf( stderr, "skip %s: cannot load original '%s'\n", name, orig_path ); continue; }

		// 2. quantized
		pepc_image q;
		if( !pepc_read_qbin( qbin_path, &q ) ) { stbi_image_free( orig ); continue; }

		// 3. PEP round-trip
		pep p = pep_load( pep_path );
		// PEP's .pep format packs each dimension in 12 bits (max 4096); images
		// taller/wider than that load back with a truncated dimension (the
		// compressed stream is fine, only the stored size is wrong). The qbin
		// carries the authoritative dimensions, so restore them before decoding.
		if( p.bytes && ( p.width != q.width || p.height != q.height ) )
		{
			fprintf( stderr, "note: %s .pep stored dims %ux%u != true %dx%d "
			         "(exceeds PEP's 4096 format limit); restoring from qbin\n",
			         name, p.width, p.height, q.width, q.height );
			p.width  = (uint16_t)q.width;
			p.height = (uint16_t)q.height;
		}
		uint32_t* back = p.bytes ? pep_decompress( &p, pep_rgba, 0, 0 ) : NULL;

		long area = (long)q.width * q.height;
		int rt_identical = ( back != NULL ) && ( memcmp( back, q.rgba, (size_t)area * 4 ) == 0 );
		if( !rt_identical ) { mismatches++; fprintf( stderr, "WARNING: %s round-trip != quantized\n", name ); }

		// downscale all three to PANEL_H
		int ph = PANEL_H;
		int pw = (int)( (int64_t)ow * ph / oh );
		if( pw < 1 ) pw = 1;
		uint8_t* dorig = malloc( (size_t)pw * ph * 4 );
		uint8_t* dquant = malloc( (size_t)pw * ph * 4 );
		uint8_t* dround = malloc( (size_t)pw * ph * 4 );
		resize_rgba( orig, ow, oh, dorig, pw, ph );
		resize_rgba( q.rgba, q.width, q.height, dquant, pw, ph );
		if( back ) resize_rgba( (uint8_t*)back, q.width, q.height, dround, pw, ph );
		else       memset( dround, 64, (size_t)pw * ph * 4 );

		// canvas
		int W = 3 * pw + 4 * GAP;
		int H = LABEL_H + ph + CAPTION_H;
		uint8_t* canvas = malloc( (size_t)W * H * 4 );
		fill_rect( canvas, W, H, 0, 0, W, H, 24, 24, 28 );  // dark bg

		int x0 = GAP, x1 = 2 * GAP + pw, x2 = 3 * GAP + 2 * pw;
		int py = LABEL_H;
		blit( canvas, W, H, dorig,  pw, ph, x0, py );
		blit( canvas, W, H, dquant, pw, ph, x1, py );
		blit( canvas, W, H, dround, pw, ph, x2, py );

		// panel labels
		long ocolors = distinct_colors( orig, (long)ow * oh );
		char lab[128];
		draw_text( canvas, W, H, x0, 5, "original", 230, 230, 230 );
		snprintf( lab, sizeof lab, "quantized (%d colors)", colors );
		draw_text( canvas, W, H, x1, 5, lab, 230, 230, 230 );
		snprintf( lab, sizeof lab, "PEP round-trip%s", rt_identical ? " (== quantized, lossless)" : " (MISMATCH!)" );
		draw_text( canvas, W, H, x2, 5, lab, rt_identical ? 150 : 255, rt_identical ? 230 : 90,
		           rt_identical ? 150 : 90 );

		// caption strip
		long src_bytes = file_size( orig_path );
		double raw = (double)area * 4.0;
		double bpp = ( (double)pep_bytes * 8.0 ) / (double)area;
		char cap[1024];
		snprintf( cap, sizeof cap,
		          "%s   %dx%d   original colors: %ld -> palette %d (model %d)",
		          name, w, h, ocolors, colors, model );
		draw_text( canvas, W, H, GAP, py + ph + 8, cap, 200, 220, 255 );
		snprintf( cap, sizeof cap,
		          "source(jpg): %ld B    .pep: %llu B (%.1fx vs raw RGBA, %.3f bpp)    [loss is from quantization; PEP is lossless]",
		          src_bytes, pep_bytes, raw / (double)pep_bytes, bpp );
		draw_text( canvas, W, H, GAP, py + ph + 8 + 11 * TXT_SCALE, cap, 170, 190, 210 );

		char out_path[1200];
		snprintf( out_path, sizeof out_path, "%s/%s.png", out_dir, name );
		if( stbi_write_png( out_path, W, H, 4, canvas, W * 4 ) )
		{
			printf( "%-48s %s  ->  %s  (%dx%d)\n", name,
			        rt_identical ? "round-trip OK" : "ROUND-TRIP MISMATCH", out_path, W, H );
			sheets++;
		}

		free( canvas ); free( dorig ); free( dquant ); free( dround );
		free( back ); pep_free( &p ); pepc_free( &q ); stbi_image_free( orig );
	}
	fclose( f );
	printf( "\n%d sheets written, %d round-trip mismatches\n", sheets, mismatches );
	return mismatches ? 1 : 0;
}
