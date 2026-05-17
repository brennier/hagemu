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

struct {
	uint8_t volume_left;
	uint8_t volume_right;
	bool apu_enabled;
	bool channel1_right;
	bool channel1_left;
	bool channel2_right;
	bool channel2_left;
	bool channel3_right;
	bool channel3_left;
	bool channel4_right;
	bool channel4_left;
} master_controls = { 0 };

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

AudioFrame apu_generate_frame(void);
AudioFrame highpass_filter(AudioFrame frame);
AudioFrame lowpass_filter(AudioFrame frame);

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
} channel1 = { 0 }, channel2 = { 0 }, channel3 = { 0 }, channel4 = { 0 };

struct HagemuAPU {
	struct Channel ch1;
	struct Channel ch2;
	struct Channel ch3;
	struct Channel ch4;
	unsigned ticks;
	unsigned frame_sequencer_clock_step;
	uint8_t wave_data[16];
	uint8_t raw_regs[APU_REGISTER_LENGTH];
} apu = { 0 };

void apu_reset(void) {
	memset(&channel1, 0, sizeof(struct Channel));
	memset(&channel2, 0, sizeof(struct Channel));
	memset(&channel3, 0, sizeof(struct Channel));
	memset(&channel4, 0, sizeof(struct Channel));
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

	tick_pulse_channel(&channel1);
	tick_pulse_channel(&channel2);
	tick_wave_channel(&channel3);
	tick_noise_channel(&channel4);

	// The frame frequencer ticks at 512 Hz
	if (apu.ticks == (APU_TICK_RATE / 512)) {
		apu.ticks = 0;
		apu.frame_sequencer_clock_step++;
		apu.frame_sequencer_clock_step %= 8;

		switch (apu.frame_sequencer_clock_step) {

		case 2: case 6:
			tick_sweep(&channel1);
			// FALL THROUGH ON PURPOSE

		case 0: case 4:
			tick_length_timer(&channel1);
			tick_length_timer(&channel2);
			tick_length_timer(&channel3);
			tick_length_timer(&channel4);
			break;

		case 1: case 3: case 5:
			break;

		case 7:
			tick_envelope(&channel1);
			tick_envelope(&channel2);
			tick_envelope(&channel4);
			break;
		}
	}

	AudioFrame current_frame = apu_generate_frame();
	static AudioFrame accumulate = { 0 };
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
	accumulate.left  /= DECIMATION_FACTOR;
	accumulate.right /= DECIMATION_FACTOR;

	accumulate.left  *= (master_controls.volume_left  + 1);
	accumulate.right *= (master_controls.volume_right + 1);
	accumulate.left   = (accumulate.left  - 240) / 240.0;
	accumulate.right  = (accumulate.right - 240) / 240.0;

	accumulate = lowpass_filter(accumulate);
	accumulate = highpass_filter(accumulate);
	queue_push(&audio_fifo, accumulate);

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

AudioFrame apu_generate_frame(void) {
	AudioFrame frame = { 0 };
	if (!master_controls.apu_enabled)
		return frame;

	// Each channel outputs an integer in [0, 15]
	int ch1 = channel_output_pulse(&channel1);
	int ch2 = channel_output_pulse(&channel2);
	int ch3 = channel_output_wave(&channel3);
	int ch4 = channel_output_noise(&channel4);

	frame.left = master_controls.channel1_left * ch1
		+ master_controls.channel2_left * ch2
		+ master_controls.channel3_left * ch3
		+ master_controls.channel4_left * ch4;

	frame.right = master_controls.channel1_right * ch1
		+ master_controls.channel2_right * ch2
		+ master_controls.channel3_right * ch3
		+ master_controls.channel4_right * ch4;

	return frame;
}

// Alpha should be 1 - exp(-2 * pi * cutoff_freqency / sample_rate)
AudioFrame lowpass_filter(AudioFrame frame) {
	static AudioFrame prev_frame;
	/* const float alpha = 0.730f; // 48kHz sample rate, 10kHz cutoff */
	const float alpha = 0.649f; // 48kHz sample rate, 8kHz cutoff

	prev_frame.left  += alpha * (frame.left  - prev_frame.left);
	prev_frame.right += alpha * (frame.right - prev_frame.right);
	return prev_frame;
}

// Emulates the DC Blocking of the gameboy
AudioFrame highpass_filter(AudioFrame frame) {
	static AudioFrame prev_input, prev_output;
	const float R = 0.995f;
	AudioFrame output_frame = {
		.left  = frame.left  - prev_input.left  + R * prev_output.left,
		.right = frame.right - prev_input.right + R * prev_output.right,
	};
	prev_input = frame;
	prev_output = output_frame;
	return output_frame;
}

unsigned apu_read_audio(float *output, unsigned max_frames) {
	if (max_frames > apu_audio_available())
		max_frames = apu_audio_available();
	queue_drain(&audio_fifo, output, max_frames);
	return max_frames;
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
	if (master_controls.apu_enabled == false && address != SOUND_NR52)
		return;

	apu.raw_regs[address - APU_REGISTER_START] = value;
	switch (address) {

	// CHANNEL 1
	case SOUND_NR10:
		channel1.sweep_step = get_bits(value, 0, 2);
		channel1.sweep_direction = get_bits(value, 3, 3);
		channel1.sweep_pace = get_bits(value, 4, 6);
		return;

	case SOUND_NR11:
		channel1.length_current = 64 - get_bits(value, 0, 5);
		channel1.duty_wave_type = get_bits(value, 6, 7);
		return;

	case SOUND_NR12:
		channel1.envelope_pace = get_bits(value, 0, 2);
		channel1.envelope_direction = get_bits(value, 3, 3);
		channel1.volume_initial = get_bits(value, 4, 7);
		channel1.volume_current = channel1.volume_initial;
		if (channel1.volume_initial || channel1.envelope_direction)
			channel1.dac_enabled = true;
		else
			channel1.dac_enabled = channel1.enabled = false;
		return;

	case SOUND_NR13:
		channel1.period_value &= ~(0x00FF);
		channel1.period_value |= value;
		return;

	case SOUND_NR14:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			if (channel1.length_current == 0)
				channel1.length_current = 64;
			if (channel1.length_enabled && apu.frame_sequencer_clock_step % 2 == 0 && channel1.length_current != 0) {
				channel1.length_current--;
			}
			channel1.envelope_current = 0;
			channel1.sweep_current = 0;
			channel1.volume_current = channel1.volume_initial;
			channel1.duty_wave_index = 0;
			if (channel1.dac_enabled && channel1.length_current != 0)
				channel1.enabled = true;
		}

		bool old_enabled = channel1.length_enabled;
		channel1.length_enabled = get_bits(value, 6, 6);
		if (old_enabled == 0 && channel1.length_enabled && apu.frame_sequencer_clock_step % 2 == 0 && channel1.length_current != 0) {
			channel1.length_current--;
			if (channel1.length_current == 0)
				channel1.enabled = false;
		}
		channel1.period_value &= ~(0xFF00);
		channel1.period_value |= get_bits(value, 0, 2) << 8;
		return;

	// CHANNEL 2
	case SOUND_NR21:
		channel2.length_current = 64 - get_bits(value, 0, 5);
		channel2.duty_wave_type = get_bits(value, 6, 7);
		return;

	case SOUND_NR22:
		channel2.envelope_pace = get_bits(value, 0, 2);
		channel2.envelope_direction = get_bits(value, 3, 3);
		channel2.volume_initial = get_bits(value, 4, 7);
		channel2.volume_current = channel2.volume_initial;
		if (channel2.volume_initial || channel2.envelope_direction)
			channel2.dac_enabled = true;
		else
			channel2.dac_enabled = channel2.enabled = false;
		return;

	case SOUND_NR23:
		channel2.period_value &= ~(0x00FF);
		channel2.period_value |= value;
		return;

	case SOUND_NR24:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel2.volume_current = channel2.volume_initial;
			channel2.duty_wave_index = 0;
			channel2.envelope_current = 0;
			if (channel2.length_current == 0)
				channel2.length_current = 64;
			if (channel2.dac_enabled)
				channel2.enabled = true;
		}
		channel2.length_enabled = get_bits(value, 6, 6);
		channel2.period_value &= ~(0xFF00);
		channel2.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR30:
		channel3.dac_enabled = get_bits(value, 7, 7);
		if (!channel3.dac_enabled)
			channel3.enabled = false;
		return;

	case SOUND_NR31:
		channel3.length_current = 256 - value;
		return;

	case SOUND_NR32:
		channel3.volume_level = get_bits(value, 5, 6);
		return;

	case SOUND_NR33:
		channel3.period_value &= ~(0x00FF);
		channel3.period_value |= value;
		return;

	case SOUND_NR34:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel3.wave_index = 0;
			if (channel3.length_current == 0)
				channel3.length_current = 256;
			if (channel3.dac_enabled)
				channel3.enabled = true;
		}
		channel3.length_enabled = get_bits(value, 6, 6);
		channel3.period_value &= ~(0xFF00);
		channel3.period_value |= get_bits(value, 0, 2) << 8;
		return;

	case SOUND_NR41:
		channel4.length_current = 64 - get_bits(value, 0, 5);
		return;

	case SOUND_NR42:
		channel4.envelope_pace = get_bits(value, 0, 2);
		channel4.envelope_direction = get_bits(value, 3, 3);
		channel4.volume_initial = get_bits(value, 4, 7);
		channel4.volume_current = channel4.volume_initial;
		if (channel4.volume_initial || channel4.envelope_direction)
			channel4.dac_enabled = true;
		else
			channel4.dac_enabled = channel4.enabled = false;
		return;

	case SOUND_NR43:
		channel4.lfsr_clock_divider = get_bits(value, 0, 2);
		channel4.lfsr_width = get_bits(value, 3, 3);
		channel4.lfsr_clock_shift = get_bits(value, 4, 7);
		if (!channel4.lfsr_clock_divider)
			channel4.period_value = 4;
		else
			channel4.period_value = 8 * channel4.lfsr_clock_divider;
		channel4.period_value <<= channel4.lfsr_clock_shift;
		return;

	case SOUND_NR44:
		// Channel is triggered
		if (get_bits(value, 7, 7)) {
			channel4.volume_current = channel4.volume_initial;
			channel4.envelope_current = 0;
			channel4.lfsr = 0;
			if (channel4.length_current == 0)
				channel4.length_current = 64;
			if (channel4.dac_enabled)
				channel4.enabled = true;
		}
		channel4.length_enabled = get_bits(value, 6, 6);
		return;

	case SOUND_NR50:
		master_controls.volume_right = get_bits(value, 0, 2);
		master_controls.volume_left  = get_bits(value, 4, 6);
		return;

	case SOUND_NR51:
		master_controls.channel1_right = (value >> 0) & 0x01;
		master_controls.channel2_right = (value >> 1) & 0x01;
		master_controls.channel3_right = (value >> 2) & 0x01;
		master_controls.channel4_right = (value >> 3) & 0x01;
		master_controls.channel1_left  = (value >> 4) & 0x01;
		master_controls.channel2_left  = (value >> 5) & 0x01;
		master_controls.channel3_left  = (value >> 6) & 0x01;
		master_controls.channel4_left  = (value >> 7) & 0x01;
		return;

	case SOUND_NR52:
		master_controls.apu_enabled = get_bits(value, 7, 7);
		if (!master_controls.apu_enabled) {
			memset(apu.raw_regs, 0, APU_REGISTER_LENGTH);
			apu_channel_reset(&channel1);
			apu_channel_reset(&channel2);
			apu_channel_reset(&channel3);
			apu_channel_reset(&channel4);
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
	value |= channel1.enabled << 0;
	value |= channel2.enabled << 1;
	value |= channel3.enabled << 2;
	value |= channel4.enabled << 3;
	value |= 0x70;
	value |= master_controls.apu_enabled << 7;
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
