#ifndef HAGEMU_FILE_H
#define HAGEMU_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

char *hagemu_file_sram_name(const char* rom_name);
bool hagemu_file_save(const char *filename, const uint8_t *data, size_t size);
uint8_t *hagemu_file_load(const char *filename, size_t *out_size);

#endif
