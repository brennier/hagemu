#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "hagemu_core.h"
#include "text.h"
#include "file.h"
#include "web.h" // Does nothing unless compiled with emscripten

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

#define WINDOW_TITLE "Hagemu Gameboy Emulator"
#define SCALE_FACTOR 6
#define WINDOW_WIDTH 160 * SCALE_FACTOR
#define WINDOW_HEIGHT 144 * SCALE_FACTOR
#define APP_VERSION "0.1"
#define GB_CLOCK_FREQUENCY (1 << 22)

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76,  255 }
#define GREEN2 (Color){ 64,  133, 109, 255 }
#define GREEN3 (Color){ 48,  102, 87,  255 }
#define GREEN4 (Color){ 36,  76,  64,  255 }

bool hagemu_app_setup(struct HagemuApp *app) {
	app->gb = hagemu_create();
	app->state = HAGEMU_NO_ROM;
	memset(app->audio_buffer, 0, sizeof(app->audio_buffer));
	app->smooth_sample_rate_adjust = 1.0;
	app->smooth_delta_time  = 1.0 / 60.0;
	app->old_time = SDL_GetPerformanceCounter();

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
						SDL_PIXELFORMAT_XRGB8888,
						SDL_TEXTUREACCESS_STREAMING,
						160, 144);
	SDL_SetTextureScaleMode(app->screen_texture, SDL_SCALEMODE_NEAREST);

	if (!app->screen_texture) {
		fprintf(stderr, "Error creating screen texture: %s\n", SDL_GetError());
		return false;
	}

	SDL_AudioSpec audio_spec = {
		.format = SDL_AUDIO_F32,   // 16-bit signed int format
		.channels = 2,             // Stereo
		.freq = BASE_AUDIO_SAMPLE_RATE, // 48000 Hz
	};

	app->audio_stream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
		&audio_spec,
		NULL,
		NULL
		);
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

	if (hagemu_sram_available()) {
		size_t sram_size;
		const uint8_t *sram = hagemu_get_sram(&sram_size);
		char *sram_name = hagemu_file_sram_name(app->rom_filename);
		hagemu_file_save(sram_name, sram, sram_size);
		free(sram_name);
	}

	text_cleanup();
	free(app->rom_filename);
	SDL_DestroyAudioStream(app->audio_stream);
	SDL_DestroyTexture(app->screen_texture);
	SDL_DestroyRenderer(app->renderer);
	SDL_DestroyWindow(app->window);
	hagemu_destory(app->gb);
	SDL_Quit();
}

bool hagemu_app_load_sram(struct HagemuApp *app, const char* filename) {
	printf("Loading SRAM data from '%s'\n", filename);
	size_t sram_size;
	uint8_t *sram_data = hagemu_file_load(filename, &sram_size);
	bool result = hagemu_set_sram(sram_data, sram_size);
	if (result) {
		hagemu_reset(app->gb);
		app->state = HAGEMU_GAME_RUNNING;
		app->smooth_sample_rate_adjust = 1.0;
		app->smooth_delta_time  = 1.0 / 60.0;
		app->old_time = SDL_GetPerformanceCounter();
		SDL_ClearAudioStream(app->audio_stream);
		memset(app->audio_buffer, 0, sizeof(app->audio_buffer));
	}
	free(sram_data);
	return true;
}

bool hagemu_app_load_rom(struct HagemuApp *app, const char* filename) {
	printf("Loading the rom file '%s'\n", filename);
	size_t rom_size;
	uint8_t *rom_data = hagemu_file_load(filename, &rom_size);
	if (!rom_data) {
		fprintf(stderr, "[ERROR] Unable to load the file '%s'\n", filename);
		return false;
	}
	hagemu_set_rom(app->gb, rom_data, rom_size);
	free(rom_data);

	app->rom_filename = malloc(strlen(filename) + 1);
	strcpy(app->rom_filename, filename);

	app->state = HAGEMU_GAME_RUNNING;
	app->smooth_sample_rate_adjust = 1.0;
	app->smooth_delta_time  = 1.0 / 60.0;
	app->old_time = SDL_GetPerformanceCounter();
	SDL_ClearAudioStream(app->audio_stream);
	memset(app->audio_buffer, 0, sizeof(app->audio_buffer));

	if (!hagemu_sram_available())
		return true;

	// Load the SRAM
	char *sram_file_name = hagemu_file_sram_name(app->rom_filename);
	bool sram_file_exists = SDL_GetPathInfo(sram_file_name, NULL);
	if (sram_file_exists) {
		hagemu_app_load_sram(app, sram_file_name);
	} else {
		printf("Unable to locate an SRAM file '%s'.\n", sram_file_name);
		printf("Using a blank SRAM file instead...\n");
	}
	free(sram_file_name);
	return true;
}

void hagemu_handle_keypress(struct HagemuApp *app, SDL_Scancode scancode, bool is_pressed) {
	HagemuButton button;
	switch (scancode) {

	case SDL_SCANCODE_K: button = HAGEMU_BUTTON_A; break;
	case SDL_SCANCODE_J: button = HAGEMU_BUTTON_B; break;
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

	hagemu_set_button(app->gb, button, is_pressed);
}

void hagemu_handle_gamepad(struct HagemuApp *app, SDL_GamepadButton gpad_button, bool is_pressed) {
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

	hagemu_set_button(app->gb, button, is_pressed);
}

void hagemu_handle_drop_event(struct HagemuApp *app, const char *filename) {
	const char *ext = strrchr(filename, '.');
	if (strcmp(ext, ".gb") == 0 || strcmp(ext, ".gbc") == 0)
		hagemu_app_load_rom(app, filename);
	else if (strcmp(ext, ".sav") == 0 || strcmp(ext, ".sram") == 0)
		hagemu_app_load_sram(app, filename);
	else
		printf("Dropped file '%s' has an unknown extension. Ignoring...\n", filename);
}

void hagemu_handle_events(struct HagemuApp *app) {
	while (SDL_PollEvent(&app->event)) {
		switch (app->event.type) {

		case SDL_EVENT_QUIT:
			app->state = HAGEMU_QUIT;
			break;
		case SDL_EVENT_KEY_DOWN:
			hagemu_handle_keypress(app, app->event.key.scancode, true);
			break;
		case SDL_EVENT_KEY_UP:
			hagemu_handle_keypress(app, app->event.key.scancode, false);
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			if (app->gamepad) SDL_CloseGamepad(app->gamepad);
			app->gamepad = SDL_OpenGamepad(app->event.gdevice.which);
			printf("GamePad automatically connected\n");
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			SDL_CloseGamepad(app->gamepad);
			app->gamepad = NULL;
			printf("GamePad disconnected\n");
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			hagemu_handle_gamepad(app, app->event.gbutton.button, true);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			hagemu_handle_gamepad(app, app->event.gbutton.button, false);
			break;
		case SDL_EVENT_DROP_FILE:
		case SDL_EVENT_DROP_TEXT:
			hagemu_handle_drop_event(app, app->event.drop.data);
			break;
		default:
			break;
		}
	}
}

// Calculutes how much the audio should be resampled to meet the target number
// of frames queued in the SDL_AudioStream.
int calculate_sample_rate(struct HagemuApp *app) {
	int queued_bytes = SDL_GetAudioStreamQueued(app->audio_stream);
	/* if (queued_bytes == 0) */
		/* printf("[DEBUG] Audio buffer is empty\n"); */
	int queued_frames = queued_bytes / (2 * sizeof(float));
	float error = (AUDIO_TARGET_FRAMES - queued_frames) / (double)AUDIO_TARGET_FRAMES;
	error *= 0.05f;
	if (error < -0.005f) error = -0.005f;
	if (error >  0.005f) error =  0.005f;
	double new_sample_rate = 1.0 + error;
	app->smooth_sample_rate_adjust *= 0.95;
	app->smooth_sample_rate_adjust += 0.05 * new_sample_rate;
	return BASE_AUDIO_SAMPLE_RATE * app->smooth_sample_rate_adjust;
}

double get_smooth_delta_time(struct HagemuApp *app) {
	Uint64 now = SDL_GetPerformanceCounter();
	double delta_time = (double)(now - app->old_time) / SDL_GetPerformanceFrequency();
	app->old_time = now;
	// If dt is too high, the thread was probably frozen and reawakened.
	// Cap it at a reasonable rate and assume it'll return to normal.
	if (delta_time > 5.0 / 60.0) {
		/* printf("[DEBUG] High delta time: %.3f secs\n", delta_time); */
		delta_time = 5.0 / 60.0;
	}
	app->smooth_delta_time *= 0.95;
	app->smooth_delta_time += 0.05 * delta_time;
	return app->smooth_delta_time;
}

void main_loop(void* arg) {
	struct HagemuApp *app = (struct HagemuApp *)arg;
	hagemu_handle_events(app);

	double smooth_delta_time = get_smooth_delta_time(app);
	app->cycle_accumulator += smooth_delta_time * GB_CLOCK_FREQUENCY;
	while (app->cycle_accumulator > 0)
		app->cycle_accumulator -= hagemu_next_instruction(app->gb);

	// Even if there's not a new frame, updating the texture every loop
	// iteration makes the workload smoother and more consistent
	SDL_UpdateTexture(app->screen_texture, NULL, hagemu_get_framebuffer(), sizeof(uint32_t) * 160);
	SDL_RenderTexture(app->renderer, app->screen_texture, NULL, NULL);
	SDL_RenderPresent(app->renderer);

	int sample_rate = calculate_sample_rate(app);
	hagemu_set_audio_sample_rate(sample_rate);
	int frames_available = hagemu_audio_available();
	if (frames_available > AUDIO_TARGET_FRAMES)
		frames_available = AUDIO_TARGET_FRAMES;
	int frames = hagemu_audio_read(app->audio_buffer, frames_available);
	// Lower the volume (later this will be adjustable)
	for (int i = 0; i < 2 * frames; i++)
		app->audio_buffer[i] /= 4.0;
	SDL_PutAudioStreamData(app->audio_stream, app->audio_buffer, 2 * sizeof(float) * frames);
}

int main(int argc, char *argv[]) {
	// Does nothing unless using emscripten
	web_setup_filesystem();

	struct HagemuApp app = { 0 };
	hagemu_app_setup(&app);

	// Does nothing unless using emscripten
	web_save_pointer_for_javascript(&app);

	printf("Application started successfully\n");
	printf("Waiting for a rom file\n");

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
				   "Click the \"Upload ROM\" button below",
				   WINDOW_WIDTH / 2,
				   WINDOW_HEIGHT / 2 - 7 * SCALE_FACTOR,
				   7 * SCALE_FACTOR);
		text_draw_centered(app.renderer,
				   "or drop a rom file directly onto this window",
				   WINDOW_WIDTH / 2,
				   WINDOW_HEIGHT / 2 + 7 * SCALE_FACTOR,
				   7 * SCALE_FACTOR);
		text_draw(app.renderer,
			  "Compilation Date: " __DATE__,
			  SCALE_FACTOR,
			  WINDOW_HEIGHT - 5 * SCALE_FACTOR,
			  4 * SCALE_FACTOR);
		SDL_RenderPresent(app.renderer);
	}

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
	while (app.state != HAGEMU_QUIT) {
		main_loop(&app);
	}
#endif

	hagemu_app_cleanup(&app);
	return EXIT_SUCCESS;
}
