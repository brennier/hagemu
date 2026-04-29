#include "cart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CART_TYPE_LOCATION 0x0147
#define CART_SIZE_LOCATION 0x0148
#define RAM_SIZE_LOCATION  0x0149

struct HagemuCart {
	uint8_t *rom;
	uint8_t *ram;
	size_t   rom_size;
	size_t   ram_size;
	uint8_t  rom_index;
	uint8_t  ram_index;
	bool     ram_enabled;
};

struct HagemuCart cart = { .rom_index = 1 };

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

bool cart_sram_available() {
	return cart.ram;
}

void cart_set_sram(const uint8_t *data, size_t size) {
	if (!cart.rom) {
		printf("Error: Unable to load SRAM data before loading a rom file\n");
		return;
	} else if (size != cart.ram_size) {
		printf("Failed to copy SRAM data. Expected %zu bytes, but data was %zu bytes\n", cart.ram_size, size);
		return;
	}
	printf("Copying SRAM data to emulator core\n");
	memcpy(cart.ram, data, size);
}

const uint8_t *cart_get_sram(size_t *out_size) {
	if (!cart.ram) {
		printf("This game has no sram for saving\n");
		*out_size = 0;
		return NULL;
	}
	*out_size = cart.ram_size;
	return cart.ram;
}

void cart_set_rom(const uint8_t *data, size_t size) {
	if (cart.rom != NULL) {
		printf("Freeing previously read rom\n");
		free(cart.rom);
		cart.rom = NULL;
	}

	printf("Allocating space and copying the rom data\n");
	cart.rom = malloc(size);
	if (!cart.rom) {
		fprintf(stderr, "Error: Failed to allocated the rom data\n");
		exit(EXIT_FAILURE);
	}
	memcpy(cart.rom, data, size);

	char game_title[17] = { 0 };
	for (int i = 0; i < 16; i++)
		game_title[i] = cart.rom[0x0134 + i];
	printf("Rom Title is %s\n", game_title);
	printf("Cartridge type is %s\n", cartridge_type_description[cart.rom[CART_TYPE_LOCATION]]);
	printf("ROM size is %d KiB\n", 32 * (1 << cart.rom[CART_SIZE_LOCATION]));
	cart.ram_size = ram_size_table[cart.rom[RAM_SIZE_LOCATION]];
	printf("RAM size is %ld KiB\n", cart.ram_size / 1024);

	// Reset the cartridge ram
	if (cart.ram != NULL) {
		printf("Freeing previous SRAM data\n");
		free(cart.ram);
		cart.ram = NULL;
	}

	cart.ram = malloc(cart.ram_size);
	if (!cart.ram) {
		fprintf(stderr, "Error: Failed to allocate the SRAM data\n");
		exit(EXIT_FAILURE);
	}
	memset(cart.ram, 0xFF, cart.ram_size);

	switch (cart.rom[CART_TYPE_LOCATION]) {

	case 0x00: case 0x08: case 0x09: // No MBC
		break;

	case 0x01: case 0x02: case 0x03: case 0x04: // MBC1
		if (size > 512 * 1024)
			printf("WARNING: Cartridges of this size using the MBC1 have limited support.\n");
		else if (size > 1024 * 1024)
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

	switch (cart.rom[CART_TYPE_LOCATION]) {

	case 0x03: case 0x09: case 0x10: case 0x0D: case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
		printf("This rom supports loading and saving. Checking for an SRAM file.\n");
		break;
	}
}

uint8_t cart_rom_read(uint16_t address) {
	if (address < 0x4000)
		return cart.rom[address];
	else if (address < 0x8000)
		return cart.rom[0x4000 * cart.rom_index + (address - 0x4000)];

	printf("ERROR: Out of bounds read from the cartridge\n");
	exit(EXIT_FAILURE);
}

uint8_t cart_ram_read(uint16_t address) {
	if (!cart.ram_enabled) {
		fprintf(stderr, "Attempt to read RAM address %04X, but it was disabled\n", address);
		return 0xFF;
	}

	return cart.ram[0x2000 * cart.ram_index + address];
}

void cart_ram_write(uint16_t address, uint8_t value) {
	if (!cart.ram_enabled) {
		fprintf(stderr, "Attempt to write value %d to RAM address %04X, but it was disabled\n", value, address);
	}

	cart.ram[0x2000 * cart.ram_index + address] = value;
}

void cart_rom_write(uint16_t address, uint8_t value) {
	switch (address & 0xF000) {
		// Disable/Enable cartridge RAM
	case 0x0000: case 0x1000:
		if ((value & 0xF) == 0xA) {
			cart.ram_enabled = true;
			//fprintf(stderr, "Enabled ram with value %d at address %04X\n", value, address);
		} else {
			cart.ram_enabled = false;
			//fprintf(stderr, "Disabled ram with value %d at address %04X\n", value, address);
		}
		return;

	// Switch ROM bank
	case 0x2000: case 0x3000:
		value &= 0x7F;
		if (value == 0)
			cart.rom_index = 1;
		else
			cart.rom_index = value;
		return;

	// Switch RAM bank or select RTC
	case 0x4000: case 0x5000:
		if (value > 7)
			fprintf(stderr, "RTC not implemented\n");
		else if (value <= 3 && value >= 0)
			/* fprintf(stderr, "Switching to RAM bank %d\n", value); */
			;
		else
			fprintf(stderr, "Invalid ram bank number %d. Ignoring.\n", value);
		cart.ram_index = value;
		return;

	// Setting RTC register
	case 0x6000: case 0x7000:
		// fprintf(stderr, "The value %d was written to the RTC Data Latch area at %04X\n", value, address);
		return;
	}
}
