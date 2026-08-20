#ifndef CPU_H
#define CPU_H

#include <stdbool.h>

struct HagemuCPU;

struct HagemuCPU *cpu_create(void);
void cpu_destroy(struct HagemuCPU *cpu);
void cpu_reset(struct HagemuCPU *cpu);
int cpu_do_next_instruction(struct HagemuCPU *cpu);
void cpu_print_state(struct HagemuCPU *cpu);
void cpu_resume_if_stopped(struct HagemuCPU *cpu);

bool cpu_get_speed_mode(struct HagemuCPU *cpu);
void cpu_set_speed_mode_pending(struct HagemuCPU *cpu, bool flag);
bool cpu_get_speed_mode_pending(struct HagemuCPU *cpu);

#endif
