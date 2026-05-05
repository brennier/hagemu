#ifndef HAGEMU_MAIN_H
#define HAGEMU_MAIN_H

#define AUDIO_SAMPLE_RATE 48000
#ifdef __EMSCRIPTEN__
// Around 8 video frames worth of audio is buffered for the web build
#define AUDIO_TARGET_FRAMES (8 * (AUDIO_SAMPLE_RATE / 60))
#else
#define AUDIO_TARGET_FRAMES (3 * (AUDIO_SAMPLE_RATE / 60))
#endif

#include <SDL3/SDL.h>

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
	float audio_buffer[2 * AUDIO_TARGET_FRAMES];
	enum AppState state;
	char *rom_filename;
};

void hagemu_handle_drop_event(struct HagemuApp *app, const char *filename);
bool hagemu_app_load_rom_file(struct HagemuApp *app, const char *filename);
bool hagemu_app_load_sram(struct HagemuApp *app, const char *filename);

#endif // HAGEMU_MAIN_H
