#include "file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef PLATFORM_WEB
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

bool hagemu_file_save(const char *filename, const uint8_t *data, size_t size) {
	FILE *file = fopen(filename, "wb");
	if (file == NULL) {
		printf("Error: Failed to open the file '%s' :(\n", filename);
		return false;
	}

	size_t bytes_written = fwrite(data, 1, size, file);

	if (bytes_written != size) {
		printf("Error: Tried to write %ld bytes to '%s', but actually only wrote %ld bytes.\n", size, filename, bytes_written);
		return false;
	}

	printf("The file '%s' was sucessfully written (%ld bytes)\n", filename, size);
	fclose(file);
	return true;
}

uint8_t *hagemu_file_load(const char *filename, size_t *out_size) {
	FILE *file = fopen(filename, "rb");
	if (!file) {
		printf("Warning: Failed to open the file '%s'.\n", filename);
		return NULL;
	}

	// Seek to end to determine file size
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}

	long size = ftell(file);
	if (size < 0) {
		fclose(file);
		return NULL;
	}
	size_t usize = (size_t)size;

	// Go back to beginning
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}

	// Allocate (+1 for optional null terminator)
	unsigned char *buffer = malloc(usize);
	if (!buffer) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(buffer, 1, usize, file);
	if (read != usize) {
		free(buffer);
		fclose(file);
		return NULL;
	}

	fclose(file);

	if (out_size) *out_size = usize;

	return buffer;
}
