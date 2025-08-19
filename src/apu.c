#include "mmu.h"

#define CPU_FREQUENCY (4 * 1024 * 1024)
#define AUDIO_SAMPLE_RATE 48000
#define VOLUME 10000
#define SQUARE_WAVE_FREQUENCY 300

typedef int16_t AudioSample;

unsigned int position = 0;
void apu_generate_frames(void *buffer, unsigned int frame_count) {
	AudioSample *samples = (AudioSample *)buffer;
	int period = AUDIO_SAMPLE_RATE / SQUARE_WAVE_FREQUENCY;

	for (int i = 0; i < frame_count; i++) {
		if (position < period / 2) {
			samples[i] = VOLUME;
			position++;
		}
		else if (position < period) {
			samples[i] = -VOLUME;
			position++;
		}
		else {
			samples[i] = -VOLUME;
			position = 0;
		}
	}
}