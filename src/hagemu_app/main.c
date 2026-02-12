#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

#include "hagemu.h"
#include "web.h" // Does nothing unless PLATFORM_WEB is defined
#include "text.h"

#define WINDOW_TITLE "Hagemu Gameboy Emulator"
#define SCALE_FACTOR 5
#define WINDOW_WIDTH 160 * SCALE_FACTOR
#define WINDOW_HEIGHT 144 * SCALE_FACTOR
#define APP_VERSION "0.1"
#define AUDIO_SAMPLE_RATE 48000
// 5 video frames worth of audio should be queued at all times
#define AUDIO_TARGET_FRAMES (5 * (AUDIO_SAMPLE_RATE / 60))

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
	SDL_AudioStream *audio_stream;
	SDL_Gamepad *gamepad;
	SDL_Event event;
	enum AppState state;
};

void hagemu_app_push_audio(struct HagemuApp *app)
{
	int queued_bytes  = SDL_GetAudioStreamQueued(app->audio_stream);
	int queued_frames = queued_bytes / 4;
	int frames_needed = AUDIO_TARGET_FRAMES - queued_frames;
	uint16_t temp_buffer[2 * AUDIO_TARGET_FRAMES];

	hagemu_audio_callback(temp_buffer, frames_needed);
	SDL_PutAudioStreamData(app->audio_stream, temp_buffer, 4 * frames_needed);
}

bool hagemu_app_setup(struct HagemuApp *app) {
	app->state = HAGEMU_NO_ROM;

	SDL_SetAppMetadata(WINDOW_TITLE, APP_VERSION, NULL);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
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

	app->screen_texture = SDL_CreateTexture(app->renderer,
						SDL_PIXELFORMAT_RGBA5551,
						SDL_TEXTUREACCESS_STREAMING,
						160, 144);
	SDL_SetTextureScaleMode(app->screen_texture, SDL_SCALEMODE_NEAREST);
	if (!app->screen_texture) {
		fprintf(stderr, "Error creating screen texture: %s\n", SDL_GetError());
		return false;
	}

	SDL_AudioSpec spec = {
		.format = SDL_AUDIO_S16,   // 16-bit signed int format
		.channels = 2,             // Stereo
		.freq = AUDIO_SAMPLE_RATE, // 48000 Hz
	};

	app->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (!app->audio_stream) {
		fprintf(stderr, "Error creating screen texture: %s\n", SDL_GetError());
		return false;
	}
	SDL_ResumeAudioStreamDevice(app->audio_stream);

	if (!text_init(app->renderer)) {
		fprintf(stderr, "Error initializing font: %s\n", SDL_GetError());
		return false;
	}

	return true;
}

void hagemu_app_cleanup(struct HagemuApp *app) {
	printf("Cleaning up!\n");

	text_cleanup();
	SDL_DestroyAudioStream(app->audio_stream);
	SDL_DestroyTexture(app->screen_texture);
	SDL_DestroyRenderer(app->renderer);
	SDL_DestroyWindow(app->window);
	SDL_Quit();

	hagemu_save_sram_file();
}

bool hagemu_app_load_rom(struct HagemuApp *app, const char* filename) {
	printf("Loading the rom path '%s'\n", filename);
	hagemu_load_rom(filename);
	app->state = HAGEMU_GAME_RUNNING;
	return true;
}

void hagemu_handle_keypress(SDL_Scancode scancode, bool is_pressed) {
	HagemuButton button;
	switch (scancode) {

	case SDL_SCANCODE_L: button = HAGEMU_BUTTON_A; break;
	case SDL_SCANCODE_K: button = HAGEMU_BUTTON_B; break;
	case SDL_SCANCODE_X: button = HAGEMU_BUTTON_START;  break;
	case SDL_SCANCODE_Z: button = HAGEMU_BUTTON_SELECT; break;

	case SDL_SCANCODE_W: button = HAGEMU_BUTTON_UP;    break;
	case SDL_SCANCODE_A: button = HAGEMU_BUTTON_LEFT;  break;
	case SDL_SCANCODE_S: button = HAGEMU_BUTTON_DOWN;  break;
	case SDL_SCANCODE_D: button = HAGEMU_BUTTON_RIGHT; break;

	case SDL_SCANCODE_UP:    button = HAGEMU_BUTTON_UP;    break;
	case SDL_SCANCODE_LEFT:  button = HAGEMU_BUTTON_LEFT;  break;
	case SDL_SCANCODE_RIGHT: button = HAGEMU_BUTTON_RIGHT; break;
	case SDL_SCANCODE_DOWN:  button = HAGEMU_BUTTON_DOWN;  break;

	default: return;
	}

	hagemu_set_button(button, is_pressed);
}

void hagemu_handle_gamepad(SDL_GamepadButton gpad_button, bool is_pressed) {
	HagemuButton button;
	switch (gpad_button) {

	case SDL_GAMEPAD_BUTTON_EAST:  button = HAGEMU_BUTTON_A; break;
	case SDL_GAMEPAD_BUTTON_SOUTH: button = HAGEMU_BUTTON_B; break;
	case SDL_GAMEPAD_BUTTON_START: button = HAGEMU_BUTTON_START;  break;
	case SDL_GAMEPAD_BUTTON_BACK:  button = HAGEMU_BUTTON_SELECT; break;

	case SDL_GAMEPAD_BUTTON_DPAD_UP:    button = HAGEMU_BUTTON_UP;    break;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  button = HAGEMU_BUTTON_LEFT;  break;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: button = HAGEMU_BUTTON_RIGHT; break;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  button = HAGEMU_BUTTON_DOWN;  break;

	default: return;
	}

	hagemu_set_button(button, is_pressed);
}

void hagemu_handle_events(struct HagemuApp *app) {
	while (SDL_PollEvent(&app->event)) {
		switch (app->event.type) {

		case SDL_EVENT_QUIT:
			app->state = HAGEMU_QUIT;
			break;
		case SDL_EVENT_KEY_DOWN:
			hagemu_handle_keypress(app->event.key.scancode, true);
			break;
		case SDL_EVENT_KEY_UP:
			hagemu_handle_keypress(app->event.key.scancode, false);
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			if (app->gamepad) SDL_CloseGamepad(app->gamepad);
			app->gamepad = SDL_OpenGamepad(app->event.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			SDL_CloseGamepad(app->gamepad);
			app->gamepad = NULL;
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			hagemu_handle_gamepad(app->event.gbutton.button, true);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			hagemu_handle_gamepad(app->event.gbutton.button, false);
			break;
		case SDL_EVENT_DROP_FILE:
		case SDL_EVENT_DROP_TEXT:
			hagemu_app_load_rom(app, app->event.drop.data);
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
		hagemu_app_load_rom(&app, argv[1]);
	} else if (argc > 2) {
		fprintf(stderr, "Error: Too many arguments\n");
		exit(EXIT_FAILURE);
	}

	while (app.state == HAGEMU_NO_ROM) {
		hagemu_handle_events(&app);

		SDL_SetRenderDrawColor(app.renderer, 138, 189, 76, 255);
		SDL_RenderClear(app.renderer);
		SDL_SetRenderDrawColor(app.renderer, 48, 102, 87, 255);
		text_draw_centered(app.renderer,
				   "Please drop a .gb file onto this window",
				   WINDOW_WIDTH / 2,
				   WINDOW_HEIGHT / 2,
				   7 * SCALE_FACTOR);
		text_draw(app.renderer,
			  "Compilation Date: " __DATE__,
			  SCALE_FACTOR,
			  WINDOW_HEIGHT - 5 * SCALE_FACTOR,
			  4 * SCALE_FACTOR);
		SDL_RenderPresent(app.renderer);
	}

	while (app.state != HAGEMU_QUIT) {
		hagemu_handle_events(&app);

		hagemu_run_frame();
		hagemu_app_push_audio(&app);

		bool status = true;

		status &= SDL_UpdateTexture(app.screen_texture, NULL, hagemu_get_framebuffer(), 2 * 160);
		status &= SDL_RenderTexture(app.renderer, app.screen_texture, NULL, NULL);
		status &= SDL_RenderPresent(app.renderer);

		if (!status)
			fprintf(stderr, "Error updating the framebuffer: %s\n", SDL_GetError());
	}

	hagemu_app_cleanup(&app);
	return EXIT_SUCCESS;
}
