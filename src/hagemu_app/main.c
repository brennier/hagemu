#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "raylib.h"
#include "hagemu.h"
#include "web.h" // Does nothing unless PLATFORM_WEB is defined

#define SCALE_FACTOR 5
#define SCREEN_WIDTH 160 * SCALE_FACTOR
#define SCREEN_HEIGHT 144 * SCALE_FACTOR
#define MAX_BYTES_PER_AUDIO_CALLBACK 2048
#define AUDIO_SAMPLE_RATE 48000

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

enum AppState {
	HAGEMU_NO_ROM,
	HAGEMU_PAUSE_MENU,
	HAGEMU_GAME_RUNNING,
};

struct HagemuApp {
	enum AppState state;
	char* rom_filename;
	Texture2D screen_texture;
	AudioStream audio_stream;
};

bool hagemu_app_setup(struct HagemuApp *app) {
	SetTraceLogLevel(LOG_WARNING);
	SetTargetFPS(60);
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hagemu GameBoy Emulator");
	SetExitKey(KEY_NULL);

	app->state = HAGEMU_NO_ROM;

	// setup audio
	InitAudioDevice();
	SetAudioStreamBufferSizeDefault(MAX_BYTES_PER_AUDIO_CALLBACK);
	app->audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 2);
	SetAudioStreamCallback(app->audio_stream, hagemu_audio_callback);
	PlayAudioStream(app->audio_stream);

	// setup screen texture
	Image background_image = (Image){
		.data = NULL,
		.width = 160,
		.height = 144,
		.mipmaps = 1,
		.format = PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,
	};
	app->screen_texture = LoadTextureFromImage(background_image);
	UnloadImage(background_image);
	return true;
}

void hagemu_app_cleanup(struct HagemuApp *app) {
	hagemu_save_sram_file();
	CloseAudioDevice();
	UnloadTexture(app->screen_texture);
	UnloadAudioStream(app->audio_stream);
	CloseWindow();
}

void DrawTextCentered(char* text, int x, int y, int font_size, Color color) {
	int text_width = MeasureText(text, font_size);
	DrawText(text,
		 x - text_width / 2,
		 y - font_size / 2,
		 font_size,
		 color
		);
}

bool hagemu_app_load_rom(struct HagemuApp *app, char* filename) {
	printf("Loading the rom path '%s'\n", filename);
	if (!FileExists(filename)) {
		fprintf(stderr, "Error: The file '%s' doesn't exist\n", filename);
		return false;
	}
	app->rom_filename = filename;
	app->state = HAGEMU_GAME_RUNNING;
	hagemu_reset();
	hagemu_load_rom(filename);
	return true;
}

bool hagemu_app_load_dropped_file(struct HagemuApp *app) {
	if (IsFileDropped()) {
		FilePathList dropped_files = LoadDroppedFiles();
		bool result = hagemu_app_load_rom(app, dropped_files.paths[0]);
		UnloadDroppedFiles(dropped_files);
		return result;
	}
	return false;
}

void hagemu_app_run(struct HagemuApp *app) {
}

int main(int argc, char *argv[]) {
	web_setup_filesystem(); // Does nothing unless PLATFORM_WEB is defined

	struct HagemuApp app = { 0 };
	hagemu_app_setup(&app);

	if (argc == 2) {
		hagemu_app_load_rom(&app, argv[1]);
	} else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	while (WindowShouldClose() != true && app.state == HAGEMU_NO_ROM) {
		BeginDrawing();
		ClearBackground(GREEN1);
		DrawText("Compilation Date: " __DATE__,
			 SCALE_FACTOR,
			 SCREEN_HEIGHT - 5 * SCALE_FACTOR,
			 4 * SCALE_FACTOR,
			 GREEN3);
		DrawTextCentered(
			"Please drop a .gb file onto this window",
			SCREEN_WIDTH / 2,
			SCREEN_HEIGHT / 2,
			7 * SCALE_FACTOR,
			GREEN3
			);
		DrawFPS(10, 10);
		EndDrawing();

		hagemu_app_load_dropped_file(&app);
	}

	while (WindowShouldClose() != true) {
		hagemu_app_load_dropped_file(&app);

		if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
			hagemu_press_button(HAGEMU_BUTTON_RIGHT);

		if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
			hagemu_press_button(HAGEMU_BUTTON_LEFT);

		if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
			hagemu_press_button(HAGEMU_BUTTON_UP);

		if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
			hagemu_press_button(HAGEMU_BUTTON_DOWN);

		if (IsKeyDown(KEY_L)) hagemu_press_button(HAGEMU_BUTTON_A);
		if (IsKeyDown(KEY_K)) hagemu_press_button(HAGEMU_BUTTON_B);
		if (IsKeyDown(KEY_X)) hagemu_press_button(HAGEMU_BUTTON_START);
		if (IsKeyDown(KEY_Z)) hagemu_press_button(HAGEMU_BUTTON_SELECT);


		if (IsGamepadAvailable(0)) {
			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
				hagemu_press_button(HAGEMU_BUTTON_RIGHT);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
				hagemu_press_button(HAGEMU_BUTTON_LEFT);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
				hagemu_press_button(HAGEMU_BUTTON_UP);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
				hagemu_press_button(HAGEMU_BUTTON_DOWN);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
				hagemu_press_button(HAGEMU_BUTTON_A);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
				hagemu_press_button(HAGEMU_BUTTON_B);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
				hagemu_press_button(HAGEMU_BUTTON_START);

			if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
				hagemu_press_button(HAGEMU_BUTTON_SELECT);
		}

		hagemu_run_frame();

		UpdateTexture(app.screen_texture, hagemu_get_framebuffer());

		BeginDrawing();
		DrawTextureEx(app.screen_texture, (Vector2){ 0, 0 }, 0, SCALE_FACTOR, WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}

	hagemu_app_cleanup(&app);
	return 0;
}
