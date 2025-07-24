#ifndef CPU_H
#define CPU_H

void cpu_reset();
int cpu_do_next_instruction();
void cpu_debug_opcode_timings();

#endif