#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "bus.h"
#include "display.h"
#include "apu.h"
#include "ppmck_driver.h"
#include "wav_file.h"
#include "SDL2/SDL.h"

int
main( int argc, char *argv[] )
{
	(void)argc;
	(void)argv;

	FILE *song_f = fopen( "aibomb.bin", "rb" );

	if ( !song_f )
	{
		fprintf( stderr, "Could not open \"aibomb.bin\" to play back\n" );
		exit( EXIT_FAILURE );
	}

	size_t bytes_read = fread( &cpu_bus[0x8000], 1, 0x8000, song_f );
	fclose( song_f );

	if ( bytes_read != 0x8000 )
	{
		fprintf( stderr, "Failed to read \"aibomb.bin\" into buffer\n" );
		exit( EXIT_FAILURE );
	}

	SDL_Init( SDL_INIT_AUDIO | SDL_INIT_VIDEO );
	atexit( SDL_Quit );

	apu_init();

	display_init();	
	audio_init();
	audio_start_playback();

	int stop = 0;
	SDL_Event event;

	for ( ; ; )
	{	
		while ( SDL_PollEvent( &event ) )
		{
			if ( event.type == SDL_QUIT )
				stop = 1;
		}
		if ( stop ) break;

		audio_run_2a03();
		display_update();
		
		// sleep for a teensy bit so we don't totally consume the core
		SDL_Delay( 10 );
	}

	wav_file_close( audio_out );
	return 0;
}
