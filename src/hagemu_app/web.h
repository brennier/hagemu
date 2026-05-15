#ifndef WEB_H
#define WEB_H

#ifdef __EMSCRIPTEN__

#include "main.h"
#include "hagemu_core.h"

void web_save_pointer_for_javascript(struct HagemuApp *app);
const uint8_t* web_get_sram_pointer(void);
void web_save_sram_file(void);
size_t web_get_sram_size(void);
const char *web_get_sram_file_name(void);
void web_load_file(const char *filename);

#endif // __EMSCRIPTEN__
#endif // WEB_H
