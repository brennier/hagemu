#include "include/raylib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SCREENWIDTH 166
#define SCREENHEIGHT 144

union {
	struct {
		uint8_t f, a, c, b, e, d, l, h;
		uint16_t sp, pc;
	};
	struct {
		uint16_t af, bc, de, hl;
	};
	uint8_t raw_bytes[12];
} gb_register;

// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024];

uint16_t get_word(uint16_t address) {
	return ((uint16_t)gb_memory[address+1] << 8) | (uint16_t)gb_memory[address];
}

void load_rom(char* rom_name, size_t rom_bytes) {
	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error opening the rom file. Exiting...\n");
		exit(EXIT_FAILURE);
	}

	size_t bytes_read = fread(gb_memory, 1, rom_bytes, rom_file);
	if (bytes_read != rom_bytes) {
		fprintf(stderr, "Error reading from the rom file. Exiting...\n");
		exit(EXIT_FAILURE);
	}
	fclose(rom_file);
}

int main() {
	load_rom("test.gb", 32 * 1024);
	gb_register.pc = 0x100;

	while (true) {
		uint8_t op_byte = gb_memory[gb_register.pc++];
		printf("Processing opcode %02X...\n", op_byte);
		switch (op_byte) {

		case 0x20: // JR NZ R8
			fprintf(stderr, "Op Code not implemented: 0x%02X\n", op_byte);
			exit(EXIT_FAILURE);
			break;

		case 0x1C: // INC E
			gb_register.e++;
			break;

		case 0x12: // LD (DE) A
			gb_memory[gb_register.de] = gb_register.a;
			break;

		case 0x2A: // LD A (HL+)
			gb_register.a = gb_memory[gb_register.hl++];
			break;

		case 0x0E: // LD C 8: load 8-bit immediate into C
			gb_register.c = gb_memory[gb_register.pc];
			gb_register.pc++;
			break;

		case 0x11: // LD DE d16: load 16-bit immediate into DE
			gb_register.de = get_word(gb_register.pc);
			gb_register.pc += 2;
			break;

		case 0x47: // LD B A: load A into B
			gb_register.b = gb_register.a;
			break;

		case 0x21: // LD HL d16: load 16-bit immediate into HL
			gb_register.hl = get_word(gb_register.pc);
			gb_register.pc += 2;
			break;

		case 0xC3: // JP a16: jump to following 16-bit address
			gb_register.pc = get_word(gb_register.pc);
			break;

		case 0x00: // NOP: do nothing
			break;

		default:
			fprintf(stderr, "Op Code not implemented: 0x%02X\n", op_byte);
			exit(EXIT_FAILURE);
			break;
		}
	}

	/* InitWindow(SCREENWIDTH, SCREENHEIGHT, "GameBoy Emulator"); */
	/* while (WindowShouldClose() != true) { */
	/*	   BeginDrawing(); */
	/*	   ClearBackground(BLACK); */
	/*	   DrawFPS(10, 10); */
	/*	   EndDrawing(); */
	/* } */
	/* CloseWindow(); */

	return 0;
}
