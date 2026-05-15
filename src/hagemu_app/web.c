#include "web.h"
#include <stdio.h>
#include "file.h"

// EMSCRIPTEN functions
#ifdef __EMSCRIPTEN__

#include "emscripten.h"

struct HagemuApp *hagemu_app = NULL;
const char *sram_filename = NULL;

void EMSCRIPTEN_KEEPALIVE web_sync_filesystem(void) {
	if (hagemu_app && hagemu_app->rom_filename && hagemu_sram_available()) {
		size_t sram_size;
		const uint8_t *sram = hagemu_get_sram(&sram_size);
		char *sram_name = hagemu_file_sram_name(hagemu_app->rom_filename);
		hagemu_file_save(sram_name, sram, sram_size);
		free(sram_name);
	}

	printf("Saving the game...");
	EM_ASM(
		FS.syncfs(false, function (err) {
			if (err) {
				console.log("Error saving to the offline database");
			} else {
				console.log("Successfully saved to the offline database")
			}
		});
	);
}

void web_setup_filesystem(void) {
	EM_ASM(
		FS.mkdir('/savedata');
		FS.mount(IDBFS, {}, '/savedata');
		FS.syncfs(true, function (err) {
			if (err) {
				console.log("Error loading the offline database");
			} else {
				console.log("Successfully loaded the offline database");
			}
		});
		window.addEventListener("beforeunload", function (event) {
			Module._web_sync_filesystem();
			return "SAVING";
		});
	);
}

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
void web_load_file(const char *filename) {
	hagemu_handle_drop_event(hagemu_app, filename);
}

#else
void web_setup_filesystem(void) {}
void web_save_pointer_for_javascript(struct HagemuApp *app) {}
#endif
