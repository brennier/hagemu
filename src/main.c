#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "mmu.h"
#include "clock.h"
#include "cpu.h"

#define SCREENWIDTH 166
#define SCREENHEIGHT 144

int main(int argc, char *argv[]) {
	cpu_reset();

	// The gameboy doctor test suite requires that the LY register always returns 0x90
	mmu_write(0xFF44, 0x90);

	if (argc == 1) {
		fprintf(stderr, "Error: No rom file specified\n");
		exit(EXIT_FAILURE);
	}
	else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	mmu_load_rom(argv[1]);

	while (true) {
		cpu_do_next_instruction();
		//int t_cycles = cpu_do_next_instruction();
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