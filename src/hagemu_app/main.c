#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

#include "hagemu.h"
#include "web.h" // Does nothing unless PLATFORM_WEB is defined
#include "text.h"

#define WINDOW_TITLE "Hagemu Gameboy Emulator"
#define SCALE_FACTOR 6
#define WINDOW_WIDTH 160 * SCALE_FACTOR
#define WINDOW_HEIGHT 144 * SCALE_FACTOR
#define APP_VERSION "0.1"
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_TARGET_FRAMES (4 * (AUDIO_SAMPLE_RATE / 60))
#define GB_CLOCK_FREQUENCY (1 << 22)

// Green color palatte from lighest to darkest
#define GREEN1 (Color){ 138, 189, 76,  255 }
#define GREEN2 (Color){ 64,  133, 109, 255 }
#define GREEN3 (Color){ 48,  102, 87,  255 }
#define GREEN4 (Color){ 36,  76,  64,  255 }

enum AppState {
	HAGEMU_NO_ROM,
	HAGEMU_PAUSE_MENU,
	HAGEMU_GAME_RUNNING,
	HAGEMU_QUIT,
};

struct HagemuApp {
	struct HagemuGB *gb;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *screen_texture;
	SDL_AudioStream *audio_stream;
	SDL_Gamepad *gamepad;
	SDL_Event event;
	Uint64 old_time;
	double cycle_accumulator;
	double smooth_delta_time;
	double audio_sample_rate_adjust;
	enum AppState state;
	char *rom_filename;
};

bool hagemu_app_setup(struct HagemuApp *app) {
	app->gb = hagemu_create();
	app->state = HAGEMU_NO_ROM;
	app->old_time = 0;
	app->cycle_accumulator = 0;
	app->audio_sample_rate_adjust = 1.0;

	SDL_SetAppMetadata(WINDOW_TITLE, APP_VERSION, NULL);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "Error initializing SDL3: %s\n", SDL_GetError());
		return false;
	}

	// Set a good starting point for smooth_delta_time based on the monitor refresh rate
	const SDL_DisplayMode *display_mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
	if (display_mode != NULL)
		app->smooth_delta_time = 1 / display_mode->refresh_rate;
	else
		app->smooth_delta_time = 0;

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
						SDL_PIXELFORMAT_RGBA8888,
						SDL_TEXTUREACCESS_STREAMING,
						160, 144);
	SDL_SetTextureScaleMode(app->screen_texture, SDL_SCALEMODE_NEAREST);

	if (!app->screen_texture) {
		fprintf(stderr, "Error creating screen texture: %s\n", SDL_GetError());
		return false;
	}

	SDL_AudioSpec spec = {
		.format = SDL_AUDIO_F32,   // 16-bit signed int format
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

bool hagemu_app_save_sram(char *filename) {
	size_t sram_size;
	const uint8_t *sram = hagemu_get_sram(&sram_size);

	FILE *file = fopen(filename, "wb");
	if (file == NULL) {
		printf("Error: Failed to open the file '%s' to write the save data :(\n", filename);
		return false;
	}

	size_t bytes_written = fwrite(sram, 1, sram_size, file);

	if (bytes_written != sram_size) {
		printf("Error: Tried to write %ld bytes to '%s', but actually only wrote %ld bytes.\n", sram_size, filename, bytes_written);
		return false;
	}

	printf("Save data was sucessfully written to '%s' (%ld bytes)\n", filename, sram_size);
	fclose(file);
	return true;
}

#ifdef PLATFORM_WEB
char *hget_sram_name(const char* rom_name) {
	// Returns the string "/savedata/[basename].sav" where [basename] is the basename part of rom_name.
	// It is up to the caller to free the memory for the string.
	const char* basename_begin = strrchr(rom_name, '/');
	if (basename_begin == NULL)
		basename_begin = rom_name;
	else
		basename_begin++;

	char* basename_end = strrchr(rom_name, '.');
	size_t basename_length;
	if (basename_end != NULL)
		basename_length = basename_end - basename_begin;
	else
		basename_length = strlen(basename_begin);

	// Allocate memory for the full sram_path (remember to add 1 for '\0')
	char* sram_name = malloc(strlen("/savedata/") + basename_length + strlen(".sav") + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}

	strcpy(sram_name, "/savedata/");
	strncat(sram_name, basename_begin, basename_length);
	sram_name[strlen("/savedata/") + basename_length] = '\0'; // Manually null-terminate result
	strcat(sram_name, ".sav");

	return sram_name;
}
#else
char *hget_sram_name(const char* rom_name) {
	char* sram_name = malloc(strlen(rom_name) + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}
	strcpy(sram_name, rom_name);

	// Remove the extension if present
	char* last_dot = strrchr(sram_name, '.');
	if (last_dot != NULL)
		*last_dot = '\0';

	// Adjust the allocated size of sram_name to fit the ".sav" and the final NULL
	sram_name = realloc(sram_name, strlen(sram_name) + strlen(".sav") + 1);
	if (sram_name == NULL) {
		printf("Warning: Failed to allocate memory for the save data file name.\n");
		return NULL;
	}

	strcat(sram_name, ".sav");
	return sram_name;
}
#endif

void hagemu_app_cleanup(struct HagemuApp *app) {
	printf("Cleaning up!\n");

	if (hagemu_sram_available())
		hagemu_app_save_sram(hget_sram_name(app->rom_filename));

	text_cleanup();
	free(app->rom_filename);
	SDL_DestroyAudioStream(app->audio_stream);
	SDL_DestroyTexture(app->screen_texture);
	SDL_DestroyRenderer(app->renderer);
	SDL_DestroyWindow(app->window);
	hagemu_destory(app->gb);
	SDL_Quit();
}

uint8_t *hagemu_app_load_file(const char *filename, size_t *out_size) {
	FILE *file = fopen(filename, "rb");
	if (!file) {
		printf("Warning: Failed to open the file '%s'.\n", filename);
		return NULL;
	}

	// Seek to end to determine file size
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}

	long size = ftell(file);
	if (size < 0) {
		fclose(file);
		return NULL;
	}

	// Go back to beginning
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}

	// Prevent overflow when converting to size_t
	if ((unsigned long)size > SIZE_MAX - 1) {
		fclose(file);
		return NULL;
	}

	size_t usize = (size_t)size;

	// Allocate (+1 for optional null terminator)
	unsigned char *buffer = malloc(usize);
	if (!buffer) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(buffer, 1, usize, file);
	if (read != usize) {
		free(buffer);
		fclose(file);
		return NULL;
	}

	fclose(file);

	if (out_size) *out_size = usize;

	return buffer;
}

/* 	long bytes_read = fread(cartridge_ram, 1, sram_size, save_file); */
/* 	if (bytes_read != sram_size) */
/* 		printf("Error: Save file was expected to be %ld bytes, but was actually %ld bytes.\n", sram_size, bytes_read); */
/* 	else */
/* 		printf("The save file '%s' was sucessfully found and loaded (%ld bytes)\n", sram_file_name, sram_size); */
/* 	fclose(save_file); */
/* } */

bool hagemu_app_load_rom(struct HagemuApp *app, const char* filename) {
	printf("Loading the rom path '%s'\n", filename);
	app->rom_filename = malloc(strlen(filename) + 1);
	strcpy(app->rom_filename, filename);
	hagemu_load_rom(app->gb, filename);
	SDL_ClearAudioStream(app->audio_stream);
	app->state = HAGEMU_GAME_RUNNING;

	if (!hagemu_sram_available())
		return true;

	// Load the SRAM
	size_t size;
	char *sram_file_name = hget_sram_name(app->rom_filename);
	bool sram_file_exists = SDL_GetPathInfo(sram_file_name, NULL);
	if (sram_file_exists) {
		printf("Loading SRAM data from '%s'\n", sram_file_name);
		uint8_t *sram_data = hagemu_app_load_file(sram_file_name, &size);
		hagemu_set_sram(sram_data, size);
		free(sram_data);
	} else {
		printf("Unable to locate an SRAM file '%s'.\n", sram_file_name);
		printf("Using a blank SRAM file instead...\n");
	}
	free(sram_file_name);
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

// Calculutes how much the audio should be resampled to meet the target number
// of frames queued in the SDL_AudioStream.
void update_audio_sample_rate_adjust(struct HagemuApp *app) {
	int queued_bytes = SDL_GetAudioStreamAvailable(app->audio_stream);
	if (queued_bytes == 0 && SDL_GetTicks() > 5000)
		printf("AUDIO BUFFER EMPTY!\n");
	int queued_frames = queued_bytes / (2 * sizeof(float));
	float error = (AUDIO_TARGET_FRAMES - queued_frames) / (float)AUDIO_TARGET_FRAMES;
	float gain = 0.02f;
	error *= gain;
	if (error < -0.002f) error = -0.002f;
	if (error >  0.002f) error =  0.002f;
	error += 1;
	app->audio_sample_rate_adjust *= 0.99f;
	app->audio_sample_rate_adjust += 0.01f * error;
}

double get_delta_time(struct HagemuApp *app) {
	if (!app->old_time) {
		app->old_time = SDL_GetPerformanceCounter();
	}
	Uint64 now = SDL_GetPerformanceCounter();
	double delta_time = (double)(now - app->old_time) / SDL_GetPerformanceFrequency();
	app->old_time = now;
	return delta_time;
}

void main_loop(void* arg) {
	struct HagemuApp *app = (struct HagemuApp *)arg;
	hagemu_handle_events(app);

	double dt = get_delta_time(app);
	app->smooth_delta_time = 0.99 * app->smooth_delta_time + 0.01 * dt;
	app->cycle_accumulator += app->smooth_delta_time * GB_CLOCK_FREQUENCY;

	unsigned frame_count = hagemu_get_frame_count();
	/* printf("Cycles: %f\n", dt); */
	while (app->cycle_accumulator > 0) {
		app->cycle_accumulator -= hagemu_next_instruction(app->gb);
	}

	if (frame_count != hagemu_get_frame_count()) {
		frame_count = hagemu_get_frame_count();
		SDL_UpdateTexture(app->screen_texture, NULL, hagemu_get_framebuffer(), sizeof(uint32_t) * 160);
	}
	SDL_RenderTexture(app->renderer, app->screen_texture, NULL, NULL);
	SDL_RenderPresent(app->renderer);

	update_audio_sample_rate_adjust(app);
	hagemu_set_audio_sample_rate(AUDIO_SAMPLE_RATE * app->audio_sample_rate_adjust);
	/* printf("Rate Adjust: %f\n", app->audio_sample_rate_adjust); */

	float audio_buffer[2 * AUDIO_TARGET_FRAMES];
	int frames_available = hagemu_audio_available();
	if (frames_available > 2 * AUDIO_TARGET_FRAMES)
		frames_available = 2 * AUDIO_TARGET_FRAMES;
	int frames = hagemu_audio_read(audio_buffer, frames_available);
	SDL_PutAudioStreamData(app->audio_stream, audio_buffer, 2 * sizeof(float) * frames);
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
