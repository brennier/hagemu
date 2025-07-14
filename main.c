#include "include/raylib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SCREENWIDTH 166
#define SCREENHEIGHT 144

// Unions are a wonderful thing
union {
	struct {
		uint8_t f, a, c, b, e, d, l, h;
                // the least and most significant bytes of sp and pc
                uint8_t sp_lsb, sp_msb, pc_lsb, pc_msb;
	};
	struct {
		uint16_t af, bc, de, hl;
		uint16_t sp, pc;
	};
	struct {
		uint8_t padding : 4;
		uint8_t zero : 1;
		uint8_t carry : 1;
		uint8_t half_carry : 1;
		uint8_t subtract : 1;
	} flags;
	uint8_t raw_bytes[12];
} gb_register = { 0 };

bool interrupt_flag = false;

// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024];

uint8_t read_byte() {
	return gb_memory[gb_register.pc++];
}

uint16_t read_word() {
	uint8_t first_byte = read_byte();
	uint8_t second_byte = read_byte();
	return ((uint16_t)second_byte << 8) | (uint16_t)first_byte;
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

                case 0x2F: // CPL
                        gb_register.a = ~gb_register.a;
                        gb_register.flags.subtract = 1;
                        gb_register.flags.half_carry = 1;
                        break;

                case 0xC9: // RET
                        gb_register.pc_lsb = gb_memory[gb_register.sp++];
                        gb_register.pc_msb = gb_memory[gb_register.sp++];
                        break;

                case 0xB1: // OR A C
                        gb_register.a |= gb_register.c;
                        gb_register.flags.zero = !gb_register.a;
                        gb_register.flags.subtract = 0;
                        gb_register.flags.half_carry = 0;
                        gb_register.flags.carry = 0;
                        break;

                case 0x78: // LD A B
                        gb_register.a = gb_register.b;
                        break;

                case 0x0B: // DEC BC
                        gb_register.bc--;
                        break;

                case 0x01: // LD BC, u16
                        gb_register.bc = read_word();
                        break;

                case 0xCD: // CALL u16
                        ; // Empty statement as I can't write a declaration after a label
                        // It's important to read the destination first so that the pc that
                        // gets saved points to the next instruction
                        uint16_t dest = read_word();
                        gb_register.sp--;
                        gb_memory[gb_register.sp] = gb_register.pc_msb;
                        gb_register.sp--;
                        gb_memory[gb_register.sp] = gb_register.pc_lsb;
                        gb_register.pc = dest;
                        break;

                case 0x0C: // INC C
			gb_register.c++;
			gb_register.flags.zero = !gb_register.c;
			gb_register.flags.half_carry = !(gb_register.c & 0x0F);
			gb_register.flags.subtract = 0;
                        break;

                case 0xE2: // LD (FF00+C) A
                        gb_memory[0xFF00 + gb_register.c] = gb_register.a;
                        break;

                case 0x31: // LD SP u16
                        gb_register.sp = read_word();
                        break;

                case 0xEA: // LD (u16) A
                        gb_memory[read_word()] = gb_register.a;
                        break;

                case 0x36: // LD (HL) u8
                        gb_memory[gb_register.hl] = read_byte();
                        break;

                case 0xFA: // LD A (u16)
                        gb_register.a = gb_memory[read_word()];
                        break;

                case 0xFE: // CP A u8
                        ; // Empty statement as I can't write a declaration after a label
                        uint8_t next = read_byte();
                        gb_register.flags.zero = !(gb_register.a - next);
                        gb_register.flags.subtract = 0;
                        gb_register.flags.half_carry = ((gb_register.a & 0x0F) - (next & 0x0F)) & 0x10;
                        gb_register.flags.carry = next > gb_register.a;

                case 0xF0: // LD A (FF00 + u8)
                        gb_register.a = gb_memory[0xFF00 + read_byte()];
                        break;

                case 0xE0: // LD (FF00 + u8) A
                        gb_memory[0xFF00 + read_byte()] = gb_register.a;
                        break;

                case 0xF3: // DI Disable interrupts
                        interrupt_flag = false;
                        break;

                case 0x3E: // LD A u8
                        gb_register.a = read_byte();
                        break;

                case 0x05: // DEC B
			gb_register.b--;
			gb_register.flags.zero = !gb_register.b;
			// TODO: Check if the next line is correct
			gb_register.flags.half_carry = !(gb_register.b & 0x0F);
			gb_register.flags.subtract = 1;
			break;

                case 0x32: // LD (HL-) A
                        gb_memory[gb_register.hl--] = gb_register.a;
                        break;

                case 0x06: // LD B u8
                        gb_register.b = read_byte();
                        break;

                case 0xAF: // XOR A A
                        gb_register.a ^= gb_register.a;
                        gb_register.flags.zero = !gb_register.a;
                        break;

		case 0x14: // INC D
			gb_register.d++;
			gb_register.flags.zero = !gb_register.d;
			gb_register.flags.half_carry = !(gb_register.d & 0x0F);
			gb_register.flags.subtract = 0;
			break;

		case 0x0D: // DEC C
			gb_register.c--;
			gb_register.flags.zero = !gb_register.c;
			// TODO: Check if the next line is correct
			gb_register.flags.half_carry = !(gb_register.c & 0x0F);
			gb_register.flags.subtract = 1;
			break;

		case 0xFB: // EI Enable interrupts
			interrupt_flag = true;
			break;

		case 0x20: // JR NZ R8
			if (!gb_register.flags.zero)
				gb_register.pc += (int8_t)read_byte();
                        else
                                gb_register.pc++;
			break;

		case 0x1C: // INC E
			gb_register.e++;
			gb_register.flags.zero = !gb_register.e;
			gb_register.flags.half_carry = !(gb_register.e & 0x0F);
			gb_register.flags.subtract = 0;
			break;

		case 0x12: // LD (DE) A
			gb_memory[gb_register.de] = gb_register.a;
			break;

		case 0x2A: // LD A (HL+)
			gb_register.a = gb_memory[gb_register.hl++];
			break;

		case 0x0E: // LD C 8: load 8-bit immediate into C
			gb_register.c = read_byte();
			break;

		case 0x11: // LD DE d16: load 16-bit immediate into DE
			gb_register.de = read_word();
			break;

		case 0x47: // LD B A: load A into B
			gb_register.b = gb_register.a;
			break;

		case 0x21: // LD HL d16: load 16-bit immediate into HL
			gb_register.hl = read_word();
			break;

		case 0xC3: // JP a16: jump to following 16-bit address
			gb_register.pc = read_word();
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
