#include "file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *hagemu_file_sram_name(const char *rom_name) {
	const char *base = strrchr(rom_name, '/');
	base = base ? base + 1 : rom_name;

	const char *dot = strrchr(base, '.');
	size_t base_length = dot ? (dot - base) : strlen(base);

#ifdef __EMSCRIPTEN__
	// Use a fixed save directory
	const char *dir = "/savedata/";
	size_t dir_length = strlen(dir);
#else
	// Keep the original directory of the rom name
	const char *dir = rom_name;
	size_t dir_length = base - rom_name;
#endif

	const char *ext = ".sav";
	size_t ext_length = strlen(ext);

	// +1 for the null terminator
	char *sram_name = malloc(dir_length + base_length + ext_length + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}

	memcpy(sram_name, dir, dir_length);
	memcpy(sram_name + dir_length, base, base_length);
	memcpy(sram_name + dir_length + base_length, ext, ext_length);
	sram_name[dir_length + base_length + ext_length] = '\0';
	return sram_name;
}
