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

void process_extra_opcodes(uint8_t opcode) {
        switch (opcode) {
        
        case 0x37: // SWAP A
                ;
                uint8_t lower = (gb_register.a & 0x0F);
                uint8_t upper = (gb_register.a & 0xF0);
                gb_register.a = (lower << 4) | (upper >> 4);
                gb_register.flags.zero = !gb_register.a;
                gb_register.flags.subtract = 0;
                gb_register.flags.half_carry = 0;
                gb_register.flags.carry = 0;
                break;
        
        case 0x87: // RES 0 A
                gb_register.a = gb_register.a & (~0x01);
                break;

        default:
		fprintf(stderr, "Extra Op Code not implemented: 0x%02X\n", opcode);
		exit(EXIT_FAILURE);
		break;
        }
}

void print_debug() {
        printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
        gb_register.a, gb_register.f, gb_register.b, gb_register.c, gb_register.d, gb_register.e, gb_register.h, gb_register.l, gb_register.sp, gb_register.pc, gb_memory[gb_register.pc], gb_memory[gb_register.pc+1], gb_memory[gb_register.pc+2], gb_memory[gb_register.pc+3]);
}

int main() {
	load_rom("cpu_instrs_01.gb", 32 * 1024);

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

	while (true) {
                print_debug();
		uint8_t op_byte = gb_memory[gb_register.pc++];
		//printf("Processing opcode %02X...\n", op_byte);
		switch (op_byte) {
                
                case 0xC6: // Add A u8
                        ;
                        uint8_t operand = read_byte();
                        gb_register.flags.half_carry = ((gb_register.a & 0x0F) + (operand & 0x0F)) & 0x10;
                        gb_register.flags.carry = (0xFF - gb_register.a) < operand;
                        gb_register.a += operand;
                        gb_register.flags.zero = !gb_register.a;
                        gb_register.flags.subtract = 0;
                        break;

                case 0x24: // INC H
			gb_register.h++;
			gb_register.flags.zero = !gb_register.h;
			gb_register.flags.half_carry = !(gb_register.h & 0x0F);
			gb_register.flags.subtract = 0;
                        break;
                        
                
                case 0x2C: // INC L
			gb_register.l++;
			gb_register.flags.zero = !gb_register.l;
			gb_register.flags.half_carry = !(gb_register.l & 0x0F);
			gb_register.flags.subtract = 0;
                        break;
                
                case 0x77: // LD (HL) A
                        gb_memory[gb_register.hl] = gb_register.a;
                        break;
                        
                case 0xC4: // CALL NZ u16
                        if (!gb_register.flags.zero) {
                                // I need to add 2 so that the saved program counter points after the jump destination
                                push_stack(gb_register.pc+2);
                                gb_register.pc = read_word();
                        }
                        else {
                                gb_register.pc += 2;
                        }
                        break;

                case 0x3C: // INC A
			gb_register.a++;
			gb_register.flags.zero = !gb_register.a;
			gb_register.flags.half_carry = !(gb_register.a & 0x0F);
			gb_register.flags.subtract = 0;
                        break;

                case 0xC1: // POP BC
                        gb_register.bc = pop_stack();
                        break;
                
                case 0x28: // JR Z i8
			if (gb_register.flags.zero)
				gb_register.pc += (int8_t)read_byte();
                        else
                                gb_register.pc++;
			break;
                
                case 0x03: // INC BC
                        gb_register.bc++;
                        break;
                
                case 0xC5: // PUSH BC
                        push_stack(gb_register.bc);
                        break;
                
                case 0xF1: // POP AF
                        gb_register.af = pop_stack();
                        break;
                
                case 0xF5: // PUSH AF
                        push_stack(gb_register.af);
                        break;

		case 0x18: // JR i8
			gb_register.pc += (int8_t)read_byte();
                        break;

                case 0x7D: // LD A L
                        gb_register.a = gb_register.l;
                        break;

                case 0xF8: // LD HL SP+i8
                        gb_register.hl = gb_memory[gb_register.sp + (int8_t)read_byte()];
                        gb_register.flags.zero = 0;
                        gb_register.flags.subtract = 0;
                        // TODO: Figure out the carries
                        //gb_register.flags.carry = ;
                        //gb_register.flags.half_carry = ;
                        printf("Program Counter: %d", gb_register.pc);
                        break;
                
                case 0x7C: // LD A H
                        gb_register.a = gb_register.h;
                        break;
                
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
                
                case 0xE9: // JP HL
                        gb_register.pc = gb_register.hl;
                        break;
                
                case 0xD5: // PUSH DE
                        push_stack(gb_register.de);
                        break;
                
                case 0x56: // LD D (HL)
                        gb_register.d = gb_memory[gb_register.hl];
                        break;
                
                case 0x23: // INC HL
                        gb_register.hl++;
                        break;

                case 0x5E: // LD E (HL)
                        gb_register.e = gb_memory[gb_register.hl];
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

                case 0x5F: // LD E A
                        gb_register.e = gb_register.a;
                        break;

                case 0xE1: // POP HL
                        gb_register.hl = pop_stack();
                        break;

                case 0x87: // ADD A A
                        gb_register.flags.half_carry = (((gb_register.a & 0x0F) + (gb_register.a & 0x0F)) & 0x10) != 0;
                        gb_register.flags.carry = (0xFF - gb_register.a) < gb_register.a;
                        gb_register.a += gb_register.a;
                        gb_register.flags.zero = !gb_register.a;
                        gb_register.flags.subtract = 0;
                        break;

                case 0xEF: // RST 0x28
                        gb_register.sp--;
                        gb_memory[gb_register.sp] = gb_register.pc_msb;
                        gb_register.sp--;
                        gb_memory[gb_register.sp] = gb_register.pc_lsb;
                        gb_register.pc = 0x28;
                        break;

                case 0x79: // LD A C
                        gb_register.a = gb_register.c;
                        break;

                case 0xA1: // AND A C
                        gb_register.a &= gb_register.c;
                        gb_register.flags.zero = !gb_register.a;
                        gb_register.flags.subtract = 0;
                        gb_register.flags.half_carry = 1;
                        gb_register.flags.carry = 0;
                        break;

                case 0xA9: // XOR A C
                        gb_register.a ^= gb_register.c;
                        gb_register.flags.zero = !gb_register.a;
                        gb_register.flags.subtract = 0;
                        gb_register.flags.half_carry = 0;
                        gb_register.flags.carry = 0;
                        break;

                case 0x4F: // LD C A
                        gb_register.c = gb_register.a;
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
                        // I need to add 2 so that the saved program counter points after the jump destination
                        push_stack(gb_register.pc+2);
                        gb_register.pc = read_word();
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
                        gb_register.flags.half_carry = (((gb_register.a & 0x0F) - (next & 0x0F)) & 0x10) != 0;
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
			gb_register.flags.half_carry = (gb_register.b & 0x0F) == 0x0F;
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
			gb_register.flags.half_carry = (gb_register.c & 0x0F) == 0x0F;
			gb_register.flags.zero = !gb_register.c;
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
