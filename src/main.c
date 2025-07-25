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

void debug_blargg_check_serial() {
	// Check for the word "Passed" and exit successfuly if detected
	char *sucessful_string = "Passed";
	static int current_char = 0;

	if (mmu_read(SERIAL_CONTROL) == 0x81) {
		char character = (char)mmu_read(SERIAL_DATA);
		printf("%c", character);
		mmu_write(SERIAL_CONTROL, 0);
		if (character == sucessful_string[current_char])
			current_char++;
		else
			current_char = 0;

		if (sucessful_string[current_char] == '\0') {
			printf("\n");
			exit(EXIT_SUCCESS);
		}
	}
}

void debug_blargg_test_memory() {
	uint16_t address = 0xA000;
	mmu_write(address, 0x80);
	while (mmu_read(address) == 0x80)
		cpu_do_next_instruction();

	printf("Signature: %02X %02X %02X",
		mmu_read(address + 1),
		mmu_read(address + 2),
		mmu_read(address + 3));
	
	address = 0xA004;
	printf("%s", "Test Result: ");
	while (mmu_read(address) != 0) {
		printf("%c", (char)mmu_read(address));
		address++;
	}

	printf("\n%s", "Blargg Test Finished");
}

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
		debug_blargg_check_serial();
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