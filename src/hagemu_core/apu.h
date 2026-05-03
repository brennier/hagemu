#ifndef APU_H
#define APU_H
#include <stdint.h>

unsigned apu_read_audio(float *output, unsigned frame_count);
void apu_audio_register_write(uint16_t address, uint8_t value);
uint8_t apu_audio_register_read(uint16_t address);
unsigned apu_audio_available();
void apu_tick(unsigned t_cycles);
void apu_set_audio_sample_rate(unsigned new_sample_rate);
void apu_reset();

#endif
