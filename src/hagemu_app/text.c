#include <SDL3/SDL.h>
#include <stdio.h>

SDL_Texture* font_texture = NULL;

const int chars_width[224] = {
	3, 1, 4, 6, 5, 7, 6, 2, 3, 3, 5, 5, 2, 4, 1, 7, 5, 2, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 3, 4, 3, 6,
	7, 6, 6, 6, 6, 6, 6, 6, 6, 3, 5, 6, 5, 7, 6, 6, 6, 6, 6, 6, 7, 6, 7, 7, 6, 6, 6, 2, 7, 2, 3, 5,
	2, 5, 5, 5, 5, 5, 4, 5, 5, 1, 2, 5, 2, 5, 5, 5, 5, 5, 5, 5, 4, 5, 5, 5, 5, 5, 5, 3, 1, 3, 4, 4,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 5, 5, 5, 7, 1, 5, 3, 7, 3, 5, 4, 1, 7, 4, 3, 5, 3, 3, 2, 5, 6, 1, 2, 2, 3, 5, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 6, 3, 3, 3, 3, 7, 6, 6, 6, 6, 6, 6, 5, 6, 6, 6, 6, 6, 6, 4, 6,
	5, 5, 5, 5, 5, 5, 9, 5, 5, 5, 5, 5, 2, 2, 3, 3, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5,
};

bool text_init(SDL_Renderer *renderer) {
	SDL_Surface *font_surface = SDL_LoadBMP("raylib_font.bmp");
	if (!font_surface) {
		SDL_Log("Unable to load BMP: %s", SDL_GetError());
		return false;
	}

	// Set white as a transparent color
	const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(font_surface->format);
	Uint32 bg_color = SDL_MapRGB(format, SDL_GetSurfacePalette(font_surface), 255, 255, 255);
	SDL_SetSurfaceColorKey(font_surface, true, bg_color);

	font_texture = SDL_CreateTextureFromSurface(renderer, font_surface);
	SDL_SetTextureScaleMode(font_texture, SDL_SCALEMODE_NEAREST);
	if (!font_texture) {
		SDL_Log("Unable to load BMP: %s", SDL_GetError());
		return false;
	}
	SDL_DestroySurface(font_surface);
	return true;
}

void text_cleanup() {
	SDL_DestroyTexture(font_texture);
}


float text_draw_char(SDL_Renderer *renderer, char c, int x, int y, int font_size) {
	unsigned value = (unsigned)c;
	value -= 32;

	int x_offset = 1;
	int y_offset = 1;
	for (int i = 0; i < value; i++) {
		x_offset += chars_width[i] + 1;
		if (x_offset + chars_width[i+1] > 128) {
			x_offset = 1;
			y_offset += 11;
		}
	}

	float output_width = chars_width[value]*(font_size / 10.0);
	SDL_FRect source_rect = { x_offset, y_offset, chars_width[value], 10 };
	SDL_FRect dest_rect = { x, y, output_width, font_size };
	SDL_SetTextureColorMod(font_texture, 255, 255, 0);
	SDL_RenderTexture(renderer, font_texture, &source_rect, &dest_rect);
	return output_width;
}

void text_draw(SDL_Renderer *renderer, char* text, int x, int y, int font_size) {
	float float_x = (float)x;
	for (char *c = text; *c != '\0'; c++) {
		float_x += text_draw_char(renderer, *c, float_x, y, font_size);
		float_x += (font_size / 10.0);
	}
}

float text_pixel_length(char* text, int font_size) {
	float length = 0.0f;
	for (char *c = text; *c != '\0'; c++) {
		int value = (*c) - 32;
		length += chars_width[value] * (font_size / 10.0);
		length += (font_size / 10.0);
	}
	return length;
}

void text_draw_centered(SDL_Renderer *renderer, char* text, int center_x, int center_y, int font_size) {
	float new_x = center_x - text_pixel_length(text, font_size) / 2;
	float new_y = center_y - (font_size / 2.0);

	text_draw(renderer, text, new_x, new_y, font_size);
}
