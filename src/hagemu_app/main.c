#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

#include "hagemu.h"
#include "web.h" // Does nothing unless PLATFORM_WEB is defined

#define WINDOW_TITLE "Hagemu Gameboy Emulator"
#define SCALE_FACTOR 5
#define WINDOW_WIDTH 160 * SCALE_FACTOR
#define WINDOW_HEIGHT 144 * SCALE_FACTOR
#define MAX_BYTES_PER_AUDIO_CALLBACK 2048
#define AUDIO_SAMPLE_RATE 48000
#define APP_VERSION "0.1"

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76, 255 }
#define GREEN2 (Color){ 64, 133, 109, 255 }
#define GREEN3 (Color){ 48, 102, 87, 255 }
#define GREEN4 (Color){ 36, 76, 64, 255 }

enum AppState {
	HAGEMU_NO_ROM,
	HAGEMU_PAUSE_MENU,
	HAGEMU_GAME_RUNNING,
	HAGEMU_QUIT,
};

struct HagemuApp {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *screen_texture;
	SDL_Event event;

	char* rom_filename;
	enum AppState state;
	/* AudioStream audio_stream; */
};

bool hagemu_app_setup(struct HagemuApp *app) {
	app->state = HAGEMU_NO_ROM;

	SDL_SetAppMetadata(WINDOW_TITLE, APP_VERSION, NULL);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		fprintf(stderr, "Error initializing SDL3: %s\n", SDL_GetError());
		return false;
	}

	app->window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	if (!app->window) {
		fprintf(stderr, "Error creating Window: %s\n", SDL_GetError());
		return false;
	}

	app->renderer = SDL_CreateRenderer(app->window, NULL);
	if (!app->renderer) {
		fprintf(stderr, "Error creating Renderer: %s\n", SDL_GetError());
		return false;
	}

	if (!SDL_SetRenderVSync(app->renderer, 1)) {
		fprintf(stderr, "Error failed to set vsync: %s\n", SDL_GetError());
		return false;
	}

	/* // setup audio */
	/* InitAudioDevice(); */
	/* SetAudioStreamBufferSizeDefault(MAX_BYTES_PER_AUDIO_CALLBACK); */
	/* app->audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 2); */
	/* SetAudioStreamCallback(app->audio_stream, hagemu_audio_callback); */
	/* PlayAudioStream(app->audio_stream); */

	app->screen_texture = SDL_CreateTexture(app->renderer,
						SDL_PIXELFORMAT_RGBA5551,
						SDL_TEXTUREACCESS_STREAMING,
						160, 144);
	SDL_SetTextureScaleMode(app->screen_texture, SDL_SCALEMODE_NEAREST);
	if (!app->screen_texture) {
		fprintf(stderr, "Error creating screen texture: %s\n", SDL_GetError());
		return false;
	}

	return true;
}

void hagemu_app_cleanup(struct HagemuApp *app) {
	printf("Cleaning up!\n");

	/* CloseAudioDevice(); */
	/* UnloadAudioStream(app->audio_stream); */

	SDL_DestroyTexture(app->screen_texture);
	SDL_DestroyRenderer(app->renderer);
	SDL_DestroyWindow(app->window);
	SDL_Quit();

	hagemu_save_sram_file();
}

/* void DrawTextCentered(char* text, int x, int y, int font_size, Color color) { */
/* 	int text_width = MeasureText(text, font_size); */
/* 	DrawText(text, */
/* 		 x - text_width / 2, */
/* 		 y - font_size / 2, */
/* 		 font_size, */
/* 		 color */
/* 		); */
/* } */

/* bool hagemu_app_load_rom(struct HagemuApp *app, char* filename) { */
/* 	printf("Loading the rom path '%s'\n", filename); */
/* 	if (!FileExists(filename)) { */
/* 		fprintf(stderr, "Error: The file '%s' doesn't exist\n", filename); */
/* 		return false; */
/* 	} */
/* 	app->rom_filename = filename; */
/* 	app->state = HAGEMU_GAME_RUNNING; */
/* 	hagemu_reset(); */
/* 	hagemu_load_rom(filename); */
/* 	return true; */
/* } */

/* bool hagemu_app_load_dropped_file(struct HagemuApp *app) { */
/* 	if (IsFileDropped()) { */
/* 		FilePathList dropped_files = LoadDroppedFiles(); */
/* 		bool result = hagemu_app_load_rom(app, dropped_files.paths[0]); */
/* 		UnloadDroppedFiles(dropped_files); */
/* 		return result; */
/* 	} */
/* 	return false; */
/* } */

void hagemu_handle_events(struct HagemuApp *app) {
	while (SDL_PollEvent(&app->event)) {
		switch (app->event.type) {

		case SDL_EVENT_QUIT:
			app->state = HAGEMU_QUIT;
			break;
		default:
			break;
		}
	}
}


int main(int argc, char *argv[]) {
	web_setup_filesystem(); // Does nothing unless PLATFORM_WEB is defined

	struct HagemuApp app = { 0 };
	hagemu_app_setup(&app);

	if (argc == 2) {
		app.state = HAGEMU_GAME_RUNNING;
		hagemu_load_rom(argv[1]);
	} else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	while (app.state != HAGEMU_QUIT) {
		hagemu_handle_events(&app);
		hagemu_run_frame();

		SDL_UpdateTexture(app.screen_texture, NULL, hagemu_get_framebuffer(), 2 * 160);
		SDL_RenderTexture(app.renderer, app.screen_texture, NULL, NULL);
		SDL_RenderPresent(app.renderer);
	}

	/* while (WindowShouldClose() != true && app.state == HAGEMU_NO_ROM) { */
	/* 	BeginDrawing(); */
	/* 	ClearBackground(GREEN1); */
	/* 	DrawText("Compilation Date: " __DATE__, */
	/* 		 SCALE_FACTOR, */
	/* 		 SCREEN_HEIGHT - 5 * SCALE_FACTOR, */
	/* 		 4 * SCALE_FACTOR, */
	/* 		 GREEN3); */
	/* 	DrawTextCentered( */
	/* 		"Please drop a .gb file onto this window", */
	/* 		SCREEN_WIDTH / 2, */
	/* 		SCREEN_HEIGHT / 2, */
	/* 		7 * SCALE_FACTOR, */
	/* 		GREEN3 */
	/* 		); */
	/* 	DrawFPS(10, 10); */
	/* 	EndDrawing(); */

	/* 	hagemu_app_load_dropped_file(&app); */
	/* } */

	/* while (WindowShouldClose() != true) { */
	/* 	hagemu_app_load_dropped_file(&app); */

		/* bool button_state[HAGEMU_BUTTON_COUNT]; */

		/* button_state[HAGEMU_BUTTON_RIGHT] = IsKeyDown(KEY_RIGHT); */
		/* button_state[HAGEMU_BUTTON_LEFT]  = IsKeyDown(KEY_LEFT); */
		/* button_state[HAGEMU_BUTTON_UP]    = IsKeyDown(KEY_UP); */
		/* button_state[HAGEMU_BUTTON_DOWN]  = IsKeyDown(KEY_DOWN); */

		/* button_state[HAGEMU_BUTTON_RIGHT] |= IsKeyDown(KEY_D); */
		/* button_state[HAGEMU_BUTTON_LEFT]  |= IsKeyDown(KEY_A); */
		/* button_state[HAGEMU_BUTTON_UP]    |= IsKeyDown(KEY_W); */
		/* button_state[HAGEMU_BUTTON_DOWN]  |= IsKeyDown(KEY_S); */

		/* button_state[HAGEMU_BUTTON_A]      = IsKeyDown(KEY_L); */
		/* button_state[HAGEMU_BUTTON_B]      = IsKeyDown(KEY_K); */
		/* button_state[HAGEMU_BUTTON_START]  = IsKeyDown(KEY_X); */
		/* button_state[HAGEMU_BUTTON_SELECT] = IsKeyDown(KEY_Z); */

		/* if (IsGamepadAvailable(0)) { */
		/* 	button_state[HAGEMU_BUTTON_RIGHT] |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT); */
		/* 	button_state[HAGEMU_BUTTON_LEFT]  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT); */
		/* 	button_state[HAGEMU_BUTTON_UP]    |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP); */
		/* 	button_state[HAGEMU_BUTTON_DOWN]  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN); */

		/* 	button_state[HAGEMU_BUTTON_A]      |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT); */
		/* 	button_state[HAGEMU_BUTTON_B]      |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN); */
		/* 	button_state[HAGEMU_BUTTON_START]  |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT); */
		/* 	button_state[HAGEMU_BUTTON_SELECT] |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT); */
		/* } */

		/* for (HagemuButton b = 0; b < HAGEMU_BUTTON_COUNT; b++) */
		/* 	hagemu_set_button(b, button_state[b]); */

	/* 	BeginDrawing(); */
	/* 	DrawTextureEx(app.screen_texture, (Vector2){ 0, 0 }, 0, SCALE_FACTOR, WHITE); */
	/* 	DrawFPS(10, 10); */
	/* 	EndDrawing(); */
	/* } */

	hagemu_app_cleanup(&app);
	return 0;
}
