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
		uint8_t unused : 4;
		uint8_t carry : 1;
		uint8_t half_carry : 1;
		uint8_t subtract : 1;
		uint8_t zero : 1;
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

uint16_t pop_stack() {
	uint8_t lower = gb_memory[gb_register.sp++];
	uint8_t upper = gb_memory[gb_register.sp++];
	return (upper << 8) | lower;
}

void push_stack(uint16_t reg16) {
	uint8_t lower = (reg16 & 0x00FF);
	uint8_t upper = (reg16 & 0xFF00) >> 8;
	gb_register.sp--;
	gb_memory[gb_register.sp] = upper;
	gb_register.sp--;
	gb_memory[gb_register.sp] = lower;
}

void load_rom(char* rom_name, size_t rom_bytes) {
	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error: Failed to find the rom file `%s'\n", rom_name);
		exit(EXIT_FAILURE);
	}

	size_t bytes_read = fread(gb_memory, 1, rom_bytes, rom_file);
	if (bytes_read != rom_bytes) {
		fprintf(stderr, "Error: Failed reading from the rom file\n");
		exit(EXIT_FAILURE);
	}
	fclose(rom_file);
}

void op_rlc(uint8_t* reg) {
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) <<= 1;
	(*reg) += highest_bit;
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = highest_bit;
}

void op_rrc(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	(*reg) >>= 1;
	(*reg) += (lowest_bit << 7);
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = lowest_bit;
}

void op_rr(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	(*reg) >>= 1;
	(*reg) += (gb_register.flags.carry << 7);
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = lowest_bit;
}

void op_rl(uint8_t* reg) {
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) <<= 1;
	(*reg) += gb_register.flags.carry;
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = highest_bit;
}

void op_sla(uint8_t* reg) {
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) <<= 1;
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = highest_bit;
}

void op_sra(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) >>= 1;
	(*reg) += (highest_bit << 7);
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.carry = lowest_bit;
}

void op_srl(uint8_t* reg) {
	gb_register.flags.carry = (*reg) % 2;
	(*reg) >>= 1;
	gb_register.flags.subtract = 0;
	gb_register.flags.half_carry = 0;
	gb_register.flags.zero = !(*reg);
}

void op_swap(uint8_t* reg) {
	uint8_t lower = ((*reg) & 0x0F);
	uint8_t upper = ((*reg) & 0xF0);
	(*reg) = (lower << 4) | (upper >> 4);
	gb_register.f = 0;
	gb_register.flags.zero = !(*reg);
}

void op_bit(int bit_num, uint8_t value) {
	gb_register.flags.subtract = 0;
	gb_register.flags.half_carry = 1;
	gb_register.flags.zero = !(value & (1 << bit_num));
}

void op_res(int bit_num, uint8_t* reg) {
	(*reg) &= ~((uint8_t)0x01 << bit_num);
}

void op_set(int bit_num, uint8_t* reg) {
	(*reg) |= (1 << bit_num);
}

void process_extra_opcodes(uint8_t opcode) {
	switch (opcode) {

	// ROTATE LEFT CIRCULAR
	case 0x00: op_rlc(&gb_register.b); break;
	case 0x01: op_rlc(&gb_register.c); break;
	case 0x02: op_rlc(&gb_register.d); break;
	case 0x03: op_rlc(&gb_register.e); break;
	case 0x04: op_rlc(&gb_register.h); break;
	case 0x05: op_rlc(&gb_register.l); break;
	case 0x06: op_rlc(&gb_memory[gb_register.hl]); break;
	case 0x07: op_rlc(&gb_register.a); break;

	// ROTATE RIGHT CIRCULAR
	case 0x08: op_rrc(&gb_register.b); break;
	case 0x09: op_rrc(&gb_register.c); break;
	case 0x0A: op_rrc(&gb_register.d); break;
	case 0x0B: op_rrc(&gb_register.e); break;
	case 0x0C: op_rrc(&gb_register.h); break;
	case 0x0D: op_rrc(&gb_register.l); break;
	case 0x0E: op_rrc(&gb_memory[gb_register.hl]); break;
	case 0x0F: op_rrc(&gb_register.a); break;

	// ROTATE LEFT OPERATIONS
	case 0x10: op_rl(&gb_register.b); break;
	case 0x11: op_rl(&gb_register.c); break;
	case 0x12: op_rl(&gb_register.d); break;
	case 0x13: op_rl(&gb_register.e); break;
	case 0x14: op_rl(&gb_register.h); break;
	case 0x15: op_rl(&gb_register.l); break;
	case 0x16: op_rl(&gb_memory[gb_register.hl]); break;
	case 0x17: op_rl(&gb_register.a); break;

	// ROTATE RIGHT OPERATIONS
	case 0x18: op_rr(&gb_register.b); break;
	case 0x19: op_rr(&gb_register.c); break;
	case 0x1A: op_rr(&gb_register.d); break;
	case 0x1B: op_rr(&gb_register.e); break;
	case 0x1C: op_rr(&gb_register.h); break;
	case 0x1D: op_rr(&gb_register.l); break;
	case 0x1E: op_rr(&gb_memory[gb_register.hl]); break;
	case 0x1F: op_rr(&gb_register.a); break;

	// SHIFT LEFT ARITHMETIC
	case 0x20: op_sla(&gb_register.b); break;
	case 0x21: op_sla(&gb_register.c); break;
	case 0x22: op_sla(&gb_register.d); break;
	case 0x23: op_sla(&gb_register.e); break;
	case 0x24: op_sla(&gb_register.h); break;
	case 0x25: op_sla(&gb_register.l); break;
	case 0x26: op_sla(&gb_memory[gb_register.hl]); break;
	case 0x27: op_sla(&gb_register.a); break;

	// SHIFT RIGHT ARITHEMTIC
	case 0x28: op_sra(&gb_register.b); break;
	case 0x29: op_sra(&gb_register.c); break;
	case 0x2A: op_sra(&gb_register.d); break;
	case 0x2B: op_sra(&gb_register.e); break;
	case 0x2C: op_sra(&gb_register.h); break;
	case 0x2D: op_sra(&gb_register.l); break;
	case 0x2E: op_sra(&gb_memory[gb_register.hl]); break;
	case 0x2F: op_sra(&gb_register.a); break;

	// SWAP OPERATIONS
	case 0x30: op_swap(&gb_register.b); break;
	case 0x31: op_swap(&gb_register.c); break;
	case 0x32: op_swap(&gb_register.d); break;
	case 0x33: op_swap(&gb_register.e); break;
	case 0x34: op_swap(&gb_register.h); break;
	case 0x35: op_swap(&gb_register.l); break;
	case 0x36: op_swap(&gb_memory[gb_register.hl]); break;
	case 0x37: op_swap(&gb_register.a); break;

	// SHIFT RIGHT LOGICAL
	case 0x38: op_srl(&gb_register.b); break;
	case 0x39: op_srl(&gb_register.c); break;
	case 0x3A: op_srl(&gb_register.d); break;
	case 0x3B: op_srl(&gb_register.e); break;
	case 0x3C: op_srl(&gb_register.h); break;
	case 0x3D: op_srl(&gb_register.l); break;
	case 0x3E: op_srl(&gb_memory[gb_register.hl]); break;
	case 0x3F: op_srl(&gb_register.a); break;

	// TEST BIT OPERATIONS
	case 0x40: op_bit(0, gb_register.b); break;
	case 0x41: op_bit(0, gb_register.c); break;
	case 0x42: op_bit(0, gb_register.d); break;
	case 0x43: op_bit(0, gb_register.e); break;
	case 0x44: op_bit(0, gb_register.h); break;
	case 0x45: op_bit(0, gb_register.l); break;
	case 0x46: op_bit(0, gb_memory[gb_register.hl]); break;
	case 0x47: op_bit(0, gb_register.a); break;
	case 0x48: op_bit(1, gb_register.b); break;
	case 0x49: op_bit(1, gb_register.c); break;
	case 0x4A: op_bit(1, gb_register.d); break;
	case 0x4B: op_bit(1, gb_register.e); break;
	case 0x4C: op_bit(1, gb_register.h); break;
	case 0x4D: op_bit(1, gb_register.l); break;
	case 0x4E: op_bit(1, gb_memory[gb_register.hl]); break;
	case 0x4F: op_bit(1, gb_register.a); break;
	case 0x50: op_bit(2, gb_register.b); break;
	case 0x51: op_bit(2, gb_register.c); break;
	case 0x52: op_bit(2, gb_register.d); break;
	case 0x53: op_bit(2, gb_register.e); break;
	case 0x54: op_bit(2, gb_register.h); break;
	case 0x55: op_bit(2, gb_register.l); break;
	case 0x56: op_bit(2, gb_memory[gb_register.hl]); break;
	case 0x57: op_bit(2, gb_register.a); break;
	case 0x58: op_bit(3, gb_register.b); break;
	case 0x59: op_bit(3, gb_register.c); break;
	case 0x5A: op_bit(3, gb_register.d); break;
	case 0x5B: op_bit(3, gb_register.e); break;
	case 0x5C: op_bit(3, gb_register.h); break;
	case 0x5D: op_bit(3, gb_register.l); break;
	case 0x5E: op_bit(3, gb_memory[gb_register.hl]); break;
	case 0x5F: op_bit(3, gb_register.a); break;
	case 0x60: op_bit(4, gb_register.b); break;
	case 0x61: op_bit(4, gb_register.c); break;
	case 0x62: op_bit(4, gb_register.d); break;
	case 0x63: op_bit(4, gb_register.e); break;
	case 0x64: op_bit(4, gb_register.h); break;
	case 0x65: op_bit(4, gb_register.l); break;
	case 0x66: op_bit(4, gb_memory[gb_register.hl]); break;
	case 0x67: op_bit(4, gb_register.a); break;
	case 0x68: op_bit(5, gb_register.b); break;
	case 0x69: op_bit(5, gb_register.c); break;
	case 0x6A: op_bit(5, gb_register.d); break;
	case 0x6B: op_bit(5, gb_register.e); break;
	case 0x6C: op_bit(5, gb_register.h); break;
	case 0x6D: op_bit(5, gb_register.l); break;
	case 0x6E: op_bit(5, gb_memory[gb_register.hl]); break;
	case 0x6F: op_bit(5, gb_register.a); break;
	case 0x70: op_bit(6, gb_register.b); break;
	case 0x71: op_bit(6, gb_register.c); break;
	case 0x72: op_bit(6, gb_register.d); break;
	case 0x73: op_bit(6, gb_register.e); break;
	case 0x74: op_bit(6, gb_register.h); break;
	case 0x75: op_bit(6, gb_register.l); break;
	case 0x76: op_bit(6, gb_memory[gb_register.hl]); break;
	case 0x77: op_bit(6, gb_register.a); break;
	case 0x78: op_bit(7, gb_register.b); break;
	case 0x79: op_bit(7, gb_register.c); break;
	case 0x7A: op_bit(7, gb_register.d); break;
	case 0x7B: op_bit(7, gb_register.e); break;
	case 0x7C: op_bit(7, gb_register.h); break;
	case 0x7D: op_bit(7, gb_register.l); break;
	case 0x7E: op_bit(7, gb_memory[gb_register.hl]); break;
	case 0x7F: op_bit(7, gb_register.a); break;

	// RESET BIT OPERATIONS
	case 0x80: op_res(0, &gb_register.b); break;
	case 0x81: op_res(0, &gb_register.c); break;
	case 0x82: op_res(0, &gb_register.d); break;
	case 0x83: op_res(0, &gb_register.e); break;
	case 0x84: op_res(0, &gb_register.h); break;
	case 0x85: op_res(0, &gb_register.l); break;
	case 0x86: op_res(0, &gb_memory[gb_register.hl]); break;
	case 0x87: op_res(0, &gb_register.a); break;
	case 0x88: op_res(1, &gb_register.b); break;
	case 0x89: op_res(1, &gb_register.c); break;
	case 0x8A: op_res(1, &gb_register.d); break;
	case 0x8B: op_res(1, &gb_register.e); break;
	case 0x8C: op_res(1, &gb_register.h); break;
	case 0x8D: op_res(1, &gb_register.l); break;
	case 0x8E: op_res(1, &gb_memory[gb_register.hl]); break;
	case 0x8F: op_res(1, &gb_register.a); break;
	case 0x90: op_res(2, &gb_register.b); break;
	case 0x91: op_res(2, &gb_register.c); break;
	case 0x92: op_res(2, &gb_register.d); break;
	case 0x93: op_res(2, &gb_register.e); break;
	case 0x94: op_res(2, &gb_register.h); break;
	case 0x95: op_res(2, &gb_register.l); break;
	case 0x96: op_res(2, &gb_memory[gb_register.hl]); break;
	case 0x97: op_res(2, &gb_register.a); break;
	case 0x98: op_res(3, &gb_register.b); break;
	case 0x99: op_res(3, &gb_register.c); break;
	case 0x9A: op_res(3, &gb_register.d); break;
	case 0x9B: op_res(3, &gb_register.e); break;
	case 0x9C: op_res(3, &gb_register.h); break;
	case 0x9D: op_res(3, &gb_register.l); break;
	case 0x9E: op_res(3, &gb_memory[gb_register.hl]); break;
	case 0x9F: op_res(3, &gb_register.a); break;
	case 0xA0: op_res(4, &gb_register.b); break;
	case 0xA1: op_res(4, &gb_register.c); break;
	case 0xA2: op_res(4, &gb_register.d); break;
	case 0xA3: op_res(4, &gb_register.e); break;
	case 0xA4: op_res(4, &gb_register.h); break;
	case 0xA5: op_res(4, &gb_register.l); break;
	case 0xA6: op_res(4, &gb_memory[gb_register.hl]); break;
	case 0xA7: op_res(4, &gb_register.a); break;
	case 0xA8: op_res(5, &gb_register.b); break;
	case 0xA9: op_res(5, &gb_register.c); break;
	case 0xAA: op_res(5, &gb_register.d); break;
	case 0xAB: op_res(5, &gb_register.e); break;
	case 0xAC: op_res(5, &gb_register.h); break;
	case 0xAD: op_res(5, &gb_register.l); break;
	case 0xAE: op_res(5, &gb_memory[gb_register.hl]); break;
	case 0xAF: op_res(5, &gb_register.a); break;
	case 0xB0: op_res(6, &gb_register.b); break;
	case 0xB1: op_res(6, &gb_register.c); break;
	case 0xB2: op_res(6, &gb_register.d); break;
	case 0xB3: op_res(6, &gb_register.e); break;
	case 0xB4: op_res(6, &gb_register.h); break;
	case 0xB5: op_res(6, &gb_register.l); break;
	case 0xB6: op_res(6, &gb_memory[gb_register.hl]); break;
	case 0xB7: op_res(6, &gb_register.a); break;
	case 0xB8: op_res(7, &gb_register.b); break;
	case 0xB9: op_res(7, &gb_register.c); break;
	case 0xBA: op_res(7, &gb_register.d); break;
	case 0xBB: op_res(7, &gb_register.e); break;
	case 0xBC: op_res(7, &gb_register.h); break;
	case 0xBD: op_res(7, &gb_register.l); break;
	case 0xBE: op_res(7, &gb_memory[gb_register.hl]); break;
	case 0xBF: op_res(7, &gb_register.a); break;

	// SET BIT OPERATIONS
	case 0xC0: op_set(0, &gb_register.b); break;
	case 0xC1: op_set(0, &gb_register.c); break;
	case 0xC2: op_set(0, &gb_register.d); break;
	case 0xC3: op_set(0, &gb_register.e); break;
	case 0xC4: op_set(0, &gb_register.h); break;
	case 0xC5: op_set(0, &gb_register.l); break;
	case 0xC6: op_set(0, &gb_memory[gb_register.hl]); break;
	case 0xC7: op_set(0, &gb_register.a); break;
	case 0xC8: op_set(1, &gb_register.b); break;
	case 0xC9: op_set(1, &gb_register.c); break;
	case 0xCA: op_set(1, &gb_register.d); break;
	case 0xCB: op_set(1, &gb_register.e); break;
	case 0xCC: op_set(1, &gb_register.h); break;
	case 0xCD: op_set(1, &gb_register.l); break;
	case 0xCE: op_set(1, &gb_memory[gb_register.hl]); break;
	case 0xCF: op_set(1, &gb_register.a); break;
	case 0xD0: op_set(2, &gb_register.b); break;
	case 0xD1: op_set(2, &gb_register.c); break;
	case 0xD2: op_set(2, &gb_register.d); break;
	case 0xD3: op_set(2, &gb_register.e); break;
	case 0xD4: op_set(2, &gb_register.h); break;
	case 0xD5: op_set(2, &gb_register.l); break;
	case 0xD6: op_set(2, &gb_memory[gb_register.hl]); break;
	case 0xD7: op_set(2, &gb_register.a); break;
	case 0xD8: op_set(3, &gb_register.b); break;
	case 0xD9: op_set(3, &gb_register.c); break;
	case 0xDA: op_set(3, &gb_register.d); break;
	case 0xDB: op_set(3, &gb_register.e); break;
	case 0xDC: op_set(3, &gb_register.h); break;
	case 0xDD: op_set(3, &gb_register.l); break;
	case 0xDE: op_set(3, &gb_memory[gb_register.hl]); break;
	case 0xDF: op_set(3, &gb_register.a); break;
	case 0xE0: op_set(4, &gb_register.b); break;
	case 0xE1: op_set(4, &gb_register.c); break;
	case 0xE2: op_set(4, &gb_register.d); break;
	case 0xE3: op_set(4, &gb_register.e); break;
	case 0xE4: op_set(4, &gb_register.h); break;
	case 0xE5: op_set(4, &gb_register.l); break;
	case 0xE6: op_set(4, &gb_memory[gb_register.hl]); break;
	case 0xE7: op_set(4, &gb_register.a); break;
	case 0xE8: op_set(5, &gb_register.b); break;
	case 0xE9: op_set(5, &gb_register.c); break;
	case 0xEA: op_set(5, &gb_register.d); break;
	case 0xEB: op_set(5, &gb_register.e); break;
	case 0xEC: op_set(5, &gb_register.h); break;
	case 0xED: op_set(5, &gb_register.l); break;
	case 0xEE: op_set(5, &gb_memory[gb_register.hl]); break;
	case 0xEF: op_set(5, &gb_register.a); break;
	case 0xF0: op_set(6, &gb_register.b); break;
	case 0xF1: op_set(6, &gb_register.c); break;
	case 0xF2: op_set(6, &gb_register.d); break;
	case 0xF3: op_set(6, &gb_register.e); break;
	case 0xF4: op_set(6, &gb_register.h); break;
	case 0xF5: op_set(6, &gb_register.l); break;
	case 0xF6: op_set(6, &gb_memory[gb_register.hl]); break;
	case 0xF7: op_set(6, &gb_register.a); break;
	case 0xF8: op_set(7, &gb_register.b); break;
	case 0xF9: op_set(7, &gb_register.c); break;
	case 0xFA: op_set(7, &gb_register.d); break;
	case 0xFB: op_set(7, &gb_register.e); break;
	case 0xFC: op_set(7, &gb_register.h); break;
	case 0xFD: op_set(7, &gb_register.l); break;
	case 0xFE: op_set(7, &gb_memory[gb_register.hl]); break;
	case 0xFF: op_set(7, &gb_register.a); break;

	default:
		printf("Error: Unknown prefixed opcode `%02X'\n", opcode);
		exit(EXIT_FAILURE);
		break;
	}
}

void print_debug() {
	printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
	gb_register.a, gb_register.f, gb_register.b, gb_register.c, gb_register.d, gb_register.e, gb_register.h, gb_register.l,
	gb_register.sp, gb_register.pc, gb_memory[gb_register.pc], gb_memory[gb_register.pc+1], gb_memory[gb_register.pc+2], gb_memory[gb_register.pc+3]);
}

void op_add(uint8_t value) {
	gb_register.flags.half_carry = (((gb_register.a & 0x0F) + (value & 0x0F)) & 0x10) == 0x10;
	gb_register.flags.carry = (0xFF - gb_register.a) < value;
	gb_register.a += value;
	gb_register.flags.zero = !(gb_register.a);
	gb_register.flags.subtract = 0;
}

void op_adc(uint8_t value) {
	int oldcarry = gb_register.flags.carry;
	gb_register.flags.half_carry = (((gb_register.a & 0x0F) + (value & 0x0F) + oldcarry) & 0x10) == 0x10;
	gb_register.flags.carry = (value == 0xFF && oldcarry == 1) || ((0xFF - gb_register.a) < value + oldcarry);
	gb_register.flags.subtract = 0;
	gb_register.a += value + oldcarry;
	gb_register.flags.zero = !gb_register.a;
}

void op_sub(uint8_t value) {
	gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (value & 0x0F)) & 0x10) == 0x10;
	gb_register.flags.carry = gb_register.a < value;
	gb_register.a -= value;
	gb_register.flags.zero = !(gb_register.a);
	gb_register.flags.subtract = 1;
}

void op_sbc(uint8_t value) {
	int oldcarry = gb_register.flags.carry;
	gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (value & 0x0F) - oldcarry) & 0x10) == 0x10;
	gb_register.flags.carry = (value == 0xFF && oldcarry == 1) || (gb_register.a < value + oldcarry);
	gb_register.flags.subtract = 1;
	gb_register.a -= value + oldcarry;
	gb_register.flags.zero = !gb_register.a;
}

void op_inc(uint8_t *reg) {
	(*reg)++;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.half_carry = !(*reg & 0x0F);
	gb_register.flags.subtract = 0;
}

void op_dec(uint8_t *reg) {
	(*reg)--;
	gb_register.flags.zero = !(*reg);
	gb_register.flags.half_carry = (*reg & 0x0F) == 0x0F;
	gb_register.flags.subtract = 1;
}

void op_jump(bool condition, uint16_t address) {
	if (condition) gb_register.pc = address;
}

void op_ret(bool condition) {
	if (condition) gb_register.pc = pop_stack();
}

void op_jr(bool condition) {
	int8_t relative_address = (int8_t)read_byte();
	if (condition) gb_register.pc += relative_address;
}

void op_and(uint8_t value) {
	gb_register.a &= value;
	gb_register.f = 0;
	gb_register.flags.zero = !gb_register.a;
	gb_register.flags.half_carry = 1;
}
void op_or(uint8_t value) {
	gb_register.a |= value;
	gb_register.f = 0;
	gb_register.flags.zero = !gb_register.a;
}

void op_xor(uint8_t value) {
	gb_register.a ^= value;
	gb_register.f = 0;
	gb_register.flags.zero = !gb_register.a;
}

void op_cp(uint8_t value) {
	gb_register.flags.zero = !(gb_register.a - value);
	gb_register.flags.subtract = 1;
	gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (value & 0x0F)) & 0x10) == 0x10;
	gb_register.flags.carry = value > gb_register.a;
}

void op_add_16bit(uint16_t value) {
	gb_register.flags.half_carry = (((gb_register.hl & 0x0FFF) + (value & 0x0FFF)) & 0x1000) == 0x1000;
	gb_register.flags.carry = (0xFFFF - gb_register.hl) < value;
	gb_register.hl += value;
	gb_register.flags.subtract = 0;
}

void op_call(bool condition) {
	uint16_t address = read_word();
	if (condition) {
		push_stack(gb_register.pc);
		gb_register.pc = address;
	}
}

void op_daa() {
	int offset = 0;
	if (!gb_register.flags.subtract) {
		if ((gb_register.a & 0x0F) > 0x09 || gb_register.flags.half_carry)
			offset |= 0x06;
		if (gb_register.a > 0x99 || gb_register.flags.carry)
			offset |= 0x60;
		gb_register.flags.carry |= (gb_register.a > (0xFF - offset));
		gb_register.a += offset;
	} else {
		if (gb_register.flags.half_carry) offset |= 0x06;
		if (gb_register.flags.carry)      offset |= 0x60;
		gb_register.a -= offset;
	}
	gb_register.flags.zero = !gb_register.a;
	gb_register.flags.half_carry = 0;
}

void op_nop() {
	;
}

void handle_interrupt(uint8_t interrupts) {
	if (!interrupts) return;

	interrupt_flag = false;
	push_stack(gb_register.pc);

	if (interrupts & 0x01) {
		// VBLANK INTERRUPT
		gb_register.pc = 0x0040;
		gb_memory[0xFF0F] &= ~((uint8_t)0x01);
	}
	if ((interrupts >> 1) & 0x01) {
		// LCD INTERRUPT
		gb_register.pc = 0x0048;
		gb_memory[0xFF0F] &= ~((uint8_t)0x01 << 1);
	}
	if ((interrupts >> 2) & 0x01) {
		// TIMER INTERRUPT
		gb_register.pc = 0x0050;
		gb_memory[0xFF0F] &= ~((uint8_t)0x01 << 2);
	}
	if ((interrupts >> 3) & 0x01) {
		// SERIAL INTERRUPT
		gb_register.pc = 0x0058;
		gb_memory[0xFF0F] &= ~((uint8_t)0x01 << 3);
	}
	if ((interrupts >> 4) & 0x01) {
		// JOYPAD INTERRUPT
		gb_register.pc = 0x0060;
		gb_memory[0xFF0F] &= ~((uint8_t)0x01 << 4);
	}
}

int main(int argc, char *argv[]) {
	if (argc == 1) {
		fprintf(stderr, "Error: No rom file specified\n");
		exit(EXIT_FAILURE);
	}
	else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	load_rom(argv[1], 32 * 1024);

	// Inital state of registers
	gb_register.a = 0x01;
	gb_register.f = 0xB0;
	gb_register.b = 0x00;
	gb_register.c = 0x13;
	gb_register.d = 0x00;
	gb_register.e = 0xD8;
	gb_register.h = 0x01;
	gb_register.l = 0x4D;
	gb_register.sp = 0xFFFE;
	gb_register.pc = 0x0100;

	// The gameboy doctor test suite requires that the LY register always returns 0x90
	gb_memory[0xFF44] = 0x90;

	while (true) {
		print_debug();
		if (interrupt_flag)
			handle_interrupt(gb_memory[0xFF0F] & gb_memory[0xFFFF] & 0x1F);

		uint8_t op_byte = gb_memory[gb_register.pc++];
		switch (op_byte) {

		// LOAD OPERATIONS (These are in sequential order)
		case 0x40: gb_register.b = gb_register.b; break;
		case 0x41: gb_register.b = gb_register.c; break;
		case 0x42: gb_register.b = gb_register.d; break;
		case 0x43: gb_register.b = gb_register.e; break;
		case 0x44: gb_register.b = gb_register.h; break;
		case 0x45: gb_register.b = gb_register.l; break;
		case 0x46: gb_register.b = gb_memory[gb_register.hl]; break;
		case 0x47: gb_register.b = gb_register.a; break;

		case 0x48: gb_register.c = gb_register.b; break;
		case 0x49: gb_register.c = gb_register.c; break;
		case 0x4A: gb_register.c = gb_register.d; break;
		case 0x4B: gb_register.c = gb_register.e; break;
		case 0x4C: gb_register.c = gb_register.h; break;
		case 0x4D: gb_register.c = gb_register.l; break;
		case 0x4E: gb_register.c = gb_memory[gb_register.hl]; break;
		case 0x4F: gb_register.c = gb_register.a; break;

		case 0x50: gb_register.d = gb_register.b; break;
		case 0x51: gb_register.d = gb_register.c; break;
		case 0x52: gb_register.d = gb_register.d; break;
		case 0x53: gb_register.d = gb_register.e; break;
		case 0x54: gb_register.d = gb_register.h; break;
		case 0x55: gb_register.d = gb_register.l; break;
		case 0x56: gb_register.d = gb_memory[gb_register.hl]; break;
		case 0x57: gb_register.d = gb_register.a; break;

		case 0x58: gb_register.e = gb_register.b; break;
		case 0x59: gb_register.e = gb_register.c; break;
		case 0x5A: gb_register.e = gb_register.d; break;
		case 0x5B: gb_register.e = gb_register.e; break;
		case 0x5C: gb_register.e = gb_register.h; break;
		case 0x5D: gb_register.e = gb_register.l; break;
		case 0x5E: gb_register.e = gb_memory[gb_register.hl]; break;
		case 0x5F: gb_register.e = gb_register.a; break;

		case 0x60: gb_register.h = gb_register.b; break;
		case 0x61: gb_register.h = gb_register.c; break;
		case 0x62: gb_register.h = gb_register.d; break;
		case 0x63: gb_register.h = gb_register.e; break;
		case 0x64: gb_register.h = gb_register.h; break;
		case 0x65: gb_register.h = gb_register.l; break;
		case 0x66: gb_register.h = gb_memory[gb_register.hl]; break;
		case 0x67: gb_register.h = gb_register.a; break;

		case 0x68: gb_register.l = gb_register.b; break;
		case 0x69: gb_register.l = gb_register.c; break;
		case 0x6A: gb_register.l = gb_register.d; break;
		case 0x6B: gb_register.l = gb_register.e; break;
		case 0x6C: gb_register.l = gb_register.h; break;
		case 0x6D: gb_register.l = gb_register.l; break;
		case 0x6E: gb_register.l = gb_memory[gb_register.hl]; break;
		case 0x6F: gb_register.l = gb_register.a; break;

		case 0x70: gb_memory[gb_register.hl] = gb_register.b; break;
		case 0x71: gb_memory[gb_register.hl] = gb_register.c; break;
		case 0x72: gb_memory[gb_register.hl] = gb_register.d; break;
		case 0x73: gb_memory[gb_register.hl] = gb_register.e; break;
		case 0x74: gb_memory[gb_register.hl] = gb_register.h; break;
		case 0x75: gb_memory[gb_register.hl] = gb_register.l; break;
		case 0x77: gb_memory[gb_register.hl] = gb_register.a; break;

		case 0x78: gb_register.a = gb_register.b; break;
		case 0x79: gb_register.a = gb_register.c; break;
		case 0x7A: gb_register.a = gb_register.d; break;
		case 0x7B: gb_register.a = gb_register.e; break;
		case 0x7C: gb_register.a = gb_register.h; break;
		case 0x7D: gb_register.a = gb_register.l; break;
		case 0x7E: gb_register.a = gb_memory[gb_register.hl]; break;
		case 0x7F: gb_register.a = gb_register.a; break;

		// LOAD IMMEDIATE OPERATIONS
		case 0x06: gb_register.b = read_byte(); break;
		case 0x0E: gb_register.c = read_byte(); break;
		case 0x16: gb_register.d = read_byte(); break;
		case 0x1E: gb_register.e = read_byte(); break;
		case 0x26: gb_register.h = read_byte(); break;
		case 0x2E: gb_register.l = read_byte(); break;
		case 0x36: gb_memory[gb_register.hl] = read_byte(); break;
		case 0x3E: gb_register.a = read_byte(); break;

		case 0x01: gb_register.bc = read_word(); break;
		case 0x11: gb_register.de = read_word(); break;
		case 0x21: gb_register.hl = read_word(); break;
		case 0x31: gb_register.sp = read_word(); break;

		// LOAD IMMEDIATE ADDRESS
		case 0xEA: gb_memory[read_word()] = gb_register.a; break;
		case 0xFA: gb_register.a = gb_memory[read_word()]; break;

		// LOAD HIGH OPERATIONS
		case 0xE0: gb_memory[0xFF00 | read_byte()] = gb_register.a; break;
		case 0xE2: gb_memory[0xFF00 | gb_register.c] = gb_register.a; break;
		case 0xF0: gb_register.a = gb_memory[0xFF00 | read_byte()]; break;
		case 0xF2: gb_register.a = gb_memory[0xFF00 | gb_register.c]; break;

		// LOAD ADDRESS AT REGISTER WITH A
		case 0x02: gb_memory[gb_register.bc] = gb_register.a; break;
		case 0x12: gb_memory[gb_register.de] = gb_register.a; break;
		case 0x0A: gb_register.a = gb_memory[gb_register.bc]; break;
		case 0x1A: gb_register.a = gb_memory[gb_register.de]; break;

		// LOAD AND INCREMENT / DECREMENT OPERATIONS
		case 0x22: gb_memory[gb_register.hl++] = gb_register.a; break;
		case 0x32: gb_memory[gb_register.hl--] = gb_register.a; break;
		case 0x2A: gb_register.a = gb_memory[gb_register.hl++]; break;
		case 0x3A: gb_register.a = gb_memory[gb_register.hl--]; break;

		// RST OPERATIONS
		case 0xC7: push_stack(gb_register.pc); gb_register.pc = 0x00; break;
		case 0xCF: push_stack(gb_register.pc); gb_register.pc = 0x08; break;
		case 0xD7: push_stack(gb_register.pc); gb_register.pc = 0x10; break;
		case 0xDF: push_stack(gb_register.pc); gb_register.pc = 0x18; break;
		case 0xE7: push_stack(gb_register.pc); gb_register.pc = 0x20; break;
		case 0xEF: push_stack(gb_register.pc); gb_register.pc = 0x28; break;
		case 0xF7: push_stack(gb_register.pc); gb_register.pc = 0x30; break;
		case 0xFF: push_stack(gb_register.pc); gb_register.pc = 0x38; break;

		// JP OPERATIONS
		case 0xC2: op_jump(!gb_register.flags.zero,  read_word()); break;
		case 0xC3: op_jump(true,                     read_word()); break;
		case 0xCA: op_jump(gb_register.flags.zero,   read_word()); break;
		case 0xD2: op_jump(!gb_register.flags.carry, read_word()); break;
		case 0xDA: op_jump(gb_register.flags.carry,  read_word()); break;
		case 0xE9: op_jump(true,                     gb_register.hl); break;

		// JR OPERATIONS
		case 0x18: op_jr(true); break;
		case 0x20: op_jr(!gb_register.flags.zero); break;
		case 0x28: op_jr(gb_register.flags.zero); break;
		case 0x30: op_jr(!gb_register.flags.carry); break;
		case 0x38: op_jr(gb_register.flags.carry); break;

		// RET OPERATIONS
		case 0xC0: op_ret(!gb_register.flags.zero); break;
		case 0xC8: op_ret(gb_register.flags.zero); break;
		case 0xC9: op_ret(true); break;
		case 0xD0: op_ret(!gb_register.flags.carry); break;
		case 0xD8: op_ret(gb_register.flags.carry); break;
		case 0xD9: op_ret(true); interrupt_flag = true; break;

		// CALL OPERATIONS
		case 0xC4: op_call(!gb_register.flags.zero); break;
		case 0xCC: op_call(gb_register.flags.zero); break;
		case 0xCD: op_call(true); break;
		case 0xD4: op_call(!gb_register.flags.carry); break;
		case 0xDC: op_call(gb_register.flags.carry); break;

		// INC OPERATIONS
		case 0x04: op_inc(&gb_register.b); break;
		case 0x0C: op_inc(&gb_register.c); break;
		case 0x14: op_inc(&gb_register.d); break;
		case 0x1C: op_inc(&gb_register.e); break;
		case 0x24: op_inc(&gb_register.h); break;
		case 0x2C: op_inc(&gb_register.l); break;
		case 0x34: op_inc(gb_memory + gb_register.hl); break;
		case 0x3C: op_inc(&gb_register.a); break;
		case 0x03: gb_register.bc++; break;
		case 0x13: gb_register.de++; break;
		case 0x23: gb_register.hl++; break;
		case 0x33: gb_register.sp++; break;

		// DEC OPERATIONS
		case 0x05: op_dec(&gb_register.b); break;
		case 0x0D: op_dec(&gb_register.c); break;
		case 0x15: op_dec(&gb_register.d); break;
		case 0x1D: op_dec(&gb_register.e); break;
		case 0x25: op_dec(&gb_register.h); break;
		case 0x2D: op_dec(&gb_register.l); break;
		case 0x35: op_dec(gb_memory + gb_register.hl); break;
		case 0x3D: op_dec(&gb_register.a); break;
		case 0x0B: gb_register.bc--; break;
		case 0x1B: gb_register.de--; break;
		case 0x2B: gb_register.hl--; break;
		case 0x3B: gb_register.sp--; break;

		// XOR OPERATIONS
		case 0xAF: op_xor(gb_register.a); break;
		case 0xAE: op_xor(gb_memory[gb_register.hl]); break;
		case 0xAD: op_xor(gb_register.l); break;
		case 0xAC: op_xor(gb_register.h); break;
		case 0xAB: op_xor(gb_register.e); break;
		case 0xAA: op_xor(gb_register.d); break;
		case 0xA9: op_xor(gb_register.c); break;
		case 0xA8: op_xor(gb_register.b); break;

		// AND OPERATIONS
		case 0xA7: op_and(gb_register.a); break;
		case 0xA6: op_and(gb_memory[gb_register.hl]); break;
		case 0xA5: op_and(gb_register.l); break;
		case 0xA4: op_and(gb_register.h); break;
		case 0xA3: op_and(gb_register.e); break;
		case 0xA2: op_and(gb_register.d); break;
		case 0xA1: op_and(gb_register.c); break;
		case 0xA0: op_and(gb_register.b); break;

		// SBC OPERATIONS
		case 0x9F: op_sbc(gb_register.a); break;
		case 0x9E: op_sbc(gb_memory[gb_register.hl]); break;
		case 0x9D: op_sbc(gb_register.l); break;
		case 0x9C: op_sbc(gb_register.h); break;
		case 0x9B: op_sbc(gb_register.e); break;
		case 0x9A: op_sbc(gb_register.d); break;
		case 0x99: op_sbc(gb_register.c); break;
		case 0x98: op_sbc(gb_register.b); break;

		// SUB OPERATIONS
		case 0x97: op_sub(gb_register.a); break;
		case 0x96: op_sub(gb_memory[gb_register.hl]); break;
		case 0x95: op_sub(gb_register.l); break;
		case 0x94: op_sub(gb_register.h); break;
		case 0x93: op_sub(gb_register.e); break;
		case 0x92: op_sub(gb_register.d); break;
		case 0x91: op_sub(gb_register.c); break;
		case 0x90: op_sub(gb_register.b); break;

		// ADC OPERATIONS
		case 0x8F: op_adc(gb_register.a); break;
		case 0x8E: op_adc(gb_memory[gb_register.hl]); break;
		case 0x8D: op_adc(gb_register.l); break;
		case 0x8C: op_adc(gb_register.h); break;
		case 0x8B: op_adc(gb_register.e); break;
		case 0x8A: op_adc(gb_register.d); break;
		case 0x89: op_adc(gb_register.c); break;
		case 0x88: op_adc(gb_register.b); break;

		// ADD OPERATIONS
		case 0x87: op_add(gb_register.a); break;
		case 0x86: op_add(gb_memory[gb_register.hl]); break;
		case 0x85: op_add(gb_register.l); break;
		case 0x84: op_add(gb_register.h); break;
		case 0x83: op_add(gb_register.e); break;
		case 0x82: op_add(gb_register.d); break;
		case 0x81: op_add(gb_register.c); break;
		case 0x80: op_add(gb_register.b); break;

		// CP OPERATIONS
		case 0xBF: op_cp(gb_register.a); break;
		case 0xBE: op_cp(gb_memory[gb_register.hl]); break;
		case 0xBD: op_cp(gb_register.l); break;
		case 0xBC: op_cp(gb_register.h); break;
		case 0xBB: op_cp(gb_register.e); break;
		case 0xBA: op_cp(gb_register.d); break;
		case 0xB9: op_cp(gb_register.c); break;
		case 0xB8: op_cp(gb_register.b); break;

		// OR OPERATIONS
		case 0xB7: op_or(gb_register.a); break;
		case 0xB6: op_or(gb_memory[gb_register.hl]); break;
		case 0xB5: op_or(gb_register.l); break;
		case 0xB4: op_or(gb_register.h); break;
		case 0xB3: op_or(gb_register.e); break;
		case 0xB2: op_or(gb_register.d); break;
		case 0xB1: op_or(gb_register.c); break;
		case 0xB0: op_or(gb_register.b); break;

		// OPERATIONS BETWEEN A AND AN IMMEDIATE
		case 0xC6: op_add(read_byte()); break;
		case 0xD6: op_sub(read_byte()); break;
		case 0xE6: op_and(read_byte()); break;
		case 0xF6: op_or(read_byte());  break;
		case 0xCE: op_adc(read_byte()); break;
		case 0xDE: op_sbc(read_byte()); break;
		case 0xEE: op_xor(read_byte()); break;
		case 0xFE: op_cp(read_byte());  break;

		// PUSH OPERATIONS
		case 0xC5: push_stack(gb_register.bc); break;
		case 0xD5: push_stack(gb_register.de); break;
		case 0xE5: push_stack(gb_register.hl); break;
		case 0xF5: push_stack(gb_register.af); break;

		// POP OPERATIONS
		case 0xC1: gb_register.bc = pop_stack(); break;
		case 0xD1: gb_register.de = pop_stack(); break;
		case 0xE1: gb_register.hl = pop_stack(); break;
		case 0xF1: gb_register.af = pop_stack();
			// Need to clear the unused part of F
			gb_register.flags.unused = 0;
			break;

		// 16-bit ADD OPERATIONS
		case 0x09: op_add_16bit(gb_register.bc); break;
		case 0x19: op_add_16bit(gb_register.de); break;
		case 0x29: op_add_16bit(gb_register.hl); break;
		case 0x39: op_add_16bit(gb_register.sp); break;

		// ROTATIONS ON REGISTER A
		case 0x07: op_rlc(&gb_register.a); gb_register.flags.zero = 0; break;
		case 0x0F: op_rrc(&gb_register.a); gb_register.flags.zero = 0; break;
		case 0x17: op_rl(&gb_register.a);  gb_register.flags.zero = 0; break;
		case 0x1F: op_rr(&gb_register.a);  gb_register.flags.zero = 0; break;

		// FLAG OPERATIONS
		case 0xF3: interrupt_flag = false; break;
		case 0xFB: interrupt_flag = true;  break;
		case 0x3F: // CCF
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 0;
			gb_register.flags.carry = !gb_register.flags.carry;
			break;

		case 0x37: // SCF
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 0;
			gb_register.flags.carry = 1;
			break;
			
		case 0x2F: // CPL
			gb_register.a = ~gb_register.a;
			gb_register.flags.subtract = 1;
			gb_register.flags.half_carry = 1;
			break;


		// SPECIAL STACK POINTER OPERATIONS
		case 0xF9: // LD SP HL
			gb_register.sp = gb_register.hl;
			break;

		case 0x08: // LD (u16) SP
		{
			uint16_t address = read_word();
			gb_memory[address] = gb_register.sp_lsb;
			gb_memory[address+1] = gb_register.sp_msb;
			break;
		}

		case 0xF8: // LD HL SP+i8
		{
			uint8_t next = read_byte();
			gb_register.f = 0;
			gb_register.flags.half_carry = (((gb_register.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = (gb_register.sp & 0x00FF) + next > 0x00FF;
			gb_register.hl = gb_register.sp + (int8_t)next;
			break;
		}

		case 0xE8: // ADD SP i8
		{
			uint8_t next = read_byte();
			gb_register.f = 0;
			gb_register.flags.half_carry = (((gb_register.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = (gb_register.sp & 0x00FF) + next > 0x00FF;
			gb_register.sp += (int8_t)next;
			break;
		}
		
		case 0x27: // DAA instruction
			op_daa();
			break;

		case 0xCB: // Rotate, shift, and bit operations
			process_extra_opcodes(read_byte());
			break;

		case 0x00: // NOP: do nothing
			op_nop();
			break;

		default:
			printf("Error: Op Code 0x%02X is not implemented\n", op_byte);
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
