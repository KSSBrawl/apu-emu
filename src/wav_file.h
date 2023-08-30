#ifndef WAV_FILE_H
#define WAV_FILE_H

#include <stdio.h>

#define WAV_FMT_PCM_INT		1
#define WAV_FMT_ADPCM		2
#define WAV_FMT_PCM_FLOAT	3
#define WAV_FMT_A_LAW		6
#define WAV_FMT_U_LAW		7

FILE	*wav_file_open( char *filename, int sample_rate, int format, int bit_depth, int num_channels );
void	wav_file_write_samples( FILE *stream, void *samples, size_t len );
void	wav_file_close( FILE *stream );

#endif // WAV_FILE_H
