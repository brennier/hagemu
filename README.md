## Hagemu is a GameBoy emulator

This is an attempt to write a GameBoy emulator in C99. This is a personal project to learn more about computers and emulation. Raylib is used to write the pixels and handle input. The end goal is to compile this project into WebAssembly using Emscripten. At the moment, this project is still a work in progress.

### Progress Report

- [ ] Pass Blargg's CPU test roms
  - [x] cpu_instrs test
  - [x] instr_timing test
  - [x] mem_timing test
  - [x] mem_timing_2 test
  - [ ] interrupt_time test
- [ ] Implement a basic PPU
  - [x] Display tiles from VRAM
  - [x] Write tiles to window with Raylib
  - [x] Draw the background layer
  - [ ] Implement the scroll registers SCX and SCY
  - [ ] Add window overlay
  - [ ] Add sprites
  - [ ] Implement DMA transfers
  - [ ] Switch to a scanline based renderer
- [ ] Implement support for various Memory Bank Controller (MBC) chips
  - [x] MCB1 (Basic support)
  - [ ] MCB3 (Basic support)
  - [ ] Save game support
  - [ ] Real Time Clock support
- [ ] Add the APU (Audio Processing Unit)
- [ ] Get the WebAssembly version working
- [ ] Finish the UI
  - [x] Drag and drop rom files onto the window
- [ ] Add save states
- [ ] Add GBC functionality
