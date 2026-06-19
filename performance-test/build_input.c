// build_input.c — build-stage / pre-processing step (EXCLUDED from the timed
// compression measurement).
//
// Usage: build_input <images-dir> <out-qdir> <list-out> [max_colors]
//
// Enumerates the image files in <images-dir> (jpg/jpeg/png/tga/bmp/gif), sorts
// them by name for deterministic order, loads each, quantizes it to
// <= max_colors (default 256) with deterministic median-cut, and writes
// <out-qdir>/<name>.qbin. Appends each .qbin path to <list-out>.
//
// This is a GENERAL transform of any valid input directory — it is not keyed to
// the committed images. Quantization is pre-processing; the timing harness
// consumes the .qbin and times only pep_compress.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pep_codec.h"

static int has_image_ext( const char* name )
{
	const char* dot = strrchr( name, '.' );
	if( !dot ) return 0;
	const char* exts[] = { ".jpg", ".jpeg", ".png", ".tga", ".bmp", ".gif", NULL };
	char low[16];
	size_t n = strlen( dot );
	if( n >= sizeof low ) return 0;
	for( size_t i = 0; i <= n; i++ )
		low[i] = ( dot[i] >= 'A' && dot[i] <= 'Z' ) ? (char)( dot[i] + 32 ) : dot[i];
	for( int i = 0; exts[i]; i++ )
		if( strcmp( low, exts[i] ) == 0 ) return 1;
	return 0;
}

static int cmp_str( const void* a, const void* b )
{
	return strcmp( *(const char* const*)a, *(const char* const*)b );
}

int main( int argc, char** argv )
{
	if( argc < 4 || argc > 5 )
	{
		fprintf( stderr, "usage: %s <images-dir> <out-qdir> <list-out> [max_colors]\n", argv[0] );
		return 2;
	}
	const char* img_dir = argv[1];
	const char* out_dir = argv[2];
	const char* list_out = argv[3];
	int max_colors = ( argc == 5 ) ? atoi( argv[4] ) : 256;

	mkdir( out_dir, 0777 );

	DIR* d = opendir( img_dir );
	if( !d ) { fprintf( stderr, "cannot open dir '%s'\n", img_dir ); return 2; }

	char** names = NULL;
	size_t count = 0, cap = 0;
	struct dirent* e;
	while( ( e = readdir( d ) ) != NULL )
	{
		if( !has_image_ext( e->d_name ) ) continue;
		if( count == cap )
		{
			cap = cap ? cap * 2 : 16;
			names = (char**)realloc( names, cap * sizeof( char* ) );
		}
		names[count++] = strdup( e->d_name );
	}
	closedir( d );

	if( count == 0 ) { fprintf( stderr, "no images found in '%s'\n", img_dir ); return 2; }
	qsort( names, count, sizeof( char* ), cmp_str );

	FILE* list = fopen( list_out, "w" );
	if( !list ) { fprintf( stderr, "cannot open list '%s'\n", list_out ); return 2; }

	int rc = 0;
	for( size_t i = 0; i < count; i++ )
	{
		char in_path[1024], qbin_path[1024];
		snprintf( in_path, sizeof in_path, "%s/%s", img_dir, names[i] );
		snprintf( qbin_path, sizeof qbin_path, "%s/%s.qbin", out_dir, names[i] );

		pepc_image img;
		if( !pepc_load( in_path, &img ) ) { rc = 1; continue; }
		if( !pepc_quantize( &img, max_colors ) ) { pepc_free( &img ); rc = 1; continue; }
		if( !pepc_write_qbin( qbin_path, &img ) ) { pepc_free( &img ); rc = 1; continue; }
		fprintf( list, "%s\n", qbin_path );
		printf( "quantized %s -> %s  (%dx%d, %d colors)\n",
		        names[i], qbin_path, img.width, img.height, img.colors );
		pepc_free( &img );
		free( names[i] );
	}
	fclose( list );
	free( names );
	return rc;
}
