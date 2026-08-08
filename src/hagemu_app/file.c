#include "file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#ifdef __EMSCRIPTEN__
char *hagemu_file_sram_name(const char* rom_name) {
	// Returns the string "/savedata/[basename].sav" where [basename] is the basename part of rom_name.
	// It is up to the caller to free the memory for the string.
	const char* basename_begin = strrchr(rom_name, '/');
	if (basename_begin == NULL)
		basename_begin = rom_name;
	else
		basename_begin++;

	char* basename_end = strrchr(rom_name, '.');
	size_t basename_length;
	if (basename_end != NULL)
		basename_length = basename_end - basename_begin;
	else
		basename_length = strlen(basename_begin);

	// Allocate memory for the full sram_path (remember to add 1 for '\0')
	char* sram_name = malloc(strlen("/savedata/") + basename_length + strlen(".sav") + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}

	strcpy(sram_name, "/savedata/");
	strncat(sram_name, basename_begin, basename_length);
	sram_name[strlen("/savedata/") + basename_length] = '\0'; // Manually null-terminate result
	strcat(sram_name, ".sav");

	return sram_name;
}
#else
char *hagemu_file_sram_name(const char* rom_name) {
	char* sram_name = malloc(strlen(rom_name) + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}
	strcpy(sram_name, rom_name);

	// Remove the extension if present
	char* last_dot = strrchr(sram_name, '.');
	if (last_dot != NULL)
		*last_dot = '\0';

	// Adjust the allocated size of sram_name to fit the ".sav" and the final NULL
	sram_name = realloc(sram_name, strlen(sram_name) + strlen(".sav") + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}

	strcat(sram_name, ".sav");
	return sram_name;
}
#endif
