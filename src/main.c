#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "mmu.h"
#include "cpu.h"

#define SCALE_FACTOR 5
#define SCREENWIDTH 160 * SCALE_FACTOR
#define SCREENHEIGHT 144 * SCALE_FACTOR
#define SPEED_FACTOR 1
#define CLOCK_SPEED 4194304L * SPEED_FACTOR

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

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

			case 0: *(raw_tile_data_pos++) = GREEN1; break;
			case 1: *(raw_tile_data_pos++) = GREEN2; break;
			case 2: *(raw_tile_data_pos++) = GREEN3; break;
			case 3: *(raw_tile_data_pos++) = GREEN4; break;
			}
		}
	}
	UpdateTexture(*tile_data_block, raw_tile_data);
	free(raw_tile_data);
}

char* ask_for_file_drop() {
	InitWindow(SCREENWIDTH, SCREENHEIGHT, "GameBoy Emulator");
	BeginDrawing();
	ClearBackground(GREEN1);
	int text_width = MeasureText("Please drop a .gb file onto this window", 7 * SCALE_FACTOR);
	DrawText("Please drop a .gb file onto this window",
		 SCREENWIDTH / 2 - text_width / 2,
		 SCREENHEIGHT / 2 - 3.5 * SCALE_FACTOR,
		 7 * SCALE_FACTOR,
		 GREEN3);
	EndDrawing();

	while (!WindowShouldClose()) {
		if (IsFileDropped()) {
			FilePathList dropped_files = LoadDroppedFiles();
			unsigned int length = TextLength(dropped_files.paths[0]);
			char *rom_path = calloc(length + 1, sizeof(dropped_files.paths[0]));
			if (rom_path == NULL) {
				fprintf(stderr, "Couldn't malloc space for the rom_path");
				exit(EXIT_FAILURE);
			}
			if (length != TextCopy(rom_path, dropped_files.paths[0])) {
				fprintf(stderr, "There was an error getting the filename");
				exit(EXIT_FAILURE);
			}
			UnloadDroppedFiles(dropped_files);
			return rom_path;
		}
	}

	printf("Drop file window closed");
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
	cpu_reset();
	char *rom_path;

	if (argc == 1) {
		rom_path = ask_for_file_drop();
	}
	else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	} else {
		rom_path = argv[1];
	}

	mmu_load_rom(rom_path);
	free(rom_path);

	InitWindow(SCREENWIDTH, SCREENHEIGHT, "GameBoy Emulator");
	SetWindowState(FLAG_VSYNC_HINT);

	Image tile_image = GenImageColor(8, 1024, BLACK);
	Texture2D tile_data_block_1 = LoadTextureFromImage(tile_image);
	Texture2D tile_data_block_2 = LoadTextureFromImage(tile_image);
	Texture2D tile_data_block_3 = LoadTextureFromImage(tile_image);
	UnloadImage(tile_image);

	while (WindowShouldClose() != true) {
		if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_LEFT) ||
		    IsKeyDown(KEY_UP)    || IsKeyDown(KEY_DOWN) ||
		    IsKeyDown(KEY_J)     || IsKeyDown(KEY_K)    ||
		    IsKeyDown(KEY_Z)     || IsKeyDown(KEY_X)     )
			mmu_set_bit(JOYPAD_INTERRUPT_FLAG_BIT);

		long cycles_since_last_frame = 0;
		float delta_time = GetFrameTime();

		while (cycles_since_last_frame < delta_time * CLOCK_SPEED) {
			cycles_since_last_frame += cpu_do_next_instruction();
		}

		load_tile_data_block(&tile_data_block_1, 0x8000);
		load_tile_data_block(&tile_data_block_2, 0x8800);
		load_tile_data_block(&tile_data_block_3, 0x9000);

		BeginDrawing();
		ClearBackground(BLACK);

		uint16_t tile_map_start = 0x9800;
		for (int row = 0; row < 32; row++)
			for (int col = 0; col < 32; col++) {
				int tile_pos = mmu_read(tile_map_start + row * 32 + col);
				if (tile_pos < 128)
					DrawTexturePro(tile_data_block_1, (Rectangle){ .x = 0, .y = 8 * tile_pos, .width = 8, .height = 8 }, (Rectangle){ .x = col * 40, .y = row * 40, .width = 40, .height = 40}, (Vector2){ 0, 0}, 0.0, WHITE);
				else
					DrawTexturePro(tile_data_block_2, (Rectangle){ .x = 0, .y = 8 * (tile_pos - 128), .width = 8, .height = 8 }, (Rectangle){ .x = col * 40, .y = row * 40, .width = 40, .height = 40}, (Vector2){ 0, 0}, 0.0, WHITE);
			}

		/* for (int i = 0; i < 8; i++) */
			/* DrawTexturePro(tile_data_block_1, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE); */
		/* for (int i = 0; i < 8; i++) */
			/* DrawTexturePro(tile_data_block_2, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i + 256 + 32, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE); */
		/* for (int i = 0; i < 8; i++) */
			/* DrawTexturePro(tile_data_block_3, (Rectangle){ .x = 0, .y = 128 * i, .width = 8, .height = 128 }, (Rectangle){ .x = 32 * i + 512 + 64, .y = 0, .width = 32, .height = 512 }, (Vector2){ 0, 0 }, 0.0, WHITE); */
		DrawFPS(10, 10);
		EndDrawing();
		mmu_set_bit(VBLANK_INTERRUPT_FLAG_BIT);
	}

	UnloadTexture(tile_data_block_1);
	UnloadTexture(tile_data_block_2);
	UnloadTexture(tile_data_block_3);
	CloseWindow();

	return 0;
}
