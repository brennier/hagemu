## Hagemu is a GameBoy emulator

This is an attempt to write a GameBoy emulator in C99. This is a personal project to learn more about computers and emulation. Raylib is used to write the pixels and handle input. The end goal is to compile this project into WebAssembly using Emscripten. At the moment, this project is still a work in progress.

- [ ] Pass Blargg's CPU test roms
  - [x] cpu_instrs test
  - [x] instr_timing test
  - [x] mem_timing test
  - [x] mem_timing_2 test
  - [ ] interrupt_time test
- [ ] Implement a basic PPU
  - [ ] Display tiles from VRAM
  - [ ] Write tiles to window with Raylib
  - [ ] Implement SCX and SCY
  - [ ] Add window overlay
  - [ ] Add sprites
  - [ ] Get the timing correct with the CPU
- [ ] Implement support for various MBC chips
  - [x] MCB1 (Basic support)
- [ ] Add the APU (Audio Processing Unit)
- [ ] Get the web version working
- [ ] Add save states
- [ ] Add a UI
- [ ] Add GBC functionality
