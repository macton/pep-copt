// compare_results.c — the core of the PEP harness: a DECOMPRESSED-PIXEL comparator.
//
// Usage: compare_results <reference-results> <optimized-results>
//
// Match criterion (RELAXED from byte-identical .pep to pixel-identical output):
// the optimized compressor MAY use a different file format / entropy coder than
// the reference. What must hold for every image is that, once DECOMPRESSED, the
// pixels are identical to the reference's decompressed pixels (which equal the
// quantized input — PEP is lossless on palettized pixels). The .pep bytes
// themselves are NOT compared.
//
// The harness already does the decompression: perf_main decompresses each .pep
// it writes (with the matching library — reference decompresses reference .pep,
// optimized decompresses optimized .pep), verifies the round-trip is lossless
// against the quantized input (round_trip_ok), and records a digest of the
// decompressed pixels (decoded_fnv). This comparator therefore compares the
// DECOMPRESSED-pixel digests (not the compressed bytes) and requires a lossless
// round-trip on both sides:
//
//   - round_trip_ok == 1 on BOTH sides   (each format losslessly reproduces the
//                                          quantized input — a full memcmp done
//                                          in-harness)
//   - decoded_fnv_ref == decoded_fnv_opt  (the two decompressed pixel streams are
//                                          identical)
//   - same width/height
//
// Compressed size (pep_bytes), palette size, and model id MAY now differ between
// the two builds; those are reported as information, not gated.
//
// Loud failure (exit 2) on malformed lines, differing image counts, or a row
// that does not line up by index + basename. Exit 0 only on a full PASS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct
{
	int idx, w, h, colors, model, rt_ok;
	unsigned long long pep_bytes, pep_fnv, decoded_fnv;
	char pep_path[1024];
} row;

static int parse_file( const char* path, row** out_rows, int* out_n )
{
	FILE* f = fopen( path, "r" );
	if( !f ) { fprintf( stderr, "compare_results: cannot open '%s'\n", path ); return 0; }
	row* rows = NULL;
	int n = 0, cap = 0, lineno = 0;
	char line[1200];
	while( fgets( line, sizeof line, f ) )
	{
		lineno++;
		if( line[0] == '\n' || line[0] == '\0' ) continue;
		if( n == cap ) { cap = cap ? cap * 2 : 16; rows = realloc( rows, cap * sizeof( row ) ); }
		row r;
		int got = sscanf( line, "%d %d %d %d %d %llu %llx %llx %d %1023s",
		                  &r.idx, &r.w, &r.h, &r.colors, &r.model,
		                  &r.pep_bytes, &r.pep_fnv, &r.decoded_fnv, &r.rt_ok, r.pep_path );
		if( got != 10 )
		{
			fprintf( stderr, "compare_results: malformed line %d in '%s' (got %d fields)\n",
			         lineno, path, got );
			free( rows );
			fclose( f );
			return 0;
		}
		rows[n++] = r;
	}
	fclose( f );
	*out_rows = rows;
	*out_n = n;
	return 1;
}

static const char* base_of( const char* p )
{
	const char* s = strrchr( p, '/' );
	return s ? s + 1 : p;
}

int main( int argc, char** argv )
{
	if( argc != 3 )
	{
		fprintf( stderr, "usage: %s <reference-results> <optimized-results>\n", argv[0] );
		return 2;
	}
	row *ref = NULL, *opt = NULL;
	int nref = 0, nopt = 0;
	if( !parse_file( argv[1], &ref, &nref ) ) return 2;
	if( !parse_file( argv[2], &opt, &nopt ) ) return 2;

	if( nref != nopt )
	{
		fprintf( stderr, "compare_results: image count differs (ref %d, opt %d)\n", nref, nopt );
		return 2;
	}

	int rt_failures = 0;      // round-trip not lossless on either side
	int pixel_mismatches = 0; // decompressed pixels differ between ref and opt
	int dim_mismatches = 0;   // width/height differ
	int size_regressions = 0; // optimized .pep LARGER than reference .pep
	long long max_size_regression = 0; // worst (opt - ref) in bytes
	unsigned long long ref_total = 0, opt_total = 0;

	for( int i = 0; i < nref; i++ )
	{
		if( ref[i].idx != opt[i].idx
		    || strcmp( base_of( ref[i].pep_path ), base_of( opt[i].pep_path ) ) != 0 )
		{
			fprintf( stderr, "compare_results: row %d misaligned (ref idx %d '%s' vs opt idx %d '%s')\n",
			         i, ref[i].idx, base_of( ref[i].pep_path ), opt[i].idx, base_of( opt[i].pep_path ) );
			return 2;
		}

		if( !ref[i].rt_ok || !opt[i].rt_ok ) rt_failures++;
		if( ref[i].w != opt[i].w || ref[i].h != opt[i].h ) dim_mismatches++;
		// THE match check: decompressed pixels must be identical (not the .pep bytes).
		if( ref[i].decoded_fnv != opt[i].decoded_fnv ) pixel_mismatches++;

		// SIZE check: the optimized .pep may differ from the reference, but it must
		// NOT be larger (otherwise "faster" could just be a cheaper-but-bigger
		// encoding, defeating the point of a compressor).
		if( opt[i].pep_bytes > ref[i].pep_bytes )
		{
			size_regressions++;
			long long delta = (long long)opt[i].pep_bytes - (long long)ref[i].pep_bytes;
			if( delta > max_size_regression ) max_size_regression = delta;
		}

		ref_total += ref[i].pep_bytes;
		opt_total += opt[i].pep_bytes;
	}

	int pass = ( rt_failures == 0 && pixel_mismatches == 0 && dim_mismatches == 0
	             && size_regressions == 0 );

	printf( "images %d\n", nref );
	printf( "round_trip_failures %d\n", rt_failures );
	printf( "dim_mismatches %d\n", dim_mismatches );
	printf( "pixel_mismatches %d\n", pixel_mismatches );
	// Size: optimized .pep must be <= reference .pep for every image.
	printf( "size_regressions %d\n", size_regressions );
	printf( "max_size_regression_bytes %lld\n", max_size_regression );
	printf( "ref_total_pep_bytes %llu\n", ref_total );
	printf( "opt_total_pep_bytes %llu\n", opt_total );
	if( ref_total > 0 )
		printf( "opt_size_vs_ref %.4f\n", (double)opt_total / (double)ref_total );
	printf( "%s\n", pass ? "PASS" : "FAIL" );

	free( ref );
	free( opt );
	return pass ? 0 : 1;
}
