// gen_images_seed.c — deterministic synthetic image generator for the Phase 4
// generalization check. Produces low-color pixel-art-style PNGs that PEP is
// actually designed for, so the optimization is exercised on data other than
// the committed images (no overfitting to the three benchmark photos).
//
// Usage: gen_images_seed <out-dir> <seed> [count=4] [size=256]
//
// Fully determined by <seed>: same seed -> byte-identical PNGs. Writes
// <out-dir>/gen_<NN>.png. Standalone (owns its stb_image_write); shares no code
// with the solver or the reference generator.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static uint64_t splitmix64( uint64_t* s )
{
	uint64_t z = ( *s += 0x9E3779B97F4A7C15ULL );
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}
static uint32_t rnd( uint64_t* s, uint32_t n ) { return n ? (uint32_t)( splitmix64( s ) % n ) : 0; }

int main( int argc, char** argv )
{
	if( argc < 3 || argc > 5 )
	{
		fprintf( stderr, "usage: %s <out-dir> <seed> [count=4] [size=256]\n", argv[0] );
		return 2;
	}
	const char* out_dir = argv[1];
	uint64_t state = strtoull( argv[2], NULL, 0 );
	int count = ( argc >= 4 ) ? atoi( argv[3] ) : 4;
	int size  = ( argc >= 5 ) ? atoi( argv[4] ) : 256;
	if( count < 1 ) count = 1;
	if( size  < 8 ) size  = 8;

	mkdir( out_dir, 0777 );

	uint8_t* img = (uint8_t*)malloc( (size_t)size * size * 4 );
	uint8_t palette[256][4];

	int rc = 0;
	for( int k = 0; k < count; k++ )
	{
		// a small random palette (PEP's sweet spot: <=16 colors)
		int ncol = 4 + (int)rnd( &state, 13 );  // 4..16 colors
		for( int c = 0; c < ncol; c++ )
		{
			palette[c][0] = (uint8_t)rnd( &state, 256 );
			palette[c][1] = (uint8_t)rnd( &state, 256 );
			palette[c][2] = (uint8_t)rnd( &state, 256 );
			palette[c][3] = 255;
		}
		// blocky pattern: random-walk fill with occasional block recolors,
		// which gives PEP's predictor something with spatial structure
		int block = 4 + (int)rnd( &state, 13 );
		for( int y = 0; y < size; y++ )
		{
			for( int x = 0; x < size; x++ )
			{
				int bx = x / block, by = y / block;
				uint64_t h = ( (uint64_t)bx * 73856093u ) ^ ( (uint64_t)by * 19349663u ) ^ ( state * 83492791u );
				int c = (int)( h % (uint64_t)ncol );
				// add a diagonal gradient band to vary runs
				if( ( ( x + y ) / ( block * 2 + 1 ) ) & 1 ) c = ( c + 1 ) % ncol;
				size_t i = ( (size_t)y * size + x ) * 4;
				img[i+0] = palette[c][0]; img[i+1] = palette[c][1];
				img[i+2] = palette[c][2]; img[i+3] = palette[c][3];
			}
		}
		// advance state per image so each image differs deterministically
		(void)splitmix64( &state );

		char path[1024];
		snprintf( path, sizeof path, "%s/gen_%02d.png", out_dir, k );
		if( !stbi_write_png( path, size, size, 4, img, size * 4 ) )
		{
			fprintf( stderr, "gen_images_seed: failed to write '%s'\n", path );
			rc = 1;
		}
		else
			printf( "generated %s (%dx%d, ~%d colors)\n", path, size, size, ncol );
	}
	free( img );
	return rc;
}
