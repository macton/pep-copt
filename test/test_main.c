// test_main.c — correctness suite for the PEP codec. Run against both the
// reference library (build/test_runner) and the optimized library
// (build/test_runner_opt) — the include path is the only difference.
//
// The contract PEP must satisfy, and that an optimization must preserve:
//   - lossless round-trip: decompress(compress(x)) == x for palettized pixels
//   - determinism: same pixels -> byte-identical serialized .pep
//   - quantization yields <= requested colors and is itself deterministic
//   - save/load round-trips and matches in-memory decompression
//   - degenerate sizes (1x1) work
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pep.h"
#include "pep_codec.h"

static int g_pass = 0, g_fail = 0;
#define CHECK( cond, msg ) do { \
	if( cond ) { g_pass++; } \
	else { g_fail++; printf( "  FAIL: %s\n", msg ); } } while( 0 )

// Build a deterministic synthetic RGBA image with structured low-color content.
static uint8_t* make_image( int w, int h )
{
	uint8_t* p = (uint8_t*)malloc( (size_t)w * h * 4 );
	for( int y = 0; y < h; y++ )
		for( int x = 0; x < w; x++ )
		{
			size_t i = ( (size_t)y * w + x ) * 4;
			int c = ( ( x / 7 ) + ( y / 5 ) ) % 6;
			p[i+0] = (uint8_t)( 30 * c );
			p[i+1] = (uint8_t)( 200 - 20 * c );
			p[i+2] = (uint8_t)( ( x ^ y ) & 0xF0 );
			p[i+3] = 255;
		}
	return p;
}

static void test_roundtrip_lossless( void )
{
	printf( "test_roundtrip_lossless\n" );
	int w = 96, h = 64;
	pepc_image img = { w, h, make_image( w, h ), 0 };
	pepc_quantize( &img, 64 );
	uint32_t area = (uint32_t)w * h;

	pep p = pep_compress( (const uint32_t*)img.rgba, (uint16_t)w, (uint16_t)h, pep_rgba, pep_8bit );
	CHECK( p.bytes != NULL, "compress produced output" );
	uint32_t* back = pep_decompress( &p, pep_rgba, 0, 0 );
	CHECK( back != NULL, "decompress produced output" );
	CHECK( back && memcmp( back, img.rgba, (size_t)area * 4 ) == 0,
	       "decompressed pixels byte-identical to input" );
	free( back );
	pep_free( &p );
	pepc_free( &img );
}

static void test_determinism( void )
{
	printf( "test_determinism\n" );
	int w = 80, h = 80;
	pepc_image img = { w, h, make_image( w, h ), 0 };
	pepc_quantize( &img, 32 );

	pep a = pep_compress( (const uint32_t*)img.rgba, (uint16_t)w, (uint16_t)h, pep_rgba, pep_8bit );
	pep b = pep_compress( (const uint32_t*)img.rgba, (uint16_t)w, (uint16_t)h, pep_rgba, pep_8bit );
	uint32_t sa = 0, sb = 0;
	uint8_t* ba = pep_serialize( &a, &sa );
	uint8_t* bb = pep_serialize( &b, &sb );
	CHECK( ba && bb, "serialize produced output" );
	CHECK( sa == sb, "two compressions produce equal serialized size" );
	CHECK( ba && bb && sa == sb && memcmp( ba, bb, sa ) == 0,
	       "two compressions produce byte-identical output" );
	free( ba ); free( bb );
	pep_free( &a ); pep_free( &b );
	pepc_free( &img );
}

static void test_quantize_bound( void )
{
	printf( "test_quantize_bound\n" );
	int w = 128, h = 128;
	pepc_image img = { w, h, make_image( w, h ), 0 };
	int c = pepc_quantize( &img, 16 );
	CHECK( c >= 1 && c <= 16, "quantize respects the <=16 color bound" );

	// determinism of quantization: same input -> same pixels
	pepc_image img2 = { w, h, make_image( w, h ), 0 };
	pepc_quantize( &img2, 16 );
	CHECK( memcmp( img.rgba, img2.rgba, (size_t)w * h * 4 ) == 0,
	       "quantization is deterministic" );
	pepc_free( &img );
	pepc_free( &img2 );
}

static void test_save_load( void )
{
	printf( "test_save_load\n" );
	int w = 50, h = 40;
	pepc_image img = { w, h, make_image( w, h ), 0 };
	pepc_quantize( &img, 32 );
	uint32_t area = (uint32_t)w * h;

	pep p = pep_compress( (const uint32_t*)img.rgba, (uint16_t)w, (uint16_t)h, pep_rgba, pep_8bit );
	const char* path = "build/_test_save.pep";
	CHECK( pep_save( &p, path ) != 0, "pep_save succeeds" );

	pep q = pep_load( path );
	CHECK( q.bytes != NULL, "pep_load produced output" );
	uint32_t* back = pep_decompress( &q, pep_rgba, 0, 0 );
	CHECK( back && memcmp( back, img.rgba, (size_t)area * 4 ) == 0,
	       "save->load->decompress matches input" );
	free( back );
	pep_free( &p );
	pep_free( &q );
	pepc_free( &img );
	remove( path );
}

static void test_tiny( void )
{
	printf( "test_tiny\n" );
	uint8_t px[4] = { 12, 34, 56, 255 };
	pep p = pep_compress( (const uint32_t*)px, 1, 1, pep_rgba, pep_8bit );
	CHECK( p.bytes != NULL, "1x1 compress produces output" );
	uint32_t* back = pep_decompress( &p, pep_rgba, 0, 0 );
	CHECK( back && memcmp( back, px, 4 ) == 0, "1x1 round-trip is lossless" );
	free( back );
	pep_free( &p );
}

int main( void )
{
	test_roundtrip_lossless();
	test_determinism();
	test_quantize_bound();
	test_save_load();
	test_tiny();
	printf( "\n%d passed, %d failed\n", g_pass, g_fail );
	return g_fail ? 1 : 0;
}
