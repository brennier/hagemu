#ifndef HAGEMU_MAIN_H
#define HAGEMU_MAIN_H

#define BASE_AUDIO_SAMPLE_RATE 48000
#define AUDIO_TARGET_FRAMES 4096

#include <SDL3/SDL.h>
#include "hagemu_core.h"

enum AppState {
	HAGEMU_NO_ROM,
	HAGEMU_PAUSE_MENU,
	HAGEMU_GAME_RUNNING,
	HAGEMU_QUIT,
};

struct HagemuApp {
	struct HagemuGB *gb;
	enum GBModel gb_model;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *screen_texture;
	SDL_AudioStream *audio_stream;
	SDL_Gamepad *gamepad;
	SDL_Event event;
	float audio_buffer[2 * AUDIO_TARGET_FRAMES];
	Uint64 old_time;
	double smooth_delta_time;
	double smooth_sample_rate_adjust;
	enum AppState state;
	char *rom_filename;
};

bool hagemu_app_load_rom(struct HagemuApp *app, const char *filename, enum GBModel model);
bool hagemu_app_load_sram(struct HagemuApp *app, const char *filename);
void hagemu_app_reset(struct HagemuApp *app, enum GBModel model);
void hagemu_save_sram_file(struct HagemuApp *app);
char *hagemu_file_sram_name(const char *rom_name);
void hagemu_quit_rom(struct HagemuApp *app);

#endif // HAGEMU_MAIN_H
