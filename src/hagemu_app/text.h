#ifndef TEXT_H
#define TEXT_H

bool text_init(SDL_Renderer *renderer);
void text_cleanup();

void text_draw(SDL_Renderer *renderer, char* text, int x, int y, int font_size);
float text_pixel_length(char* text, int font_size);
void text_draw_centered(SDL_Renderer *renderer, char* text, int center_x, int center_y, int font_size);

#endif // TEXT_H
