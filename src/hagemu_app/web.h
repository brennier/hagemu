#ifndef WEB_H
#define WEB_H

// EMSCRIPTEN functions
#ifdef __EMSCRIPTEN__

#include "emscripten.h"
void EMSCRIPTEN_KEEPALIVE web_sync_filesystem() {
	if (hagemu_sram_available()) {
		size_t sram_size;
		const uint8_t *sram = hagemu_get_sram(&sram_size);
		char *sram_name = hagemu_file_sram_name("/savedata/test.sav");
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

void web_setup_filesystem() {
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

#else
void web_setup_filesystem() {}
#endif // __EMSCRIPTEN__

#endif // WEB_H
