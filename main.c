#include "include/raylib.h"

#define SCREENWIDTH 166
#define SCREENHEIGHT 144

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
