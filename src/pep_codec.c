// pep_codec.c — implementation of the PEP harness pre-processing library.
// See pep_codec.h. Single TU that owns the stb_image / stb_image_write
// implementations. Depends on nothing from pep.h.
#include "pep_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---- load ---------------------------------------------------------------

int pepc_load( const char* path, pepc_image* out )
{
	int w, h, n;
	uint8_t* rgba = stbi_load( path, &w, &h, &n, 4 );
	if( !rgba )
	{
		fprintf( stderr, "pepc_load: failed to load '%s': %s\n", path, stbi_failure_reason() );
		return 0;
	}
	out->width  = w;
	out->height = h;
	out->rgba   = rgba;
	out->colors = 0;
	return 1;
}

void pepc_free( pepc_image* img )
{
	if( img && img->rgba )
	{
		free( img->rgba );
		img->rgba = NULL;
	}
}

// ---- deterministic median-cut quantization ------------------------------
//
// Each pixel is mapped to the average color of the median-cut box it lands in,
// giving an O(area) mapping with no per-pixel nearest-color search. The sort
// key and split order depend only on pixel values, so the result is a
// deterministic function of the input image (same bytes -> same output bytes).

typedef struct { uint8_t r, g, b, a; uint32_t pos; } qpx;
typedef struct { uint32_t begin, end; } qbox;

static int g_axis;
static int qcmp( const void* a, const void* b )
{
	const uint8_t* x = &( (const qpx*)a )->r;
	const uint8_t* y = &( (const qpx*)b )->r;
	int d = (int)x[g_axis] - (int)y[g_axis];
	if( d != 0 ) return d;
	// stable tie-break on full color then position -> fully deterministic order
	const qpx* pa = (const qpx*)a;
	const qpx* pb = (const qpx*)b;
	if( pa->r != pb->r ) return (int)pa->r - (int)pb->r;
	if( pa->g != pb->g ) return (int)pa->g - (int)pb->g;
	if( pa->b != pb->b ) return (int)pa->b - (int)pb->b;
	if( pa->a != pb->a ) return (int)pa->a - (int)pb->a;
	return ( pa->pos < pb->pos ) ? -1 : ( pa->pos > pb->pos );
}

static int box_longest_axis( const qpx* px, qbox b )
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
	int axis = 0, best = -1;
	for( int k = 0; k < 4; k++ )
	{
		int range = hi[k] - lo[k];
		if( range > best ) { best = range; axis = k; }
	}
	return axis;
}

int pepc_quantize( pepc_image* img, int max_colors )
{
	if( !img || !img->rgba ) return 0;
	if( max_colors < 2 )   max_colors = 2;
	if( max_colors > 256 ) max_colors = 256;

	uint32_t area = (uint32_t)img->width * (uint32_t)img->height;
	if( area == 0 ) return 0;

	qpx* px = (qpx*)malloc( (size_t)area * sizeof( qpx ) );
	qbox* boxes = (qbox*)malloc( (size_t)max_colors * sizeof( qbox ) );
	if( !px || !boxes ) { free( px ); free( boxes ); return 0; }

	for( uint32_t i = 0; i < area; i++ )
	{
		px[i].r = img->rgba[i*4+0]; px[i].g = img->rgba[i*4+1];
		px[i].b = img->rgba[i*4+2]; px[i].a = img->rgba[i*4+3];
		px[i].pos = i;
	}

	int nboxes = 1;
	boxes[0].begin = 0; boxes[0].end = area;

	while( nboxes < max_colors )
	{
		int target = -1; uint32_t best = 1;
		for( int i = 0; i < nboxes; i++ )
		{
			uint32_t cnt = boxes[i].end - boxes[i].begin;
			if( cnt > best ) { best = cnt; target = i; }
		}
		if( target < 0 ) break;  // every box is a single color: cannot split

		qbox b = boxes[target];
		g_axis = box_longest_axis( px, b );
		qsort( px + b.begin, b.end - b.begin, sizeof( qpx ), qcmp );
		uint32_t mid = b.begin + ( b.end - b.begin ) / 2;
		if( mid == b.begin ) mid++;

		boxes[target].end  = mid;
		boxes[nboxes].begin = mid;
		boxes[nboxes].end   = b.end;
		nboxes++;
	}

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
			img->rgba[pos*4+0] = r; img->rgba[pos*4+1] = g;
			img->rgba[pos*4+2] = b8; img->rgba[pos*4+3] = a;
		}
	}

	free( boxes );
	free( px );
	img->colors = nboxes;
	return nboxes;
}

// ---- .qbin I/O ----------------------------------------------------------

static int wr_u32( FILE* f, uint32_t v )
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)( v >> 8 ), (uint8_t)( v >> 16 ), (uint8_t)( v >> 24 ) };
	return fwrite( b, 1, 4, f ) == 4;
}
static int rd_u32( FILE* f, uint32_t* v )
{
	uint8_t b[4];
	if( fread( b, 1, 4, f ) != 4 ) return 0;
	*v = (uint32_t)b[0] | ( (uint32_t)b[1] << 8 ) | ( (uint32_t)b[2] << 16 ) | ( (uint32_t)b[3] << 24 );
	return 1;
}

int pepc_write_qbin( const char* path, const pepc_image* img )
{
	FILE* f = fopen( path, "wb" );
	if( !f ) { fprintf( stderr, "pepc_write_qbin: cannot open '%s'\n", path ); return 0; }
	int ok = fwrite( "PQB1", 1, 4, f ) == 4
	      && wr_u32( f, (uint32_t)img->width )
	      && wr_u32( f, (uint32_t)img->height )
	      && wr_u32( f, (uint32_t)img->colors );
	size_t n = (size_t)img->width * (size_t)img->height * 4;
	ok = ok && ( fwrite( img->rgba, 1, n, f ) == n );
	fclose( f );
	if( !ok ) fprintf( stderr, "pepc_write_qbin: short write to '%s'\n", path );
	return ok;
}

int pepc_read_qbin( const char* path, pepc_image* out )
{
	FILE* f = fopen( path, "rb" );
	if( !f ) { fprintf( stderr, "pepc_read_qbin: cannot open '%s'\n", path ); return 0; }
	char magic[4];
	uint32_t w = 0, h = 0, c = 0;
	if( fread( magic, 1, 4, f ) != 4 || memcmp( magic, "PQB1", 4 ) != 0
	    || !rd_u32( f, &w ) || !rd_u32( f, &h ) || !rd_u32( f, &c ) )
	{
		fprintf( stderr, "pepc_read_qbin: bad header in '%s'\n", path );
		fclose( f );
		return 0;
	}
	size_t n = (size_t)w * (size_t)h * 4;
	uint8_t* rgba = (uint8_t*)malloc( n );
	if( !rgba || fread( rgba, 1, n, f ) != n )
	{
		fprintf( stderr, "pepc_read_qbin: short read in '%s'\n", path );
		free( rgba );
		fclose( f );
		return 0;
	}
	fclose( f );
	out->width = (int)w; out->height = (int)h; out->colors = (int)c; out->rgba = rgba;
	return 1;
}

// ---- FNV-1a 64-bit ------------------------------------------------------

uint64_t pepc_fnv1a( const void* data, size_t n )
{
	const uint8_t* p = (const uint8_t*)data;
	uint64_t h = 1469598103934665603ULL;
	for( size_t i = 0; i < n; i++ )
	{
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

int pepc_fnv1a_file( const char* path, uint64_t* out_digest )
{
	FILE* f = fopen( path, "rb" );
	if( !f ) return 0;
	uint64_t h = 1469598103934665603ULL;
	uint8_t buf[65536];
	size_t got;
	while( ( got = fread( buf, 1, sizeof buf, f ) ) > 0 )
	{
		for( size_t i = 0; i < got; i++ ) { h ^= buf[i]; h *= 1099511628211ULL; }
	}
	fclose( f );
	*out_digest = h;
	return 1;
}
