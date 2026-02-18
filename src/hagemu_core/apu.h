#ifndef APU_H
#define APU_H
#include <stdint.h>

unsigned apu_read_audio(float *output, unsigned frame_count);
void apu_audio_register_write(uint16_t address, uint8_t value);
uint8_t apu_audio_register_read(uint16_t address);

#endif
