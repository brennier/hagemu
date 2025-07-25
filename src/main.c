#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "mmu.h"
#include "clock.h"
#include "cpu.h"

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

#define SCALE_FACTOR 5
#define SCREENWIDTH 166 * SCALE_FACTOR
#define SCREENHEIGHT 144 * SCALE_FACTOR
#define SPEED_FACTOR 10
#define CLOCK_SPEED 4194304 * SPEED_FACTOR

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

	InitWindow(SCREENWIDTH, SCREENHEIGHT, "GameBoy Emulator");
	SetWindowState(FLAG_VSYNC_HINT);

	Image tile_image = GenImageColor(8, 3072, BLACK);
	Texture2D tile_data = LoadTextureFromImage(tile_image);
	UnloadImage(tile_image);

	while (WindowShouldClose() != true) {
		int cycles_since_last_frame = 0;
		float delta_time = GetFrameTime();
		while (cycles_since_last_frame < delta_time * CLOCK_SPEED)
			cycles_since_last_frame += cpu_do_next_instruction();

		uint16_t tile_data_start = 0x8000;
		uint16_t tile_data_end = 0x9800;
		Color *raw_tile_data = (Color*)malloc(384 * 8 * 8 * sizeof(Color));
		Color *raw_tile_data_pos = raw_tile_data;

		mmu_write(tile_data_start, 0x3C);
		mmu_write(tile_data_start + 1, 0x7E);
		mmu_write(tile_data_start + 2, 0x42);
		mmu_write(tile_data_start + 3, 0x42);
		mmu_write(tile_data_start + 4, 0x42);
		mmu_write(tile_data_start + 5, 0x42);
		mmu_write(tile_data_start + 6, 0x42);
		mmu_write(tile_data_start + 7, 0x42);
		mmu_write(tile_data_start + 8, 0x7E);
		mmu_write(tile_data_start + 9, 0x5E);
		mmu_write(tile_data_start + 10, 0x7E);
		mmu_write(tile_data_start + 11, 0x0A);
		mmu_write(tile_data_start + 12, 0x7C);
		mmu_write(tile_data_start + 13, 0x56);
		mmu_write(tile_data_start + 14, 0x38);
		mmu_write(tile_data_start + 15, 0x7C);

		for (int pos = tile_data_start; pos < tile_data_end; pos += 2) {
			uint8_t byte1 = mmu_read(pos);
			uint8_t byte2 = mmu_read(pos+1);

			for (int bit_num = 0; bit_num < 8; bit_num++) {
				int byte1_bit = (byte1 >> (7 - bit_num)) & 0x01;
				int byte2_bit = (byte2 >> (7 - bit_num)) & 0x01;

				switch ((byte2_bit << 1) | byte1_bit) {

				case 0: *(raw_tile_data_pos++) = GREEN4; break;
				case 1: *(raw_tile_data_pos++) = GREEN3; break;
				case 2: *(raw_tile_data_pos++) = GREEN2; break;
				case 3: *(raw_tile_data_pos++) = GREEN1; break;
				}
			}
		}
		UpdateTexture(tile_data, raw_tile_data);
		free(raw_tile_data);

		BeginDrawing();
		ClearBackground(BLACK);
		DrawRectangle(0, 0, 32 * 8 + 10, 512 + 10, BLUE);
		DrawRectangle(32 * 8, 0, 32 * 8 + 10, 512 + 10, RED);
		DrawRectangle(64 * 8, 0, 32 * 8 + 10, 512 + 10, YELLOW);
		for (int i = 0; i < 24; i++)
			DrawTexturePro(tile_data, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}
	CloseWindow();

	return 0;
}
