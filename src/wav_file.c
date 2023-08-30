#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wav_file.h"

static struct {
	char			magic1[4];
	uint32_t		file_size;
	char			magic2[4];
	char			magic3[4];
	uint32_t		subchunk_1_size;
	uint16_t		format;
	uint16_t		num_channels;
	uint32_t		sample_rate;
	uint32_t		byte_rate;
	uint16_t		block_align;
	uint16_t		bit_depth;
	char			magic4[4];
	uint32_t		subchunk_2_size;
} wav_header;

static size_t out_size = 0;

FILE *
wav_file_open( char *filename, int sample_rate, int format, int bit_depth, int num_channels )
{
	FILE *wav_f = fopen( filename, "wb" );

	if ( !wav_f )
	{
		fprintf( stderr, "%s: Could not open \"%s\" for writing\n", __func__, filename );
		exit( EXIT_FAILURE );
	}

	memset( &wav_header, 0, sizeof(wav_header) );
	out_size = 0;

	wav_header.subchunk_1_size	= 16;
	wav_header.format			= format;
	wav_header.num_channels		= num_channels;
	wav_header.sample_rate		= sample_rate;
	wav_header.byte_rate		= sample_rate * bit_depth;
	wav_header.block_align		= bit_depth / 8;
	wav_header.bit_depth		= bit_depth;

	// temporary, will be overwritten later
	fwrite( &wav_header, sizeof(wav_header), 1, wav_f );
	return wav_f;
}

void
wav_file_write_samples( FILE *stream, void *samples, size_t len )
{
	fwrite( samples, 1, len, stream );
	out_size += len;
}

void
wav_file_close( FILE *stream )
{
	wav_header.magic1[0]		= 'R';
	wav_header.magic1[1]		= 'I';
	wav_header.magic1[2]		= 'F';
	wav_header.magic1[3]		= 'F';

	wav_header.file_size		= out_size + 36;
	
	wav_header.magic2[0]		= 'W';
	wav_header.magic2[1]		= 'A';
	wav_header.magic2[2]		= 'V';
	wav_header.magic2[3]		= 'E';

	wav_header.magic3[0]		= 'f';
	wav_header.magic3[1]		= 'm';
	wav_header.magic3[2]		= 't';
	wav_header.magic3[3]		= ' ';

	wav_header.magic4[0]		= 'd';
	wav_header.magic4[1]		= 'a';
	wav_header.magic4[2]		= 't';
	wav_header.magic4[3]		= 'a';

	wav_header.subchunk_2_size	= out_size;

	rewind( stream );
	fwrite( &wav_header, sizeof(wav_header), 1, stream );
	fclose( stream );
}
