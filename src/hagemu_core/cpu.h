#ifndef CPU_H
#define CPU_H

struct HagemuCPU;

struct HagemuCPU *cpu_create();
void cpu_destory(struct HagemuCPU *cpu);
void cpu_reset(struct HagemuCPU *cpu);
int cpu_do_next_instruction(struct HagemuCPU *cpu);
void cpu_print_state(struct HagemuCPU *cpu);
void cpu_resume_if_stopped(struct HagemuCPU *cpu);

#endif
