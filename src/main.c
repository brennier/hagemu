#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "ppu.h"
#include "mmu.h"
#include "cpu.h"
#include "web.h" // Does nothing unless PLATFORM_WEB is defined
#include "apu.h"

#define SCALE_FACTOR 5
#define SCREEN_WIDTH 160 * SCALE_FACTOR
#define SCREEN_HEIGHT 144 * SCALE_FACTOR
#define MAX_BYTES_PER_AUDIO_CALLBACK 2048
#define AUDIO_SAMPLE_RATE (2 * 1024 * 1024)

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

Texture2D screen_texture;

void DrawTextCentered(char* text, int x, int y, int font_size, Color color) {
	int text_width = MeasureText(text, font_size);
	DrawText(text,
		 x - text_width / 2,
		 y - font_size / 2,
		 font_size,
		 color
		);
}

int main(int argc, char *argv[]) {
	web_setup_filesystem(); // Does nothing unless PLATFORM_WEB is defined
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

	SetTraceLogLevel(LOG_WARNING);
	SetTargetFPS(60);
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hagemu GameBoy Emulator");
	SetExitKey(KEY_NULL);

	InitAudioDevice();
	SetAudioStreamBufferSizeDefault(MAX_BYTES_PER_AUDIO_CALLBACK);
	AudioStream audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 2);
	SetAudioStreamCallback(audio_stream, apu_generate_frames);
	PlayAudioStream(audio_stream);

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
			DrawTextCentered(
				"Please drop a .gb file onto this window",
				SCREEN_WIDTH / 2,
				SCREEN_HEIGHT / 2,
				7 * SCALE_FACTOR,
				GREEN3
				);
			DrawFPS(10, 10);
			EndDrawing();
			continue;
		}

		mmu_joypad_inputs[JOYPAD_RIGHT]  = IsKeyDown(KEY_RIGHT) | IsKeyDown(KEY_D);
		mmu_joypad_inputs[JOYPAD_LEFT]   = IsKeyDown(KEY_LEFT)  | IsKeyDown(KEY_A);
		mmu_joypad_inputs[JOYPAD_UP]     = IsKeyDown(KEY_UP)    | IsKeyDown(KEY_W);
		mmu_joypad_inputs[JOYPAD_DOWN]   = IsKeyDown(KEY_DOWN)  | IsKeyDown(KEY_S);
		mmu_joypad_inputs[JOYPAD_A]      = IsKeyDown(KEY_L)     | IsKeyDown(KEY_SPACE);
		mmu_joypad_inputs[JOYPAD_B]      = IsKeyDown(KEY_K);
		mmu_joypad_inputs[JOYPAD_START]  = IsKeyDown(KEY_X)     | IsKeyDown(KEY_ESCAPE);
		mmu_joypad_inputs[JOYPAD_SELECT] = IsKeyDown(KEY_Z);

		if (IsGamepadAvailable(0)) {
			mmu_joypad_inputs[JOYPAD_RIGHT]  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
			mmu_joypad_inputs[JOYPAD_LEFT]   |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
			mmu_joypad_inputs[JOYPAD_UP]     |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
			mmu_joypad_inputs[JOYPAD_DOWN]   |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
			mmu_joypad_inputs[JOYPAD_A]      |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
			mmu_joypad_inputs[JOYPAD_B]      |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
			mmu_joypad_inputs[JOYPAD_START]  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT);
			mmu_joypad_inputs[JOYPAD_SELECT] |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT);
		}

		for (int i = 0; i < 8; i++)
			if (mmu_joypad_inputs[i])
				mmu_set_bit(JOYPAD_INTERRUPT_FLAG_BIT);

		int current_cycle = 0;
		while (!ppu_frame_finished(current_cycle)) {
			current_cycle += cpu_do_next_instruction();
			ppu_update(current_cycle);
		}

		UpdateTexture(background_texture, ppu_get_frame());

		BeginDrawing();
		DrawTextureEx(background_texture, (Vector2){ 0, 0 }, 0, SCALE_FACTOR, WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}

	mmu_save_sram_file();
	UnloadTexture(background_texture);
	UnloadAudioStream(audio_stream);
	CloseAudioDevice();
	CloseWindow();

	return 0;
}
