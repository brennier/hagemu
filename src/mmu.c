#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mmu.h"
#include "clock.h"
#include "ppu.h"

uint8_t *rom_memory = NULL;
// A maximum of 32kb of RAM
long cartridge_ram_size = 0;
uint8_t cartridge_ram[32 * 1024] = { 0 };
// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024] = { 0 };

char *rom_file_name = NULL;
char *sram_file_name = NULL;
bool ram_enabled = false;
bool save_ram_to_file = false;
bool mmu_joypad_inputs[8];
int rom_bank_index = 1;
int ram_bank_index = 0;

uint8_t get_joypad_input() {
	uint8_t joypad_byte = gb_memory[JOYPAD_INPUT];
	joypad_byte |= 0x0F;

	if (mmu_get_bit(JOYPAD_SELECT_DPAD) == 0) {
		for (int i = 0; i < 4; i++)
			if (mmu_joypad_inputs[i])
				joypad_byte &= ~(0x01 << i);
	}

	if (mmu_get_bit(JOYPAD_SELECT_BUTTONS) == 0) {
		for (int i = 4; i < 8; i++)
			if (mmu_joypad_inputs[i])
				joypad_byte &= ~(0x01 << (i - 4));
	}

	return joypad_byte;
}

uint8_t mmu_read(uint16_t address) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		return ((clock_get() & 0xFF00) >> 8);

	case JOYPAD_INPUT:
		return get_joypad_input();

	case LCD_Y_COORDINATE:
		return ppu_get_current_line();

	case LCD_STATUS:
		return gb_memory[LCD_STATUS] | ppu_get_lcd_status();
	}

	switch (address & 0xF000) {

	// Read from ROM bank 00 (16 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
		return rom_memory[address];

	// Read from switchable ROM bank (16 KiB)
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		return rom_memory[0x4000 * rom_bank_index + (address - 0x4000)];

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		if (ram_enabled) {
			return cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)];
		} else {
			fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
			return 0xFF;
		}

	// Work RAM (8 KiB)
	case 0xC000: case 0xD000:
		return gb_memory[address];

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00)
			return gb_memory[address - 0x2000];
		// Object Attribute Memory
		else if (address < 0xFEA0)
			return gb_memory[address];
		// Unusable memory
		else if (address < 0xFEFF)
			return 0xFF;
		// TODO: IO Registers and High RAM
		else
			return gb_memory[address];
	}

	fprintf(stderr, "Error: Illegal memory access at location `0x%04X'", address);
	exit(EXIT_FAILURE);
}

void mmu_write(uint16_t address, uint8_t value) {
	// Handle special cases first
	switch (address) {

	case TIMER_DIVIDER:
		clock_reset();
		return;

	case TIMER_CONTROL:
		value &= 0x07; // Mask all but the lowest 3 bits
		break;

	case DMA_START:
		if (value > 0xDF) {
			fprintf(stderr, "Illegal DMA Request!");
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < 0xA0; i++)
			gb_memory[0xFE00 + i] = gb_memory[(value << 8) + i];
		return;

	case LCD_Y_COORDINATE:
		printf("Illegal write to LCD Y Coordinate. Ignoring...\n");
		return;
	}

	switch (address & 0xF000) {

	// ROM BANK 0
	case 0x0000: case 0x1000:
		if ((value & 0xF) == 0xA) {
			ram_enabled = true;
			fprintf(stderr, "Enabled ram with value %d at address %04X\n", value, address);
		} else {
			ram_enabled = false;
			fprintf(stderr, "Disabled ram with value %d at address %04X\n", value, address);
		}
		return;

	// ROM BANK SWITCH
	case 0x2000: case 0x3000:
		value &= 0x7F;
		if (value == 0)
			rom_bank_index = 1;
		else
			rom_bank_index = value;
		return;

	case 0x4000: case 0x5000:
		if (value > 7)
			fprintf(stderr, "RTC not implemented\n");
		else
			fprintf(stderr, "Switching to RAM bank %d\n", value);
		ram_bank_index = value;
		return;

	case 0x6000: case 0x7000:
		fprintf(stderr, "The value %d was written to the RTC Data Latch area\n", value);
		return;

	case 0xA000: case 0xBFFF:
		if (ram_enabled) {
			cartridge_ram[0x2000 * ram_bank_index + (address - 0xA000)] = value;
		} else {
			fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
		}
		return;

	case 0xE000: case 0xF000:
		// Echo RAM (about 8 KiB)
		if (address < 0xFE00) {
			gb_memory[address - 0x2000] = value;
			return;
		}
		// Object Attribute Memory
		else if (address < 0xFEA0) {
			gb_memory[address] = value;
			return;
		}
		// Unusable memory
		else if (address < 0xFEFF) {
			// fprintf(stderr, "Warning: Attempted write at the forbidden address '%02X'. Ignoring...\n", address);
			return;
		}
		// TODO: IO Registers and High RAM
		else {
			gb_memory[address] = value;
			return;
		}
	}

	// Else just write the value normally
	gb_memory[address] = value;
}


void mmu_set_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	gb_memory[bit_address] |= (1 << bit_position);
}

bool mmu_get_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	return (gb_memory[bit_address] >> bit_position) & 0x01;
}

void mmu_clear_bit(enum special_bit bit) {
	uint16_t bit_address = (bit & 0xFFFF0) >> 4;
	int bit_position = bit & 0xF;
	gb_memory[bit_address] &= ~(1 << bit_position);
}

const char* cartridge_type_description[] = {
	[0x00] = "ROM ONLY",
	[0x01] = "MBC1",
	[0x02] = "MBC1+RAM",
	[0x03] = "MBC1+RAM+BATTERY",
	[0x05] = "MBC2",
	[0x06] = "MBC2+BATTERY",
	[0x08] = "ROM+RAM",
	[0x09] = "ROM+RAM+BATTERY",
	[0x0B] = "MMM01",
	[0x0C] = "MMM01+RAM",
	[0x0D] = "MMM01+RAM+BATTERY",
	[0x0F] = "MBC3+TIMER+BATTERY",
	[0x10] = "MBC3+TIMER+RAM+BATTERY",
	[0x11] = "MBC3",
	[0x12] = "MBC3+RAM",
	[0x13] = "MBC3+RAM+BATTERY",
	[0x19] = "MBC5",
	[0x1A] = "MBC5+RAM",
	[0x1B] = "MBC5+RAM+BATTERY",
	[0x1C] = "MBC5+RUMBLE",
	[0x1D] = "MBC5+RUMBLE+RAM",
	[0x1E] = "MBC5+RUMBLE+RAM+BATTERY",
	[0x20] = "MBC6",
	[0x22] = "MBC7+SENSOR+RUMBLE+RAM+BATTERY",
	[0xFC] = "POCKET CAMERA",
	[0xFD] = "BANDAI TAMA5",
	[0xFE] = "HuC3",
	[0xFF] = "HuC1+RAM+BATTERY",
};

const int ram_size_table[] = {
	[0x00] = 0,
	[0x01] = 0,
	[0x02] = 8 * 1024,
	[0x03] = 32 * 1024,
	[0x04] = 128 * 1024,
	[0x05] = 64 * 1024,
};

char* get_sram_name(char* rom_name) {
    char* point_to_end = rom_name;
    char* save_file_name = NULL;
    while (*point_to_end)
	point_to_end++;
    while (*point_to_end != '.')
	point_to_end--;

    size_t rom_name_length = point_to_end - rom_name;

    save_file_name = malloc((rom_name_length + 5) * sizeof(char));
    char* save_file_name_pos = save_file_name;
    while (rom_name != point_to_end) {
	*save_file_name_pos = *rom_name;
	save_file_name_pos++;
	rom_name++;
    }
    *(save_file_name_pos++) = '.';
    *(save_file_name_pos++) = 's';
    *(save_file_name_pos++) = 'a';
    *(save_file_name_pos++) = 'v';
    *(save_file_name_pos++) = '\0';

    return save_file_name;
}

void mmu_load_sram_file() {
	long sram_size = cartridge_ram_size;
	FILE *save_file = fopen(sram_file_name, "rb");
	if (save_file == NULL) {
		printf("Warning: Failed to find a save file. Using a new save...\n");
		return;
	}

	long bytes_read = fread(cartridge_ram, 1, sram_size, save_file);
	if (bytes_read != sram_size)
		printf("Error: Save file was expected to be %ld bytes, but was actually %ld bytes.\n", sram_size, bytes_read);
	else
		printf("The save file '%s' was sucessfully found and loaded (%ld bytes)\n", sram_file_name, sram_size);
	fclose(save_file);
}

void mmu_save_sram_file() {
	if (!save_ram_to_file) {
		printf("Error: This game has no ability to save\n");
		return;
	}

	long sram_size = cartridge_ram_size;
	FILE *save_file = fopen(sram_file_name, "wb");
	if (save_file == NULL) {
		printf("Error: Failed to open the file '%s' to write the save data :(\n", sram_file_name);
		return;
	}

	long bytes_written = fwrite(cartridge_ram, 1, sram_size, save_file);
	if (bytes_written != sram_size)
		printf("Error: Tried to write %ld bytes to '%s', but actually only wrote %ld bytes.\n", sram_size, sram_file_name, bytes_written);
	else
		printf("Save data was sucessfully written to '%s' (%ld bytes)\n", sram_file_name, sram_size);
	fclose(save_file);
}

void mmu_load_rom(char* rom_name) {
	if (rom_memory != NULL) {
		printf("Freeing previously read rom...\n");
		free(rom_memory);
		rom_memory = NULL;
	}

	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error: Failed to find the rom file `%s'\n", rom_name);
		exit(EXIT_FAILURE);
	}

	fseek(rom_file, 0L, SEEK_END);
	long rom_size = ftell(rom_file);
	printf("Allocating %ld bytes for the rom...\n", rom_size);

	rom_memory = malloc(rom_size);
	if (rom_memory == NULL) {
		fprintf(stderr, "Error: Couldn't allocate the space for the rom\n");
		exit(EXIT_FAILURE);
	}

	rewind(rom_file);
	long bytes_read = fread(rom_memory, 1, rom_size, rom_file);
	if (bytes_read != rom_size) {
		fprintf(stderr, "Error: Rom was expected to be %ld bytes, but was actually %ld bytes\n", rom_size, bytes_read);
		exit(EXIT_FAILURE);
	}
	fclose(rom_file);

	char game_title[17] = { 0 };
	for (int i = 0; i < 16; i++)
		game_title[i] = rom_memory[0x0134 + i];
	printf("Rom Title is %s\n", game_title);
	printf("Cartridge type is %s\n", cartridge_type_description[rom_memory[CARTRIDGE_TYPE]]);
	printf("ROM size is %d KiB\n", 32 * (1 << rom_memory[CARTRIDGE_SIZE]));
	cartridge_ram_size = ram_size_table[rom_memory[RAM_SIZE]];
	printf("RAM size is %ld KiB\n", cartridge_ram_size / 1024);

	switch (rom_memory[CARTRIDGE_TYPE]) {

	case 0x00: case 0x08: case 0x09: // No MBC
		break;

	case 0x01: case 0x02: case 0x03: case 0x04: // MBC1
		if (rom_size > 512 * 1024)
			printf("WARNING: Cartridges of this size using the MBC1 have limited support.\n");
		else if (rom_size > 1024 * 1024)
			printf("WARNING: Cartridges of this size using the MBC1 may crash.\n");
		break;

	case 0x05: case 0x06: // MBC2
		printf("WARNING: Cartridge type MBC2 is not supported yet.\n");
		break;

	case 0x0B: case 0x0C: case 0x0D: // MMM01
		printf("WARNING: Cartridge type MMM01 is not supported yet.\n");
		break;

	case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: // MBC3
		printf("WARNING: The real time clock feature is not supported yet.\n");
		break;

	case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: // MBC3
		printf("WARNING: Cartridge type MBC5 is not supported yet.\n");
		break;

	case 0x20: case 0x22: case 0xFC: case 0xFD: case 0xFE: case 0xFF: // Other controllers
		printf("WARNING: This cartridge type is not supported yet.\n");
		break;

	}

	switch (rom_memory[CARTRIDGE_TYPE]) {

	case 0x03: case 0x09: case 0x0D: case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
		printf("This rom supports loading and saving. Checking for a save file...\n");
		if (sram_file_name != NULL)
			free(sram_file_name);
		else
			sram_file_name = get_sram_name(rom_name);
		mmu_load_sram_file();
		save_ram_to_file = true;
		break;

	default:
		save_ram_to_file = false;
		for (unsigned i = 0; i < cartridge_ram_size; i++)
			cartridge_ram[i] = 0;
		break;
	}
}
