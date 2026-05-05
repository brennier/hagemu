<h1><img align="center" height="75" src="hagemu_logo.png">Hagemu is a GameBoy emulator</h1>

This is an attempt to write a GameBoy emulator in C99. This is a personal project to learn more about computers and emulation. SDL3 is used to draw the pixels and handle input. Emscripten is used to compile the project into WebAssembly. At the moment, this project is still a work in progress.

## You can run this emulator inside your web browser!
You can try out this emulator in your web browser at this link: https://hagemu.dev/

Disclaimers:
 - You need to use a keyboard or game controller. There is no key remapping or touch screen support at the moment.
 - To run a .gb file, please drag and drop the file directly onto the web page.

### Tested games (no noticable glitches)
Most original gameboy games should run fine. If you find any glitches, please let me know.
The following games have been tested:
- Tetris
- Dr. Mario
- Super Mario Land
- Metroid II
- Zelda: Link's Awakening
- Pokemon Red, Blue, and Yellow

<details>
  <summary><h3>Progress Report (click here to expand)</h3></summary>

- [x] Finish the CPU
  - [x] Parse opcodes
  - [x] Implement double registers
  - [x] Write functions for the opcodes
  - [x] Set flags in the f register
  - [x] Write the interrupt dispatch code
  - [x] Add a clock and timer interrupts
  - [x] Fix cycle timing of opcodes
  - [x] Cycle-correct memory reads/writes
  - [x] Pass Blargg's CPU test roms
    - [x] cpu_instrs test
    - [x] instr_timing test
    - [x] mem_timing test
    - [x] mem_timing_2 test
- [x] Finish the PPU (Picture Processing Unit)
  - [x] Display tiles from VRAM
  - [x] Draw tiles to window
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
- [x] Finish the APU (Audio Processing Unit)
  - [x] Setup sound and add an audio callback function
  - [x] Synchronize the length, sweep, and envelope timers
  - [x] Downsample from 2MHz to 48kHz
  - [x] Synchronize the sample rates of the individual sound channels
  - [x] Finish Master controls
    - [x] Turn APU on/off
    - [x] Mono volume control
    - [x] Panning left/right per channel
    - [x] Volume control left/right per channel
  - [x] Finish channels 1 and 2 (pulse waves)
    - [x] Turn DAC on/off
    - [x] Basic volume
    - [x] Correct frequency
    - [x] Wave duty
    - [x] Envelope
    - [x] Reset trigger
    - [x] Sweep (channel 1 only)
    - [x] Length timer
  - [x] Finish channel 3 (custom waveform)
    - [x] Turn DAC on/off
    - [x] Correct frequency
    - [x] Basic volume
    - [x] Read and output the custom wave data
    - [x] Reset trigger
    - [x] Length timer
  - [x] Finish channel 4 (random noise)
    - [x] Turn DAC on/off
    - [x] Basic volume
    - [x] Correct frequency
    - [x] Linear-feedback shift register
    - [x] Envelope
    - [x] Reset trigger
    - [x] Length timer
- [ ] Implement support for various Memory Bank Controller (MBC) chips
  - [x] Basic support for saving and loading .sav files
  - [x] Separate MBC code into its own file
  - [x] MCB1 (Full support, tested with Mooneye's MBC1 test roms)
  - [ ] MCB1M
  - [ ] MCB2
  - [x] MCB3 (without RTC) (tested with ZoomTen's MBC30 test rom)
  - [x] MCB5 (without rumble) (tested with Mooneye's MBC5 test roms)
  - [ ] MCB6
  - [ ] MCB7
  - [x] MBC30 (tested with ZoomTen's MBC30 test rom)
  - [ ] Other, less popular MCBs
  - [ ] Real Time Clock support
  - [ ] Rumble support
- [ ] Minor fixes
  - [x] Separate the joypad logic from the Raylib library
  - [x] Add support for gamepads
  - [x] Test gamepad support
  - [x] Calculate the half-carry as (a ^ b ^ result) & 0x10
  - [x] Add 'inline' the CPU opcode functions
  - [x] Rewrite the f register as separate bools
  - [x] Fix glitch where a sprite partially clips if it's on the left or top border
  - [x] Fix bug where window X Position is less than 7
  - [x] Fill the sound buffer directly instead of using a callback
  - [x] Use float instead of int16_t for audio bit depth
  - [x] Use RGBA8888 instead of RGBA5551 for pixel format
  - [x] All file handling is done by the SDL app. The core simply takes a pointer to data.
  - [x] Add support for disabling the PPU
  - [x] Block access to the VRAM and OAM while they're being accessed by the ppu
  - [x] Emulate the timing of the DMA
  - [x] Add a custom boot rom
  - [x] Block CPU access to all memory except high ram during DMA transfer
  - [x] The CPU ticks all other components (this is a stop gap until the CPU can tick one M-cycle at a time)
  - [x] Writes to unused IO registers are ignored and reads return 0xFF
  - [x] Implement the STOP instruction (untested)
  - [ ] Make the color palette settable instead of internal to the ppu
  - [ ] Make the audio registers readable
  - [ ] Clear all APU registers when disabled
  - [ ] Mute a sound channel if its frequency is above 20kHz
  - [ ] Add support for the VIN sound channel
  - [ ] Rewrite PPU to be more modular
  - [ ] Add option to blend frames
  - [ ] Add support for the serial data port
  - [ ] Pass Blargg's interrupt_time test
  - [ ] Run the Mooneye Test Suite
  - [ ] Make a cool logo
  - [ ] Use some profiling tools to find critical code blocks
  - [ ] Compile program using -O3 and -flto and -ffast-math
- [x] Glitches that took more than 1 day to fix
  - [x] Pokemon Red corrupted save data
    - Fixed! My SRAM implementation was off-by-one
  - [x] Pokemon Aka character constantly moves up
    - Fixed! The upper two bits of joypad register should always be 1
  - [x] Pokemon Gold crashes during screen transitions
    - Fixed! If the PPU is set to disabled, reset the current line/dot and don't tick the PPU
- [ ] WebAssembly version
  - [x] Get the WebAssembly version working
  - [x] Add a simple front-end UI
  - [x] Upload to my website (https://uezu.dev/projects/hagemu)
  - [x] Automatically save and load .sav files using a local IndexedDB file system
  - [x] Download and upload save files
  - [x] Select file using a mobile browser
  - [ ] Progressive Web Application support
    - [x] Basic support
    - [ ] Add a service worker for caching and retrieving files without the internet
  - [ ] Rewrite the html index file from scratch
  - [ ] Automatically cache and load the last played game
  - [ ] Touchscreen support
  - [ ] Put the loading bar where the canvas is
- [ ] Add a UI
  - [x] Drag and drop rom files onto the window
  - [ ] Select file using a file dialog window
  - [ ] Add custom shaders (mostly for a grid overlay)
  - [ ] Settings menu
    - [ ] Button mapping menu
    - [ ] Audio menu
    - [ ] Color palette menu
    - [ ] Save state menu
- [ ] Add GBC functionality
  - [ ] Double speed mode
  - [ ] Extra work and video ram
  - [ ] HDMA features
  - [ ] Pass cgb-acid2
- [ ] Future Refactoring Ideas
  - [x] Separate the core from the interface
  - [x] Switch from Raylib to SDL3
  - [x] Change audio from uint16_t to float
  - [x] Change video from RGBA5551 to RGBA8888
  - [x] Double buffer the video output
  - [x] Synchronize the APU along with the CPU and PPU
  - [x] Separate cart.c and joypad.c from mmu.c
  - [x] Remove the union type punning of the CPU registers
  - [x] Add dynamic audio resampling
  - [x] Using box averaging to downsample the audio
  - [x] Add a high-pass filter and a low-pass filter to the APU
  - [ ] Use CMake instead of make
  - [ ] Organize all state into a gameboy struct
  - [ ] Support save/load states
  - [ ] Rewrite the CPU so that it can tick 1 m-cycle per call
  - [ ] Rewrite the PPU using a pixel pusher renderer
</details>

<details>
  <summary><h3>Links for the future (click here to expand)</h3></summary>

- Links for the future
  - Gameboy Color differences: https://jsgroth.dev/blog/posts/game-boy-color/
  - Gameboy Color PPU: https://github.com/mattcurrie/cgb-acid2
  - Gameboy Color HDMA: https://gbdev.io/pandocs/CGB_Registers.html
  - Using WASM: https://gioarc.me/posts/games/wasm.html
  - Sound info: https://gbdev.gg8.se/wiki/articles/Gameboy_sound_hardware
  - General info: https://hacktix.github.io/GBEDG/
  - Timing of the LYC STAT: https://gbdev.io/guides/lyc_timing.html
  - Custom bootrom: https://github.com/Hacktix/Bootix
- Links about MBC3's real-time clock
  - MBC3 .sav format: https://bgb.bircd.org/rtcsave.html
  - MBC3 RTC test rom: https://github.com/aaaaaa123456789/rtc3test
  - Discussion about MBC3 RTC: https://www.reddit.com/r/EmuDev/comments/12vk8io/gameboy_color_mbc3_rtc/
  - MBC3 Pandocs: https://gbdev.io/pandocs/MBC3.html
  - More detail about the implementation: https://hacktix.github.io/GBEDG/mbcs/mbc3/ 
</details>
