#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "ppu.h"
#include "mmu.h"
#include "cpu.h"

#define SCALE_FACTOR 5
#define SCREENWIDTH 160 * SCALE_FACTOR
#define SCREENHEIGHT 144 * SCALE_FACTOR

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

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

		int current_cycle = 0;
		while (!ppu_frame_finished(current_cycle)) {
			current_cycle += cpu_do_next_instruction();
			ppu_update(current_cycle);
		}

		UpdateTexture(background_texture, ppu_get_frame());

		BeginDrawing();
		DrawTexturePro(background_texture,
			(Rectangle){ .x = 0, .y = 0, .width = 160, .height = 144},
			(Rectangle){ .x = 0, .y = 0, .width = 160 * SCALE_FACTOR, .height = 144 * SCALE_FACTOR},
			(Vector2){ 0, 0 },
			0.0,
			WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}

	UnloadTexture(background_texture);
	CloseWindow();

	return 0;
}
