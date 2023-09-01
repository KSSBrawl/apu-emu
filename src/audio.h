#ifndef AUDIO_H
#define AUDIO_H

#define SAMPLE_RATE 48000

#include <stdio.h>

extern float sample_buffer[SAMPLE_RATE / 60];
extern FILE *audio_out;

void audio_init();
void audio_start_playback();
void audio_run_2a03();

#endif // AUDIO_H
