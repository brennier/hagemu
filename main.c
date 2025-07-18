#include "include/raylib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SCREENWIDTH 166
#define SCREENHEIGHT 144
#define CARTRIDGE_SIZE 32 * 1024

// TODO:
// - Pass Blargg's interrupt handling
// - Display LCD
// - Add timings
// - Add MMU functions for reading and writing memory
// - Make cpu flags into separate bools
// - Switchable ROM bank
// - Implement STOP and HALT
// - Update timer registers
// - Add bit manipulation macros

// Unions are a wonderful thing
union {
	// Regular 8-bit registers
	struct {
		uint8_t f, a, c, b, e, d, l, h;
	} reg;
	// Wide 16-bit registers
	struct {
		uint16_t af, bc, de, hl, sp, pc;
	} wreg;
	// Various flags
	struct {
		uint8_t unused : 4;
		uint8_t carry : 1;
		uint8_t half_carry : 1;
		uint8_t subtract : 1;
		uint8_t zero : 1;
	} flag;
	uint8_t raw_bytes[12];
} cpu = { 0 };

bool master_interrupt_flag = false;
bool master_interrupt_flag_pending = false;

uint16_t m_cycle_clock = 0;
bool clock_running = true;

void increment_clock() {
	if (clock_running) m_cycle_clock++;
}

enum special_addresses {
	CARTRIDGE_TYPE = 0x0147,
	JOYPAD_INPUT = 0xFF00,
	SERIAL_DATA = 0xFF01,
	SERIAL_CONTROL = 0xFF02,
	DIVIDER_REGISTER = 0xFF04,
	TIMER_COUNTER = 0xFF05,
	TIMER_MODULO = 0xFF06,
	TIMER_CONTROL = 0xFF07,
	INTERRUPT_FLAGS = 0xFF0F,
	BOOT_ROM_CONTROL = 0xFF50,
	INTERRUPT_ENABLE = 0xFFFF,
};

uint8_t rom_memory[CARTRIDGE_SIZE] = { 0 };
// The GB has 64kb of mapped memory
uint8_t gb_memory[64 * 1024]  = { 0 };

int rom_bank_index = 0;

uint8_t mmu_read(uint16_t address) {
	// Handle special cases first
	switch (address) {

	case DIVIDER_REGISTER:
		return ((m_cycle_clock & 0xFF00) >> 8);
	}

	switch (address & 0xF000) {

	// Read from ROM bank 00 (16 KiB)
	case 0x0000: case 0x1000: case 0x2000: case 0x3000:
		return rom_memory[address];

	// Read from switchable ROM bank (16 KiB)
	case 0x4000: case 0x5000: case 0x6000: case 0x7000:
		if (rom_bank_index == 0)
			return rom_memory[address];
		return rom_memory[(address & 0x3FFF) + rom_bank_index * 0x4000];

	// Video Ram (8 KiB)
	case 0x8000: case 0x9000:
		return gb_memory[address];

	// External switchable RAM from cartridge (8 KiB)
	case 0xA000: case 0xB000:
		return gb_memory[address];

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
			return 0;
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

	case DIVIDER_REGISTER:
		m_cycle_clock = 0; return;
	}

	switch (address & 0xF0000) {

	// ROM BANK SWITCH
	case 0x2000: case 0x3000:
		fprintf(stderr, "ROM BANK SWITCHED TO %d \n", value & 0x1F);
		rom_bank_index = value & 0x1F;
		return;
	}

	// Else just write the value normally
	gb_memory[address] = value;
}

uint8_t read_byte() {
	increment_clock();
	return mmu_read(cpu.wreg.pc++);
}

uint16_t read_word() {
	uint8_t first_byte = read_byte();
	uint8_t second_byte = read_byte();
	return ((uint16_t)second_byte << 8) | (uint16_t)first_byte;
}

uint16_t pop_stack() {
	uint8_t lower = mmu_read(cpu.wreg.sp++);
	uint8_t upper = mmu_read(cpu.wreg.sp++);
	return (upper << 8) | lower;
}

void push_stack(uint16_t reg16) {
	uint8_t lower = (reg16 & 0x00FF);
	uint8_t upper = (reg16 & 0xFF00) >> 8;
	cpu.wreg.sp--;
	mmu_write(cpu.wreg.sp, upper);
	cpu.wreg.sp--;
	mmu_write(cpu.wreg.sp, lower);
}

void load_rom(char* rom_name, size_t rom_bytes) {
	FILE *rom_file = fopen(rom_name, "rb"); // binary read mode
	if (rom_file == NULL) {
		fprintf(stderr, "Error: Failed to find the rom file `%s'\n", rom_name);
		exit(EXIT_FAILURE);
	}

	size_t bytes_read = fread(rom_memory, 1, rom_bytes, rom_file);
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
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = highest_bit;
}

void op_rrc(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	(*reg) >>= 1;
	(*reg) += (lowest_bit << 7);
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = lowest_bit;
}

void op_rr(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	(*reg) >>= 1;
	(*reg) += (cpu.flag.carry << 7);
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = lowest_bit;
}

void op_rl(uint8_t* reg) {
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) <<= 1;
	(*reg) += cpu.flag.carry;
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = highest_bit;
}

void op_sla(uint8_t* reg) {
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) <<= 1;
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = highest_bit;
}

void op_sra(uint8_t* reg) {
	int lowest_bit = (*reg) % 2;
	int highest_bit = ((*reg) & 0x80) >> 7;
	(*reg) >>= 1;
	(*reg) += (highest_bit << 7);
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
	cpu.flag.carry = lowest_bit;
}

void op_srl(uint8_t* reg) {
	cpu.flag.carry = (*reg) % 2;
	(*reg) >>= 1;
	cpu.flag.subtract = 0;
	cpu.flag.half_carry = 0;
	cpu.flag.zero = !(*reg);
}

void op_swap(uint8_t* reg) {
	uint8_t lower = ((*reg) & 0x0F);
	uint8_t upper = ((*reg) & 0xF0);
	(*reg) = (lower << 4) | (upper >> 4);
	cpu.reg.f = 0;
	cpu.flag.zero = !(*reg);
}

void op_bit(int bit_num, uint8_t* reg) {
	cpu.flag.subtract = 0;
	cpu.flag.half_carry = 1;
	cpu.flag.zero = !((*reg) & (1 << bit_num));
}

void op_res(int bit_num, uint8_t* reg) {
	(*reg) &= ~((uint8_t)0x01 << bit_num);
}

void op_set(int bit_num, uint8_t* reg) {
	(*reg) |= (1 << bit_num);
}

void process_extra_opcodes(uint8_t opcode) {
	// The lower 4 bits of the opcode determines the register
	uint8_t *location;
	switch (opcode & 0x07) {
		case 0x00: location = &cpu.reg.b; break;
		case 0x01: location = &cpu.reg.c; break;
		case 0x02: location = &cpu.reg.d; break;
		case 0x03: location = &cpu.reg.e; break;
		case 0x04: location = &cpu.reg.h; break;
		case 0x05: location = &cpu.reg.l; break;
		case 0x06: location = &gb_memory[cpu.wreg.hl]; break;
		case 0x07: location = &cpu.reg.a; break;
	}

	// The upper 5 bits of the opcode determines the operation
	switch (opcode & 0xF8) {

	case 0x00: op_rlc(location);    break; // ROTATE LEFT CIRCULAR
	case 0x08: op_rrc(location);    break; // ROTATE RIGHT CIRCULAR
	case 0x10: op_rl(location);     break; // ROTATE LEFT
	case 0x18: op_rr(location);     break; // ROTATE RIGHT
	case 0x20: op_sla(location);    break; // SHIFT LEFT ARITHMETIC
	case 0x28: op_sra(location);    break; // SHIFT RIGHT ARITHEMTIC
	case 0x30: op_swap(location);   break; // SWAP
	case 0x38: op_srl(location);    break; // SHIFT RIGHT LOGICAL

	case 0x40: op_bit(0, location); break; // TEST BIT 0
	case 0x48: op_bit(1, location); break; // TEST BIT 1
	case 0x50: op_bit(2, location); break; // TEST BIT 2
	case 0x58: op_bit(3, location); break; // TEST BIT 3
	case 0x60: op_bit(4, location); break; // TEST BIT 4
	case 0x68: op_bit(5, location); break; // TEST BIT 5
	case 0x70: op_bit(6, location); break; // TEST BIT 6
	case 0x78: op_bit(7, location); break; // TEST BIT 7

	case 0x80: op_res(0, location); break; // RESET BIT 0
	case 0x88: op_res(1, location); break; // RESET BIT 1
	case 0x90: op_res(2, location); break; // RESET BIT 2
	case 0x98: op_res(3, location); break; // RESET BIT 3
	case 0xA0: op_res(4, location); break; // RESET BIT 4
	case 0xA8: op_res(5, location); break; // RESET BIT 5
	case 0xB0: op_res(6, location); break; // RESET BIT 6
	case 0xB8: op_res(7, location); break; // RESET BIT 7

	case 0xC0: op_set(0, location); break; // SET BIT 0
	case 0xC8: op_set(1, location); break; // SET BIT 1
	case 0xD0: op_set(2, location); break; // SET BIT 2
	case 0xD8: op_set(3, location); break; // SET BIT 3
	case 0xE0: op_set(4, location); break; // SET BIT 4
	case 0xE8: op_set(5, location); break; // SET BIT 5
	case 0xF0: op_set(6, location); break; // SET BIT 6
	case 0xF8: op_set(7, location); break; // SET BIT 7

	default:
		printf("Error: Unknown prefixed opcode `%02X'\n", opcode);
		exit(EXIT_FAILURE);
		break;
	}

	increment_clock();
}

void print_debug_gameboy_doctor() {
	printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
	cpu.reg.a, cpu.reg.f, cpu.reg.b, cpu.reg.c, cpu.reg.d, cpu.reg.e, cpu.reg.h, cpu.reg.l,
	       cpu.wreg.sp, cpu.wreg.pc, mmu_read(cpu.wreg.pc), mmu_read(cpu.wreg.pc+1), mmu_read(cpu.wreg.pc+2), mmu_read(cpu.wreg.pc+3));
}

void print_debug_blargg_test() {
	if (mmu_read(SERIAL_CONTROL) == 0x81)
	{
		printf("%c", mmu_read(SERIAL_DATA));
		mmu_write(SERIAL_CONTROL, 0);
	}
}

void op_add(uint8_t value) {
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) + (value & 0x0F)) & 0x10) == 0x10;
	cpu.flag.carry = (0xFF - cpu.reg.a) < value;
	cpu.reg.a += value;
	cpu.flag.zero = !(cpu.reg.a);
	cpu.flag.subtract = 0;
}

void op_adc(uint8_t value) {
	int oldcarry = cpu.flag.carry;
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) + (value & 0x0F) + oldcarry) & 0x10) == 0x10;
	cpu.flag.carry = (value == 0xFF && oldcarry == 1) || ((0xFF - cpu.reg.a) < value + oldcarry);
	cpu.flag.subtract = 0;
	cpu.reg.a += value + oldcarry;
	cpu.flag.zero = !cpu.reg.a;
}

void op_sub(uint8_t value) {
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) - (value & 0x0F)) & 0x10) == 0x10;
	cpu.flag.carry = cpu.reg.a < value;
	cpu.reg.a -= value;
	cpu.flag.zero = !(cpu.reg.a);
	cpu.flag.subtract = 1;
}

void op_sbc(uint8_t value) {
	int oldcarry = cpu.flag.carry;
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) - (value & 0x0F) - oldcarry) & 0x10) == 0x10;
	cpu.flag.carry = (value == 0xFF && oldcarry == 1) || (cpu.reg.a < value + oldcarry);
	cpu.flag.subtract = 1;
	cpu.reg.a -= value + oldcarry;
	cpu.flag.zero = !cpu.reg.a;
}

void op_inc(uint8_t *reg) {
	(*reg)++;
	cpu.flag.zero = !(*reg);
	cpu.flag.half_carry = !(*reg & 0x0F);
	cpu.flag.subtract = 0;
}

void op_dec(uint8_t *reg) {
	(*reg)--;
	cpu.flag.zero = !(*reg);
	cpu.flag.half_carry = (*reg & 0x0F) == 0x0F;
	cpu.flag.subtract = 1;
}

void op_jump(bool condition, uint16_t address) {
	if (condition) cpu.wreg.pc = address;
}

void op_ret(bool condition) {
	if (condition) cpu.wreg.pc = pop_stack();
}

void op_jr(bool condition) {
	int8_t relative_address = (int8_t)read_byte();
	if (condition) cpu.wreg.pc += relative_address;
}

void op_and(uint8_t value) {
	cpu.reg.a &= value;
	cpu.reg.f = 0;
	cpu.flag.zero = !cpu.reg.a;
	cpu.flag.half_carry = 1;
}
void op_or(uint8_t value) {
	cpu.reg.a |= value;
	cpu.reg.f = 0;
	cpu.flag.zero = !cpu.reg.a;
}

void op_xor(uint8_t value) {
	cpu.reg.a ^= value;
	cpu.reg.f = 0;
	cpu.flag.zero = !cpu.reg.a;
}

void op_cp(uint8_t value) {
	cpu.flag.zero = !(cpu.reg.a - value);
	cpu.flag.subtract = 1;
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) - (value & 0x0F)) & 0x10) == 0x10;
	cpu.flag.carry = value > cpu.reg.a;
}

void op_add_16bit(uint16_t value) {
	cpu.flag.half_carry = (((cpu.wreg.hl & 0x0FFF) + (value & 0x0FFF)) & 0x1000) == 0x1000;
	cpu.flag.carry = (0xFFFF - cpu.wreg.hl) < value;
	cpu.wreg.hl += value;
	cpu.flag.subtract = 0;
}

void op_call(bool condition) {
	uint16_t address = read_word();
	if (condition) {
		push_stack(cpu.wreg.pc);
		cpu.wreg.pc = address;
	}
}

void op_daa() {
	int offset = 0;
	if (!cpu.flag.subtract) {
		if ((cpu.reg.a & 0x0F) > 0x09 || cpu.flag.half_carry)
			offset |= 0x06;
		if (cpu.reg.a > 0x99 || cpu.flag.carry)
			offset |= 0x60;
		cpu.flag.carry |= (cpu.reg.a > (0xFF - offset));
		cpu.reg.a += offset;
	} else {
		if (cpu.flag.half_carry) offset |= 0x06;
		if (cpu.flag.carry)      offset |= 0x60;
		cpu.reg.a -= offset;
	}
	cpu.flag.zero = !cpu.reg.a;
	cpu.flag.half_carry = 0;
}

enum interrupt_type {
	VBLANK_INTERRUPT_BIT = (1 << 0),
	LCD_INTERRUPT_BIT    = (1 << 1),
	TIMER_INTERRUPT_BIT  = (1 << 2),
	SERIAL_INTERRUPT_BIT = (1 << 3),
	JOYPAD_INTERRUPT_BIT = (1 << 4),
};

void handle_interrupts() {
	uint8_t interrupts = mmu_read(INTERRUPT_FLAGS);
	interrupts &= mmu_read(INTERRUPT_ENABLE);
	if (!interrupts) return;

	master_interrupt_flag = false;
	push_stack(cpu.wreg.pc);

	if (interrupts & VBLANK_INTERRUPT_BIT) {
		cpu.wreg.pc = 0x0040;
		gb_memory[INTERRUPT_FLAGS] &= ~VBLANK_INTERRUPT_BIT;
	}
	if (interrupts & LCD_INTERRUPT_BIT) {
		cpu.wreg.pc = 0x0048;
		gb_memory[INTERRUPT_FLAGS] &= ~LCD_INTERRUPT_BIT;
	}
	if (interrupts & TIMER_INTERRUPT_BIT) {
		cpu.wreg.pc = 0x0050;
		gb_memory[INTERRUPT_FLAGS] &= ~TIMER_INTERRUPT_BIT;
	}
	if (interrupts & SERIAL_INTERRUPT_BIT) {
		cpu.wreg.pc = 0x0058;
		gb_memory[INTERRUPT_FLAGS] &= ~SERIAL_INTERRUPT_BIT;
	}
	if (interrupts & JOYPAD_INTERRUPT_BIT) {
		cpu.wreg.pc = 0x0060;
		gb_memory[INTERRUPT_FLAGS] &= ~JOYPAD_INTERRUPT_BIT;
	}
}

void process_opcode(uint8_t op_byte);
void test_opcode_timing();

int main(int argc, char *argv[]) {
	// Inital state of registers
	cpu.reg.a = 0x01;
	cpu.reg.f = 0xB0;
	cpu.reg.b = 0x00;
	cpu.reg.c = 0x13;
	cpu.reg.d = 0x00;
	cpu.reg.e = 0xD8;
	cpu.reg.h = 0x01;
	cpu.reg.l = 0x4D;
	cpu.wreg.sp = 0xFFFE;
	cpu.wreg.pc = 0x0100;

	/* test_opcode_timing(); */

	// The gameboy doctor test suite requires that the LY register always returns 0x90
	gb_memory[0xFF44] = 0x90;

	if (argc == 1) {
		fprintf(stderr, "Error: No rom file specified\n");
		exit(EXIT_FAILURE);
	}
	else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	load_rom(argv[1], CARTRIDGE_SIZE);

	while (true) {
		print_debug_gameboy_doctor();

		if (master_interrupt_flag_pending) {
			master_interrupt_flag_pending = false;
			master_interrupt_flag = true;
		}
		else if (master_interrupt_flag) {
			handle_interrupts();
		}

		uint8_t op_byte = mmu_read(cpu.wreg.pc++);
		process_opcode(op_byte);
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

int opcode_timing[256] = {
    1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 3, 3, 3, 2, 1,
    1, 1, 3, 0, 3, 1, 2, 1, 1, 1, 3, 0, 3, 0, 2, 1,
    2, 1, 2, 0, 0, 1, 2, 1, 2, 1, 3, 0, 0, 0, 2, 1,
    2, 1, 2, 1, 0, 1, 2, 1, 2, 1, 3, 1, 0, 0, 2, 1,
};

void test_opcode_timing() {
	for (int i = 0; i < 256; i++) {
		switch(i) {
		case 0x10: case 0x76: // STOP and HALT
		case 0xD3: case 0xDB: case 0xDD: case 0xE3:
		case 0xE4: case 0xEB: case 0xEC: case 0xED:
		case 0xF4: case 0xFC: case 0xFD: // Not used
			continue;
		}
		m_cycle_clock = 0;
		process_opcode(i);
		if (m_cycle_clock != opcode_timing[i])
			printf("Opcode %02X has timing %d, but should have timing %d\n",
			       i, m_cycle_clock, opcode_timing[i]);
	}
	printf("\n");

	exit(EXIT_SUCCESS);
}

void process_opcode(uint8_t op_byte) {
	switch (op_byte) {

	// LOAD OPERATIONS (These are in sequential order)
	case 0x40: cpu.reg.b = cpu.reg.b; break;
	case 0x41: cpu.reg.b = cpu.reg.c; break;
	case 0x42: cpu.reg.b = cpu.reg.d; break;
	case 0x43: cpu.reg.b = cpu.reg.e; break;
	case 0x44: cpu.reg.b = cpu.reg.h; break;
	case 0x45: cpu.reg.b = cpu.reg.l; break;
	case 0x46: cpu.reg.b = mmu_read(cpu.wreg.hl); break;
	case 0x47: cpu.reg.b = cpu.reg.a; break;

	case 0x48: cpu.reg.c = cpu.reg.b; break;
	case 0x49: cpu.reg.c = cpu.reg.c; break;
	case 0x4A: cpu.reg.c = cpu.reg.d; break;
	case 0x4B: cpu.reg.c = cpu.reg.e; break;
	case 0x4C: cpu.reg.c = cpu.reg.h; break;
	case 0x4D: cpu.reg.c = cpu.reg.l; break;
	case 0x4E: cpu.reg.c = mmu_read(cpu.wreg.hl); break;
	case 0x4F: cpu.reg.c = cpu.reg.a; break;

	case 0x50: cpu.reg.d = cpu.reg.b; break;
	case 0x51: cpu.reg.d = cpu.reg.c; break;
	case 0x52: cpu.reg.d = cpu.reg.d; break;
	case 0x53: cpu.reg.d = cpu.reg.e; break;
	case 0x54: cpu.reg.d = cpu.reg.h; break;
	case 0x55: cpu.reg.d = cpu.reg.l; break;
	case 0x56: cpu.reg.d = mmu_read(cpu.wreg.hl); break;
	case 0x57: cpu.reg.d = cpu.reg.a; break;

	case 0x58: cpu.reg.e = cpu.reg.b; break;
	case 0x59: cpu.reg.e = cpu.reg.c; break;
	case 0x5A: cpu.reg.e = cpu.reg.d; break;
	case 0x5B: cpu.reg.e = cpu.reg.e; break;
	case 0x5C: cpu.reg.e = cpu.reg.h; break;
	case 0x5D: cpu.reg.e = cpu.reg.l; break;
	case 0x5E: cpu.reg.e = mmu_read(cpu.wreg.hl); break;
	case 0x5F: cpu.reg.e = cpu.reg.a; break;

	case 0x60: cpu.reg.h = cpu.reg.b; break;
	case 0x61: cpu.reg.h = cpu.reg.c; break;
	case 0x62: cpu.reg.h = cpu.reg.d; break;
	case 0x63: cpu.reg.h = cpu.reg.e; break;
	case 0x64: cpu.reg.h = cpu.reg.h; break;
	case 0x65: cpu.reg.h = cpu.reg.l; break;
	case 0x66: cpu.reg.h = mmu_read(cpu.wreg.hl); break;
	case 0x67: cpu.reg.h = cpu.reg.a; break;

	case 0x68: cpu.reg.l = cpu.reg.b; break;
	case 0x69: cpu.reg.l = cpu.reg.c; break;
	case 0x6A: cpu.reg.l = cpu.reg.d; break;
	case 0x6B: cpu.reg.l = cpu.reg.e; break;
	case 0x6C: cpu.reg.l = cpu.reg.h; break;
	case 0x6D: cpu.reg.l = cpu.reg.l; break;
	case 0x6E: cpu.reg.l = mmu_read(cpu.wreg.hl); break;
	case 0x6F: cpu.reg.l = cpu.reg.a; break;

	case 0x70: mmu_write(cpu.wreg.hl, cpu.reg.b); break;
	case 0x71: mmu_write(cpu.wreg.hl, cpu.reg.c); break;
	case 0x72: mmu_write(cpu.wreg.hl, cpu.reg.d); break;
	case 0x73: mmu_write(cpu.wreg.hl, cpu.reg.e); break;
	case 0x74: mmu_write(cpu.wreg.hl, cpu.reg.h); break;
	case 0x75: mmu_write(cpu.wreg.hl, cpu.reg.l); break;
	case 0x77: mmu_write(cpu.wreg.hl, cpu.reg.a); break;

	case 0x78: cpu.reg.a = cpu.reg.b; break;
	case 0x79: cpu.reg.a = cpu.reg.c; break;
	case 0x7A: cpu.reg.a = cpu.reg.d; break;
	case 0x7B: cpu.reg.a = cpu.reg.e; break;
	case 0x7C: cpu.reg.a = cpu.reg.h; break;
	case 0x7D: cpu.reg.a = cpu.reg.l; break;
	case 0x7E: cpu.reg.a = mmu_read(cpu.wreg.hl); break;
	case 0x7F: cpu.reg.a = cpu.reg.a; break;

	// LOAD IMMEDIATE OPERATIONS
	case 0x06: cpu.reg.b = read_byte(); break;
	case 0x0E: cpu.reg.c = read_byte(); break;
	case 0x16: cpu.reg.d = read_byte(); break;
	case 0x1E: cpu.reg.e = read_byte(); break;
	case 0x26: cpu.reg.h = read_byte(); break;
	case 0x2E: cpu.reg.l = read_byte(); break;
	case 0x36: mmu_write(cpu.wreg.hl, read_byte()); break;
	case 0x3E: cpu.reg.a = read_byte(); break;

	case 0x01: cpu.wreg.bc = read_word(); break;
	case 0x11: cpu.wreg.de = read_word(); break;
	case 0x21: cpu.wreg.hl = read_word(); break;
	case 0x31: cpu.wreg.sp = read_word(); break;

	// LOAD IMMEDIATE ADDRESS
	case 0xEA: mmu_write(read_word(), cpu.reg.a); break;
	case 0xFA: cpu.reg.a = mmu_read(read_word()); break;

	// LOAD HIGH OPERATIONS
	case 0xE0: mmu_write(0xFF00 | read_byte(), cpu.reg.a); break;
	case 0xE2: mmu_write(0xFF00 | cpu.reg.c  , cpu.reg.a); break;
	case 0xF0: cpu.reg.a = mmu_read(0xFF00 | read_byte()); break;
	case 0xF2: cpu.reg.a = mmu_read(0xFF00 | cpu.reg.c); break;

	// LOAD ADDRESS AT REGISTER WITH A
	case 0x02: mmu_write(cpu.wreg.bc, cpu.reg.a); break;
	case 0x0A: cpu.reg.a = mmu_read(cpu.wreg.bc); break;
	case 0x12: mmu_write(cpu.wreg.de, cpu.reg.a); break;
	case 0x1A: cpu.reg.a = mmu_read(cpu.wreg.de); break;

	// LOAD AND INCREMENT / DECREMENT OPERATIONS
	case 0x22: mmu_write(cpu.wreg.hl++, cpu.reg.a); break;
	case 0x2A: cpu.reg.a = mmu_read(cpu.wreg.hl++); break;
	case 0x32: mmu_write(cpu.wreg.hl--, cpu.reg.a); break;
	case 0x3A: cpu.reg.a = mmu_read(cpu.wreg.hl--); break;

	// RST OPERATIONS
	case 0xC7: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x00; break;
	case 0xCF: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x08; break;
	case 0xD7: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x10; break;
	case 0xDF: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x18; break;
	case 0xE7: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x20; break;
	case 0xEF: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x28; break;
	case 0xF7: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x30; break;
	case 0xFF: push_stack(cpu.wreg.pc); cpu.wreg.pc = 0x38; break;

	// JP OPERATIONS
	case 0xC2: op_jump(!cpu.flag.zero,  read_word()); break;
	case 0xC3: op_jump(true,                     read_word()); break;
	case 0xCA: op_jump(cpu.flag.zero,   read_word()); break;
	case 0xD2: op_jump(!cpu.flag.carry, read_word()); break;
	case 0xDA: op_jump(cpu.flag.carry,  read_word()); break;
	case 0xE9: op_jump(true,                     cpu.wreg.hl); break;

	// JR OPERATIONS
	case 0x18: op_jr(true); break;
	case 0x20: op_jr(!cpu.flag.zero); break;
	case 0x28: op_jr(cpu.flag.zero); break;
	case 0x30: op_jr(!cpu.flag.carry); break;
	case 0x38: op_jr(cpu.flag.carry); break;

	// RET OPERATIONS
	case 0xC0: op_ret(!cpu.flag.zero); break;
	case 0xC8: op_ret(cpu.flag.zero); break;
	case 0xC9: op_ret(true); break;
	case 0xD0: op_ret(!cpu.flag.carry); break;
	case 0xD8: op_ret(cpu.flag.carry); break;
	case 0xD9: op_ret(true); master_interrupt_flag = true; break;

	// CALL OPERATIONS
	case 0xC4: op_call(!cpu.flag.zero); break;
	case 0xCC: op_call(cpu.flag.zero); break;
	case 0xCD: op_call(true); break;
	case 0xD4: op_call(!cpu.flag.carry); break;
	case 0xDC: op_call(cpu.flag.carry); break;

	// INC OPERATIONS
	case 0x04: op_inc(&cpu.reg.b); break;
	case 0x0C: op_inc(&cpu.reg.c); break;
	case 0x14: op_inc(&cpu.reg.d); break;
	case 0x1C: op_inc(&cpu.reg.e); break;
	case 0x24: op_inc(&cpu.reg.h); break;
	case 0x2C: op_inc(&cpu.reg.l); break;
	case 0x34: op_inc(gb_memory + cpu.wreg.hl); break;
	case 0x3C: op_inc(&cpu.reg.a); break;

	case 0x03: cpu.wreg.bc++; break;
	case 0x13: cpu.wreg.de++; break;
	case 0x23: cpu.wreg.hl++; break;
	case 0x33: cpu.wreg.sp++; break;

	// DEC OPERATIONS
	case 0x05: op_dec(&cpu.reg.b); break;
	case 0x0D: op_dec(&cpu.reg.c); break;
	case 0x15: op_dec(&cpu.reg.d); break;
	case 0x1D: op_dec(&cpu.reg.e); break;
	case 0x25: op_dec(&cpu.reg.h); break;
	case 0x2D: op_dec(&cpu.reg.l); break;
	case 0x35: op_dec(gb_memory + cpu.wreg.hl); break;
	case 0x3D: op_dec(&cpu.reg.a); break;

	case 0x0B: cpu.wreg.bc--; break;
	case 0x1B: cpu.wreg.de--; break;
	case 0x2B: cpu.wreg.hl--; break;
	case 0x3B: cpu.wreg.sp--; break;

	// XOR OPERATIONS
	case 0xA8: op_xor(cpu.reg.b); break;
	case 0xA9: op_xor(cpu.reg.c); break;
	case 0xAA: op_xor(cpu.reg.d); break;
	case 0xAB: op_xor(cpu.reg.e); break;
	case 0xAC: op_xor(cpu.reg.h); break;
	case 0xAD: op_xor(cpu.reg.l); break;
	case 0xAE: op_xor(gb_memory[cpu.wreg.hl]); break;
	case 0xAF: op_xor(cpu.reg.a); break;

	// AND OPERATIONS
	case 0xA0: op_and(cpu.reg.b); break;
	case 0xA1: op_and(cpu.reg.c); break;
	case 0xA2: op_and(cpu.reg.d); break;
	case 0xA3: op_and(cpu.reg.e); break;
	case 0xA4: op_and(cpu.reg.h); break;
	case 0xA5: op_and(cpu.reg.l); break;
	case 0xA6: op_and(gb_memory[cpu.wreg.hl]); break;
	case 0xA7: op_and(cpu.reg.a); break;

	// SBC OPERATIONS
	case 0x98: op_sbc(cpu.reg.b); break;
	case 0x99: op_sbc(cpu.reg.c); break;
	case 0x9A: op_sbc(cpu.reg.d); break;
	case 0x9B: op_sbc(cpu.reg.e); break;
	case 0x9C: op_sbc(cpu.reg.h); break;
	case 0x9D: op_sbc(cpu.reg.l); break;
	case 0x9E: op_sbc(gb_memory[cpu.wreg.hl]); break;
	case 0x9F: op_sbc(cpu.reg.a); break;

	// SUB OPERATIONS
	case 0x90: op_sub(cpu.reg.b); break;
	case 0x91: op_sub(cpu.reg.c); break;
	case 0x92: op_sub(cpu.reg.d); break;
	case 0x93: op_sub(cpu.reg.e); break;
	case 0x94: op_sub(cpu.reg.h); break;
	case 0x95: op_sub(cpu.reg.l); break;
	case 0x96: op_sub(gb_memory[cpu.wreg.hl]); break;
	case 0x97: op_sub(cpu.reg.a); break;

	// ADC OPERATIONS
	case 0x88: op_adc(cpu.reg.b); break;
	case 0x89: op_adc(cpu.reg.c); break;
	case 0x8A: op_adc(cpu.reg.d); break;
	case 0x8B: op_adc(cpu.reg.e); break;
	case 0x8C: op_adc(cpu.reg.h); break;
	case 0x8D: op_adc(cpu.reg.l); break;
	case 0x8E: op_adc(gb_memory[cpu.wreg.hl]); break;
	case 0x8F: op_adc(cpu.reg.a); break;

	// ADD OPERATIONS
	case 0x80: op_add(cpu.reg.b); break;
	case 0x81: op_add(cpu.reg.c); break;
	case 0x82: op_add(cpu.reg.d); break;
	case 0x83: op_add(cpu.reg.e); break;
	case 0x84: op_add(cpu.reg.h); break;
	case 0x85: op_add(cpu.reg.l); break;
	case 0x86: op_add(gb_memory[cpu.wreg.hl]); break;
	case 0x87: op_add(cpu.reg.a); break;

	// CP OPERATIONS
	case 0xB8: op_cp(cpu.reg.b); break;
	case 0xB9: op_cp(cpu.reg.c); break;
	case 0xBA: op_cp(cpu.reg.d); break;
	case 0xBB: op_cp(cpu.reg.e); break;
	case 0xBC: op_cp(cpu.reg.h); break;
	case 0xBD: op_cp(cpu.reg.l); break;
	case 0xBE: op_cp(gb_memory[cpu.wreg.hl]); break;
	case 0xBF: op_cp(cpu.reg.a); break;

	// OR OPERATIONS
	case 0xB0: op_or(cpu.reg.b); break;
	case 0xB1: op_or(cpu.reg.c); break;
	case 0xB2: op_or(cpu.reg.d); break;
	case 0xB3: op_or(cpu.reg.e); break;
	case 0xB4: op_or(cpu.reg.h); break;
	case 0xB5: op_or(cpu.reg.l); break;
	case 0xB6: op_or(gb_memory[cpu.wreg.hl]); break;
	case 0xB7: op_or(cpu.reg.a); break;

	// OPERATIONS BETWEEN A AND AN IMMEDIATE
	case 0xC6: op_add(read_byte()); break;
	case 0xCE: op_adc(read_byte()); break;
	case 0xD6: op_sub(read_byte()); break;
	case 0xDE: op_sbc(read_byte()); break;
	case 0xE6: op_and(read_byte()); break;
	case 0xEE: op_xor(read_byte()); break;
	case 0xF6: op_or(read_byte());  break;
	case 0xFE: op_cp(read_byte());  break;

	// PUSH OPERATIONS
	case 0xC5: push_stack(cpu.wreg.bc); break;
	case 0xD5: push_stack(cpu.wreg.de); break;
	case 0xE5: push_stack(cpu.wreg.hl); break;
	case 0xF5: push_stack(cpu.wreg.af); break;

	// POP OPERATIONS
	case 0xC1: cpu.wreg.bc = pop_stack(); break;
	case 0xD1: cpu.wreg.de = pop_stack(); break;
	case 0xE1: cpu.wreg.hl = pop_stack(); break;
	case 0xF1: cpu.wreg.af = pop_stack();
		// Need to clear the unused part of F
		cpu.flag.unused = 0;
		break;

	// 16-bit ADD OPERATIONS
	case 0x09: op_add_16bit(cpu.wreg.bc); break;
	case 0x19: op_add_16bit(cpu.wreg.de); break;
	case 0x29: op_add_16bit(cpu.wreg.hl); break;
	case 0x39: op_add_16bit(cpu.wreg.sp); break;

	// ROTATIONS ON REGISTER A
	case 0x07: op_rlc(&cpu.reg.a); cpu.flag.zero = 0; break;
	case 0x0F: op_rrc(&cpu.reg.a); cpu.flag.zero = 0; break;
	case 0x17: op_rl(&cpu.reg.a);  cpu.flag.zero = 0; break;
	case 0x1F: op_rr(&cpu.reg.a);  cpu.flag.zero = 0; break;

	// FLAG OPERATIONS
	case 0xF3: // DE
		master_interrupt_flag_pending = false;
		master_interrupt_flag = false;
		break;
	case 0xFB: // IE
		master_interrupt_flag_pending = true;
		break;
	case 0x3F: // CCF
		cpu.flag.subtract = 0;
		cpu.flag.half_carry = 0;
		cpu.flag.carry = !cpu.flag.carry;
		break;
	case 0x37: // SCF
		cpu.flag.subtract = 0;
		cpu.flag.half_carry = 0;
		cpu.flag.carry = 1;
		break;
	case 0x2F: // CPL
		cpu.reg.a = ~cpu.reg.a;
		cpu.flag.subtract = 1;
		cpu.flag.half_carry = 1;
		break;

	// SPECIAL STACK POINTER OPERATIONS
	case 0xF9: // LD SP HL
		cpu.wreg.sp = cpu.wreg.hl;
		break;

	case 0x08: // LD (u16) SP
	{
		uint16_t address = read_word();
		mmu_write(address, cpu.wreg.sp & 0x00FF);
		mmu_write(address + 1, (cpu.wreg.sp & 0xFF00) >> 8);
		break;
	}

	case 0xF8: // LD HL SP+i8
	{
		uint8_t next = read_byte();
		cpu.reg.f = 0;
		cpu.flag.half_carry = (((cpu.wreg.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
		cpu.flag.carry = (cpu.wreg.sp & 0x00FF) + next > 0x00FF;
		cpu.wreg.hl = cpu.wreg.sp + (int8_t)next;
		break;
	}

	case 0xE8: // ADD SP i8
	{
		uint8_t next = read_byte();
		cpu.reg.f = 0;
		cpu.flag.half_carry = (((cpu.wreg.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
		cpu.flag.carry = (cpu.wreg.sp & 0x00FF) + next > 0x00FF;
		cpu.wreg.sp += (int8_t)next;
		break;
	}

	case 0x27: // DAA instruction
		op_daa();
		break;

	case 0xCB: // Rotate, shift, and bit operations
		process_extra_opcodes(read_byte());
		break;

	case 0x00: // NOP: do nothing
		break;

	case 0x10: // STOP: Implement later
		break;

	case 0x76: // HALT: Implement later
		break;

	default:
		printf("Error: Op Code 0x%02X is not implemented\n", op_byte);
		exit(EXIT_FAILURE);
		break;
	}

	increment_clock();
}
