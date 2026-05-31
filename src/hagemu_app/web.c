#include "web.h"
#include <stdio.h>
#include "file.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

struct HagemuApp *hagemu_app = NULL;
const char *sram_filename = NULL;

void web_save_pointer_for_javascript(struct HagemuApp *app) {
	hagemu_app = app;
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* web_get_sram_pointer(void) {
	if (hagemu_app && hagemu_app->rom_filename && hagemu_sram_available()) {
		size_t out_size;
		return hagemu_get_sram(&out_size);
	}
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
void web_save_sram_file(void) {
	if (hagemu_app && hagemu_app->rom_filename)
		hagemu_save_sram_file(hagemu_app);
}

EMSCRIPTEN_KEEPALIVE
size_t web_get_sram_size(void) {
	if (hagemu_app && hagemu_app->rom_filename && hagemu_sram_available()) {
		size_t out_size;
		hagemu_get_sram(&out_size);
		return out_size;
	}
	return 0;
}

EMSCRIPTEN_KEEPALIVE
const char *web_get_sram_file_name(void) {
	if (hagemu_app && hagemu_app->rom_filename && hagemu_sram_available()) {
		sram_filename = hagemu_file_sram_name(hagemu_app->rom_filename);
		const char *basename = strrchr(sram_filename, '/');
		if (basename)
			basename++;
		else
			basename = sram_filename;
		return basename;
	}
	return "";
}

EMSCRIPTEN_KEEPALIVE
bool web_load_rom(const char *filename, bool is_cgb_mode) {
	enum GBModel model = is_cgb_mode ? MODEL_CGB : MODEL_DMG;
	return hagemu_app_load_rom(hagemu_app, filename, model);
}

EMSCRIPTEN_KEEPALIVE
bool web_load_sram(const char *filename) {
	return hagemu_app_load_sram(hagemu_app, filename);
}

EMSCRIPTEN_KEEPALIVE
void web_quit_rom(void) {
	hagemu_quit_rom(hagemu_app);
}

EMSCRIPTEN_KEEPALIVE
bool web_is_rom_running(void) {
	return (hagemu_app->state == HAGEMU_GAME_RUNNING);
}

EMSCRIPTEN_KEEPALIVE
void web_rom_reset(bool is_cgb_mode) {
	enum GBModel model = is_cgb_mode ? MODEL_CGB : MODEL_DMG;
	hagemu_app_reset(hagemu_app, model);
}

EMSCRIPTEN_KEEPALIVE
void web_set_button(int button_id) {
	hagemu_set_button(hagemu_app->gb, button_id, true);
}

EMSCRIPTEN_KEEPALIVE
void web_unset_button(int button_id) {
	hagemu_set_button(hagemu_app->gb, button_id, false);
}


#endif // __EMSCRIPTEN__
