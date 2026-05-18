#include "mmu.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define APU_TICK_RATE (1 << 21)
#define AUDIO_QUEUE_FRAME_SIZE 8192
#define INITIAL_TARGET_SAMPLE_RATE 48000

#define APU_REGISTER_START  0xFF10
#define APU_REGISTER_LENGTH 0x0030
#define APU_WAVE_DATA_START 0xFF30

int TARGET_SAMPLE_RATE = INITIAL_TARGET_SAMPLE_RATE;
float DECIMATION_FACTOR = ((float)APU_TICK_RATE / (float)INITIAL_TARGET_SAMPLE_RATE);
float decimation_counter = 0.0;

typedef struct {
	float left;
	float right;
} AudioFrame;

typedef struct {
	int left;
	int right;
} IntegerAudioFrame;

struct AudioQueue {
	AudioFrame frames[AUDIO_QUEUE_FRAME_SIZE];
	unsigned start;
	unsigned end;
	unsigned size;
	unsigned capacity;
} audio_fifo = {
	.capacity = AUDIO_QUEUE_FRAME_SIZE,
};

typedef struct AudioQueue AudioQueue;

IntegerAudioFrame apu_generate_frame(void);
IntegerAudioFrame highpass_filter(IntegerAudioFrame frame);
IntegerAudioFrame lowpass_filter(IntegerAudioFrame frame);

void apu_set_audio_sample_rate(unsigned new_sample_rate) {
	TARGET_SAMPLE_RATE = new_sample_rate;
	DECIMATION_FACTOR = ((float)APU_TICK_RATE / (float)new_sample_rate);
}

void queue_clear(AudioQueue *queue) {
	memset(queue, 0, sizeof(AudioQueue));
	queue->capacity = AUDIO_QUEUE_FRAME_SIZE;
}

unsigned queue_size(AudioQueue *queue) {
	return queue->size;
}

unsigned apu_audio_available(void) {
	return queue_size(&audio_fifo);
}

void queue_push(AudioQueue *queue, AudioFrame frame) {
	if (queue->size == queue->capacity) {
		printf("Audio Frame was dropped because the queue was full.\n");
		return;
	}
	queue->frames[queue->end] = frame;
	queue->size++;
	queue->end++;
	queue->end %= queue->capacity;
}

AudioFrame queue_pop(AudioQueue *queue) {
	if (queue->size == 0) {
		printf("Audio Queue was popped, but returned an empty frame");
		return (AudioFrame){ 0 };
	}
	AudioFrame frame = queue->frames[queue->start];
	queue->frames[queue->end] = frame;
	queue->size--;
	queue->start++;
	queue->start %= queue->capacity;
	return frame;
}

void queue_drain(AudioQueue *queue, float* output, unsigned count) {
	unsigned bytes_per_frame = sizeof(AudioFrame);
	if (queue->start + count > queue->capacity) {
		unsigned until_end = queue->capacity - queue->start;
		memcpy(output, queue->frames + queue->start, until_end * bytes_per_frame);
		memcpy(output + 2 * until_end, queue->frames, (count - until_end) * bytes_per_frame);
	} else {
		memcpy(output, queue->frames + queue->start, count * bytes_per_frame);
	}
	queue->size -= count;
	queue->start += count;
	queue->start %= queue->capacity;
}

unsigned apu_read_audio(float *output, unsigned max_frames) {
	if (max_frames > apu_audio_available())
		max_frames = apu_audio_available();
	queue_drain(&audio_fifo, output, max_frames);
	return max_frames;
}

struct Channel {
	// All channels
	bool enabled;
	bool dac_enabled;
	unsigned ticks;
	unsigned period_value;

	// All channels
	bool length_enabled;
	unsigned length_current;

	// Channels 1, 2, and 4
	unsigned volume_initial;
	unsigned volume_current;
	unsigned envelope_current;
	unsigned envelope_pace;
	bool envelope_direction;

	// Channels 1 and 2
	unsigned duty_wave_type;
	unsigned duty_wave_index;

	// Channel 1 only
	unsigned sweep_current;
	bool sweep_direction;
	unsigned sweep_step;
	unsigned sweep_pace;

	// Channel 3 only
	unsigned volume_level;
	unsigned wave_index;

	// Channel 4 only
	uint16_t lfsr;
	bool     lfsr_width;
	unsigned lfsr_clock_shift;
	unsigned lfsr_clock_divider;
};

struct HagemuAPU {
	struct Channel ch1;
	struct Channel ch2;
	struct Channel ch3;
	struct Channel ch4;
	unsigned ticks;
	unsigned frame_sequencer_clock_step;
	uint8_t wave_data[16];
	uint8_t raw_regs[APU_REGISTER_LENGTH];
	uint8_t volume_left;
	uint8_t volume_right;
	bool enabled;
	bool ch1_output_right;
	bool ch1_output_left;
	bool ch2_output_right;
	bool ch2_output_left;
	bool ch3_output_right;
	bool ch3_output_left;
	bool ch4_output_right;
	bool ch4_output_left;
} apu = { 0 };

void apu_reset(void) {
	memset(&apu.ch1, 0, sizeof(struct Channel));
	memset(&apu.ch2, 0, sizeof(struct Channel));
	memset(&apu.ch3, 0, sizeof(struct Channel));
	memset(&apu.ch4, 0, sizeof(struct Channel));
	queue_clear(&audio_fifo);
}

void apu_channel_reset(struct Channel *channel) {
	memset(channel, 0, sizeof(struct Channel));
}

void tick_length_timer(struct Channel *channel) {
	if (!channel->length_enabled)
		return;

	channel->length_current--;
	if (channel->length_current == 0) {
		channel->enabled = false;
	}
}

void tick_sweep(struct Channel *channel) {
	if (!channel->sweep_pace)
		return;

	channel->sweep_current++;
	if (channel->sweep_current == channel->sweep_pace) {
		channel->sweep_current = 0;

		int period_adjustment;
		if (channel->sweep_direction == 0)
			period_adjustment = channel->period_value >> channel->sweep_step;
		else
			period_adjustment = - (channel->period_value >> channel->sweep_step);

		// Period value overflowed
		if (channel->period_value + period_adjustment > 0x7FF)
			channel->enabled = false;
		else
			channel->period_value += period_adjustment;
	}
}

void tick_envelope(struct Channel *channel) {
	if (!channel->envelope_pace)
		return;

	channel->envelope_current++;
	if (channel->envelope_current == channel->envelope_pace) {
		channel->envelope_current = 0;
		if (channel->envelope_direction && channel->volume_current < 15)
			channel->volume_current++;
		else if (!channel->envelope_direction && channel->volume_current > 0)
			channel->volume_current--;
	}
}

void tick_pulse_channel(struct Channel *channel) {
	channel->ticks++;
	uint32_t period = 2 * (2048 - channel->period_value);
	if (channel->ticks >= period ) {
		channel->ticks -= period;
		channel->duty_wave_index++;
		channel->duty_wave_index %= 8;
	}
}

void tick_wave_channel(struct Channel *channel) {
	channel->ticks++;
	uint32_t period = 2048 - channel->period_value;
	if (channel->ticks >= period) {
		channel->ticks -= period;
		channel->wave_index++;
		channel->wave_index %= 32;
	}
}

void tick_noise_channel(struct Channel *channel) {
	channel->ticks++;
	uint32_t period = channel->period_value;
	if (channel->ticks > period) {
		channel->ticks -= period;
		bool next_bit = channel->lfsr & 0x01;
		channel->lfsr >>= 1;

		next_bit ^= (channel->lfsr & 0x01);
		next_bit = !next_bit;

		// Since we already shifted, we copy to the 14 bit (and maybe the 6th bit)
		channel->lfsr &= ~(1 << 14);
		channel->lfsr |= (next_bit << 14);
		if (channel->lfsr_width) {
			channel->lfsr &= ~(1 << 6);
			channel->lfsr |= (next_bit << 6);
		}
	}
}

// The APU ticks twice per M-cycle (approximation 2MHz)
void apu_tick_once(void) {
	apu.ticks++;

	tick_pulse_channel(&apu.ch1);
	tick_pulse_channel(&apu.ch2);
	tick_wave_channel(&apu.ch3);
	tick_noise_channel(&apu.ch4);

	// The frame frequencer ticks at 512 Hz
	if (apu.ticks == (APU_TICK_RATE / 512)) {
		apu.ticks = 0;
		apu.frame_sequencer_clock_step++;
		apu.frame_sequencer_clock_step %= 8;

		switch (apu.frame_sequencer_clock_step) {

		case 2: case 6:
			tick_sweep(&apu.ch1);
			// FALL THROUGH ON PURPOSE

		case 0: case 4:
			tick_length_timer(&apu.ch1);
			tick_length_timer(&apu.ch2);
			tick_length_timer(&apu.ch3);
			tick_length_timer(&apu.ch4);
			break;

		case 1: case 3: case 5:
			break;

		case 7:
			tick_envelope(&apu.ch1);
			tick_envelope(&apu.ch2);
			tick_envelope(&apu.ch4);
			break;
		}
	}

	IntegerAudioFrame current_frame = apu_generate_frame();
	static IntegerAudioFrame accumulate = { 0 };
	decimation_counter += 1.0;

	if (decimation_counter < DECIMATION_FACTOR) {
		accumulate.left  += current_frame.left;
		accumulate.right += current_frame.right;
		return;
	}

	float leftover = decimation_counter - DECIMATION_FACTOR;
	float step = 1.0 - leftover;
	accumulate.left  += current_frame.left  * step;
	accumulate.right += current_frame.right * step;
	accumulate.left  *= (apu.volume_left  + 1);
	accumulate.right *= (apu.volume_right + 1);
	accumulate = lowpass_filter(accumulate);
	accumulate = highpass_filter(accumulate);

	// Normalize to [-1.0, 1.0]
	AudioFrame output;
	output.left  = accumulate.left  / (240.0 * DECIMATION_FACTOR);
	output.right = accumulate.right / (240.0 * DECIMATION_FACTOR);
	queue_push(&audio_fifo, output);

	decimation_counter = leftover;
	accumulate.left  = current_frame.left  * leftover;
	accumulate.right = current_frame.right * leftover;
}

void apu_tick(void) {
	apu_tick_once();
	apu_tick_once();
}

uint8_t channel_output_pulse(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	static const bool duty_wave_forms[4][8] = {
		{0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 1, 1, 1},
		{0, 1, 1, 1, 1, 1, 1, 0},
	};

 	if (duty_wave_forms[channel->duty_wave_type][channel->duty_wave_index])
		return channel->volume_current;
 	else
		return 0;
}

uint8_t channel_output_wave(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

	uint8_t data = apu.wave_data[channel->wave_index / 2];
	if (channel->wave_index % 2 == 0)
		data >>= 4;
	else
		data &= 0x0F;

	if (channel->volume_level)
		return data >> (channel->volume_level - 1);
	else
		return 0;
}

uint8_t channel_output_noise(struct Channel *channel) {
	if (!channel->dac_enabled || !channel->enabled)
		return 0;

 	if (channel->lfsr & 0x01)
		return channel->volume_current;
 	else
		return 0;
}

IntegerAudioFrame apu_generate_frame(void) {
	IntegerAudioFrame frame = { 0 };
	if (!apu.enabled)
		return frame;

	// Each channel outputs an integer in [0, 15]
	int ch1 = channel_output_pulse(&apu.ch1);
	int ch2 = channel_output_pulse(&apu.ch2);
	int ch3 = channel_output_wave(&apu.ch3);
	int ch4 = channel_output_noise(&apu.ch4);

	frame.left = apu.ch1_output_left * ch1
		+ apu.ch2_output_left * ch2
		+ apu.ch3_output_left * ch3
		+ apu.ch4_output_left * ch4;

	frame.right = apu.ch1_output_right * ch1
		+ apu.ch2_output_right * ch2
		+ apu.ch3_output_right * ch3
		+ apu.ch4_output_right * ch4;

	// Normalize to [-30, 30]
	frame.left  -= 30;
	frame.right -= 30;

	return frame;
}

// Alpha should be 1 - exp(-2 * pi * cutoff_freqency / sample_rate)
/* const float alpha = 0.730f; // 48kHz sample rate, 10kHz cutoff */
/* const float alpha = 0.649f; // 48kHz sample rate, 8kHz cutoff */
IntegerAudioFrame lowpass_filter(IntegerAudioFrame frame) {
	static IntegerAudioFrame prev_frame = { 0 };
	IntegerAudioFrame frame_diff;
	frame_diff.left  = frame.left  - prev_frame.left;
	frame_diff.right = frame.right - prev_frame.right;

	// This effectively multiplies by 0.6485
	frame_diff.left  *= 664;
	frame_diff.right *= 664;
	frame_diff.left  /= 1024;
	frame_diff.right /= 1024;

	prev_frame.left  += frame_diff.left;
	prev_frame.right += frame_diff.right;
	return prev_frame;
}

// Emulates the DC Blocking of the gameboy
/* const float R = 0.996f; */
IntegerAudioFrame highpass_filter(IntegerAudioFrame frame) {
	static IntegerAudioFrame prev_input  = { 0 };
	static IntegerAudioFrame prev_output = { 0 };

	// This effectively multiplies by 0.9961
	prev_output.left   *= 1020;
	prev_output.right  *= 1020;
	prev_output.left   /= 1024;
	prev_output.right  /= 1024;
	IntegerAudioFrame output_frame = {
		.left  = frame.left  - prev_input.left  + prev_output.left,
		.right = frame.right - prev_input.right + prev_output.right,
	};
	prev_input  = frame;
	prev_output = output_frame;
	return output_frame;
}

// Use bit shifting and bitmasks to get the value of the
// bits between bit_start and bit_end (both inclusive)
static inline unsigned get_bits(unsigned value, unsigned bit_start, unsigned bit_end) {
	return (value >> bit_start) & ((1 << (bit_end - bit_start + 1)) - 1);
}


// Channel 1 Registers
#define SOUND_NR10 0xFF10
#define SOUND_NR11 0xFF11
#define SOUND_NR12 0xFF12
#define SOUND_NR13 0xFF13
#define SOUND_NR14 0xFF14

// Channel 2 Registers
#define SOUND_NR20 0xFF15
#define SOUND_NR21 0xFF16
#define SOUND_NR22 0xFF17
#define SOUND_NR23 0xFF18
#define SOUND_NR24 0xFF19

// Channel 3 Registers
#define SOUND_NR30 0xFF1A
#define SOUND_NR31 0xFF1B
#define SOUND_NR32 0xFF1C
#define SOUND_NR33 0xFF1D
#define SOUND_NR34 0xFF1E

// Channel 4 Registers
#define SOUND_NR40 0xFF1F
#define SOUND_NR41 0xFF20
#define SOUND_NR42 0xFF21
#define SOUND_NR43 0xFF22
#define SOUND_NR44 0xFF23

// Audio Control Registers
#define SOUND_NR50 0xFF24
#define SOUND_NR51 0xFF25
#define SOUND_NR52 0xFF26

void apu_register_write(uint16_t address, uint8_t value) {
	if (apu.enabled == false && address != SOUND_NR52)
		return;

	apu.raw_regs[address - APU_REGISTER_START] = value;
	switch (address) {

	// CHANNEL 1
	case SOUND_NR10:
		apu.ch1.sweep_step = get_bits(value, 0, 2);
		apu.ch1.sweep_direction = get_bits(value, 3, 3);
		apu.ch1.sweep_pace = get_bits(value, 4, 6);
		return;

	case SOUND_NR11:
		apu.ch1.length_current = 64 - get_bits(value, 0, 5);
		apu.ch1.duty_wave_type = get_bits(value, 6, 7);
		return;

	case SOUND_NR12:
		apu.ch1.envelope_pace = get_bits(value, 0, 2);
		apu.ch1.envelope_direction = get_bits(value, 3, 3);
		apu.ch1.volume_initial = get_bits(value, 4, 7);
		apu.ch1.volume_current = apu.ch1.volume_initial;
		if (apu.ch1.volume_initial || apu.ch1.envelope_direction)
			apu.ch1.dac_enabled = true;
		else
			apu.ch1.dac_enabled = apu.ch1.enabled = false;
		return;

	case SOUND_NR13:
		apu.ch1.period_value &= ~(0x00FF);
		apu.ch1.period_value |= value;
		return;

	case SOUND_NR14:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			if (apu.ch1.length_current == 0)
				apu.ch1.length_current = 64;
			if (apu.ch1.length_enabled && apu.frame_sequencer_clock_step % 2 == 0 && apu.ch1.length_current != 0) {
				apu.ch1.length_current--;
			}
			apu.ch1.envelope_current = 0;
			apu.ch1.sweep_current = 0;
			apu.ch1.volume_current = apu.ch1.volume_initial;
			apu.ch1.duty_wave_index = 0;
			if (apu.ch1.dac_enabled && apu.ch1.length_current != 0)
				apu.ch1.enabled = true;
		}

		bool old_enabled = apu.ch1.length_enabled;
		apu.ch1.length_enabled = get_bits(value, 6, 6);
		if (old_enabled == 0 && apu.ch1.length_enabled && apu.frame_sequencer_clock_step % 2 == 0 && apu.ch1.length_current != 0) {
			apu.ch1.length_current--;
			if (apu.ch1.length_current == 0)
				apu.ch1.enabled = false;
		}
		apu.ch1.period_value &= ~(0xFF00);
		apu.ch1.period_value |= get_bits(value, 0, 2) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		apu.ch2.length_current = 64 - get_bits(value, 0, 5);
		apu.ch2.duty_wave_type = get_bits(value, 6, 7);
		return;

	case SOUND_NR22:
		apu.ch2.envelope_pace = get_bits(value, 0, 2);
		apu.ch2.envelope_direction = get_bits(value, 3, 3);
		apu.ch2.volume_initial = get_bits(value, 4, 7);
		apu.ch2.volume_current = apu.ch2.volume_initial;
		if (apu.ch2.volume_initial || apu.ch2.envelope_direction)
			apu.ch2.dac_enabled = true;
		else
			apu.ch2.dac_enabled = apu.ch2.enabled = false;
		return;

	case SOUND_NR23:
		apu.ch2.period_value &= ~(0x00FF);
		apu.ch2.period_value |= value;
		return;

	case SOUND_NR24:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			apu.ch2.volume_current = apu.ch2.volume_initial;
			apu.ch2.duty_wave_index = 0;
			apu.ch2.envelope_current = 0;
			if (apu.ch2.length_current == 0)
				apu.ch2.length_current = 64;
			if (apu.ch2.dac_enabled)
				apu.ch2.enabled = true;
		}
		apu.ch2.length_enabled = get_bits(value, 6, 6);
		apu.ch2.period_value &= ~(0xFF00);
		apu.ch2.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR30:
		apu.ch3.dac_enabled = get_bits(value, 7, 7);
		if (!apu.ch3.dac_enabled)
			apu.ch3.enabled = false;
		return;

	case SOUND_NR31:
		apu.ch3.length_current = 256 - value;
		return;

	case SOUND_NR32:
		apu.ch3.volume_level = get_bits(value, 5, 6);
		return;

	case SOUND_NR33:
		apu.ch3.period_value &= ~(0x00FF);
		apu.ch3.period_value |= value;
		return;

	case SOUND_NR34:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			apu.ch3.wave_index = 0;
			if (apu.ch3.length_current == 0)
				apu.ch3.length_current = 256;
			if (apu.ch3.dac_enabled)
				apu.ch3.enabled = true;
		}
		apu.ch3.length_enabled = get_bits(value, 6, 6);
		apu.ch3.period_value &= ~(0xFF00);
		apu.ch3.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR41:
		apu.ch4.length_current = 64 - get_bits(value, 0, 5);
		return;

	case SOUND_NR42:
		apu.ch4.envelope_pace = get_bits(value, 0, 2);
		apu.ch4.envelope_direction = get_bits(value, 3, 3);
		apu.ch4.volume_initial = get_bits(value, 4, 7);
		apu.ch4.volume_current = apu.ch4.volume_initial;
		if (apu.ch4.volume_initial || apu.ch4.envelope_direction)
			apu.ch4.dac_enabled = true;
		else
			apu.ch4.dac_enabled = apu.ch4.enabled = false;
		return;

	case SOUND_NR43:
		apu.ch4.lfsr_clock_divider = get_bits(value, 0, 2);
		apu.ch4.lfsr_width = get_bits(value, 3, 3);
		apu.ch4.lfsr_clock_shift = get_bits(value, 4, 7);
		if (!apu.ch4.lfsr_clock_divider)
			apu.ch4.period_value = 4;
		else
			apu.ch4.period_value = 8 * apu.ch4.lfsr_clock_divider;
		apu.ch4.period_value <<= apu.ch4.lfsr_clock_shift;
		return;

	case SOUND_NR44:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			apu.ch4.volume_current = apu.ch4.volume_initial;
			apu.ch4.envelope_current = 0;
			apu.ch4.lfsr = 0;
			if (apu.ch4.length_current == 0)
				apu.ch4.length_current = 64;
			if (apu.ch4.dac_enabled)
				apu.ch4.enabled = true;
		}
		apu.ch4.length_enabled = get_bits(value, 6, 6);
		return;

	case SOUND_NR50:
		apu.volume_right = get_bits(value, 0, 2);
		apu.volume_left  = get_bits(value, 4, 6);
		return;

	case SOUND_NR51:
		apu.ch1_output_right = (value >> 0) & 0x01;
		apu.ch2_output_right = (value >> 1) & 0x01;
		apu.ch3_output_right = (value >> 2) & 0x01;
		apu.ch4_output_right = (value >> 3) & 0x01;
		apu.ch1_output_left  = (value >> 4) & 0x01;
		apu.ch2_output_left  = (value >> 5) & 0x01;
		apu.ch3_output_left  = (value >> 6) & 0x01;
		apu.ch4_output_left  = (value >> 7) & 0x01;
		return;

	case SOUND_NR52:
		apu.enabled = get_bits(value, 7, 7);
		if (!apu.enabled) {
			memset(apu.raw_regs, 0, APU_REGISTER_LENGTH);
			apu_channel_reset(&apu.ch1);
			apu_channel_reset(&apu.ch2);
			apu_channel_reset(&apu.ch3);
			apu_channel_reset(&apu.ch4);
		}
		return;

		// Channel 3 wave data
	case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
	case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
	case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
	case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
		apu.wave_data[address - APU_WAVE_DATA_START] = value;
		return;

	default:
		return; // Unimplemented
	}
}

uint8_t apu_register_read_nr52(void) {
	/* printf("CHANNEL3 LENGTH: %d\n", channel3.length_current); */
	uint8_t value = 0;
	value |= apu.ch1.enabled << 0;
	value |= apu.ch2.enabled << 1;
	value |= apu.ch3.enabled << 2;
	value |= apu.ch4.enabled << 3;
	value |= 0x70;
	value |= apu.enabled << 7;
	return value;
}

uint8_t apu_register_read(uint16_t address) {
	uint8_t bit_mask = 0x00;
	switch (address) {

	case SOUND_NR10: bit_mask = 0x80; break;
	case SOUND_NR11: bit_mask = 0x3F; break;
	case SOUND_NR12: bit_mask = 0x00; break;
	case SOUND_NR13: bit_mask = 0xFF; break;
	case SOUND_NR14: bit_mask = 0xBF; break;

	case SOUND_NR20: bit_mask = 0xFF; break;
	case SOUND_NR21: bit_mask = 0x3F; break;
	case SOUND_NR22: bit_mask = 0x00; break;
	case SOUND_NR23: bit_mask = 0xFF; break;
	case SOUND_NR24: bit_mask = 0xBF; break;

	case SOUND_NR30: bit_mask = 0x7F; break;
	case SOUND_NR31: bit_mask = 0xFF; break;
	case SOUND_NR32: bit_mask = 0x9F; break;
	case SOUND_NR33: bit_mask = 0xFF; break;
	case SOUND_NR34: bit_mask = 0xBF; break;

	case SOUND_NR40: bit_mask = 0xFF; break;
	case SOUND_NR41: bit_mask = 0xFF; break;
	case SOUND_NR42: bit_mask = 0x00; break;
	case SOUND_NR43: bit_mask = 0x00; break;
	case SOUND_NR44: bit_mask = 0xBF; break;

	case SOUND_NR50: bit_mask = 0x00; break;
	case SOUND_NR51: bit_mask = 0x00; break;

	// This is an exception. It should actual update with the state of the APU.
	case SOUND_NR52: return apu_register_read_nr52();

	// Channel 3 wave data
	case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
	case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
	case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
	case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
		return apu.wave_data[address - APU_WAVE_DATA_START];

	default:
		return 0xFF;
	}

	return apu.raw_regs[address - APU_REGISTER_START] | bit_mask;
}
