#ifndef APU_H
#define APU_H
#include <stdint.h>

void apu_tick(void);
void apu_reset(void);

void apu_register_write(uint16_t address, uint8_t value);
uint8_t apu_register_read(uint16_t address);

unsigned apu_read_audio(float *output, unsigned frame_count);
unsigned apu_audio_available(void);
void apu_set_audio_sample_rate(unsigned new_sample_rate);

#endif
