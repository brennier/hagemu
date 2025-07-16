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

void process_extra_opcodes(uint8_t opcode) {
	switch (opcode) {

	case 0x00: // RLC B
	{
		uint8_t value = gb_register.b;
		int highest_bit = (value & 0x80) >> 7;
		value <<= 1;
		value += highest_bit;
		gb_register.f = 0;
		gb_register.flags.zero = !value;
		gb_register.flags.carry = highest_bit;
		gb_register.b = value;
		break;
	}

	case 0x06: // RLC (HL)
	{
		uint8_t value = gb_memory[gb_register.hl];
		int highest_bit = (value & 0x80) >> 7;
		value <<= 1;
		value += highest_bit;
		gb_register.f = 0;
		gb_register.flags.zero = !value;
		gb_register.flags.carry = highest_bit;
		gb_memory[gb_register.hl] = value;
		break;
	}

	case 0x0E: // RRC (HL)
	{
		uint8_t value = gb_memory[gb_register.hl];
		int lowest_bit = value % 2;
		value >>= 1;
		value += (lowest_bit << 7);
		gb_register.f = 0;
		gb_register.flags.zero = !value;
		gb_register.flags.carry = lowest_bit;
		gb_memory[gb_register.hl] = value;
		break;
	}

	case 0x1B: // RR E
	{
		int oldcarry = gb_register.flags.carry;
		gb_register.flags.carry = gb_register.e % 2;
		gb_register.e >>= 1;
		gb_register.e += (oldcarry << 7);
		gb_register.flags.zero = !(gb_register.e);
		gb_register.flags.subtract = 0;
		gb_register.flags.half_carry = 0;
		break;
	}

	case 0x1A: // RR D
	{
		int oldcarry = gb_register.flags.carry;
		gb_register.flags.carry = gb_register.d % 2;
		gb_register.d >>= 1;
		gb_register.d += (oldcarry << 7);
		gb_register.flags.zero = !(gb_register.d);
		gb_register.flags.subtract = 0;
		gb_register.flags.half_carry = 0;
		break;
	}

	case 0x19: // RR C
	{
		int oldcarry = gb_register.flags.carry;
		gb_register.flags.carry = gb_register.c % 2;
		gb_register.c >>= 1;
		gb_register.c += (oldcarry << 7);
		gb_register.flags.zero = !(gb_register.c);
		gb_register.flags.subtract = 0;
		gb_register.flags.half_carry = 0;
		break;
	}

	case 0x37: // SWAP A
	{
		uint8_t lower = (gb_register.a & 0x0F);
		uint8_t upper = (gb_register.a & 0xF0);
		gb_register.a = (lower << 4) | (upper >> 4);
		gb_register.flags.zero = !gb_register.a;
		gb_register.flags.subtract = 0;
		gb_register.flags.half_carry = 0;
		gb_register.flags.carry = 0;
		break;
	}

	case 0x38: // SRL B
		gb_register.flags.carry = gb_register.b % 2;
		gb_register.b >>= 1;
		gb_register.flags.subtract = 0;
		gb_register.flags.half_carry = 0;
		gb_register.flags.zero = !gb_register.b;
		break;

	case 0x87: // RES 0 A
		gb_register.a = gb_register.a & (~0x01);
		break;

	default:
		printf("Error: Extra Op Code 0x%02X is not implemented\n", opcode);
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

void op_inc_reg8(uint8_t *reg) {
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

void op_call(bool condition) {
	uint16_t address = read_word();
	if (condition) {
		push_stack(gb_register.pc);
		gb_register.pc = address;
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

		case 0x15: op_dec(&gb_register.d); break;

		case 0xAF: op_xor(gb_register.a); break;
		case 0xAE: op_xor(gb_memory[gb_register.hl]); break;
		case 0xAD: op_xor(gb_register.l); break;
		case 0xAC: op_xor(gb_register.h); break;
		case 0xAB: op_xor(gb_register.e); break;
		case 0xAA: op_xor(gb_register.d); break;
		case 0xA9: op_xor(gb_register.c); break;
		case 0xA8: op_xor(gb_register.b); break;

		case 0xA7: op_and(gb_register.a); break;
		case 0xA6: op_and(gb_memory[gb_register.hl]); break;
		case 0xA5: op_and(gb_register.l); break;
		case 0xA4: op_and(gb_register.h); break;
		case 0xA3: op_and(gb_register.e); break;
		case 0xA2: op_and(gb_register.d); break;
		case 0xA1: op_and(gb_register.c); break;
		case 0xA0: op_and(gb_register.b); break;

		case 0x9F: op_sbc(gb_register.a); break;
		case 0x9E: op_sbc(gb_memory[gb_register.hl]); break;
		case 0x9D: op_sbc(gb_register.l); break;
		case 0x9C: op_sbc(gb_register.h); break;
		case 0x9B: op_sbc(gb_register.e); break;
		case 0x9A: op_sbc(gb_register.d); break;
		case 0x99: op_sbc(gb_register.c); break;
		case 0x98: op_sbc(gb_register.b); break;

		case 0x97: op_sub(gb_register.a); break;
		case 0x96: op_sub(gb_memory[gb_register.hl]); break;
		case 0x95: op_sub(gb_register.l); break;
		case 0x94: op_sub(gb_register.h); break;
		case 0x93: op_sub(gb_register.e); break;
		case 0x92: op_sub(gb_register.d); break;
		case 0x91: op_sub(gb_register.c); break;
		case 0x90: op_sub(gb_register.b); break;

		case 0x8F: op_adc(gb_register.a); break;
		case 0x8E: op_adc(gb_memory[gb_register.hl]); break;
		case 0x8D: op_adc(gb_register.l); break;
		case 0x8C: op_adc(gb_register.h); break;
		case 0x8B: op_adc(gb_register.e); break;
		case 0x8A: op_adc(gb_register.d); break;
		case 0x89: op_adc(gb_register.c); break;
		case 0x88: op_adc(gb_register.b); break;

		case 0x87: op_add(gb_register.a); break;
		case 0x86: op_add(gb_memory[gb_register.hl]); break;
		case 0x85: op_add(gb_register.l); break;
		case 0x84: op_add(gb_register.h); break;
		case 0x83: op_add(gb_register.e); break;
		case 0x82: op_add(gb_register.d); break;
		case 0x81: op_add(gb_register.c); break;
		case 0x80: op_add(gb_register.b); break;

		case 0xBF: op_cp(gb_register.a); break;
		case 0xBD: op_cp(gb_register.l); break;
		case 0xBC: op_cp(gb_register.h); break;
		case 0xBA: op_cp(gb_register.d); break;
		case 0xB9: op_cp(gb_register.c); break;
		case 0xB8: op_cp(gb_register.b); break;

		case 0xB5: op_or(gb_register.l); break;
		case 0xB4: op_or(gb_register.h); break;
		case 0xB3: op_or(gb_register.e); break;
		case 0xB2: op_or(gb_register.d); break;

		case 0x0F: // RRCA
		{
			uint8_t value = gb_register.a;
			int lowest_bit = value % 2;
			value >>= 1;
			value += (lowest_bit << 7);
			gb_register.f = 0;
			gb_register.flags.carry = lowest_bit;
			gb_register.a = value;
			break;
		}

		case 0x17: // RLA
		{
			uint8_t value = gb_register.a;
			int highest_bit = (value & 0x80) >> 7;
			value <<= 1;
			value += gb_register.flags.carry;
			gb_register.f = 0;
			gb_register.flags.carry = highest_bit;
			gb_register.a = value;
			break;
		}

		case 0x07: // RLCA
		{
			uint8_t value = gb_register.a;
			int highest_bit = (value & 0x80) >> 7;
			value <<= 1;
			value += highest_bit;
			gb_register.f = 0;
			gb_register.flags.carry = highest_bit;
			gb_register.a = value;
			break;
		}

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

		case 0xD6: // SUB A u8
			op_sub(read_byte());
			break;

		case 0x34: // INC (HL)
			op_inc_reg8(gb_memory + gb_register.hl);
			break;

		case 0xBE: // CP A (HL)
		{
			uint8_t value = gb_memory[gb_register.hl];
			gb_register.flags.zero = !(gb_register.a - value);
			gb_register.flags.subtract = 1;
			gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (value & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = value > gb_register.a;
			break;
		}

		case 0x3A: // LD A (HL-)
			gb_register.a = gb_memory[gb_register.hl--];
			break;

		case 0x02: // LD (BC) A
			gb_memory[gb_register.bc] = gb_register.a;
			break;

		case 0x0A: // LD A (BC)
			gb_register.a = gb_memory[gb_register.bc];
			break;

		case 0xE8: // ADD SP i8
		{
			uint8_t next = read_byte();
			gb_register.f = 0;
			gb_register.flags.half_carry = (((gb_register.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = (gb_register.sp & 0x00FF) + next > 0x00FF;
			gb_register.sp += (int8_t)next;
			break;
		}

		case 0x33: // INC SP
			gb_register.sp++;
			break;

		case 0x3B: // DEC SP
			gb_register.sp--;
			break;

		case 0x39: // ADD HL SP
			gb_register.flags.half_carry = (((gb_register.hl & 0x0FFF) + (gb_register.sp & 0x0FFF)) & 0x1000) != 0;
			gb_register.flags.carry = (0xFFFF - gb_register.hl) < gb_register.sp;
			gb_register.hl += gb_register.sp;
			gb_register.flags.subtract = 0;
			break;

		case 0x09: // ADD HL BC
			gb_register.flags.half_carry = (((gb_register.hl & 0x0FFF) + (gb_register.bc & 0x0FFF)) & 0x1000) != 0;
			gb_register.flags.carry = (0xFFFF - gb_register.hl) < gb_register.bc;
			gb_register.hl += gb_register.bc;
			gb_register.flags.subtract = 0;
			break;

		case 0x2B: // DEC HL
			gb_register.hl--;
			break;

		case 0xF2: // LD A (FF00 + C)
			gb_register.a = gb_memory[0xFF00 + gb_register.c];
			break;

		case 0xDE: // SBC A u8
		{
			uint8_t next = read_byte();
			int oldcarry = gb_register.flags.carry;
			gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (next & 0x0F) - oldcarry) & 0x10) == 0x10;
			gb_register.flags.carry = (next == 0xFF && oldcarry == 1) || (gb_register.a < next + oldcarry);
			gb_register.flags.subtract = 1;
			gb_register.a -= next + oldcarry;
			gb_register.flags.zero = !gb_register.a;
			break;
		}

		case 0x1E: // LD E u8
			gb_register.e = read_byte();
			break;

		case 0xF6: // OR A u8
			gb_register.a |= read_byte();
			gb_register.f = 0;
			gb_register.flags.zero = !(gb_register.a);
			break;

		case 0xF9: // LD SP,HL
			gb_register.sp = gb_register.hl;
			break;

		case 0x08: // LD (u16) SP
		{
			uint16_t address = read_word();
			gb_memory[address] = gb_register.sp_lsb;
			gb_memory[address+1] = gb_register.sp_msb;
			break;
		}

		case 0x2E: // LD L u8
			gb_register.l = read_byte();
			break;

		case 0x04: // INC B
			op_inc_reg8(&gb_register.b);
			break;

		case 0xBB: // CP A E
		{
			uint8_t next = gb_register.e;
			gb_register.flags.zero = !(gb_register.a - next);
			gb_register.flags.subtract = 1;
			gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = next > gb_register.a;
			break;
		}

		case 0x1D: // DEC E
			op_dec(&gb_register.e);
			break;

		case 0x1B: // DEC DE
			gb_register.de--;
			break;

		case 0x29: // ADD HL HL
			gb_register.flags.half_carry = (((gb_register.hl & 0x0FFF) + (gb_register.hl & 0x0FFF)) & 0x1000) == 0x1000;
			gb_register.flags.carry = (0xFFFF - gb_register.hl) < gb_register.hl;
			gb_register.hl += gb_register.hl;
			gb_register.flags.subtract = 0;
			break;

		case 0x35: // DEC (HL)
		{
			uint8_t num = gb_memory[gb_register.hl];
			num--;
			gb_register.flags.zero = !num;
			gb_register.flags.half_carry = (num & 0x0F) == 0x0F;
			gb_register.flags.subtract = 1;
			gb_memory[gb_register.hl] = num;
			break;
		}

		case 0xB6: // OR A (HL)
			gb_register.a |= gb_memory[gb_register.hl];
			gb_register.f = 0;
			gb_register.flags.zero = !gb_register.a;
			break;

		case 0x3D: // DEC A
			op_dec(&gb_register.a);
			break;

		case 0xCE: // ADC A u8
		{
			uint8_t next = read_byte();
			int oldcarry = gb_register.flags.carry;
			gb_register.flags.half_carry = (((gb_register.a & 0x0F) + (next & 0x0F) + oldcarry) & 0x10) == 0x10;
			gb_register.flags.carry = (next == 0xFF && oldcarry == 1) || ((0xFF - gb_register.a) < next + oldcarry);
			gb_register.flags.subtract = 0;
			gb_register.a += next + oldcarry;
			gb_register.flags.zero = !gb_register.a;
			break;
		}

		case 0x25: // DEC H
			op_dec(&gb_register.h);
			break;

		case 0xEE: // XOR A u8
			gb_register.a ^= read_byte();
			gb_register.f = 0;
			gb_register.flags.zero = !gb_register.a;
			break;

		case 0x1F: // RRA
		{
			int oldcarry = gb_register.flags.carry;
			gb_register.f = 0;
			gb_register.flags.carry = gb_register.a % 2;
			gb_register.a >>= 1;
			gb_register.a += (oldcarry << 7);
			break;
		}

		case 0x26: // LD H u8
			gb_register.h = read_byte();
			break;

		case 0x2D: // DEC L
			op_dec(&gb_register.l);
			break;

		case 0xB7: // OR A A
			gb_register.a |= gb_register.a;
			gb_register.flags.zero = !gb_register.a;
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 0;
			gb_register.flags.carry = 0;
			break;

		case 0xC6: // Add A u8
			op_add(read_byte());
			break;

		case 0x24: // INC H
			op_inc_reg8(&gb_register.h);
			break;

		case 0x2C: // INC L
			op_inc_reg8(&gb_register.l);
			break;

		case 0x3C: // INC A
			op_inc_reg8(&gb_register.a);
			break;

		case 0xC1: // POP BC
			gb_register.bc = pop_stack();
			break;

		case 0x03: // INC BC
			gb_register.bc++;
			break;

		case 0xC5: // PUSH BC
			push_stack(gb_register.bc);
			break;

		case 0xF1: // POP AF
			gb_register.af = pop_stack();
			gb_register.flags.unused = 0;
			break;

		case 0xF5: // PUSH AF
			push_stack(gb_register.af);
			break;

		case 0xF8: // LD HL SP+i8
		{
			uint8_t next = read_byte();
			gb_register.f = 0;
			gb_register.flags.half_carry = (((gb_register.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = (gb_register.sp & 0x00FF) + next > 0x00FF;
			gb_register.hl = gb_register.sp + (int8_t)next;
			break;
		}

		case 0xD1: // POP DE
			gb_register.de = pop_stack();
			break;

		case 0x22: // LD (HL+) A
			gb_memory[gb_register.hl++] = gb_register.a;
			break;

		case 0x1A: // LD A (DE)
			gb_register.a = gb_memory[gb_register.de];
			break;

		case 0xE5: // PUSH HL
			push_stack(gb_register.hl);
			break;

		case 0x13: // INC DE
			gb_register.de++;
			break;

		case 0xD5: // PUSH DE
			push_stack(gb_register.de);
			break;

		case 0x23: // INC HL
			gb_register.hl++;
			break;

		case 0x19: // ADD HL DE
			gb_register.flags.half_carry = (((gb_register.hl & 0x0FFF) + (gb_register.de & 0x0FFF)) & 0x1000) != 0;
			gb_register.flags.carry = (0xFFFF - gb_register.hl) < gb_register.de;
			gb_register.hl += gb_register.de;
			gb_register.flags.subtract = 0;
			break;

		case 0x16: // LD D u8
			gb_register.d = read_byte();
			break;

		case 0xE1: // POP HL
			gb_register.hl = pop_stack();
			break;

		case 0xB0: // OR A B
			gb_register.a |= gb_register.b;
			gb_register.flags.zero = !gb_register.a;
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 0;
			gb_register.flags.carry = 0;
			break;

		case 0xCB: // Rotate, shift, and bit operations
			process_extra_opcodes(read_byte());
			break;

		case 0xE6: // AND A u8
			gb_register.a &= read_byte();
			gb_register.flags.zero = !gb_register.a;
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 1;
			gb_register.flags.carry = 0;
			break;

		case 0x2F: // CPL
			gb_register.a = ~gb_register.a;
			gb_register.flags.subtract = 1;
			gb_register.flags.half_carry = 1;
			break;

		case 0xB1: // OR A C
			gb_register.a |= gb_register.c;
			gb_register.flags.zero = !gb_register.a;
			gb_register.flags.subtract = 0;
			gb_register.flags.half_carry = 0;
			gb_register.flags.carry = 0;
			break;

		case 0x0B: // DEC BC
			gb_register.bc--;
			break;

		case 0x01: // LD BC, u16
			gb_register.bc = read_word();
			break;

		case 0x0C: // INC C
			op_inc_reg8(&gb_register.c);
			break;

		case 0xE2: // LD (FF00+C) A
			gb_memory[0xFF00 | gb_register.c] = gb_register.a;
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
		{
			uint8_t next = read_byte();
			gb_register.flags.zero = !(gb_register.a - next);
			gb_register.flags.subtract = 1;
			gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (next & 0x0F)) & 0x10) == 0x10;
			gb_register.flags.carry = next > gb_register.a;
			break;
		}

		case 0xF0: // LD A (FF00 + u8)
			gb_register.a = gb_memory[0xFF00 | read_byte()];
			break;

		case 0xE0: // LD (FF00 + u8) A
			gb_memory[0xFF00 | read_byte()] = gb_register.a;
			break;

		case 0xF3: // DI Disable interrupts
			interrupt_flag = false;
			break;

		case 0x3E: // LD A u8
			gb_register.a = read_byte();
			break;

		case 0x05: // DEC B
			op_dec(&gb_register.b);
			break;

		case 0x32: // LD (HL-) A
			gb_memory[gb_register.hl--] = gb_register.a;
			break;

		case 0x06: // LD B u8
			gb_register.b = read_byte();
			break;

		case 0x14: // INC D
			op_inc_reg8(&gb_register.d);
			break;

		case 0x0D: // DEC C
			op_dec(&gb_register.c);
			break;

		case 0xFB: // EI Enable interrupts
			interrupt_flag = true;
			break;

		case 0x1C: // INC E
			op_inc_reg8(&gb_register.e);
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

		case 0x21: // LD HL d16: load 16-bit immediate into HL
			gb_register.hl = read_word();
			break;

		case 0x00: // NOP: do nothing
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
