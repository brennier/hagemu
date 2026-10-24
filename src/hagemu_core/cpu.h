#ifndef CPU_H
#define CPU_H

#include <stdbool.h>
#include <stdint.h>

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
	bool double_speed_mode;
	bool speed_mode_odd_cycle;
	bool set_speed_mode_pending;
	bool master_interrupt;
	bool master_interrupt_pending;
	bool is_halted;
	bool is_stopped;
	bool halt_bug;
	uint8_t cycles_passed;
};

void cpu_reset(struct HagemuCPU *cpu);
int cpu_do_next_instruction(struct HagemuCPU *cpu);
void cpu_print_state(struct HagemuCPU *cpu);
void cpu_resume_if_stopped(struct HagemuCPU *cpu);

bool cpu_get_speed_mode(struct HagemuCPU *cpu);
void cpu_set_speed_mode_pending(struct HagemuCPU *cpu, bool flag);
bool cpu_get_speed_mode_pending(struct HagemuCPU *cpu);

#endif
