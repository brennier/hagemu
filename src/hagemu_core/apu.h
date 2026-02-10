#ifndef APU_H
#define APU_H
#include <stdint.h>

void apu_generate_frames(void *buffer, unsigned int frame_count);
void apu_audio_register_write(uint16_t address, uint8_t value);
uint8_t apu_audio_register_read(uint16_t address);

#endif