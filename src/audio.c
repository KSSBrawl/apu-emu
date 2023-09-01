#include <stdlib.h>

#include "audio.h"
#include "apu.h"
#include "ppmck_driver.h"
#include "wav_file.h"
#include "SDL2/SDL_audio.h"

float sample_buffer[SAMPLE_RATE / 60];
static SDL_AudioDeviceID device;

FILE *audio_out;

void
audio_run_2a03()
{
	static int cpu_cycle = 0;

	if ( SDL_GetQueuedAudioSize( device ) > sizeof(sample_buffer) )
		return;

	size_t i = 0;

	while ( i < sizeof(sample_buffer) / sizeof(float) )
	{
		if ( apu_clock( &sample_buffer[i], NULL ) )
			i++;

		if ( cpu_cycle == 0 )
			sound_driver_start();

		if ( cpu_cycle++ == 29780 )
			cpu_cycle = 0;
	}

	SDL_QueueAudio( device, sample_buffer, sizeof(sample_buffer) );
	wav_file_write_samples( audio_out, sample_buffer, sizeof(sample_buffer) );
}

void
audio_init()
{
	static SDL_AudioSpec *desired, *got;

	desired	= malloc( sizeof(SDL_AudioSpec) );
	got		= malloc( sizeof(SDL_AudioSpec) );

	desired->freq		= SAMPLE_RATE;
	desired->format		= AUDIO_F32SYS;
	desired->channels	= 1;
	desired->samples	= sizeof(sample_buffer) / sizeof(float);
	desired->callback	= NULL;
	desired->userdata	= NULL;

	device = SDL_OpenAudioDevice( NULL, 0, desired, got, 0 );
	free( desired );
	free( got );

	sound_init();

	audio_out = wav_file_open( "audio_out.wav", SAMPLE_RATE, WAV_FMT_PCM_FLOAT, 32, 1 );
	audio_pull_flag = 0;
}

void
audio_start_playback()
{
	SDL_PauseAudioDevice( device, 0 );
}
