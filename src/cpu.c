#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "mmu.h"
#include "clock.h"

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
bool cpu_halted = false;

void cpu_print_state() {
	printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
		cpu.reg.a, cpu.reg.f, cpu.reg.b, cpu.reg.c, cpu.reg.d, cpu.reg.e, cpu.reg.h, cpu.reg.l, cpu.wreg.sp,
		cpu.wreg.pc, mmu_read(cpu.wreg.pc), mmu_read(cpu.wreg.pc+1), mmu_read(cpu.wreg.pc+2), mmu_read(cpu.wreg.pc+3));
}

void cpu_reset() {
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

	// Various flags
	master_interrupt_flag = false;
	master_interrupt_flag_pending = false;
	cpu_halted = false;
}

void increment_clock_once() {
	if (clock_is_running())
		clock_update(4);
	uint16_t clock_time = clock_get();

	// Return early if timer control is off
	if (!mmu_get_bit(TIMER_CONTROL_ENABLE_BIT))
		return;

	bool increment_counter = false;

	switch (mmu_read(TIMER_CONTROL) & 0x3) {

        case 0x00:
		if (clock_time % 1024 == 0) increment_counter = true;
		break;
	case 0x01:
		if (clock_time % 16 == 0) increment_counter = true;
		break;
	case 0x02:
		if (clock_time % 64 == 0) increment_counter = true;
		break;
	case 0x03:
		if (clock_time % 256 == 0) increment_counter = true;
		break;
	}

	if (increment_counter && mmu_read(TIMER_COUNTER) == 0xFF)
	{
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_MODULO));
		mmu_set_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else if (increment_counter) {
		mmu_write(TIMER_COUNTER, mmu_read(TIMER_COUNTER) + 1);
	}
}

void increment_clock(int m_cycles) {
	for (int i = 0; i < m_cycles; i++)
		increment_clock_once();
}

uint8_t fetch_next_byte() {
	uint8_t value = mmu_read(cpu.wreg.pc++);
	increment_clock(1);
	return value;
}

uint8_t fetch_byte(uint16_t address) {
	uint8_t value = mmu_read(address);
	increment_clock(1);
	return value;
}

void write_byte(uint16_t address, uint8_t value) {
	mmu_write(address, value);
	increment_clock(1);
}

uint16_t fetch_word() {
	uint8_t first_byte = fetch_next_byte();
	uint8_t second_byte = fetch_next_byte();
	return ((uint16_t)second_byte << 8) | (uint16_t)first_byte;
}

uint16_t pop_stack() {
	uint8_t lower = fetch_byte(cpu.wreg.sp++);
	uint8_t upper = fetch_byte(cpu.wreg.sp++);
	return (upper << 8) | lower;
}

void push_stack(uint16_t reg16) {
	increment_clock(1); // internal increment (reason unknown)
	uint8_t lower = (reg16 & 0x00FF);
	uint8_t upper = (reg16 & 0xFF00) >> 8;
	cpu.wreg.sp--;
	write_byte(cpu.wreg.sp, upper);
	cpu.wreg.sp--;
	write_byte(cpu.wreg.sp, lower);
}

uint8_t op_rlc(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	value |= highest_bit;
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = highest_bit;
	return value;
}

uint8_t op_rrc(uint8_t value) {
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (lowest_bit << 7);
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = lowest_bit;
	return value;
}

uint8_t op_rr(uint8_t value) {
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (cpu.flag.carry << 7);
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = lowest_bit;
	return value;
}

uint8_t op_rl(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	value |= cpu.flag.carry;
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = highest_bit;
	return value;
}

uint8_t op_sla(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = highest_bit;
	return value;
}

uint8_t op_sra(uint8_t value) {
	int lowest_bit = value & 0x01;
	int highest_bit = value & 0x80;
	value >>= 1;
	value |= highest_bit;
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	cpu.flag.carry = lowest_bit;
	return value;
}

uint8_t op_srl(uint8_t value) {
	cpu.flag.carry = value & 0x01;
	value >>= 1;
	cpu.flag.subtract = 0;
	cpu.flag.half_carry = 0;
	cpu.flag.zero = !value;
	return value;
}

uint8_t op_swap(uint8_t value) {
	uint8_t lower = (value & 0x0F);
	uint8_t upper = (value & 0xF0);
	value = (lower << 4) | (upper >> 4);
	cpu.reg.f = 0;
	cpu.flag.zero = !value;
	return value;
}

void op_bit(int bit_num, uint8_t value) {
	cpu.flag.subtract = 0;
	cpu.flag.half_carry = 1;
	cpu.flag.zero = !(value & (1 << bit_num));
}

uint8_t op_res(int bit_num, uint8_t value) {
	value &= ~((uint8_t)0x01 << bit_num);
	return value;
}

uint8_t op_set(int bit_num, uint8_t value) {
	value |= (1 << bit_num);
	return value;
}

void process_extra_opcodes(uint8_t opcode) {
	// The lower 4 bits of the opcode determines the operand
    uint8_t value = 0;
	switch (opcode & 0x07) {

	case 0x00: value = cpu.reg.b; break;
	case 0x01: value = cpu.reg.c; break;
	case 0x02: value = cpu.reg.d; break;
	case 0x03: value = cpu.reg.e; break;
	case 0x04: value = cpu.reg.h; break;
	case 0x05: value = cpu.reg.l; break;
	case 0x06: value = fetch_byte(cpu.wreg.hl); break;
	case 0x07: value = cpu.reg.a; break;
	}

	// The upper 5 bits of the opcode determines the operation
	switch (opcode & 0xF8) {

	case 0x00: value = op_rlc(value);  break; // ROTATE LEFT CIRCULAR
	case 0x08: value = op_rrc(value);  break; // ROTATE RIGHT CIRCULAR
	case 0x10: value = op_rl(value);   break; // ROTATE LEFT
	case 0x18: value = op_rr(value);   break; // ROTATE RIGHT
	case 0x20: value = op_sla(value);  break; // SHIFT LEFT ARITHMETIC
	case 0x28: value = op_sra(value);  break; // SHIFT RIGHT ARITHEMTIC
	case 0x30: value = op_swap(value); break; // SWAP
	case 0x38: value = op_srl(value);  break; // SHIFT RIGHT LOGICAL

	// Return early since we don't need to the write the data back
	case 0x40: op_bit(0, value); return; // TEST BIT 0
	case 0x48: op_bit(1, value); return; // TEST BIT 1
	case 0x50: op_bit(2, value); return; // TEST BIT 2
	case 0x58: op_bit(3, value); return; // TEST BIT 3
	case 0x60: op_bit(4, value); return; // TEST BIT 4
	case 0x68: op_bit(5, value); return; // TEST BIT 5
	case 0x70: op_bit(6, value); return; // TEST BIT 6
	case 0x78: op_bit(7, value); return; // TEST BIT 7

	case 0x80: value = op_res(0, value); break; // RESET BIT 0
	case 0x88: value = op_res(1, value); break; // RESET BIT 1
	case 0x90: value = op_res(2, value); break; // RESET BIT 2
	case 0x98: value = op_res(3, value); break; // RESET BIT 3
	case 0xA0: value = op_res(4, value); break; // RESET BIT 4
	case 0xA8: value = op_res(5, value); break; // RESET BIT 5
	case 0xB0: value = op_res(6, value); break; // RESET BIT 6
	case 0xB8: value = op_res(7, value); break; // RESET BIT 7

	case 0xC0: value = op_set(0, value); break; // SET BIT 0
	case 0xC8: value = op_set(1, value); break; // SET BIT 1
	case 0xD0: value = op_set(2, value); break; // SET BIT 2
	case 0xD8: value = op_set(3, value); break; // SET BIT 3
	case 0xE0: value = op_set(4, value); break; // SET BIT 4
	case 0xE8: value = op_set(5, value); break; // SET BIT 5
	case 0xF0: value = op_set(6, value); break; // SET BIT 6
	case 0xF8: value = op_set(7, value); break; // SET BIT 7

	default:
		printf("Error: Unknown prefixed opcode `%02X'\n", opcode);
		exit(EXIT_FAILURE);
		break;
	}

	switch (opcode & 0x07) {

	case 0x00: cpu.reg.b = value; break;
	case 0x01: cpu.reg.c = value; break;
	case 0x02: cpu.reg.d = value; break;
	case 0x03: cpu.reg.e = value; break;
	case 0x04: cpu.reg.h = value; break;
	case 0x05: cpu.reg.l = value; break;
	case 0x06: write_byte(cpu.wreg.hl, value); break;
	case 0x07: cpu.reg.a = value; break;
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
	bool oldcarry = cpu.flag.carry;
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) + (value & 0x0F) + oldcarry) & 0x10) == 0x10;
	cpu.flag.carry = ((unsigned)oldcarry + (unsigned)cpu.reg.a + (unsigned)value) > 0xFF;
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
	bool oldcarry = cpu.flag.carry;
	cpu.flag.half_carry = (((cpu.reg.a & 0x0F) - (value & 0x0F) - oldcarry) & 0x10) == 0x10;
	cpu.flag.carry = ((unsigned)oldcarry - (unsigned)cpu.reg.a - (unsigned)value) > 0xFF;
	cpu.flag.subtract = 1;
	cpu.reg.a -= value + oldcarry;
	cpu.flag.zero = !cpu.reg.a;
}

uint8_t op_inc(uint8_t value) {
	value++;
	cpu.flag.zero = !value;
	cpu.flag.half_carry = !(value & 0x0F);
	cpu.flag.subtract = 0;
    return value;
}

uint8_t op_dec(uint8_t value) {
	value--;
	cpu.flag.zero = !value;
	cpu.flag.half_carry = (value & 0x0F) == 0x0F;
	cpu.flag.subtract = 1;
    return value;
}

void op_jump(bool condition, uint16_t address) {
	if (condition) {
		cpu.wreg.pc = address;
		increment_clock(1);
	}
}

void op_ret(bool condition) {
	if (condition) {
		cpu.wreg.pc = pop_stack();
		increment_clock(1);
	}
}

void op_rst(uint16_t address) {
    push_stack(cpu.wreg.pc);
    cpu.wreg.pc = address;
}

void op_jr(bool condition) {
	int8_t relative_address = (int8_t)fetch_next_byte();
	if (condition) {
		cpu.wreg.pc += relative_address;
		increment_clock(1);
	}
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
	uint16_t address = fetch_word();
	if (condition) {
		push_stack(cpu.wreg.pc);
		cpu.wreg.pc = address;
	}
}

void op_daa() {
	unsigned offset = 0;
	if (!cpu.flag.subtract) {
		if (cpu.flag.half_carry || (cpu.reg.a & 0x0F) > 0x09)
			offset |= 0x06;
		if (cpu.flag.carry || cpu.reg.a > 0x99)
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

void handle_interrupts() {
	uint8_t interrupts = mmu_read(INTERRUPT_FLAGS);
	interrupts &= mmu_read(INTERRUPT_ENABLE);
	if (!interrupts) return;

	increment_clock(2);
	master_interrupt_flag = false;
	push_stack(cpu.wreg.pc);

	if (interrupts & 0x01) {
		cpu.wreg.pc = 0x0040;
		mmu_clear_bit(VBLANK_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x02) {
		printf("LCD INTERRUPT OCCURED\n");
		cpu.wreg.pc = 0x0048;
		mmu_clear_bit(LCD_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x04) {
		printf("TIMER INTERRUPT OCCURED\n");
		cpu.wreg.pc = 0x0050;
		mmu_clear_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x08) {
		printf("SERIAL INTERRUPT OCCURED\n");
		cpu.wreg.pc = 0x0058;
		mmu_clear_bit(SERIAL_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x10) {
		printf("JOYPAD INTERRUPT OCCURED\n");
		cpu.wreg.pc = 0x0060;
		mmu_clear_bit(JOYPAD_INTERRUPT_FLAG_BIT);
	}
	increment_clock(1);
}

void process_opcode(uint8_t op_byte);

int blargg_opcode_timing[256] = {
	0,0,0,1,0,0,0,0, 0,1,0,1,0,0,0,0,
	0,0,0,1,0,0,0,0, 0,1,0,1,0,0,0,0,
	0,0,0,1,0,0,0,0, 0,1,0,1,0,0,0,0,
	0,0,0,1,0,0,0,0, 0,1,0,1,0,0,0,0,

	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

	1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,
	1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 2,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0
};

void process_opcode(uint8_t op_byte) {
	switch (op_byte) {

	// LOAD OPERATIONS (These are in sequential order)
	case 0x40: cpu.reg.b = cpu.reg.b; break;
	case 0x41: cpu.reg.b = cpu.reg.c; break;
	case 0x42: cpu.reg.b = cpu.reg.d; break;
	case 0x43: cpu.reg.b = cpu.reg.e; break;
	case 0x44: cpu.reg.b = cpu.reg.h; break;
	case 0x45: cpu.reg.b = cpu.reg.l; break;
	case 0x46: cpu.reg.b = fetch_byte(cpu.wreg.hl); break;
	case 0x47: cpu.reg.b = cpu.reg.a; break;

	case 0x48: cpu.reg.c = cpu.reg.b; break;
	case 0x49: cpu.reg.c = cpu.reg.c; break;
	case 0x4A: cpu.reg.c = cpu.reg.d; break;
	case 0x4B: cpu.reg.c = cpu.reg.e; break;
	case 0x4C: cpu.reg.c = cpu.reg.h; break;
	case 0x4D: cpu.reg.c = cpu.reg.l; break;
	case 0x4E: cpu.reg.c = fetch_byte(cpu.wreg.hl); break;
	case 0x4F: cpu.reg.c = cpu.reg.a; break;

	case 0x50: cpu.reg.d = cpu.reg.b; break;
	case 0x51: cpu.reg.d = cpu.reg.c; break;
	case 0x52: cpu.reg.d = cpu.reg.d; break;
	case 0x53: cpu.reg.d = cpu.reg.e; break;
	case 0x54: cpu.reg.d = cpu.reg.h; break;
	case 0x55: cpu.reg.d = cpu.reg.l; break;
	case 0x56: cpu.reg.d = fetch_byte(cpu.wreg.hl); break;
	case 0x57: cpu.reg.d = cpu.reg.a; break;

	case 0x58: cpu.reg.e = cpu.reg.b; break;
	case 0x59: cpu.reg.e = cpu.reg.c; break;
	case 0x5A: cpu.reg.e = cpu.reg.d; break;
	case 0x5B: cpu.reg.e = cpu.reg.e; break;
	case 0x5C: cpu.reg.e = cpu.reg.h; break;
	case 0x5D: cpu.reg.e = cpu.reg.l; break;
	case 0x5E: cpu.reg.e = fetch_byte(cpu.wreg.hl); break;
	case 0x5F: cpu.reg.e = cpu.reg.a; break;

	case 0x60: cpu.reg.h = cpu.reg.b; break;
	case 0x61: cpu.reg.h = cpu.reg.c; break;
	case 0x62: cpu.reg.h = cpu.reg.d; break;
	case 0x63: cpu.reg.h = cpu.reg.e; break;
	case 0x64: cpu.reg.h = cpu.reg.h; break;
	case 0x65: cpu.reg.h = cpu.reg.l; break;
	case 0x66: cpu.reg.h = fetch_byte(cpu.wreg.hl); break;
	case 0x67: cpu.reg.h = cpu.reg.a; break;

	case 0x68: cpu.reg.l = cpu.reg.b; break;
	case 0x69: cpu.reg.l = cpu.reg.c; break;
	case 0x6A: cpu.reg.l = cpu.reg.d; break;
	case 0x6B: cpu.reg.l = cpu.reg.e; break;
	case 0x6C: cpu.reg.l = cpu.reg.h; break;
	case 0x6D: cpu.reg.l = cpu.reg.l; break;
	case 0x6E: cpu.reg.l = fetch_byte(cpu.wreg.hl); break;
	case 0x6F: cpu.reg.l = cpu.reg.a; break;

	case 0x70: write_byte(cpu.wreg.hl, cpu.reg.b); break;
	case 0x71: write_byte(cpu.wreg.hl, cpu.reg.c); break;
	case 0x72: write_byte(cpu.wreg.hl, cpu.reg.d); break;
	case 0x73: write_byte(cpu.wreg.hl, cpu.reg.e); break;
	case 0x74: write_byte(cpu.wreg.hl, cpu.reg.h); break;
	case 0x75: write_byte(cpu.wreg.hl, cpu.reg.l); break;
	case 0x77: write_byte(cpu.wreg.hl, cpu.reg.a); break;

	case 0x78: cpu.reg.a = cpu.reg.b; break;
	case 0x79: cpu.reg.a = cpu.reg.c; break;
	case 0x7A: cpu.reg.a = cpu.reg.d; break;
	case 0x7B: cpu.reg.a = cpu.reg.e; break;
	case 0x7C: cpu.reg.a = cpu.reg.h; break;
	case 0x7D: cpu.reg.a = cpu.reg.l; break;
	case 0x7E: cpu.reg.a = fetch_byte(cpu.wreg.hl); break;
	case 0x7F: cpu.reg.a = cpu.reg.a; break;

	// LOAD IMMEDIATE OPERATIONS
	case 0x06: cpu.reg.b = fetch_next_byte(); break;
	case 0x0E: cpu.reg.c = fetch_next_byte(); break;
	case 0x16: cpu.reg.d = fetch_next_byte(); break;
	case 0x1E: cpu.reg.e = fetch_next_byte(); break;
	case 0x26: cpu.reg.h = fetch_next_byte(); break;
	case 0x2E: cpu.reg.l = fetch_next_byte(); break;
	case 0x36: write_byte(cpu.wreg.hl, fetch_next_byte()); break;
	case 0x3E: cpu.reg.a = fetch_next_byte(); break;

	case 0x01: cpu.wreg.bc = fetch_word(); break;
	case 0x11: cpu.wreg.de = fetch_word(); break;
	case 0x21: cpu.wreg.hl = fetch_word(); break;
	case 0x31: cpu.wreg.sp = fetch_word(); break;

	// LOAD IMMEDIATE ADDRESS
	case 0xEA: write_byte(fetch_word(), cpu.reg.a); break;
	case 0xFA: cpu.reg.a = fetch_byte(fetch_word()); break;

	// LOAD HIGH OPERATIONS
	case 0xE0: write_byte(0xFF00 | fetch_next_byte(), cpu.reg.a); break;
	case 0xE2: write_byte(0xFF00 | cpu.reg.c  , cpu.reg.a); break;
	case 0xF0: cpu.reg.a = fetch_byte(0xFF00 | fetch_next_byte()); break;
	case 0xF2: cpu.reg.a = fetch_byte(0xFF00 | cpu.reg.c); break;

	// LOAD ADDRESS AT REGISTER WITH A
	case 0x02: write_byte(cpu.wreg.bc, cpu.reg.a); break;
	case 0x0A: cpu.reg.a = fetch_byte(cpu.wreg.bc); break;
	case 0x12: write_byte(cpu.wreg.de, cpu.reg.a); break;
	case 0x1A: cpu.reg.a = fetch_byte(cpu.wreg.de); break;

	// LOAD AND INCREMENT / DECREMENT OPERATIONS
	case 0x22: write_byte(cpu.wreg.hl++, cpu.reg.a); break;
	case 0x2A: cpu.reg.a = fetch_byte(cpu.wreg.hl++); break;
	case 0x32: write_byte(cpu.wreg.hl--, cpu.reg.a); break;
	case 0x3A: cpu.reg.a = fetch_byte(cpu.wreg.hl--); break;

	// RST OPERATIONS
	case 0xC7: op_rst(0x00); break;
	case 0xCF: op_rst(0x08); break;
	case 0xD7: op_rst(0x10); break;
	case 0xDF: op_rst(0x18); break;
	case 0xE7: op_rst(0x20); break;
	case 0xEF: op_rst(0x28); break;
	case 0xF7: op_rst(0x30); break;
	case 0xFF: op_rst(0x38); break;

	// JP OPERATIONS
	case 0xC3: op_jump(true ,fetch_word()); break;
	case 0xC2: op_jump(!cpu.flag.zero,  fetch_word()); break;
	case 0xCA: op_jump(cpu.flag.zero,   fetch_word()); break;
	case 0xD2: op_jump(!cpu.flag.carry, fetch_word()); break;
	case 0xDA: op_jump(cpu.flag.carry,  fetch_word()); break;
	// This one is separate because it doesn't increment the clock after jumping
	case 0xE9: cpu.wreg.pc = cpu.wreg.hl; break;

	// JR OPERATIONS
	case 0x18: op_jr(true); break;
	case 0x20: op_jr(!cpu.flag.zero);  break;
	case 0x28: op_jr(cpu.flag.zero);   break;
	case 0x30: op_jr(!cpu.flag.carry); break;
	case 0x38: op_jr(cpu.flag.carry);  break;

	// RET OPERATIONS
	case 0xC9: op_ret(true); break;
	case 0xD9: op_ret(true); master_interrupt_flag = true; break;
	case 0xC0: op_ret(!cpu.flag.zero);  break;
	case 0xC8: op_ret(cpu.flag.zero);   break;
	case 0xD0: op_ret(!cpu.flag.carry); break;
	case 0xD8: op_ret(cpu.flag.carry);  break;

	// CALL OPERATIONS
	case 0xCD: op_call(true); break;
	case 0xC4: op_call(!cpu.flag.zero);  break;
	case 0xCC: op_call(cpu.flag.zero);   break;
	case 0xD4: op_call(!cpu.flag.carry); break;
	case 0xDC: op_call(cpu.flag.carry);  break;

	// INC OPERATIONS
	case 0x04: cpu.reg.b = op_inc(cpu.reg.b); break;
	case 0x0C: cpu.reg.c = op_inc(cpu.reg.c); break;
	case 0x14: cpu.reg.d = op_inc(cpu.reg.d); break;
	case 0x1C: cpu.reg.e = op_inc(cpu.reg.e); break;
	case 0x24: cpu.reg.h = op_inc(cpu.reg.h); break;
	case 0x2C: cpu.reg.l = op_inc(cpu.reg.l); break;
	case 0x3C: cpu.reg.a = op_inc(cpu.reg.a); break;
	case 0x34:
	{
		uint8_t value = fetch_byte(cpu.wreg.hl);
		value = op_inc(value);
		write_byte(cpu.wreg.hl, value);
		break;
	}

	case 0x03: cpu.wreg.bc++; break;
	case 0x13: cpu.wreg.de++; break;
	case 0x23: cpu.wreg.hl++; break;
	case 0x33: cpu.wreg.sp++; break;

	// DEC OPERATIONS
	case 0x05: cpu.reg.b = op_dec(cpu.reg.b); break;
	case 0x0D: cpu.reg.c = op_dec(cpu.reg.c); break;
	case 0x15: cpu.reg.d = op_dec(cpu.reg.d); break;
	case 0x1D: cpu.reg.e = op_dec(cpu.reg.e); break;
	case 0x25: cpu.reg.h = op_dec(cpu.reg.h); break;
	case 0x2D: cpu.reg.l = op_dec(cpu.reg.l); break;
	case 0x3D: cpu.reg.a = op_dec(cpu.reg.a); break;
	case 0x35:
	{
		uint8_t value = fetch_byte(cpu.wreg.hl);
		value = op_dec(value);
		write_byte(cpu.wreg.hl, value);
		break;
	}

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
	case 0xAE: op_xor(fetch_byte(cpu.wreg.hl)); break;
	case 0xAF: op_xor(cpu.reg.a); break;

	// AND OPERATIONS
	case 0xA0: op_and(cpu.reg.b); break;
	case 0xA1: op_and(cpu.reg.c); break;
	case 0xA2: op_and(cpu.reg.d); break;
	case 0xA3: op_and(cpu.reg.e); break;
	case 0xA4: op_and(cpu.reg.h); break;
	case 0xA5: op_and(cpu.reg.l); break;
	case 0xA6: op_and(fetch_byte(cpu.wreg.hl)); break;
	case 0xA7: op_and(cpu.reg.a); break;

	// SBC OPERATIONS
	case 0x98: op_sbc(cpu.reg.b); break;
	case 0x99: op_sbc(cpu.reg.c); break;
	case 0x9A: op_sbc(cpu.reg.d); break;
	case 0x9B: op_sbc(cpu.reg.e); break;
	case 0x9C: op_sbc(cpu.reg.h); break;
	case 0x9D: op_sbc(cpu.reg.l); break;
	case 0x9E: op_sbc(fetch_byte(cpu.wreg.hl)); break;
	case 0x9F: op_sbc(cpu.reg.a); break;

	// SUB OPERATIONS
	case 0x90: op_sub(cpu.reg.b); break;
	case 0x91: op_sub(cpu.reg.c); break;
	case 0x92: op_sub(cpu.reg.d); break;
	case 0x93: op_sub(cpu.reg.e); break;
	case 0x94: op_sub(cpu.reg.h); break;
	case 0x95: op_sub(cpu.reg.l); break;
	case 0x96: op_sub(fetch_byte(cpu.wreg.hl)); break;
	case 0x97: op_sub(cpu.reg.a); break;

	// ADC OPERATIONS
	case 0x88: op_adc(cpu.reg.b); break;
	case 0x89: op_adc(cpu.reg.c); break;
	case 0x8A: op_adc(cpu.reg.d); break;
	case 0x8B: op_adc(cpu.reg.e); break;
	case 0x8C: op_adc(cpu.reg.h); break;
	case 0x8D: op_adc(cpu.reg.l); break;
	case 0x8E: op_adc(fetch_byte(cpu.wreg.hl)); break;
	case 0x8F: op_adc(cpu.reg.a); break;

	// ADD OPERATIONS
	case 0x80: op_add(cpu.reg.b); break;
	case 0x81: op_add(cpu.reg.c); break;
	case 0x82: op_add(cpu.reg.d); break;
	case 0x83: op_add(cpu.reg.e); break;
	case 0x84: op_add(cpu.reg.h); break;
	case 0x85: op_add(cpu.reg.l); break;
	case 0x86: op_add(fetch_byte(cpu.wreg.hl)); break;
	case 0x87: op_add(cpu.reg.a); break;

	// CP OPERATIONS
	case 0xB8: op_cp(cpu.reg.b); break;
	case 0xB9: op_cp(cpu.reg.c); break;
	case 0xBA: op_cp(cpu.reg.d); break;
	case 0xBB: op_cp(cpu.reg.e); break;
	case 0xBC: op_cp(cpu.reg.h); break;
	case 0xBD: op_cp(cpu.reg.l); break;
	case 0xBE: op_cp(fetch_byte(cpu.wreg.hl)); break;
	case 0xBF: op_cp(cpu.reg.a); break;

	// OR OPERATIONS
	case 0xB0: op_or(cpu.reg.b); break;
	case 0xB1: op_or(cpu.reg.c); break;
	case 0xB2: op_or(cpu.reg.d); break;
	case 0xB3: op_or(cpu.reg.e); break;
	case 0xB4: op_or(cpu.reg.h); break;
	case 0xB5: op_or(cpu.reg.l); break;
	case 0xB6: op_or(fetch_byte(cpu.wreg.hl)); break;
	case 0xB7: op_or(cpu.reg.a); break;

	// OPERATIONS BETWEEN A AND AN IMMEDIATE
	case 0xC6: op_add(fetch_next_byte()); break;
	case 0xCE: op_adc(fetch_next_byte()); break;
	case 0xD6: op_sub(fetch_next_byte()); break;
	case 0xDE: op_sbc(fetch_next_byte()); break;
	case 0xE6: op_and(fetch_next_byte()); break;
	case 0xEE: op_xor(fetch_next_byte()); break;
	case 0xF6: op_or(fetch_next_byte());  break;
	case 0xFE: op_cp(fetch_next_byte());  break;

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
	case 0x07: cpu.reg.a = op_rlc(cpu.reg.a); cpu.flag.zero = 0; break;
	case 0x0F: cpu.reg.a = op_rrc(cpu.reg.a); cpu.flag.zero = 0; break;
	case 0x17: cpu.reg.a = op_rl(cpu.reg.a);  cpu.flag.zero = 0; break;
	case 0x1F: cpu.reg.a = op_rr(cpu.reg.a);  cpu.flag.zero = 0; break;

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
        increment_clock(1); // internal increment reason unknown
		cpu.wreg.sp = cpu.wreg.hl;
		break;

	case 0x08: // LD (u16) SP
	{
		uint16_t address = fetch_word();
		write_byte(address, cpu.wreg.sp & 0x00FF);
		write_byte(address + 1, (cpu.wreg.sp & 0xFF00) >> 8);
		break;
	}

	case 0xF8: // LD HL SP+i8
	{
		uint8_t next = fetch_next_byte();
		cpu.reg.f = 0;
		cpu.flag.half_carry = (((cpu.wreg.sp & 0x000F) + (next & 0x0F)) & 0x10) == 0x10;
		cpu.flag.carry = (cpu.wreg.sp & 0x00FF) + next > 0x00FF;
		cpu.wreg.hl = cpu.wreg.sp + (int8_t)next;
		break;
	}

	case 0xE8: // ADD SP i8
	{
		uint8_t next = fetch_next_byte();
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
	{
		uint8_t sub_opcode = fetch_next_byte();
		process_extra_opcodes(sub_opcode);
		break;
	}

	case 0x00: // NOP: do nothing
		break;

	case 0x10: // STOP: Implement later
		clock_reset();
		clock_stop();
		fprintf(stderr, "Warning: STOP is not fully implemented yet\n");
		// The next byte is ignored for some reason
		fetch_next_byte();
		/* exit(EXIT_FAILURE); */
		break;

	case 0x76: // HALT: Implement later
		cpu_halted = true;
		break;

	default:
		printf("Error: Op Code 0x%02X is not implemented\n", op_byte);
		exit(EXIT_FAILURE);
		break;
	}
}

// Returns the number of t-cycles it took to complete the next instruction
int cpu_do_next_instruction() {
	int old_clock = clock_get();

	if (mmu_read(INTERRUPT_FLAGS) & mmu_read(INTERRUPT_ENABLE))
		cpu_halted = false;

	if (cpu_halted) {
		increment_clock(1);
		if (clock_get() - old_clock > 0)
			return clock_get() - old_clock;
		else
			return 0xFFFF - (old_clock - clock_get());
	}

	if (master_interrupt_flag_pending) {
		master_interrupt_flag_pending = false;
		master_interrupt_flag = true;
	} else if (master_interrupt_flag) {
		handle_interrupts();
	}

	uint8_t op_byte = fetch_next_byte();
	process_opcode(op_byte);
	increment_clock(blargg_opcode_timing[op_byte]);
	if (clock_get() - old_clock > 0)
		return clock_get() - old_clock;
	else
		return 0xFFFF - (old_clock - clock_get());
}
