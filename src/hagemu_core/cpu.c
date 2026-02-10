#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "mmu.h"
#include "clock.h"

// When using cpu.reg.f or cpu.wreg.af,
// make sure to call update_f_register() and/or update_f_flags()
union HagemuCPU {
	// Regular 8-bit registers
	struct {
		uint8_t f, a, c, b, e, d, l, h;
	} reg;

	// Wide 16-bit registers
	struct {
		uint16_t af, bc, de, hl, sp, pc;
	} wreg;

	struct {
		uint16_t padding[6];
		// flags that correspond to the f register
		bool carry;
		bool half_carry;
		bool subtract;
		bool zero;

		// other misc flags
		bool master_interrupt;
		bool master_interrupt_pending;
		bool is_halted;
	} flags;
} cpu = { 0 };

static inline void update_f_register() {
	cpu.reg.f = 0;
	cpu.reg.f |= cpu.flags.carry << 4;
	cpu.reg.f |= cpu.flags.half_carry << 5;
	cpu.reg.f |= cpu.flags.subtract << 6;
	cpu.reg.f |= cpu.flags.zero << 7;
}

static inline void update_f_flags() {
	cpu.flags.carry      = cpu.reg.f & (0x01 << 4);
	cpu.flags.half_carry = cpu.reg.f & (0x01 << 5);
	cpu.flags.subtract   = cpu.reg.f & (0x01 << 6);
	cpu.flags.zero       = cpu.reg.f & (0x01 << 7);
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
	cpu.flags.master_interrupt = false;
	cpu.flags.master_interrupt_pending = false;
	cpu.flags.is_halted = false;
	update_f_flags();
}

void cpu_print_state() {
	update_f_register();
	printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
		cpu.reg.a, cpu.reg.f, cpu.reg.b, cpu.reg.c, cpu.reg.d, cpu.reg.e, cpu.reg.h, cpu.reg.l, cpu.wreg.sp,
		cpu.wreg.pc, mmu_read(cpu.wreg.pc), mmu_read(cpu.wreg.pc+1), mmu_read(cpu.wreg.pc+2), mmu_read(cpu.wreg.pc+3));
}

static void increment_clock_once() {
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

static inline void increment_clock(int m_cycles) {
	for (int i = 0; i < m_cycles; i++)
		increment_clock_once();
}

static inline uint8_t fetch_byte(uint16_t address) {
	uint8_t value = mmu_read(address);
	increment_clock(1);
	return value;
}

static inline void write_byte(uint16_t address, uint8_t value) {
	mmu_write(address, value);
	increment_clock(1);
}

static inline uint8_t fetch_immediate8() {
	return fetch_byte(cpu.wreg.pc++);
}

static inline uint16_t fetch_immediate16() {
	uint8_t first_byte = fetch_immediate8();
	uint8_t second_byte = fetch_immediate8();
	return ((uint16_t)second_byte << 8) | (uint16_t)first_byte;
}

static inline uint16_t pop_stack() {
	uint8_t lower = fetch_byte(cpu.wreg.sp++);
	uint8_t upper = fetch_byte(cpu.wreg.sp++);
	return (upper << 8) | lower;
}

static inline void push_stack(uint16_t reg16) {
	increment_clock(1); // internal increment (reason unknown)
	uint8_t lower = (reg16 & 0x00FF);
	uint8_t upper = (reg16 & 0xFF00) >> 8;
	cpu.wreg.sp--;
	write_byte(cpu.wreg.sp, upper);
	cpu.wreg.sp--;
	write_byte(cpu.wreg.sp, lower);
}

static void handle_interrupts() {
	uint8_t interrupts = mmu_read(INTERRUPT_FLAGS);
	interrupts &= mmu_read(INTERRUPT_ENABLE);
	if (!interrupts) return;

	increment_clock(2);
	cpu.flags.master_interrupt = false;
	push_stack(cpu.wreg.pc);

	if (interrupts & 0x01) {
		cpu.wreg.pc = 0x0040;
		mmu_clear_bit(VBLANK_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x02) {
		cpu.wreg.pc = 0x0048;
		mmu_clear_bit(LCD_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x04) {
		cpu.wreg.pc = 0x0050;
		mmu_clear_bit(TIMER_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x08) {
		cpu.wreg.pc = 0x0058;
		mmu_clear_bit(SERIAL_INTERRUPT_FLAG_BIT);
	} else if (interrupts & 0x10) {
		cpu.wreg.pc = 0x0060;
		mmu_clear_bit(JOYPAD_INTERRUPT_FLAG_BIT);
	}
	increment_clock(1);
}

static inline void op_store(uint16_t address, uint8_t value) {
	write_byte(address, value);
}

static inline uint8_t op_rlc(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	value |= highest_bit;
	cpu.flags.carry      = highest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_rrc(uint8_t value) {
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (lowest_bit << 7);
	cpu.flags.carry      = lowest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_rr(uint8_t value) {
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (cpu.flags.carry << 7);
	cpu.flags.carry      = lowest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_rl(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	value |= cpu.flags.carry;
	cpu.flags.carry      = highest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_sla(uint8_t value) {
	int highest_bit = value >> 7;
	value <<= 1;
	cpu.flags.carry      = highest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_sra(uint8_t value) {
	int lowest_bit  = value & 0x01;
	int highest_bit = value & 0x80;
	value >>= 1;
	value |= highest_bit;
	cpu.flags.carry      = lowest_bit;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_srl(uint8_t value) {
	cpu.flags.carry = value & 0x01;
	value >>= 1;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline uint8_t op_swap(uint8_t value) {
	uint8_t lower = (value & 0x0F);
	uint8_t upper = (value & 0xF0);
	value = (lower << 4) | (upper >> 4);
	cpu.flags.carry      = false;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	return value;
}

static inline void op_bit(int bit_num, uint8_t value) {
	cpu.flags.half_carry = true;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !(value & (1 << bit_num));
}

static inline uint8_t op_res(int bit_num, uint8_t value) {
	value &= ~((uint8_t)0x01 << bit_num);
	return value;
}

static inline uint8_t op_set(int bit_num, uint8_t value) {
	value |= (1 << bit_num);
	return value;
}

static inline void op_add8(uint8_t value) {
	uint8_t result       = cpu.reg.a + value;
	cpu.flags.carry      = result < cpu.reg.a;
	cpu.flags.half_carry = (cpu.reg.a ^ value ^ result) & 0x10;
	cpu.flags.zero       = !result;
	cpu.flags.subtract   = false;
	cpu.reg.a            = result;
}

static inline void op_adc(uint8_t value) {
	bool oldcarry        = cpu.flags.carry;
	uint8_t result       = cpu.reg.a + value + oldcarry;
	cpu.flags.carry      = (value == 0xFF && oldcarry == 1) || (result < cpu.reg.a);
	cpu.flags.half_carry = (cpu.reg.a ^ value ^ result) & 0x10;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !result;
	cpu.reg.a            = result;
}

static inline void op_sub(uint8_t value) {
	uint8_t result       = cpu.reg.a - value;
	cpu.flags.carry      = result > cpu.reg.a;
	cpu.flags.half_carry = (cpu.reg.a ^ value ^ result) & 0x10;
	cpu.flags.subtract   = true;
	cpu.flags.zero       = !result;
	cpu.reg.a            = result;
}

static inline void op_sbc(uint8_t value) {
	bool oldcarry        = cpu.flags.carry;
	uint8_t result       = cpu.reg.a - value - oldcarry;
        cpu.flags.carry      = (value == 0xFF && oldcarry == 1) || (result > cpu.reg.a);
	cpu.flags.half_carry = (cpu.reg.a ^ value ^ result) & 0x10;
	cpu.flags.subtract   = true;
	cpu.flags.zero       = !result;
	cpu.reg.a            = result;
}

static inline void op_inc(uint8_t *location) {
	uint8_t value = *location;
	value++;
	cpu.flags.half_carry = !(value & 0x0F);
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !value;
	*location            = value;
}

static inline void op_dec(uint8_t *location) {
	uint8_t value = *location;
	value--;
	cpu.flags.half_carry = (value & 0x0F) == 0x0F;
	cpu.flags.subtract   = true;
	cpu.flags.zero       = !value;
	*location            = value;
}

static inline void op_jump(bool condition) {
	uint16_t address = fetch_immediate16();
	if (condition) {
		cpu.wreg.pc = address;
		increment_clock(1);
	}
}

static inline void op_ret() {
	cpu.wreg.pc = pop_stack();
	increment_clock(1);
}

static inline void op_ret_cond(bool condition) {
	increment_clock(1);
	if (condition) {
		op_ret();
	}
}

static inline void op_reti() {
	op_ret();
	cpu.flags.master_interrupt = true;
}

static inline void op_rst(uint16_t address) {
	push_stack(cpu.wreg.pc);
	cpu.wreg.pc = address;
}

static inline void op_jr(bool condition) {
	int8_t relative_address = (int8_t)fetch_immediate8();
	if (condition) {
		cpu.wreg.pc += relative_address;
		increment_clock(1);
	}
}

static inline void op_and(uint8_t value) {
	cpu.reg.a           &= value;
	cpu.flags.carry      = false;
	cpu.flags.half_carry = true;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !cpu.reg.a;
}

static inline void op_or(uint8_t value) {
	cpu.reg.a           |= value;
	cpu.flags.carry      = false;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !cpu.reg.a;
}

static inline void op_xor(uint8_t value) {
	cpu.reg.a           ^= value;
	cpu.flags.carry      = false;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = !cpu.reg.a;
}

static inline void op_cp(uint8_t value) {
	uint8_t result       = cpu.reg.a - value;
	cpu.flags.carry      = result > cpu.reg.a;
	cpu.flags.half_carry = (cpu.reg.a ^ value ^ result) & 0x10;
	cpu.flags.subtract   = true;
	cpu.flags.zero       = !result;
}

static inline void op_add16(uint16_t *destination, uint16_t value) {
	uint16_t result      = *(destination) + value;
	cpu.flags.carry      = result < (*destination);
	cpu.flags.half_carry = ((*destination) ^ value ^ result) & 0x1000;
	cpu.flags.subtract   = false;
	*destination         = result;
	increment_clock(1);
}

static inline void op_call(bool condition) {
	uint16_t address = fetch_immediate16();
	if (condition) {
		push_stack(cpu.wreg.pc);
		cpu.wreg.pc = address;
	}
}

static inline void op_daa() {
	unsigned offset = 0;
	if (!cpu.flags.subtract) {
		if (cpu.flags.half_carry || (cpu.reg.a & 0x0F) > 0x09)
			offset |= 0x06;
		if (cpu.flags.carry || cpu.reg.a > 0x99)
			offset |= 0x60;
		cpu.flags.carry |= (cpu.reg.a > (0xFF - offset));
		cpu.reg.a += offset;
	} else {
		if (cpu.flags.half_carry) offset |= 0x06;
		if (cpu.flags.carry)      offset |= 0x60;
		cpu.reg.a -= offset;
	}
	cpu.flags.half_carry = false;
	cpu.flags.zero = !cpu.reg.a;
}

static inline void op_nop() {
}

static inline void op_load16(uint16_t *destination, uint16_t value) {
	*destination = value;
}

static inline void op_load8(uint8_t *destination, uint8_t value) {
	*destination = value;
}

static inline void op_inc16(uint16_t *location) {
	(*location)++;
	increment_clock(1);
}

static inline void op_dec16(uint16_t *location) {
	(*location)--;
	increment_clock(1);
}

static inline void op_rlca() {
	cpu.reg.a = op_rlc(cpu.reg.a);
	cpu.flags.zero = false;
}

static inline void op_rrca() {
	cpu.reg.a = op_rrc(cpu.reg.a);
	cpu.flags.zero = false;
}

static inline void op_rla() {
	cpu.reg.a = op_rl(cpu.reg.a);
	cpu.flags.zero = false;
}

static inline void op_rra() {
	cpu.reg.a = op_rr(cpu.reg.a);
	cpu.flags.zero = false;
}

static inline void op_store16(uint16_t address, uint16_t value) {
	mmu_write(address, value & 0x00FF);
	increment_clock(1);
	mmu_write(address + 1, (value & 0xFF00) >> 8);
	increment_clock(1);
}

static inline void op_stop() {
	clock_reset();
	clock_stop();
	fprintf(stderr, "Warning: STOP is not fully implemented yet\n");
	// The next byte is ignored for some reason
	fetch_immediate8();
}

static inline void op_cpl() {
	cpu.flags.half_carry = true;
	cpu.flags.subtract   = true;
	cpu.reg.a            = ~cpu.reg.a;
}

static inline void op_scf() {
	cpu.flags.carry      = true;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
}

static inline void op_ccf() {
	cpu.flags.carry      = !cpu.flags.carry;
	cpu.flags.half_carry = false;
	cpu.flags.subtract   = false;
}

static inline void op_inc_addr(uint16_t address) {
	uint8_t value = fetch_byte(address);
	op_inc(&value);
	op_store(address, value);
}

static inline void op_dec_addr(uint16_t address) {
	uint8_t value = fetch_byte(address);
	op_dec(&value);
	op_store(address, value);
}

static inline void op_push(uint16_t value) {
	push_stack(value);
}

static inline void op_pop(uint16_t *destination) {
	*destination = pop_stack();
}

static inline void op_add_sp_offset(uint8_t value) {
	uint16_t result      = cpu.wreg.sp + (int8_t)value;
	cpu.flags.carry      = ((cpu.wreg.sp & 0x00FF) + value) & 0x0100;
	cpu.flags.half_carry = (cpu.wreg.sp ^ value ^ result) & 0x10;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = false;
	cpu.wreg.sp          = result;
	increment_clock(2);
}

static inline void op_load_hl_sp_offset(uint8_t value) {
	uint16_t result      = cpu.wreg.sp + (int8_t)value;
	cpu.flags.carry      = ((cpu.wreg.sp & 0x00FF) + value) & 0x0100;
	cpu.flags.half_carry = (cpu.wreg.sp ^ value ^ result) & 0x10;
	cpu.flags.subtract   = false;
	cpu.flags.zero       = false;
	cpu.wreg.hl          = result;
	increment_clock(1);
}

static inline void op_di() {
	cpu.flags.master_interrupt_pending = false;
	cpu.flags.master_interrupt = false;
}

static inline void op_ei() {
	cpu.flags.master_interrupt_pending = true;
}

static inline void op_halt() {
	cpu.flags.is_halted = true;
}

static inline void op_load_sp_hl() {
	increment_clock(1);
	op_load16(&cpu.wreg.sp, cpu.wreg.hl);
}

static inline void op_jp_hl() {
	cpu.wreg.pc = cpu.wreg.hl;
}

static inline void op_load_high(uint8_t *destination, uint8_t address_offset) {
	uint16_t address = 0xFF00 | address_offset;
	*destination = fetch_byte(address);
}

static inline void op_store_high(uint8_t address_offset, uint8_t value) {
	uint16_t address = 0xFF00 | address_offset;
	op_store(address, value);
}

static inline void error_no_opcode(uint8_t opcode_byte) {
	printf("Error: Op Code 0x%02X doesn't exist\n", opcode_byte);
	exit(EXIT_FAILURE);
}

static void process_cb_opcode(uint8_t opcode) {
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
	case 0x06: op_store(cpu.wreg.hl, value); break;
	case 0x07: cpu.reg.a = value; break;
	}
}

static void process_opcode(uint8_t opcode_byte) {
	switch (opcode_byte) {

	case 0x00: op_nop();                                        break;
	case 0x01: op_load16(&cpu.wreg.bc, fetch_immediate16());    break;
	case 0x02: op_store(cpu.wreg.bc, cpu.reg.a);                break;
	case 0x03: op_inc16(&cpu.wreg.bc);                          break;
	case 0x04: op_inc(&cpu.reg.b);                              break;
	case 0x05: op_dec(&cpu.reg.b);                              break;
	case 0x06: op_load8(&cpu.reg.b, fetch_immediate8());        break;
	case 0x07: op_rlca();                                       break;
	case 0x08: op_store16(fetch_immediate16(), cpu.wreg.sp);    break;
	case 0x09: op_add16(&cpu.wreg.hl, cpu.wreg.bc);             break;
	case 0x0A: op_load8(&cpu.reg.a, fetch_byte(cpu.wreg.bc));   break;
	case 0x0B: op_dec16(&cpu.wreg.bc);                          break;
	case 0x0C: op_inc(&cpu.reg.c);                              break;
	case 0x0D: op_dec(&cpu.reg.c);                              break;
	case 0x0E: op_load8(&cpu.reg.c, fetch_immediate8());        break;
	case 0x0F: op_rrca();                                       break;

	case 0x10: op_stop();                                       break;
	case 0x11: op_load16(&cpu.wreg.de, fetch_immediate16());    break;
	case 0x12: op_store(cpu.wreg.de, cpu.reg.a);                break;
	case 0x13: op_inc16(&cpu.wreg.de);                          break;
	case 0x14: op_inc(&cpu.reg.d);                              break;
	case 0x15: op_dec(&cpu.reg.d);                              break;
	case 0x16: op_load8(&cpu.reg.d, fetch_immediate8());        break;
	case 0x17: op_rla();                                        break;
	case 0x18: op_jr(true);                                     break;
	case 0x19: op_add16(&cpu.wreg.hl, cpu.wreg.de);             break;
	case 0x1A: op_load8(&cpu.reg.a, fetch_byte(cpu.wreg.de));   break;
	case 0x1B: op_dec16(&cpu.wreg.de);                          break;
	case 0x1C: op_inc(&cpu.reg.e);                              break;
	case 0x1D: op_dec(&cpu.reg.e);                              break;
	case 0x1E: op_load8(&cpu.reg.e, fetch_immediate8());        break;
	case 0x1F: op_rra();                                        break;

	case 0x20: op_jr(!cpu.flags.zero);                          break;
	case 0x21: op_load16(&cpu.wreg.hl, fetch_immediate16());    break;
	case 0x22: op_store(cpu.wreg.hl++, cpu.reg.a);              break;
	case 0x23: op_inc16(&cpu.wreg.hl);                          break;
	case 0x24: op_inc(&cpu.reg.h);                              break;
	case 0x25: op_dec(&cpu.reg.h);                              break;
	case 0x26: op_load8(&cpu.reg.h, fetch_immediate8());        break;
	case 0x27: op_daa();                                        break;
	case 0x28: op_jr(cpu.flags.zero);                           break;
	case 0x29: op_add16(&cpu.wreg.hl, cpu.wreg.hl);             break;
	case 0x2A: op_load8(&cpu.reg.a, fetch_byte(cpu.wreg.hl++)); break;
	case 0x2B: op_dec16(&cpu.wreg.hl);                          break;
	case 0x2C: op_inc(&cpu.reg.l);                              break;
	case 0x2D: op_dec(&cpu.reg.l);                              break;
	case 0x2E: op_load8(&cpu.reg.l, fetch_immediate8());        break;
	case 0x2F: op_cpl();                                        break;

	case 0x30: op_jr(!cpu.flags.carry);                         break;
	case 0x31: op_load16(&cpu.wreg.sp, fetch_immediate16());    break;
	case 0x32: op_store(cpu.wreg.hl--, cpu.reg.a);              break;
	case 0x33: op_inc16(&cpu.wreg.sp);                          break;
	case 0x34: op_inc_addr(cpu.wreg.hl);                        break;
	case 0x35: op_dec_addr(cpu.wreg.hl);                        break;
	case 0x36: op_store(cpu.wreg.hl, fetch_immediate8());       break;
	case 0x37: op_scf();                                        break;
	case 0x38: op_jr(cpu.flags.carry);                          break;
	case 0x39: op_add16(&cpu.wreg.hl, cpu.wreg.sp);             break;
	case 0x3A: op_load8(&cpu.reg.a, fetch_byte(cpu.wreg.hl--)); break;
	case 0x3B: op_dec16(&cpu.wreg.sp);                          break;
	case 0x3C: op_inc(&cpu.reg.a);                              break;
	case 0x3D: op_dec(&cpu.reg.a);                              break;
	case 0x3E: op_load8(&cpu.reg.a, fetch_immediate8());        break;
	case 0x3F: op_ccf();                                        break;

	case 0x40: op_load8(&cpu.reg.b, cpu.reg.b);                 break;
	case 0x41: op_load8(&cpu.reg.b, cpu.reg.c);                 break;
	case 0x42: op_load8(&cpu.reg.b, cpu.reg.d);                 break;
	case 0x43: op_load8(&cpu.reg.b, cpu.reg.e);                 break;
	case 0x44: op_load8(&cpu.reg.b, cpu.reg.h);                 break;
	case 0x45: op_load8(&cpu.reg.b, cpu.reg.l);                 break;
	case 0x46: op_load8(&cpu.reg.b, fetch_byte(cpu.wreg.hl));   break;
	case 0x47: op_load8(&cpu.reg.b, cpu.reg.a);                 break;
	case 0x48: op_load8(&cpu.reg.c, cpu.reg.b);                 break;
	case 0x49: op_load8(&cpu.reg.c, cpu.reg.c);                 break;
	case 0x4A: op_load8(&cpu.reg.c, cpu.reg.d);                 break;
	case 0x4B: op_load8(&cpu.reg.c, cpu.reg.e);                 break;
	case 0x4C: op_load8(&cpu.reg.c, cpu.reg.h);                 break;
	case 0x4D: op_load8(&cpu.reg.c, cpu.reg.l);                 break;
	case 0x4E: op_load8(&cpu.reg.c, fetch_byte(cpu.wreg.hl));   break;
	case 0x4F: op_load8(&cpu.reg.c, cpu.reg.a);                 break;

	case 0x50: op_load8(&cpu.reg.d, cpu.reg.b);                 break;
	case 0x51: op_load8(&cpu.reg.d, cpu.reg.c);                 break;
	case 0x52: op_load8(&cpu.reg.d, cpu.reg.d);                 break;
	case 0x53: op_load8(&cpu.reg.d, cpu.reg.e);                 break;
	case 0x54: op_load8(&cpu.reg.d, cpu.reg.h);                 break;
	case 0x55: op_load8(&cpu.reg.d, cpu.reg.l);                 break;
	case 0x56: op_load8(&cpu.reg.d, fetch_byte(cpu.wreg.hl));   break;
	case 0x57: op_load8(&cpu.reg.d, cpu.reg.a);                 break;
	case 0x58: op_load8(&cpu.reg.e, cpu.reg.b);                 break;
	case 0x59: op_load8(&cpu.reg.e, cpu.reg.c);                 break;
	case 0x5A: op_load8(&cpu.reg.e, cpu.reg.d);                 break;
	case 0x5B: op_load8(&cpu.reg.e, cpu.reg.e);                 break;
	case 0x5C: op_load8(&cpu.reg.e, cpu.reg.h);                 break;
	case 0x5D: op_load8(&cpu.reg.e, cpu.reg.l);                 break;
	case 0x5E: op_load8(&cpu.reg.e, fetch_byte(cpu.wreg.hl));   break;
	case 0x5F: op_load8(&cpu.reg.e, cpu.reg.a);                 break;

	case 0x60: op_load8(&cpu.reg.h, cpu.reg.b);                 break;
	case 0x61: op_load8(&cpu.reg.h, cpu.reg.c);                 break;
	case 0x62: op_load8(&cpu.reg.h, cpu.reg.d);                 break;
	case 0x63: op_load8(&cpu.reg.h, cpu.reg.e);                 break;
	case 0x64: op_load8(&cpu.reg.h, cpu.reg.h);                 break;
	case 0x65: op_load8(&cpu.reg.h, cpu.reg.l);                 break;
	case 0x66: op_load8(&cpu.reg.h, fetch_byte(cpu.wreg.hl));   break;
	case 0x67: op_load8(&cpu.reg.h, cpu.reg.a);                 break;
	case 0x68: op_load8(&cpu.reg.l, cpu.reg.b);                 break;
	case 0x69: op_load8(&cpu.reg.l, cpu.reg.c);                 break;
	case 0x6A: op_load8(&cpu.reg.l, cpu.reg.d);                 break;
	case 0x6B: op_load8(&cpu.reg.l, cpu.reg.e);                 break;
	case 0x6C: op_load8(&cpu.reg.l, cpu.reg.h);                 break;
	case 0x6D: op_load8(&cpu.reg.l, cpu.reg.l);                 break;
	case 0x6E: op_load8(&cpu.reg.l, fetch_byte(cpu.wreg.hl));   break;
	case 0x6F: op_load8(&cpu.reg.l, cpu.reg.a);                 break;

	case 0x70: op_store(cpu.wreg.hl, cpu.reg.b);                break;
	case 0x71: op_store(cpu.wreg.hl, cpu.reg.c);                break;
	case 0x72: op_store(cpu.wreg.hl, cpu.reg.d);                break;
	case 0x73: op_store(cpu.wreg.hl, cpu.reg.e);                break;
	case 0x74: op_store(cpu.wreg.hl, cpu.reg.h);                break;
	case 0x75: op_store(cpu.wreg.hl, cpu.reg.l);                break;
	case 0x76: op_halt();                                       break;
	case 0x77: op_store(cpu.wreg.hl, cpu.reg.a);                break;
	case 0x78: op_load8(&cpu.reg.a, cpu.reg.b);                 break;
	case 0x79: op_load8(&cpu.reg.a, cpu.reg.c);                 break;
	case 0x7A: op_load8(&cpu.reg.a, cpu.reg.d);                 break;
	case 0x7B: op_load8(&cpu.reg.a, cpu.reg.e);                 break;
	case 0x7C: op_load8(&cpu.reg.a, cpu.reg.h);                 break;
	case 0x7D: op_load8(&cpu.reg.a, cpu.reg.l);                 break;
	case 0x7E: op_load8(&cpu.reg.a, fetch_byte(cpu.wreg.hl));   break;
	case 0x7F: op_load8(&cpu.reg.a, cpu.reg.a);                 break;

	case 0x80: op_add8(cpu.reg.b);                              break;
	case 0x81: op_add8(cpu.reg.c);                              break;
	case 0x82: op_add8(cpu.reg.d);                              break;
	case 0x83: op_add8(cpu.reg.e);                              break;
	case 0x84: op_add8(cpu.reg.h);                              break;
	case 0x85: op_add8(cpu.reg.l);                              break;
	case 0x86: op_add8(fetch_byte(cpu.wreg.hl));                break;
	case 0x87: op_add8(cpu.reg.a);                              break;
	case 0x88: op_adc(cpu.reg.b);                               break;
	case 0x89: op_adc(cpu.reg.c);                               break;
	case 0x8A: op_adc(cpu.reg.d);                               break;
	case 0x8B: op_adc(cpu.reg.e);                               break;
	case 0x8C: op_adc(cpu.reg.h);                               break;
	case 0x8D: op_adc(cpu.reg.l);                               break;
	case 0x8E: op_adc(fetch_byte(cpu.wreg.hl));                 break;
	case 0x8F: op_adc(cpu.reg.a);                               break;

	case 0x90: op_sub(cpu.reg.b);                               break;
	case 0x91: op_sub(cpu.reg.c);                               break;
	case 0x92: op_sub(cpu.reg.d);                               break;
	case 0x93: op_sub(cpu.reg.e);                               break;
	case 0x94: op_sub(cpu.reg.h);                               break;
	case 0x95: op_sub(cpu.reg.l);                               break;
	case 0x96: op_sub(fetch_byte(cpu.wreg.hl));                 break;
	case 0x97: op_sub(cpu.reg.a);                               break;
	case 0x98: op_sbc(cpu.reg.b);                               break;
	case 0x99: op_sbc(cpu.reg.c);                               break;
	case 0x9A: op_sbc(cpu.reg.d);                               break;
	case 0x9B: op_sbc(cpu.reg.e);                               break;
	case 0x9C: op_sbc(cpu.reg.h);                               break;
	case 0x9D: op_sbc(cpu.reg.l);                               break;
	case 0x9E: op_sbc(fetch_byte(cpu.wreg.hl));                 break;
	case 0x9F: op_sbc(cpu.reg.a);                               break;

	case 0xA0: op_and(cpu.reg.b);                               break;
	case 0xA1: op_and(cpu.reg.c);                               break;
	case 0xA2: op_and(cpu.reg.d);                               break;
	case 0xA3: op_and(cpu.reg.e);                               break;
	case 0xA4: op_and(cpu.reg.h);                               break;
	case 0xA5: op_and(cpu.reg.l);                               break;
	case 0xA6: op_and(fetch_byte(cpu.wreg.hl));                 break;
	case 0xA7: op_and(cpu.reg.a);                               break;
	case 0xA8: op_xor(cpu.reg.b);                               break;
	case 0xA9: op_xor(cpu.reg.c);                               break;
	case 0xAA: op_xor(cpu.reg.d);                               break;
	case 0xAB: op_xor(cpu.reg.e);                               break;
	case 0xAC: op_xor(cpu.reg.h);                               break;
	case 0xAD: op_xor(cpu.reg.l);                               break;
	case 0xAE: op_xor(fetch_byte(cpu.wreg.hl));                 break;
	case 0xAF: op_xor(cpu.reg.a);                               break;

	case 0xB0: op_or(cpu.reg.b);                                break;
	case 0xB1: op_or(cpu.reg.c);                                break;
	case 0xB2: op_or(cpu.reg.d);                                break;
	case 0xB3: op_or(cpu.reg.e);                                break;
	case 0xB4: op_or(cpu.reg.h);                                break;
	case 0xB5: op_or(cpu.reg.l);                                break;
	case 0xB6: op_or(fetch_byte(cpu.wreg.hl));                  break;
	case 0xB7: op_or(cpu.reg.a);                                break;
	case 0xB8: op_cp(cpu.reg.b);                                break;
	case 0xB9: op_cp(cpu.reg.c);                                break;
	case 0xBA: op_cp(cpu.reg.d);                                break;
	case 0xBB: op_cp(cpu.reg.e);                                break;
	case 0xBC: op_cp(cpu.reg.h);                                break;
	case 0xBD: op_cp(cpu.reg.l);                                break;
	case 0xBE: op_cp(fetch_byte(cpu.wreg.hl));                  break;
	case 0xBF: op_cp(cpu.reg.a);                                break;

	case 0xC0: op_ret_cond(!cpu.flags.zero);                    break;
	case 0xC1: op_pop(&cpu.wreg.bc);                            break;
	case 0xC2: op_jump(!cpu.flags.zero);                        break;
	case 0xC3: op_jump(true);                                   break;
	case 0xC4: op_call(!cpu.flags.zero);                        break;
	case 0xC5: op_push(cpu.wreg.bc);                            break;
	case 0xC6: op_add8(fetch_immediate8());                     break;
	case 0xC7: op_rst(0x00);                                    break;
	case 0xC8: op_ret_cond(cpu.flags.zero);                     break;
	case 0xC9: op_ret();                                        break;
	case 0xCA: op_jump(cpu.flags.zero);                         break;
	case 0xCB: process_cb_opcode(fetch_immediate8());           break;
	case 0xCC: op_call(cpu.flags.zero);                         break;
	case 0xCD: op_call(true);                                   break;
	case 0xCE: op_adc(fetch_immediate8());                      break;
	case 0xCF: op_rst(0x08);                                    break;

	case 0xD0: op_ret_cond(!cpu.flags.carry);                   break;
	case 0xD1: cpu.wreg.de = pop_stack();                       break;
	case 0xD2: op_jump(!cpu.flags.carry);                       break;
	case 0xD3: error_no_opcode(opcode_byte);                    break;
	case 0xD4: op_call(!cpu.flags.carry);                       break;
	case 0xD5: op_push(cpu.wreg.de);                            break;
	case 0xD6: op_sub(fetch_immediate8());                      break;
	case 0xD7: op_rst(0x10);                                    break;
	case 0xD8: op_ret_cond(cpu.flags.carry);                    break;
	case 0xD9: op_reti();                                       break;
	case 0xDA: op_jump(cpu.flags.carry);                        break;
	case 0xDB: error_no_opcode(opcode_byte);                    break;
	case 0xDC: op_call(cpu.flags.carry);                        break;
	case 0xDD: error_no_opcode(opcode_byte);                    break;
	case 0xDE: op_sbc(fetch_immediate8());                      break;
	case 0xDF: op_rst(0x18);                                    break;

	case 0xE0: op_store_high(fetch_immediate8(), cpu.reg.a);    break;
	case 0xE1: op_pop(&cpu.wreg.hl);                            break;
	case 0xE2: op_store_high(cpu.reg.c, cpu.reg.a);             break;
	case 0xE3: error_no_opcode(opcode_byte);                    break;
	case 0xE4: error_no_opcode(opcode_byte);                    break;
	case 0xE5: op_push(cpu.wreg.hl);                            break;
	case 0xE6: op_and(fetch_immediate8());                      break;
	case 0xE7: op_rst(0x20);                                    break;
	case 0xE8: op_add_sp_offset(fetch_immediate8());            break;
	case 0xE9: op_jp_hl();                                      break;
	case 0xEA: op_store(fetch_immediate16(), cpu.reg.a);        break;
	case 0xEB: error_no_opcode(opcode_byte);                    break;
	case 0xEC: error_no_opcode(opcode_byte);                    break;
	case 0xED: error_no_opcode(opcode_byte);                    break;
	case 0xEE: op_xor(fetch_immediate8());                      break;
	case 0xEF: op_rst(0x28);                                    break;

	case 0xF0: op_load_high(&cpu.reg.a, fetch_immediate8());    break;
	case 0xF1: op_pop(&cpu.wreg.af); update_f_flags();          break;
	case 0xF2: op_load_high(&cpu.reg.a, cpu.reg.c);             break;
	case 0xF3: op_di();                                         break;
	case 0xF4: error_no_opcode(opcode_byte);                    break;
	case 0xF5: update_f_register(); op_push(cpu.wreg.af);       break;
	case 0xF6: op_or(fetch_immediate8());                       break;
	case 0xF7: op_rst(0x30);                                    break;
	case 0xF8: op_load_hl_sp_offset(fetch_immediate8());        break;
	case 0xF9: op_load_sp_hl();                                 break;
	case 0xFA: op_load8(&cpu.reg.a, fetch_byte(fetch_immediate16())); break;
	case 0xFB: op_ei();                                         break;
	case 0xFC: error_no_opcode(opcode_byte);                    break;
	case 0xFD: error_no_opcode(opcode_byte);                    break;
	case 0xFE: op_cp(fetch_immediate8());                       break;
	case 0xFF: op_rst(0x38);                                    break;

	default: error_no_opcode(opcode_byte);                      break;
	}
}

// Returns the number of t-cycles it took to complete the next instruction
int cpu_do_next_instruction() {
	int old_clock = clock_get();

	if (mmu_read(INTERRUPT_FLAGS) & mmu_read(INTERRUPT_ENABLE))
		cpu.flags.is_halted = false;

	if (cpu.flags.is_halted) {
		increment_clock(1);
		return 4; // clock only incremented once
	}

	if (cpu.flags.master_interrupt_pending) {
		cpu.flags.master_interrupt_pending = false;
		cpu.flags.master_interrupt = true;
	} else if (cpu.flags.master_interrupt) {
		handle_interrupts();
	}

	uint8_t opcode_byte = fetch_immediate8();
	process_opcode(opcode_byte);
	if (clock_get() - old_clock > 0)
		return clock_get() - old_clock;
	else
		return 0xFFFF - (old_clock - clock_get());
}
