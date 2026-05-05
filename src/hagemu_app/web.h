#ifndef WEB_H
#define WEB_H

#include "main.h"
#include "hagemu_core.h"

void web_setup_filesystem();
void web_save_pointer_for_javascript(struct HagemuApp *app);

#ifdef __EMSCRIPTEN__
const uint8_t* web_get_sram_pointer();
size_t web_get_sram_size();
const char *web_get_sram_file_name();
#endif // WEB_H

#endif // WEB_H
