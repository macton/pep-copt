// decomp_time.c — measure pep_decompress time (the harness only times compress).
// Compiled twice: with -Isrc (reference lib) and -Isrc-optimized (optimized lib),
// so each build decompresses .pep files produced by its own compressor/format.
//
// Usage: decomp_time <runs> <file.pep> [file.pep ...]
//   Loads every .pep up front (file I/O excluded from timing), then times
//   pep_decompress over the whole set, repeated <runs> times; prints the median.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "pep.h"

static double now_s( void )
{
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
}
static int cmp_d( const void* a, const void* b )
{
	double x = *(const double*)a, y = *(const double*)b;
	return ( x < y ) ? -1 : ( x > y );
}

int main( int argc, char** argv )
{
	if( argc < 3 ) { fprintf( stderr, "usage: %s <runs> <file.pep> [...]\n", argv[0] ); return 2; }
	int runs = atoi( argv[1] );
	if( runs < 1 ) runs = 1;
	if( runs > 64 ) runs = 64;
	int nf = argc - 2;

	// Load all .pep up front (NOT timed).
	pep* ps = (pep*)malloc( (size_t)nf * sizeof( pep ) );
	uint64_t total_px = 0;
	for( int i = 0; i < nf; i++ )
	{
		ps[i] = pep_load( argv[2 + i] );
		if( ps[i].bytes == NULL ) { fprintf( stderr, "decomp_time: failed to load '%s'\n", argv[2 + i] ); return 1; }
		total_px += (uint64_t)ps[i].width * ps[i].height;
	}

	double samples[64];
	for( int r = 0; r < runs; r++ )
	{
		double t0 = now_s();
		for( int i = 0; i < nf; i++ )
		{
			uint32_t* out = pep_decompress( &ps[i], pep_rgba, 0, 0 );
			// touch one byte so the decode isn't optimized away, then free
			if( out ) { volatile uint32_t sink = out[0]; (void)sink; free( out ); }
		}
		double t1 = now_s();
		samples[r] = t1 - t0;
		printf( "  sample %d: %.6f s\n", r + 1, samples[r] );
	}

	qsort( samples, runs, sizeof( double ), cmp_d );
	double median = samples[runs / 2];
	printf( "decompress_median %.6f s  (%d files, %llu px, %d runs)\n",
	        median, nf, (unsigned long long)total_px, runs );

	for( int i = 0; i < nf; i++ ) pep_free( &ps[i] );
	free( ps );
	return 0;
}
