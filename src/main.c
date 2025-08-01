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

typedef uint16_t R5G5B5A1;

// Data pipeline
// set_background_map ->
uint8_t background_index_map[32][32];
// set_raw_background_layer ->
// each row/col contains 2-bit color information
uint8_t raw_background_layer[256][256];

Color background_layer[144][160];
// render scanline (144 times) ->
R5G5B5A1 screen_buffer[144][160];
// add_sprites ->
// -- back to screen_buffer
// update_texture - >
Texture2D screen_texture;

void set_background_index_map() {
	uint16_t tile_map_start;
	if (mmu_get_bit(BG_TILE_MAP_AREA))
		tile_map_start = 0x9C00;
	else
		tile_map_start = 0x9800;

	for (int row = 0; row < 32; row++)
		for (int col = 0; col < 32; col++)
			background_index_map[row][col] = mmu_read(tile_map_start + 32 * row + col);
}

void set_raw_tile(uint16_t tile_data_start, int row_start, int col_start) {
	uint16_t current_address = tile_data_start;
	for (int row = row_start; row < row_start + 8; row++) {
		uint8_t byte1 = mmu_read(current_address++);
		uint8_t byte2 = mmu_read(current_address++);
		for (int col = col_start; col < col_start + 8; col++) {
			bool bit1 = (byte1 & 0x80) >> 7;
			bool bit2 = (byte2 & 0x80) >> 7;
			raw_background_layer[row][col] = (bit2 << 1) | bit1;
			byte1 <<= 1;
			byte2 <<= 1;
		}
	}
}

void set_raw_background() {
	uint16_t data_block_1 = mmu_get_bit(BG_TILE_DATA_AREA) ? 0x8000 : 0x9000;
	uint16_t data_block_2 = 0x8800;

	for (int row = 0; row < 32; row++)
		for (int col = 0; col < 32; col++) {
			uint8_t tile_index = background_index_map[row][col];
			uint16_t tile_data_start;
			if (tile_index < 128)
				tile_data_start = data_block_1 + 16 * tile_index;
			else
				tile_data_start = data_block_2 + (16 * (tile_index - 128));
			
			set_raw_tile(tile_data_start, row * 8, col * 8);
		}
}

void set_background_layer() {
	for (int row = 0; row < 144; row++)
		for (int col = 0; col < 160; col++)
			switch (raw_background_layer[row % 256][col % 256]) {

			case 0: background_layer[row][col] = GREEN1; break;
			case 1: background_layer[row][col] = GREEN2; break;
			case 2: background_layer[row][col] = GREEN3; break;
			case 3: background_layer[row][col] = GREEN4; break;
			}
}

void set_sprites() {
	uint16_t oam_start = 0xFE00;
	uint16_t oam_end   = 0xFE9F;

	for (int sprite_start = oam_start; sprite_start < oam_end; sprite_start += 4) {
		uint8_t y_position = mmu_read(sprite_start);
		uint8_t x_position = mmu_read(sprite_start + 1);
		uint8_t tile_index = mmu_read(sprite_start + 2);
		uint8_t attributes = mmu_read(sprite_start + 3);

		if (attributes) {
			fprintf(stderr, "Attributes not implemented yet");
		}

		uint16_t tile_data_start = 0x8000 + 16 * tile_index;

		set_raw_tile(tile_data_start, y_position - 16, x_position - 8);
	}
}

void DrawCenteredText(char* text, int font_size, Color color) {
	int text_width = MeasureText(text, font_size);
	DrawText(text,
		SCREENWIDTH / 2 - text_width / 2,
		SCREENHEIGHT / 2 - font_size / 2,
		font_size,
		color
	);
}

void DrawTile(Texture2D *tile_block, int tile_index, int row, int col) {
	Rectangle source = (Rectangle){ .x = 0, .y = 8 * tile_index, .width = 8, .height = 8 };
	Rectangle destination = (Rectangle){ .x = col * 8, .y = row * 8, .width = 8, .height = 8 };
	destination.x *= SCALE_FACTOR;
	destination.y *= SCALE_FACTOR;
	destination.width *= SCALE_FACTOR;
	destination.height *= SCALE_FACTOR;
	DrawTexturePro(*tile_block, source, destination, (Vector2){ 0, 0}, 0.0, WHITE);
}

int main(int argc, char *argv[]) {
	bool rom_loaded = false;

	if (argc == 2) {
		printf("Loading the rom path '%s'\n", argv[1]);
		mmu_load_rom(argv[1]);
		rom_loaded = true;
		cpu_reset();
	}
	else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	SetWindowState(FLAG_VSYNC_HINT);
	InitWindow(SCREENWIDTH, SCREENHEIGHT, "Hagemu GameBoy Emulator");

	Image background_image = (Image){
		.data = NULL,
		.width = 160,
		.height = 144,
		.mipmaps = 1,
		.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	};
	Texture2D background_texture = LoadTextureFromImage(background_image);
	UnloadImage(background_image);

	while (WindowShouldClose() != true) {
		if (IsFileDropped()) {
			FilePathList dropped_files = LoadDroppedFiles();
			printf("Loading the rom path '%s'\n", dropped_files.paths[0]);
			mmu_load_rom(dropped_files.paths[0]);
			UnloadDroppedFiles(dropped_files);
			rom_loaded = true;
			cpu_reset();
		}

		if (rom_loaded == false) {
			BeginDrawing();
			ClearBackground(GREEN1);
			DrawCenteredText("Please drop a .gb file onto this window", 7 * SCALE_FACTOR, GREEN3);
			EndDrawing();
			continue;
		}

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

		set_background_index_map();
		set_raw_background();
		set_sprites();
		set_background_layer();

		UpdateTexture(background_texture, &background_layer);

		BeginDrawing();
		ClearBackground(GREEN1);
		DrawTexturePro(background_texture,
			(Rectangle){ .x = 0, .y = 0, .width = 160, .height = 144},
			(Rectangle){ .x = 0, .y = 0, .width = 160 * SCALE_FACTOR, .height = 144 * SCALE_FACTOR},
			(Vector2){ 0, 0 },
			0.0,
			WHITE);
		DrawFPS(10, 10);
		EndDrawing();
		mmu_set_bit(VBLANK_INTERRUPT_FLAG_BIT);
	}

	UnloadTexture(background_texture);
	CloseWindow();

	return 0;
}
