## Hagemu is a GameBoy emulator

This is an attempt to write a GameBoy emulator in C99. This is a personal project to learn more about computers and emulation. Raylib is used to write the pixels and handle input. The end goal is to compile this project into WebAssembly using Emscripten. At the moment, this project is still a work in progress.

### Playable games (perhaps minor glitches)
- Tetris
- Dr. Mario
- Super Mario Land

### Progress Report
- [x] Pass Blargg's CPU test roms
  - [x] cpu_instrs test
  - [x] instr_timing test
  - [x] mem_timing test
  - [x] mem_timing_2 test
- [x] Implement PPU
  - [x] Display tiles from VRAM
  - [x] Write tiles to window with Raylib
  - [x] Draw the background layer
  - [x] Add window overlay
  - [x] Implement the scroll registers SCX and SCY
  - [x] Switch to a scanline based renderer
  - [x] Update the scroll registers per scanline
  - [x] Implement DMA transfers
  - [x] Add sprites
  - [x] Implement sprite attributes
  - [x] Support for 8x16 sprite mode
  - [x] Implement LCD STAT interrupts
  - [x] Respond to the LCD control register (except for disabling the PPU)
  - [x] Pass dmg-acid2 test rom
- [ ] Implement support for various Memory Bank Controller (MBC) chips
  - [x] MCB1 (Very Basic support)
  - [ ] MCB1 (Full support)
  - [ ] MCB3
  - [ ] Save game support
  - [ ] Real Time Clock support
- [ ] Minor fixes
  - [ ] Emulate the timing of the DMA
  - [ ] Rewrite the f register as separate bools
  - [ ] Add 'inline' the CPU opcode functions
  - [ ] Add support for disabling the PPU
  - [ ] Fix bug where window X Position is less than 7
  - [ ] Rewrite the CPU so that it can tick 1 m-cycle per call
  - [ ] Implement the STOP instruction
  - [ ] Test the HALT instruction
  - [ ] Is there a faster way to calculate the half-carry?
    - Idea: (a ^ b ^ result) & 0x10
  - [ ] Rewrite the PPU using a pixel pusher renderer
  - [ ] Pass Blargg's interrupt_time test
  - [ ] Run the Mooneye Test Suite
  - [ ] Make a cool logo
  - [ ] Add a custom boot rom
- [ ] Add the APU (Audio Processing Unit)
- [ ] Get the WebAssembly version working
- [ ] Finish the UI
  - [x] Drag and drop rom files onto the window
- [ ] Add save states
- [ ] Add GBC functionality
