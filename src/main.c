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
#define CLOCKS_PER_SCANLINE 456

typedef uint16_t R5G5B5A1;

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

R5G5B5A1 convert_color(Color color) {
	uint16_t result = 0;
	result |= ((color.r >> 3) << 11);
	result |= ((color.g >> 3) << 6);
	result |= ((color.b >> 3) << 1);
	result |= 1;
	return result;
}

R5G5B5A1 screen_buffer[144][160];
Texture2D screen_texture;

void DrawCenteredText(char* text, int font_size, Color color) {
	int text_width = MeasureText(text, font_size);
	DrawText(text,
		SCREENWIDTH / 2 - text_width / 2,
		SCREENHEIGHT / 2 - font_size / 2,
		font_size,
		color
	);
}

void draw_scanline() {
	if (mmu_read(LCD_Y_COORDINATE) >= 144)
		return;
	int background_row = mmu_read(LCD_Y_COORDINATE) + mmu_read(BG_SCROLL_Y);

	// Get tile indices
	uint8_t tile_indices[32];
	int tile_index_row = background_row / 8;
	int tile_pixel_row = background_row % 8;
	uint16_t tile_map_start;
	if (mmu_get_bit(BG_TILE_MAP_AREA))
		tile_map_start = 0x9C00;
	else
		tile_map_start = 0x9800;

	for (int col = 0; col < 32; col++)
		tile_indices[col] = mmu_read(tile_map_start + 32 * tile_index_row + col);

	// Convert indices to raw tile data
	uint8_t raw_tile_data[32 * 2];
	uint16_t data_block_1;
	uint16_t data_block_2 = 0x8800;
	if (mmu_get_bit(BG_TILE_DATA_AREA))
		data_block_1 = 0x8000;
	else
		data_block_1 = 0x9000;

	for (int col = 0; col < 32; col++) {
		uint16_t tile_start;
		if (tile_indices[col] < 128)
			tile_start = data_block_1 + 16 * tile_indices[col] + 2 * tile_pixel_row;
		else
			tile_start = data_block_2 + 16 * (tile_indices[col] - 128) + 2 * tile_pixel_row;
		raw_tile_data[2 * col] = mmu_read(tile_start);
		raw_tile_data[2 * col + 1] = mmu_read(tile_start + 1);
	}

	// Convert raw tile data into 2bpp format
	uint8_t tile_data[256];
	for (int i = 0; i < 64; i += 2) {
		uint8_t byte1 = raw_tile_data[i];
		uint8_t byte2 = raw_tile_data[i + 1];
		for (int col = 0; col < 8; col++) {
			bool bit1 = (byte1 & 0x80) >> 7;
			bool bit2 = (byte2 & 0x80) >> 7;
			tile_data[i * 4 + col] = (bit2 << 1) | bit1;
			byte1 <<= 1;
			byte2 <<= 1;
		}
	}

	// Convert 2bpp format to RGBA5551 format
	R5G5B5A1 colored_tile_data[256];
	for (int i = 0; i < 256; i++)
		switch (tile_data[i]) {

		case 0: colored_tile_data[i] = convert_color(GREEN1); break;
		case 1: colored_tile_data[i] = convert_color(GREEN2); break;
		case 2: colored_tile_data[i] = convert_color(GREEN3); break;
		case 3: colored_tile_data[i] = convert_color(GREEN4); break;
		}

	// Copy scanline to screen_buffer
	int x_offset = mmu_read(BG_SCROLL_X);
	int current_row = mmu_read(LCD_Y_COORDINATE);
	for (int i = 0; i < 160; i++)
		screen_buffer[current_row][i] = colored_tile_data[(x_offset + i) % 256];
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

	SetConfigFlags(FLAG_VSYNC_HINT);
	SetTargetFPS(60);
	InitWindow(SCREENWIDTH, SCREENHEIGHT, "Hagemu GameBoy Emulator");

	Image background_image = (Image){
		.data = NULL,
		.width = 160,
		.height = 144,
		.mipmaps = 1,
		.format = PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,
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

		for (int scanline = 0; scanline < 154; scanline++) {
			mmu_write(LCD_Y_COORDINATE, scanline);
			draw_scanline(scanline);
			int t_cycles = 0;
			while (t_cycles < CLOCKS_PER_SCANLINE)
				t_cycles += cpu_do_next_instruction();
		}

		UpdateTexture(background_texture, &screen_buffer);

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
