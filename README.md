## LameBoy is a GameBoy emulator (but lamer)

This is an attempt to write a GameBoy emulator in C99. Raylib is used to write the pixels and handle input. The end goal is to compile this project into WebAssembly using Emscripten. At the moment, this project is still a work in progress.

- [x] Pass all of Blargg's cpu_instrs test roms
- [x] Pass Blargg's instr_timing test rom
- [ ] Pass Blargg's mem_timing test rom
- [ ] Pass Blargg's mem_timing_2 test rom
- [ ] Display tiles from VRAM
- [ ] Implement a basic PPU
  - [ ] Display tiles from VRAM
  - [ ] Write tiles to window with Raylib
  - [ ] Implement SCX and SCY
  - [ ] Add window overlay
  - [ ] Add sprites
  - [ ] Get the timing correct with the CPU
- [ ] Implement support for various MBC chips
- [ ] Add the APU (Audio Processing Unit)
- [ ] Get the web version working
- [ ] Add save states
- [ ] Add a UI
- [ ] Add GBC functionality
