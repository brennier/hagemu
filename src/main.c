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
#define SPEED_FACTOR 1
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

void load_tile_data_block(Texture2D *tile_data_block, uint16_t address_start) {
	uint16_t address_end = address_start + 0x0800;
	Color *raw_tile_data = (Color*)malloc(128 * 8 * 8 * sizeof(Color));
	Color *raw_tile_data_pos = raw_tile_data;

	for (int pos = address_start; pos < address_end; pos += 2) {
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
	UpdateTexture(*tile_data_block, raw_tile_data);
	free(raw_tile_data);

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

	Image tile_image = GenImageColor(8, 1024, BLACK);
	Texture2D tile_data_block_1 = LoadTextureFromImage(tile_image);
	Texture2D tile_data_block_2 = LoadTextureFromImage(tile_image);
	Texture2D tile_data_block_3 = LoadTextureFromImage(tile_image);
	UnloadImage(tile_image);

	while (WindowShouldClose() != true) {
		int cycles_since_last_frame = 0;
		float delta_time = GetFrameTime();
		while (cycles_since_last_frame < delta_time * CLOCK_SPEED)
			cycles_since_last_frame += cpu_do_next_instruction();

		load_tile_data_block(&tile_data_block_1, 0x8000);
		load_tile_data_block(&tile_data_block_2, 0x8800);
		load_tile_data_block(&tile_data_block_3, 0x9000);

		BeginDrawing();
		ClearBackground(BLACK);
		for (int i = 0; i < 8; i++)
			DrawTexturePro(tile_data_block_1, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE);
		for (int i = 0; i < 8; i++)
			DrawTexturePro(tile_data_block_2, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i + 256 + 32, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE);
		for (int i = 0; i < 8; i++)
			DrawTexturePro(tile_data_block_3, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i + 512 + 64, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}

	UnloadTexture(tile_data_block_1);
	UnloadTexture(tile_data_block_2);
	UnloadTexture(tile_data_block_3);
	CloseWindow();

	return 0;
}
