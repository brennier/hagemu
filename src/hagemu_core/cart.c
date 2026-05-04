#include "cart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mbc1.h"
#include "mbc3.h"
#include "mbc5.h"

#define GAME_TITLE_LOCATION 0x0134
#define CART_TYPE_LOCATION  0x0147
#define CART_SIZE_LOCATION  0x0148
#define RAM_SIZE_LOCATION   0x0149

struct HagemuCart cart = { .rom_index = 1 };

const struct HagemuCartInfo cart_info_table[] = {
	[0x00] = { .type = NO_MBC, },
	[0x01] = { .type = MBC1, },
	[0x02] = { .type = MBC1, .has_ram = true, },
	[0x03] = { .type = MBC1, .has_ram = true, .has_battery = true, },
	[0x05] = { .type = MBC2, },
	[0x06] = { .type = MBC2, .has_battery = true, },
	[0x08] = { .type = NO_MBC, .has_ram = true, },
	[0x09] = { .type = NO_MBC, .has_ram = true, .has_battery = true, },
	[0x0B] = { .type = MMM01, },
	[0x0C] = { .type = MMM01, .has_ram = true, },
	[0x0D] = { .type = MMM01, .has_ram = true, .has_battery = true, },
	[0x0F] = { .type = MBC3, .has_timer = true, .has_battery = true, },
	[0x10] = { .type = MBC3, .has_timer = true, .has_ram = true, .has_battery = true, },
	[0x11] = { .type = MBC3, },
	[0x12] = { .type = MBC3, .has_ram = true, },
	[0x13] = { .type = MBC3, .has_ram = true, .has_battery = true, },
	[0x19] = { .type = MBC5, },
	[0x1A] = { .type = MBC5, .has_ram = true, },
	[0x1B] = { .type = MBC5, .has_ram = true, .has_battery = true, },
	[0x1C] = { .type = MBC5, .has_rumble = true, },
	[0x1D] = { .type = MBC5, .has_rumble = true, .has_ram = true, },
	[0x1E] = { .type = MBC5, .has_rumble = true, .has_ram = true, .has_battery = true, },
	[0x20] = { .type = MBC6, },
	[0x22] = { .type = MBC7, .has_rumble = true, .has_ram = true, .has_battery = true, },

	// No support at the moment for these less common MBCs
/* 	[0xFC] = "POCKET CAMERA", */
/* 	[0xFD] = "BANDAI TAMA5", */
/* 	[0xFE] = "HuC3", */
/* 	[0xFF] = "HuC1+RAM+BATTERY", */
};

const size_t ram_size_table[] = {
	[0] =   0,
	[1] =   0,
	[2] =   8 * 1024,
	[3] =  32 * 1024,
	[4] = 128 * 1024,
	[5] =  64 * 1024,
};

void cart_set_info(struct HagemuCart *cart) {
	uint8_t mbc_info_byte = cart->rom[CART_TYPE_LOCATION];
	uint8_t rom_size_byte = cart->rom[CART_SIZE_LOCATION];
	uint8_t ram_size_byte = cart->rom[RAM_SIZE_LOCATION];

	memcpy(cart->title, &cart->rom[GAME_TITLE_LOCATION], 16);
	cart->rom_size = 32 * (1 << rom_size_byte) * 1024;
	cart->ram_size = ram_size_table[ram_size_byte];
	cart->info     = cart_info_table[mbc_info_byte];
}

bool cart_sram_available() {
	return cart.ram;
}

void cart_set_sram(const uint8_t *data, size_t size) {
	if (!cart.rom) {
		printf("Error: Unable to load SRAM data before loading a rom file\n");
		return;
	} else if (!cart.info.has_ram || !cart.info.has_battery) {
		printf("Failed to load SRAM data. This cartridge doesn't support battery-backed RAM.\n");
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
	cart.rom_index = 1;
	cart.ram_index = 0;
	cart.ram_enabled = false;

	if (cart.rom != NULL) {
		printf("Freeing previously read rom\n");
		free(cart.rom);
		cart.rom = NULL;
	}

	if (cart.ram != NULL) {
		printf("Freeing previous SRAM data\n");
		free(cart.ram);
		cart.ram = NULL;
	}

	printf("Allocating space and copying the rom data\n");
	cart.rom = malloc(size);
	if (!cart.rom) {
		fprintf(stderr, "Error: Failed to allocated the rom data\n");
		exit(EXIT_FAILURE);
	}
	memcpy(cart.rom, data, size);

	cart_set_info(&cart);
	printf("Rom Title is %s\n", cart.title);
	printf("Cartridge type is MBC%d\n", cart.info.type);
	printf("ROM size is %zu KiB\n",  cart.rom_size / 1024);
	printf("RAM size is %zu KiB\n", cart.ram_size / 1024);

	if (size != cart.rom_size)
		printf("WARNING: Cartridge file is %zu bytes, but expected %zu bytes\n", size, cart.rom_size);

	if (!cart.info.has_ram || cart.ram_size == 0) {
		printf("Cartridge contains no SRAM\n");
		return;
	}

	cart.ram = malloc(cart.ram_size);
	if (!cart.ram) {
		fprintf(stderr, "Error: Failed to allocate the SRAM data\n");
		exit(EXIT_FAILURE);
	}
	memset(cart.ram, 0xFF, cart.ram_size);
}

void cart_sram_reset() {
	if (!cart.ram) return;
	memset(cart.ram, 0xFF, cart.ram_size);
}

void cart_rom_write(uint16_t address, uint8_t value) {
	switch (cart.info.type) {

	case NO_MBC: break;
	case MBC1:   cart_rom_write_mbc1(&cart, address, value); break;
	case MBC3:   cart_rom_write_mbc3(&cart, address, value); break;
	case MBC5:   cart_rom_write_mbc5(&cart, address, value); break;
	default:
		printf("This ROM type (MBC%d?) isn't supported yet. Aborting.\n", cart.info.type);
		exit(EXIT_FAILURE);
	}
}

uint8_t cart_rom_read(uint16_t address) {
	switch (cart.info.type) {

	case NO_MBC: return cart.rom[address]; break;
	case MBC1:   return cart_rom_read_mbc1(&cart, address); break;
	case MBC3:   return cart_rom_read_mbc3(&cart, address); break;
	case MBC5:   return cart_rom_read_mbc5(&cart, address); break;
	default:
		printf("This ROM type (MBC%d?) isn't supported yet. Aborting.\n", cart.info.type);
		exit(EXIT_FAILURE);
	}
}

void cart_ram_write(uint16_t address, uint8_t value) {
	if (!cart.ram) return;

	switch (cart.info.type) {
	case NO_MBC: cart.ram[address] = value; break;
	case MBC1:   cart_ram_write_mbc1(&cart, address, value); break;
	case MBC3:   cart_ram_write_mbc3(&cart, address, value); break;
	case MBC5:   cart_ram_write_mbc5(&cart, address, value); break;
	default:
		printf("This ROM type (MBC%d?) isn't supported yet. Aborting.\n", cart.info.type);
		exit(EXIT_FAILURE);
	}
}

uint8_t cart_ram_read(uint16_t address) {
	if (!cart.ram) return 0xFF;

	switch (cart.info.type) {
	case NO_MBC: return cart.ram[address]; break;
	case MBC1:   return cart_ram_read_mbc1(&cart, address); break;
	case MBC3:   return cart_ram_read_mbc3(&cart, address); break;
	case MBC5:   return cart_ram_read_mbc5(&cart, address); break;
	default:
		printf("This ROM type (MBC%d?) isn't supported yet. Aborting.\n", cart.info.type);
		exit(EXIT_FAILURE);
	}
}
