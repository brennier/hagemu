#include "include/raylib.h"
#include <stdint.h>

#define SCREENWIDTH 166
#define SCREENHEIGHT 144

union {
        struct {
                uint8_t f, a, c, b, e, d, l, h;
                uint16_t sp, pc;
        };
        struct {
                uint8_t af, bc, de, hl;
        };
        uint8_t raw_bytes[12];
} reg;

int main() {
        InitWindow(SCREENWIDTH, SCREENHEIGHT, "GameBoy Emulator");

        while (WindowShouldClose() != true) {
                BeginDrawing();
                ClearBackground(BLACK);
                DrawFPS(10, 10);
                EndDrawing();
        }

        CloseWindow();
        return 0;
}
