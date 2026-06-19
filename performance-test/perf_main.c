// perf_main.c — PEP timing harness (shared by the reference and optimized
// builds; the ONLY difference between the two builds is the include path, which
// selects src/pep.h vs src-optimized/pep.h — handled by -I in the Makefiles).
//
// Usage: perf_harness <qbin-list> <out-dir> <results.txt>
//
//   <qbin-list>  text file, one .qbin path per line (produced by build_input;
//                each .qbin holds an already-quantized RGBA image)
//   <out-dir>    directory to write <name>.pep files into
//   <results.txt> manifest, one line per image (fixed parseable format)
//
// For each image the harness:
//   1. reads the .qbin pixels                              (NOT timed)
//   2. pep_compress(...)                                   (TIMED — the metric)
//   3. pep_save(...) the .pep file                         (NOT timed)
//   4. pep_decompress(...) and verifies the result is byte-identical to the
//      input pixels (lossless round-trip)                  (NOT timed)
//
// Pre-processing (load + quantize) happened in build_input and is excluded.
// Only the cumulative pep_compress time is reported, as
// "total_compress_seconds <value>" — the single number the measurement
// protocol parses.
//
// Manifest line (space-separated, path last; our names contain no spaces):
//   <idx> <w> <h> <colors> <model> <pep_bytes> <pep_fnv> <decoded_fnv> <rt_ok> <pep_path>

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pep.h"
#include "pep_codec.h"

static double now_s( void )
{
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
}

// Derive "<out-dir>/<basename-without-.qbin>.pep" from a qbin path.
static void pep_path_for( const char* qbin, const char* out_dir, char* out, size_t out_sz )
{
	const char* base = strrchr( qbin, '/' );
	base = base ? base + 1 : qbin;
	char stem[512];
	snprintf( stem, sizeof stem, "%s", base );
	char* dot = strrchr( stem, '.' );
	if( dot && strcmp( dot, ".qbin" ) == 0 ) *dot = '\0';
	snprintf( out, out_sz, "%s/%s.pep", out_dir, stem );
}

int main( int argc, char** argv )
{
	if( argc != 4 )
	{
		fprintf( stderr, "usage: %s <qbin-list> <out-dir> <results.txt>\n", argv[0] );
		return 2;
	}
	const char* list_path = argv[1];
	const char* out_dir   = argv[2];
	const char* res_path  = argv[3];

	mkdir( out_dir, 0777 );  // ignore EEXIST

	FILE* list = fopen( list_path, "r" );
	if( !list ) { fprintf( stderr, "cannot open list '%s'\n", list_path ); return 2; }
	FILE* res = fopen( res_path, "w" );
	if( !res ) { fprintf( stderr, "cannot open results '%s'\n", res_path ); fclose( list ); return 2; }

	double total_compress = 0.0;
	int idx = 0, failures = 0;
	char line[1024];

	while( fgets( line, sizeof line, list ) )
	{
		// strip trailing newline / whitespace
		size_t L = strlen( line );
		while( L > 0 && ( line[L-1] == '\n' || line[L-1] == '\r' || line[L-1] == ' ' ) ) line[--L] = '\0';
		if( L == 0 ) continue;

		pepc_image img;
		if( !pepc_read_qbin( line, &img ) ) { failures++; continue; }
		uint32_t area = (uint32_t)img.width * (uint32_t)img.height;

		// ---- TIMED: compression only -----------------------------------
		double t0 = now_s();
		pep p = pep_compress( (const uint32_t*)img.rgba, (uint16_t)img.width,
		                      (uint16_t)img.height, pep_rgba, pep_8bit );
		double t1 = now_s();
		total_compress += ( t1 - t0 );

		if( p.bytes == NULL )
		{
			fprintf( stderr, "pep_compress failed for '%s'\n", line );
			pepc_free( &img );
			failures++;
			continue;
		}

		char pep_path[1024];
		pep_path_for( line, out_dir, pep_path, sizeof pep_path );
		if( !pep_save( &p, pep_path ) )
		{
			fprintf( stderr, "pep_save failed for '%s'\n", pep_path );
			pep_free( &p );
			pepc_free( &img );
			failures++;
			continue;
		}

		// ---- round-trip: decompress must equal the input pixels ---------
		uint32_t* back = pep_decompress( &p, pep_rgba, 0, 0 );
		int rt_ok = ( back != NULL ) && ( memcmp( back, img.rgba, (size_t)area * 4 ) == 0 );
		uint64_t decoded_fnv = back ? pepc_fnv1a( back, (size_t)area * 4 ) : 0;
		if( !rt_ok ) failures++;

		uint64_t pep_fnv = 0;
		pepc_fnv1a_file( pep_path, &pep_fnv );

		fprintf( res, "%04d %d %d %d %u %llu %016llx %016llx %d %s\n",
		         idx, img.width, img.height, img.colors, (unsigned)p.model,
		         (unsigned long long)p.bytes_size,
		         (unsigned long long)pep_fnv, (unsigned long long)decoded_fnv,
		         rt_ok, pep_path );

		free( back );
		pep_free( &p );
		pepc_free( &img );
		idx++;
	}

	fclose( res );
	fclose( list );

	printf( "images_compressed %d\n", idx );
	printf( "round_trip_failures %d\n", failures );
	printf( "total_compress_seconds %.6f\n", total_compress );

	return failures ? 1 : 0;
}
