#include "mmu.h"
#include "string.h"

#define CPU_FREQUENCY (4 * 1024 * 1024)
#define AUDIO_SAMPLE_RATE 48000
#define VOLUME 10000
#define SQUARE_WAVE_FREQUENCY 300

typedef int16_t AudioSample;

struct SquareWave {
	// NR11 register
	uint8_t length_timer : 6;
	uint8_t wave_duty : 2;

	// NR12 register
	uint8_t sweep_pace : 3;
	uint8_t envelope_direction : 1;
	uint8_t initial_volume : 4;

	// NR13
	uint8_t period_low : 8; // lower 8 bits

	// NR14
	uint8_t period_high : 3; // upper 3 bits
	uint8_t : 3;
	uint8_t length_enable : 1;
	uint8_t trigger : 1;

	uint16_t frequency;
	int ticks;
	int duty_step;
};

const bool duty_cycle_form[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 1, 1, 1},
	{0, 1, 1, 1, 1, 1, 1, 0},
};

struct SquareWave get_square_wave_info(uint16_t address_start) {
	uint8_t audio_registers[4];
	audio_registers[0] = mmu_read(address_start);
	audio_registers[1] = mmu_read(address_start + 1);
	audio_registers[2] = mmu_read(address_start + 2);
	audio_registers[3] = mmu_read(address_start + 3);

	struct SquareWave wave_info = { 0 };
	memcpy(&wave_info, audio_registers, sizeof(audio_registers));
	uint16_t period_value = (wave_info.period_high << 8) | wave_info.period_low;
	wave_info.frequency = 131072 / (2048 - period_value);
	return wave_info;
}

AudioSample generate_channel1(unsigned int frame_count) {
	struct SquareWave channel1 = get_square_wave_info(0xFF11);

	channel1.ticks++;
	if (channel1.ticks > CPU_FREQUENCY / (channel1.frequency / 8)) {
		channel1.duty_step = (channel1.duty_step + 1) % 8;
		channel1.ticks = 0;
	}

	AudioSample sample;
	if (duty_cycle_form[channel1.wave_duty][channel1.duty_step])
		sample = VOLUME;
	else
		sample = -VOLUME;

	return sample;
}

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