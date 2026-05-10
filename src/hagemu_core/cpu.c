#include "cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "mmu.h"
#include "interrupt.h"

struct HagemuCPU {
	// CPU Registers (except af)
	uint16_t bc, de, hl, sp, pc;

	// Registers a and f are split into a uint8_t and bool flags
	uint8_t a;
	bool f_carry;
	bool f_half_carry;
	bool f_subtract;
	bool f_zero;

	// other misc flags
	bool master_interrupt;
	bool master_interrupt_pending;
	bool is_halted;
	bool is_stopped;
	uint8_t cycles_passed;
};

#include "timer.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
void system_tick(struct HagemuCPU *cpu) {
	int t_cycles = 4;
	ppu_tick(t_cycles);
	apu_tick(t_cycles);
	dma_tick();
	timer_tick(t_cycles);
	cpu->cycles_passed += t_cycles;
}

struct HagemuCPU *cpu_create() {
	struct HagemuCPU *cpu = malloc(sizeof(struct HagemuCPU));
	memset(cpu, 0, sizeof(struct HagemuCPU));
	return cpu;
}

void cpu_resume_if_stopped(struct HagemuCPU *cpu) {
	cpu->is_stopped = false;
}

void cpu_destory(struct HagemuCPU *cpu) {
	free(cpu);
}

enum Reg8 {
	REG_A,
	REG_F,
	REG_B,
	REG_C,
	REG_D,
	REG_E,
	REG_H,
	REG_L,
	REG_BC_ADDR,
	REG_DE_ADDR,
	REG_HL_ADDR,
	REG_HL_ADDR_INC,
	REG_HL_ADDR_DEC,
	IMMEDIATE8,
	IMMEDIATE16_ADDR,
	HIGH_ADDR_IMM8,
	HIGH_ADDR_REG_C,
};

enum Reg16 {
	REG_AF,
	REG_BC,
	REG_DE,
	REG_HL,
	REG_SP,
	REG_PC,
	IMMEDIATE16,
};

static inline uint8_t get_f(const struct HagemuCPU *cpu) {
	uint8_t result = 0;
	result |= cpu->f_carry      << 4;
	result |= cpu->f_half_carry << 5;
	result |= cpu->f_subtract   << 6;
	result |= cpu->f_zero       << 7;
	return result;
}

static inline void set_f(struct HagemuCPU *cpu, uint8_t f_value) {
	cpu->f_carry      = f_value & (0x01 << 4);
	cpu->f_half_carry = f_value & (0x01 << 5);
	cpu->f_subtract   = f_value & (0x01 << 6);
	cpu->f_zero       = f_value & (0x01 << 7);
}

static inline uint8_t fetch_byte(struct HagemuCPU *cpu, uint16_t address) {
	system_tick(cpu);
	return mmu_read(address);
}

static inline void write_byte(struct HagemuCPU *cpu,uint16_t address, uint8_t value) {
	system_tick(cpu);
	mmu_write(address, value);
}

static inline uint8_t fetch_immediate8(struct HagemuCPU *cpu) {
	return fetch_byte(cpu, cpu->pc++);
}

static inline uint16_t fetch_immediate16(struct HagemuCPU *cpu) {
	uint8_t first_byte  = fetch_immediate8(cpu);
	uint8_t second_byte = fetch_immediate8(cpu);
	return ((uint16_t)second_byte << 8) | (uint16_t)first_byte;
}

static inline uint16_t pop_stack(struct HagemuCPU *cpu) {
	uint8_t lower = fetch_byte(cpu, cpu->sp++);
	uint8_t upper = fetch_byte(cpu, cpu->sp++);
	return (upper << 8) | lower;
}

static inline void push_stack(struct HagemuCPU *cpu, uint16_t value) {
	system_tick(cpu); // internal increment (reason unknown)
	uint8_t lower = (value & 0x00FF);
	uint8_t upper = (value & 0xFF00) >> 8;
	cpu->sp--;
	write_byte(cpu, cpu->sp, upper);
	cpu->sp--;
	write_byte(cpu, cpu->sp, lower);
}

static void handle_interrupts(struct HagemuCPU *cpu) {
	if (!interrupt_pending())
		return;
	system_tick(cpu);
	cpu->master_interrupt = false;
	push_stack(cpu, cpu->pc);

	enum HagemuInterruptFlag flag = interrupt_get_next();

	switch (flag) {
	case VBLANK_INTERRUPT: cpu->pc = 0x0040; break;
	case LCD_INTERRUPT:    cpu->pc = 0x0048; break;
	case TIMER_INTERRUPT:  cpu->pc = 0x0050; break;
	case SERIAL_INTERRUPT: cpu->pc = 0x0058; break;
	case JOYPAD_INTERRUPT: cpu->pc = 0x0060; break;
	}

	interrupt_clear(flag);
	system_tick(cpu);
}

static inline uint8_t get_reg8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = 0;
	switch (reg) {
	case REG_A: value = cpu->a;                    break;
	case REG_F: value = get_f(cpu);                break;
	case REG_B: value = (cpu->bc >> 8);            break;
	case REG_C: value = (cpu->bc & 0x00FF);        break;
	case REG_D: value = (cpu->de >> 8);            break;
	case REG_E: value = (cpu->de & 0x00FF);        break;
	case REG_H: value = (cpu->hl >> 8);            break;
	case REG_L: value = (cpu->hl & 0x00FF);        break;

	case REG_BC_ADDR:      value = fetch_byte(cpu, cpu->bc);             break;
	case REG_DE_ADDR:      value = fetch_byte(cpu, cpu->de);             break;
	case REG_HL_ADDR:      value = fetch_byte(cpu, cpu->hl);             break;
	case REG_HL_ADDR_INC:  value = fetch_byte(cpu, cpu->hl++);           break;
	case REG_HL_ADDR_DEC:  value = fetch_byte(cpu, cpu->hl--);           break;
	case IMMEDIATE8:       value = fetch_byte(cpu, cpu->pc++);           break;
	case IMMEDIATE16_ADDR: value = fetch_byte(cpu, fetch_immediate16(cpu)); break;

	case HIGH_ADDR_IMM8:  value = fetch_byte(cpu, 0xFF00 | fetch_immediate8(cpu));   break;
	case HIGH_ADDR_REG_C: value = fetch_byte(cpu, 0xFF00 | get_reg8(cpu, REG_C)); break;
	}

	return value;
}


static inline void set_reg8(struct HagemuCPU *cpu, enum Reg8 reg, uint8_t value) {
	switch (reg) {
	case REG_A: cpu->a = value;   break;
	case REG_F: set_f(cpu, value); break;
	case REG_B: cpu->bc &= 0x00FF; cpu->bc |= (value << 8); break;
	case REG_C: cpu->bc &= 0xFF00; cpu->bc |= value;        break;
	case REG_D: cpu->de &= 0x00FF; cpu->de |= (value << 8); break;
	case REG_E: cpu->de &= 0xFF00; cpu->de |= value;        break;
	case REG_H: cpu->hl &= 0x00FF; cpu->hl |= (value << 8); break;
	case REG_L: cpu->hl &= 0xFF00; cpu->hl |= value;        break;

	case REG_BC_ADDR: write_byte(cpu, cpu->bc, value); break;
	case REG_DE_ADDR: write_byte(cpu, cpu->de, value); break;
	case REG_HL_ADDR: write_byte(cpu, cpu->hl, value); break;
	case REG_HL_ADDR_INC: write_byte(cpu, cpu->hl++, value); break;
	case REG_HL_ADDR_DEC: write_byte(cpu, cpu->hl--, value); break;

	case IMMEDIATE8:
		printf("Can't write the value %02X to an immediate", value);
		exit(EXIT_FAILURE);
		break;
	case IMMEDIATE16_ADDR: write_byte(cpu, fetch_immediate16(cpu), value); break;

	case HIGH_ADDR_IMM8:  write_byte(cpu, 0xFF00 | fetch_immediate8(cpu), value); break;
	case HIGH_ADDR_REG_C: write_byte(cpu, 0xFF00 | get_reg8(cpu, REG_C), value); break;
	}
}

static inline uint16_t get_reg16(struct HagemuCPU *cpu, enum Reg16 reg) {
	uint16_t value = 0;
	switch (reg) {
	case REG_AF: value = ((uint16_t)cpu->a << 8) | get_f(cpu); break;
	case REG_BC: value = cpu->bc; break;
	case REG_DE: value = cpu->de; break;
	case REG_HL: value = cpu->hl; break;
	case REG_SP: value = cpu->sp; break;
	case REG_PC: value = cpu->pc; break;
	case IMMEDIATE16: value = fetch_immediate16(cpu); break;
	}
	return value;
}

static inline void set_reg16(struct HagemuCPU *cpu, enum Reg16 reg, uint16_t value) {
	switch (reg) {
	case REG_AF: cpu->a = (value >> 8); set_f(cpu, value & 0xFF); break;
	case REG_BC: cpu->bc = value; break;
	case REG_DE: cpu->de = value; break;
	case REG_HL: cpu->hl = value; break;
	case REG_SP: cpu->sp = value; break;
	case REG_PC: cpu->pc = value; break;
	case IMMEDIATE16:
		printf("Can't write the value %04X to an immediate", value);
		exit(EXIT_FAILURE);
		break;
	}
}

void cpu_reset(struct HagemuCPU *cpu) {
	memset(cpu, 0, sizeof(struct HagemuCPU));
}

void cpu_print_state(struct HagemuCPU *cpu) {
	// Inital state of registers
	uint8_t a = get_reg8(cpu, REG_A);
	uint8_t f = get_reg8(cpu, REG_F);
	uint8_t b = get_reg8(cpu, REG_B);
	uint8_t c = get_reg8(cpu, REG_C);
	uint8_t d = get_reg8(cpu, REG_D);
	uint8_t e = get_reg8(cpu, REG_E);
	uint8_t h = get_reg8(cpu, REG_H);
	uint8_t l = get_reg8(cpu, REG_L);
	uint16_t sp = get_reg16(cpu, REG_SP);
	uint16_t pc = get_reg16(cpu, REG_PC);
	fprintf(stderr, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
	       a, f, b, c, d, e, h, l, sp, pc, mmu_read(pc), mmu_read(pc+1), mmu_read(pc+2), mmu_read(pc+3));
}


static inline void op_rlc8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int highest_bit = value >> 7;
	value <<= 1;
	value |= highest_bit;
	cpu->f_carry      = highest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_rrc8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (lowest_bit << 7);
	cpu->f_carry      = lowest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_rr8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int lowest_bit = value & 0x01;
	value >>= 1;
	value |= (cpu->f_carry << 7);
	cpu->f_carry      = lowest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_rl8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int highest_bit = value >> 7;
	value <<= 1;
	value |= cpu->f_carry;
	cpu->f_carry      = highest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_sla8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int highest_bit = value >> 7;
	value <<= 1;
	cpu->f_carry      = highest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_sra8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	int lowest_bit  = value & 0x01;
	int highest_bit = value & 0x80;
	value >>= 1;
	value |= highest_bit;
	cpu->f_carry      = lowest_bit;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_srl8(struct HagemuCPU* cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	cpu->f_carry = value & 0x01;
	value >>= 1;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_swap8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	uint8_t lower = (value & 0x0F);
	uint8_t upper = (value & 0xF0);
	value = (lower << 4) | (upper >> 4);
	cpu->f_carry      = false;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_bit(struct HagemuCPU *cpu, int bit_num, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	cpu->f_half_carry = true;
	cpu->f_subtract   = false;
	cpu->f_zero       = !(value & (1 << bit_num));
}

static inline void op_res(struct HagemuCPU *cpu, int bit_num, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	value &= ~((uint8_t)0x01 << bit_num);
	set_reg8(cpu, reg, value);
}

static inline void op_set(struct HagemuCPU *cpu, int bit_num, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	value |= (1 << bit_num);
	set_reg8(cpu, reg, value);
}

static inline void op_add(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a + value;
	cpu->f_carry      = result < reg_a;
	cpu->f_half_carry = (reg_a ^ value ^ result) & 0x10;
	cpu->f_zero       = !result;
	cpu->f_subtract   = false;
	set_reg8(cpu, REG_A, result);
}

static inline void op_adc(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	bool oldcarry        = cpu->f_carry;
	uint8_t result       = reg_a + value + oldcarry;
	cpu->f_carry      = (value == 0xFF && oldcarry == 1) || (result < reg_a);
	cpu->f_half_carry = (reg_a ^ value ^ result) & 0x10;
	cpu->f_subtract   = false;
	cpu->f_zero       = !result;
	set_reg8(cpu, REG_A, result);
}

static inline void op_sub(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a - value;
	cpu->f_carry      = result > reg_a;
	cpu->f_half_carry = (reg_a ^ value ^ result) & 0x10;
	cpu->f_subtract   = true;
	cpu->f_zero       = !result;
	set_reg8(cpu, REG_A, result);
}

static inline void op_sbc(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	bool oldcarry        = cpu->f_carry;
	uint8_t result       = reg_a - value - oldcarry;
        cpu->f_carry      = (value == 0xFF && oldcarry == 1) || (result > reg_a);
	cpu->f_half_carry = (reg_a ^ value ^ result) & 0x10;
	cpu->f_subtract   = true;
	cpu->f_zero       = !result;
	set_reg8(cpu, REG_A, result);
}

static inline void op_inc8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	value++;
	cpu->f_half_carry = !(value & 0x0F);
	cpu->f_subtract   = false;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_dec8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t value = get_reg8(cpu, reg);
	value--;
	cpu->f_half_carry = (value & 0x0F) == 0x0F;
	cpu->f_subtract   = true;
	cpu->f_zero       = !value;
	set_reg8(cpu, reg, value);
}

static inline void op_jump(struct HagemuCPU *cpu, bool condition) {
	uint16_t address = get_reg16(cpu, IMMEDIATE16);
	if (condition) {
		cpu->pc = address;
		system_tick(cpu);
	}
}

static inline void op_rst(struct HagemuCPU *cpu, uint16_t address) {
	push_stack(cpu, cpu->pc);
	cpu->pc = address;
}

static inline void op_jr(struct HagemuCPU *cpu, bool condition) {
	int8_t offset = get_reg8(cpu, IMMEDIATE8);
	if (condition) {
		cpu->pc += offset;
		system_tick(cpu);
	}
}

static inline void op_and8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a & value;
	set_reg8(cpu, REG_A, result);
	cpu->f_carry      = false;
	cpu->f_half_carry = true;
	cpu->f_subtract   = false;
	cpu->f_zero       = !result;
}

static inline void op_or8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a | value;
	set_reg8(cpu, REG_A, result);
	cpu->f_carry      = false;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !result;
}

static inline void op_xor8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a ^ value;
	set_reg8(cpu, REG_A, result);
	cpu->f_carry      = false;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
	cpu->f_zero       = !result;
}

static inline void op_cp8(struct HagemuCPU *cpu, enum Reg8 reg) {
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	uint8_t value        = get_reg8(cpu, reg);
	uint8_t result       = reg_a - value;
	cpu->f_carry      = result > reg_a;
	cpu->f_half_carry = (reg_a ^ value ^ result) & 0x10;
	cpu->f_subtract   = true;
	cpu->f_zero       = !result;
}

static inline void op_add16(struct HagemuCPU *cpu, enum Reg16 reg1, enum Reg16 reg2) {
	uint16_t value1      = get_reg16(cpu, reg1);
	uint16_t value2      = get_reg16(cpu, reg2);
	uint16_t result      = value1 + value2;
	cpu->f_carry      = result < value1;
	cpu->f_half_carry = (value1 ^ value2 ^ result) & 0x1000;
	cpu->f_subtract   = false;
	set_reg16(cpu, reg1, result);
	system_tick(cpu);
}

static inline void op_call(struct HagemuCPU *cpu, bool condition) {
	uint16_t address = get_reg16(cpu, IMMEDIATE16);
	if (condition) {
		push_stack(cpu, cpu->pc);
		cpu->pc = address;
	}
}

static inline void op_daa(struct HagemuCPU *cpu) {
	uint8_t reg_a   = get_reg8(cpu, REG_A);
	unsigned offset = 0;
	if (!cpu->f_subtract) {
		if (cpu->f_half_carry || (reg_a & 0x0F) > 0x09)
			offset |= 0x06;
		if (cpu->f_carry || reg_a > 0x99)
			offset |= 0x60;
		cpu->f_carry |= (reg_a > (0xFF - offset));
		reg_a += offset;
	} else {
		if (cpu->f_half_carry) offset |= 0x06;
		if (cpu->f_carry)      offset |= 0x60;
		reg_a -= offset;
	}
	cpu->f_half_carry = false;
	cpu->f_zero = !reg_a;
	set_reg8(cpu, REG_A, reg_a);
}

static inline void op_nop(struct HagemuCPU *cpu) {
}

static inline void op_load8(struct HagemuCPU *cpu, enum Reg8 dest, enum Reg8 src) {
	set_reg8(cpu, dest, get_reg8(cpu, src));
}

static inline void op_load16(struct HagemuCPU *cpu, enum Reg16 dest, enum Reg16 src) {
	set_reg16(cpu, dest, get_reg16(cpu, src));
}

static inline void op_inc16(struct HagemuCPU *cpu, enum Reg16 reg) {
	system_tick(cpu);
	set_reg16(cpu, reg, get_reg16(cpu, reg) + 1);
}

static inline void op_dec16(struct HagemuCPU *cpu, enum Reg16 reg) {
	system_tick(cpu);
	set_reg16(cpu, reg, get_reg16(cpu, reg) - 1);
}

static inline void op_rlca(struct HagemuCPU *cpu) {
	op_rlc8(cpu, REG_A);
	cpu->f_zero = false;
}

static inline void op_rrca(struct HagemuCPU *cpu) {
	op_rrc8(cpu, REG_A);
	cpu->f_zero = false;
}

static inline void op_rla(struct HagemuCPU *cpu) {
	op_rl8(cpu, REG_A);
	cpu->f_zero = false;
}

static inline void op_rra(struct HagemuCPU *cpu) {
	op_rr8(cpu, REG_A);
	cpu->f_zero = false;
}

static inline void op_store_sp(struct HagemuCPU *cpu) {
	uint16_t address = get_reg16(cpu, IMMEDIATE16);
	uint16_t value   = get_reg16(cpu, REG_SP);
	system_tick(cpu);
	mmu_write(address, value & 0x00FF);
	system_tick(cpu);
	mmu_write(address + 1, (value & 0xFF00) >> 8);
}

static inline void op_stop(struct HagemuCPU *cpu) {
	printf("[WARNING] The stop operation is not fully tested\n");
	cpu->is_stopped = true;
	cpu->pc++;
}

static inline void op_cpl(struct HagemuCPU *cpu) {
	cpu->f_half_carry = true;
	cpu->f_subtract   = true;
	uint8_t reg_a        = get_reg8(cpu, REG_A);
	set_reg8(cpu, REG_A, ~reg_a);
}

static inline void op_scf(struct HagemuCPU *cpu) {
	cpu->f_carry      = true;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
}

static inline void op_ccf(struct HagemuCPU *cpu) {
	cpu->f_carry      = !cpu->f_carry;
	cpu->f_half_carry = false;
	cpu->f_subtract   = false;
}

static inline void op_push(struct HagemuCPU *cpu, enum Reg16 reg) {
	uint16_t value = get_reg16(cpu, reg);
	push_stack(cpu, value);
}

static inline void op_pop(struct HagemuCPU *cpu, enum Reg16 reg) {
	set_reg16(cpu, reg, pop_stack(cpu));
}

static inline void op_ret(struct HagemuCPU *cpu) {
	system_tick(cpu);
	cpu->pc = pop_stack(cpu);
}

static inline void op_ret_cond(struct HagemuCPU *cpu, bool condition) {
	system_tick(cpu);
	if (condition) {
		op_ret(cpu);
	}
}

static inline void op_reti(struct HagemuCPU *cpu) {
	op_ret(cpu);
	cpu->master_interrupt = true;
}

static inline void op_add_sp_offset(struct HagemuCPU *cpu, enum Reg8 offset) {
	uint8_t value     = get_reg8(cpu, offset);
	uint16_t result   = cpu->sp + (int8_t)value;
	cpu->f_carry      = ((cpu->sp & 0x00FF) + value) & 0x0100;
	cpu->f_half_carry = (cpu->sp ^ value ^ result) & 0x10;
	cpu->f_subtract   = false;
	cpu->f_zero       = false;
	cpu->sp           = result;
	system_tick(cpu);
	system_tick(cpu);
}

static inline void op_load_sp_offset(struct HagemuCPU *cpu, enum Reg16 reg, enum Reg8 offset) {
	uint8_t value     = get_reg8(cpu, offset);
	uint16_t result   = cpu->sp + (int8_t)value;
	cpu->f_carry      = ((cpu->sp & 0x00FF) + value) & 0x0100;
	cpu->f_half_carry = (cpu->sp ^ value ^ result) & 0x10;
	cpu->f_subtract   = false;
	cpu->f_zero       = false;
	set_reg16(cpu, reg, result);
	system_tick(cpu);
}

static inline void op_di(struct HagemuCPU *cpu) {
	cpu->master_interrupt_pending = false;
	cpu->master_interrupt = false;
}

static inline void op_ei(struct HagemuCPU *cpu) {
	cpu->master_interrupt_pending = true;
}

static inline void op_halt(struct HagemuCPU *cpu) {
	cpu->is_halted = true;
}

static inline void op_load_sp_hl(struct HagemuCPU *cpu) {
	system_tick(cpu);
	cpu->sp = cpu->hl;
}

static inline void error_no_opcode(uint8_t opcode_byte) {
	printf("Error: Op Code 0x%02X doesn't exist\n", opcode_byte);
	exit(EXIT_FAILURE);
}

static void process_cb_opcode(struct HagemuCPU *cpu) {
	uint8_t opcode = get_reg8(cpu, IMMEDIATE8);
	// The lower 4 bits of the opcode determines the operand
	enum Reg8 reg;
	switch (opcode & 0x07) {

	case 0x00: reg = REG_B; break;
	case 0x01: reg = REG_C; break;
	case 0x02: reg = REG_D; break;
	case 0x03: reg = REG_E; break;
	case 0x04: reg = REG_H; break;
	case 0x05: reg = REG_L; break;
	case 0x06: reg = REG_HL_ADDR; break;
	case 0x07: reg = REG_A; break;
	}

	// The upper 5 bits of the opcode determines the operation
	switch (opcode & 0xF8) {

	case 0x00: op_rlc8(cpu, reg);  break; // ROTATE LEFT CIRCULAR
	case 0x08: op_rrc8(cpu, reg);  break; // ROTATE RIGHT CIRCULAR
	case 0x10: op_rl8(cpu, reg);   break; // ROTATE LEFT
	case 0x18: op_rr8(cpu, reg);   break; // ROTATE RIGHT
	case 0x20: op_sla8(cpu, reg);  break; // SHIFT LEFT ARITHMETIC
	case 0x28: op_sra8(cpu, reg);  break; // SHIFT RIGHT ARITHEMTIC
	case 0x30: op_swap8(cpu, reg); break; // SWAP
	case 0x38: op_srl8(cpu, reg);  break; // SHIFT RIGHT LOGICAL

	case 0x40: op_bit(cpu, 0, reg); break; // TEST BIT 0
	case 0x48: op_bit(cpu, 1, reg); break; // TEST BIT 1
	case 0x50: op_bit(cpu, 2, reg); break; // TEST BIT 2
	case 0x58: op_bit(cpu, 3, reg); break; // TEST BIT 3
	case 0x60: op_bit(cpu, 4, reg); break; // TEST BIT 4
	case 0x68: op_bit(cpu, 5, reg); break; // TEST BIT 5
	case 0x70: op_bit(cpu, 6, reg); break; // TEST BIT 6
	case 0x78: op_bit(cpu, 7, reg); break; // TEST BIT 7

	case 0x80: op_res(cpu, 0, reg); break; // RESET BIT 0
	case 0x88: op_res(cpu, 1, reg); break; // RESET BIT 1
	case 0x90: op_res(cpu, 2, reg); break; // RESET BIT 2
	case 0x98: op_res(cpu, 3, reg); break; // RESET BIT 3
	case 0xA0: op_res(cpu, 4, reg); break; // RESET BIT 4
	case 0xA8: op_res(cpu, 5, reg); break; // RESET BIT 5
	case 0xB0: op_res(cpu, 6, reg); break; // RESET BIT 6
	case 0xB8: op_res(cpu, 7, reg); break; // RESET BIT 7

	case 0xC0: op_set(cpu, 0, reg); break; // SET BIT 0
	case 0xC8: op_set(cpu, 1, reg); break; // SET BIT 1
	case 0xD0: op_set(cpu, 2, reg); break; // SET BIT 2
	case 0xD8: op_set(cpu, 3, reg); break; // SET BIT 3
	case 0xE0: op_set(cpu, 4, reg); break; // SET BIT 4
	case 0xE8: op_set(cpu, 5, reg); break; // SET BIT 5
	case 0xF0: op_set(cpu, 6, reg); break; // SET BIT 6
	case 0xF8: op_set(cpu, 7, reg); break; // SET BIT 7

	default:
		printf("Error: Unknown prefixed opcode `%02X'\n", opcode);
		exit(EXIT_FAILURE);
		break;
	}
}

static void process_opcode(struct HagemuCPU *cpu, uint8_t opcode_byte) {
	switch (opcode_byte) {

	case 0x00: op_nop(cpu);                         break;
	case 0x01: op_load16(cpu, REG_BC, IMMEDIATE16); break;
	case 0x02: op_load8(cpu, REG_BC_ADDR, REG_A);   break;
	case 0x03: op_inc16(cpu, REG_BC);               break;
	case 0x04: op_inc8(cpu, REG_B);                 break;
	case 0x05: op_dec8(cpu, REG_B);                 break;
	case 0x06: op_load8(cpu, REG_B, IMMEDIATE8);    break;
	case 0x07: op_rlca(cpu);                        break;
	case 0x08: op_store_sp(cpu);                    break;
	case 0x09: op_add16(cpu, REG_HL, REG_BC);       break;
	case 0x0A: op_load8(cpu, REG_A, REG_BC_ADDR);   break;
	case 0x0B: op_dec16(cpu, REG_BC);               break;
	case 0x0C: op_inc8(cpu, REG_C);                 break;
	case 0x0D: op_dec8(cpu, REG_C);                 break;
	case 0x0E: op_load8(cpu, REG_C, IMMEDIATE8);    break;
	case 0x0F: op_rrca(cpu);                        break;

	case 0x10: op_stop(cpu);                        break;
	case 0x11: op_load16(cpu, REG_DE, IMMEDIATE16); break;
	case 0x12: op_load8(cpu, REG_DE_ADDR, REG_A);   break;
	case 0x13: op_inc16(cpu, REG_DE);               break;
	case 0x14: op_inc8(cpu, REG_D);                 break;
	case 0x15: op_dec8(cpu, REG_D);                 break;
	case 0x16: op_load8(cpu, REG_D, IMMEDIATE8);    break;
	case 0x17: op_rla(cpu);                         break;
	case 0x18: op_jr(cpu, true);                    break;
	case 0x19: op_add16(cpu, REG_HL, REG_DE);       break;
	case 0x1A: op_load8(cpu, REG_A, REG_DE_ADDR);   break;
	case 0x1B: op_dec16(cpu, REG_DE);               break;
	case 0x1C: op_inc8(cpu, REG_E);                 break;
	case 0x1D: op_dec8(cpu, REG_E);                 break;
	case 0x1E: op_load8(cpu, REG_E, IMMEDIATE8);    break;
	case 0x1F: op_rra(cpu);                         break;

	case 0x20: op_jr(cpu, !cpu->f_zero);              break;
	case 0x21: op_load16(cpu, REG_HL, IMMEDIATE16);   break;
	case 0x22: op_load8(cpu, REG_HL_ADDR_INC, REG_A); break;
	case 0x23: op_inc16(cpu, REG_HL);                 break;
	case 0x24: op_inc8(cpu, REG_H);                   break;
	case 0x25: op_dec8(cpu, REG_H);                   break;
	case 0x26: op_load8(cpu, REG_H, IMMEDIATE8);      break;
	case 0x27: op_daa(cpu);                           break;
	case 0x28: op_jr(cpu, cpu->f_zero);               break;
	case 0x29: op_add16(cpu, REG_HL, REG_HL);         break;
	case 0x2A: op_load8(cpu, REG_A, REG_HL_ADDR_INC); break;
	case 0x2B: op_dec16(cpu, REG_HL);                 break;
	case 0x2C: op_inc8(cpu, REG_L);                   break;
	case 0x2D: op_dec8(cpu, REG_L);                   break;
	case 0x2E: op_load8(cpu, REG_L, IMMEDIATE8);      break;
	case 0x2F: op_cpl(cpu);                           break;

	case 0x30: op_jr(cpu, !cpu->f_carry);              break;
	case 0x31: op_load16(cpu, REG_SP, IMMEDIATE16);    break;
	case 0x32: op_load8(cpu, REG_HL_ADDR_DEC, REG_A);  break;
	case 0x33: op_inc16(cpu, REG_SP);                  break;
	case 0x34: op_inc8(cpu, REG_HL_ADDR);              break;
	case 0x35: op_dec8(cpu, REG_HL_ADDR);              break;
	case 0x36: op_load8(cpu, REG_HL_ADDR, IMMEDIATE8); break;
	case 0x37: op_scf(cpu);                            break;
	case 0x38: op_jr(cpu, cpu->f_carry);               break;
	case 0x39: op_add16(cpu, REG_HL, REG_SP);          break;
	case 0x3A: op_load8(cpu, REG_A, REG_HL_ADDR_DEC);  break;
	case 0x3B: op_dec16(cpu, REG_SP);                  break;
	case 0x3C: op_inc8(cpu, REG_A);                    break;
	case 0x3D: op_dec8(cpu, REG_A);                    break;
	case 0x3E: op_load8(cpu, REG_A, IMMEDIATE8);       break;
	case 0x3F: op_ccf(cpu);                            break;

	case 0x40: op_load8(cpu, REG_B, REG_B);       break;
	case 0x41: op_load8(cpu, REG_B, REG_C);       break;
	case 0x42: op_load8(cpu, REG_B, REG_D);       break;
	case 0x43: op_load8(cpu, REG_B, REG_E);       break;
	case 0x44: op_load8(cpu, REG_B, REG_H);       break;
	case 0x45: op_load8(cpu, REG_B, REG_L);       break;
	case 0x46: op_load8(cpu, REG_B, REG_HL_ADDR); break;
	case 0x47: op_load8(cpu, REG_B, REG_A);       break;
	case 0x48: op_load8(cpu, REG_C, REG_B);       break;
	case 0x49: op_load8(cpu, REG_C, REG_C);       break;
	case 0x4A: op_load8(cpu, REG_C, REG_D);       break;
	case 0x4B: op_load8(cpu, REG_C, REG_E);       break;
	case 0x4C: op_load8(cpu, REG_C, REG_H);       break;
	case 0x4D: op_load8(cpu, REG_C, REG_L);       break;
	case 0x4E: op_load8(cpu, REG_C, REG_HL_ADDR); break;
	case 0x4F: op_load8(cpu, REG_C, REG_A);       break;

	case 0x50: op_load8(cpu, REG_D, REG_B);       break;
	case 0x51: op_load8(cpu, REG_D, REG_C);       break;
	case 0x52: op_load8(cpu, REG_D, REG_D);       break;
	case 0x53: op_load8(cpu, REG_D, REG_E);       break;
	case 0x54: op_load8(cpu, REG_D, REG_H);       break;
	case 0x55: op_load8(cpu, REG_D, REG_L);       break;
	case 0x56: op_load8(cpu, REG_D, REG_HL_ADDR); break;
	case 0x57: op_load8(cpu, REG_D, REG_A);       break;
	case 0x58: op_load8(cpu, REG_E, REG_B);       break;
	case 0x59: op_load8(cpu, REG_E, REG_C);       break;
	case 0x5A: op_load8(cpu, REG_E, REG_D);       break;
	case 0x5B: op_load8(cpu, REG_E, REG_E);       break;
	case 0x5C: op_load8(cpu, REG_E, REG_H);       break;
	case 0x5D: op_load8(cpu, REG_E, REG_L);       break;
	case 0x5E: op_load8(cpu, REG_E, REG_HL_ADDR); break;
	case 0x5F: op_load8(cpu, REG_E, REG_A);       break;

	case 0x60: op_load8(cpu, REG_H, REG_B);       break;
	case 0x61: op_load8(cpu, REG_H, REG_C);       break;
	case 0x62: op_load8(cpu, REG_H, REG_D);       break;
	case 0x63: op_load8(cpu, REG_H, REG_E);       break;
	case 0x64: op_load8(cpu, REG_H, REG_H);       break;
	case 0x65: op_load8(cpu, REG_H, REG_L);       break;
	case 0x66: op_load8(cpu, REG_H, REG_HL_ADDR); break;
	case 0x67: op_load8(cpu, REG_H, REG_A);       break;
	case 0x68: op_load8(cpu, REG_L, REG_B);       break;
	case 0x69: op_load8(cpu, REG_L, REG_C);       break;
	case 0x6A: op_load8(cpu, REG_L, REG_D);       break;
	case 0x6B: op_load8(cpu, REG_L, REG_E);       break;
	case 0x6C: op_load8(cpu, REG_L, REG_H);       break;
	case 0x6D: op_load8(cpu, REG_L, REG_L);       break;
	case 0x6E: op_load8(cpu, REG_L, REG_HL_ADDR); break;
	case 0x6F: op_load8(cpu, REG_L, REG_A);       break;

	case 0x70: op_load8(cpu, REG_HL_ADDR, REG_B); break;
	case 0x71: op_load8(cpu, REG_HL_ADDR, REG_C); break;
	case 0x72: op_load8(cpu, REG_HL_ADDR, REG_D); break;
	case 0x73: op_load8(cpu, REG_HL_ADDR, REG_E); break;
	case 0x74: op_load8(cpu, REG_HL_ADDR, REG_H); break;
	case 0x75: op_load8(cpu, REG_HL_ADDR, REG_L); break;
	case 0x76: op_halt(cpu);                      break;
	case 0x77: op_load8(cpu, REG_HL_ADDR, REG_A); break;
	case 0x78: op_load8(cpu, REG_A, REG_B);       break;
	case 0x79: op_load8(cpu, REG_A, REG_C);       break;
	case 0x7A: op_load8(cpu, REG_A, REG_D);       break;
	case 0x7B: op_load8(cpu, REG_A, REG_E);       break;
	case 0x7C: op_load8(cpu, REG_A, REG_H);       break;
	case 0x7D: op_load8(cpu, REG_A, REG_L);       break;
	case 0x7E: op_load8(cpu, REG_A, REG_HL_ADDR); break;
	case 0x7F: op_load8(cpu, REG_A, REG_A);       break;

	case 0x80: op_add(cpu, REG_B);       break;
	case 0x81: op_add(cpu, REG_C);       break;
	case 0x82: op_add(cpu, REG_D);       break;
	case 0x83: op_add(cpu, REG_E);       break;
	case 0x84: op_add(cpu, REG_H);       break;
	case 0x85: op_add(cpu, REG_L);       break;
	case 0x86: op_add(cpu, REG_HL_ADDR); break;
	case 0x87: op_add(cpu, REG_A);       break;
	case 0x88: op_adc(cpu, REG_B);       break;
	case 0x89: op_adc(cpu, REG_C);       break;
	case 0x8A: op_adc(cpu, REG_D);       break;
	case 0x8B: op_adc(cpu, REG_E);       break;
	case 0x8C: op_adc(cpu, REG_H);       break;
	case 0x8D: op_adc(cpu, REG_L);       break;
	case 0x8E: op_adc(cpu, REG_HL_ADDR); break;
	case 0x8F: op_adc(cpu, REG_A);       break;

	case 0x90: op_sub(cpu, REG_B);       break;
	case 0x91: op_sub(cpu, REG_C);       break;
	case 0x92: op_sub(cpu, REG_D);       break;
	case 0x93: op_sub(cpu, REG_E);       break;
	case 0x94: op_sub(cpu, REG_H);       break;
	case 0x95: op_sub(cpu, REG_L);       break;
	case 0x96: op_sub(cpu, REG_HL_ADDR); break;
	case 0x97: op_sub(cpu, REG_A);       break;
	case 0x98: op_sbc(cpu, REG_B);       break;
	case 0x99: op_sbc(cpu, REG_C);       break;
	case 0x9A: op_sbc(cpu, REG_D);       break;
	case 0x9B: op_sbc(cpu, REG_E);       break;
	case 0x9C: op_sbc(cpu, REG_H);       break;
	case 0x9D: op_sbc(cpu, REG_L);       break;
	case 0x9E: op_sbc(cpu, REG_HL_ADDR); break;
	case 0x9F: op_sbc(cpu, REG_A);       break;

	case 0xA0: op_and8(cpu, REG_B);       break;
	case 0xA1: op_and8(cpu, REG_C);       break;
	case 0xA2: op_and8(cpu, REG_D);       break;
	case 0xA3: op_and8(cpu, REG_E);       break;
	case 0xA4: op_and8(cpu, REG_H);       break;
	case 0xA5: op_and8(cpu, REG_L);       break;
	case 0xA6: op_and8(cpu, REG_HL_ADDR); break;
	case 0xA7: op_and8(cpu, REG_A);       break;
	case 0xA8: op_xor8(cpu, REG_B);       break;
	case 0xA9: op_xor8(cpu, REG_C);       break;
	case 0xAA: op_xor8(cpu, REG_D);       break;
	case 0xAB: op_xor8(cpu, REG_E);       break;
	case 0xAC: op_xor8(cpu, REG_H);       break;
	case 0xAD: op_xor8(cpu, REG_L);       break;
	case 0xAE: op_xor8(cpu, REG_HL_ADDR); break;
	case 0xAF: op_xor8(cpu, REG_A);       break;

	case 0xB0: op_or8(cpu, REG_B);        break;
	case 0xB1: op_or8(cpu, REG_C);        break;
	case 0xB2: op_or8(cpu, REG_D);        break;
	case 0xB3: op_or8(cpu, REG_E);        break;
	case 0xB4: op_or8(cpu, REG_H);        break;
	case 0xB5: op_or8(cpu, REG_L);        break;
	case 0xB6: op_or8(cpu, REG_HL_ADDR);  break;
	case 0xB7: op_or8(cpu, REG_A);        break;
	case 0xB8: op_cp8(cpu, REG_B);        break;
	case 0xB9: op_cp8(cpu, REG_C);        break;
	case 0xBA: op_cp8(cpu, REG_D);        break;
	case 0xBB: op_cp8(cpu, REG_E);        break;
	case 0xBC: op_cp8(cpu, REG_H);        break;
	case 0xBD: op_cp8(cpu, REG_L);        break;
	case 0xBE: op_cp8(cpu, REG_HL_ADDR);  break;
	case 0xBF: op_cp8(cpu, REG_A);        break;

	case 0xC0: op_ret_cond(cpu, !cpu->f_zero); break;
	case 0xC1: op_pop(cpu, REG_BC);            break;
	case 0xC2: op_jump(cpu, !cpu->f_zero);     break;
	case 0xC3: op_jump(cpu, true);             break;
	case 0xC4: op_call(cpu, !cpu->f_zero);     break;
	case 0xC5: op_push(cpu, REG_BC);           break;
	case 0xC6: op_add(cpu, IMMEDIATE8);        break;
	case 0xC7: op_rst(cpu, 0x00);              break;
	case 0xC8: op_ret_cond(cpu, cpu->f_zero);  break;
	case 0xC9: op_ret(cpu);                    break;
	case 0xCA: op_jump(cpu, cpu->f_zero);      break;
	case 0xCB: process_cb_opcode(cpu);         break;
	case 0xCC: op_call(cpu, cpu->f_zero);      break;
	case 0xCD: op_call(cpu, true);             break;
	case 0xCE: op_adc(cpu, IMMEDIATE8);        break;
	case 0xCF: op_rst(cpu, 0x08);              break;

	case 0xD0: op_ret_cond(cpu, !cpu->f_carry); break;
	case 0xD1: op_pop(cpu, REG_DE);             break;
	case 0xD2: op_jump(cpu, !cpu->f_carry);     break;
	case 0xD3: error_no_opcode(opcode_byte);    break;
	case 0xD4: op_call(cpu, !cpu->f_carry);     break;
	case 0xD5: op_push(cpu, REG_DE);            break;
	case 0xD6: op_sub(cpu, IMMEDIATE8);         break;
	case 0xD7: op_rst(cpu, 0x10);               break;
	case 0xD8: op_ret_cond(cpu, cpu->f_carry);  break;
	case 0xD9: op_reti(cpu);                    break;
	case 0xDA: op_jump(cpu, cpu->f_carry);      break;
	case 0xDB: error_no_opcode(opcode_byte);    break;
	case 0xDC: op_call(cpu, cpu->f_carry);      break;
	case 0xDD: error_no_opcode(opcode_byte);    break;
	case 0xDE: op_sbc(cpu, IMMEDIATE8);         break;
	case 0xDF: op_rst(cpu, 0x18);               break;

	case 0xE0: op_load8(cpu, HIGH_ADDR_IMM8, REG_A);      break;
	case 0xE1: op_pop(cpu, REG_HL);                       break;
	case 0xE2: op_load8(cpu, HIGH_ADDR_REG_C, REG_A);     break;
	case 0xE3: error_no_opcode(opcode_byte);              break;
	case 0xE4: error_no_opcode(opcode_byte);              break;
	case 0xE5: op_push(cpu, REG_HL);                      break;
	case 0xE6: op_and8(cpu, IMMEDIATE8);                  break;
	case 0xE7: op_rst(cpu, 0x20);                         break;
	case 0xE8: op_add_sp_offset(cpu, IMMEDIATE8);         break;
	case 0xE9: op_load16(cpu, REG_PC, REG_HL);            break;
	case 0xEA: op_load8(cpu, IMMEDIATE16_ADDR, REG_A);    break;
	case 0xEB: error_no_opcode(opcode_byte);              break;
	case 0xEC: error_no_opcode(opcode_byte);              break;
	case 0xED: error_no_opcode(opcode_byte);              break;
	case 0xEE: op_xor8(cpu, IMMEDIATE8);                  break;
	case 0xEF: op_rst(cpu, 0x28);                         break;

	case 0xF0: op_load8(cpu, REG_A, HIGH_ADDR_IMM8);       break;
	case 0xF1: op_pop(cpu, REG_AF);                        break;
	case 0xF2: op_load8(cpu, REG_A, HIGH_ADDR_REG_C);      break;
	case 0xF3: op_di(cpu);                                 break;
	case 0xF4: error_no_opcode(opcode_byte);               break;
	case 0xF5: op_push(cpu, REG_AF);                       break;
	case 0xF6: op_or8(cpu, IMMEDIATE8);                    break;
	case 0xF7: op_rst(cpu, 0x30);                          break;
	case 0xF8: op_load_sp_offset(cpu, REG_HL, IMMEDIATE8); break;
	case 0xF9: op_load_sp_hl(cpu);                         break;
	case 0xFA: op_load8(cpu, REG_A, IMMEDIATE16_ADDR);     break;
	case 0xFB: op_ei(cpu);                                 break;
	case 0xFC: error_no_opcode(opcode_byte);               break;
	case 0xFD: error_no_opcode(opcode_byte);               break;
	case 0xFE: op_cp8(cpu, IMMEDIATE8);                    break;
	case 0xFF: op_rst(cpu, 0x38);                          break;

	default: error_no_opcode(opcode_byte); break;
	}
}

// Returns the number of t-cycles it took to complete the next instruction
int cpu_do_next_instruction(struct HagemuCPU *cpu) {
	cpu->cycles_passed = 0;

	if (cpu->is_stopped) {
		// system_tick isn't called, but I still need to return 4
		// to keep hagemu_app running normally
		return 4;
	}

	if (interrupt_pending())
		cpu->is_halted = false;

	if (cpu->is_halted) {
		system_tick(cpu);
		return cpu->cycles_passed; // clock only incremented once
	}

	if (cpu->master_interrupt_pending) {
		cpu->master_interrupt_pending = false;
		cpu->master_interrupt = true;
	} else if (cpu->master_interrupt) {
		handle_interrupts(cpu);
	}

	uint8_t opcode_byte = fetch_immediate8(cpu);
	process_opcode(cpu, opcode_byte);
	return cpu->cycles_passed;
}
