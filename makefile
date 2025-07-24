CC = gcc
CFLAGS = -O3 -std=c99 -Wall -pedantic
LIBS = lib
OBJ = build/main.o build/mmu.o build/clock.o

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
# Maybe add -mwindows later
	OUTPUT = hagemu.exe
else
	LFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = hagemu
endif

${OUTPUT}: ${OBJ} build/libraylib.a
	${CC} $^ -o $@ -L ${LIBS} ${LFLAGS}
	@echo "Make sucessful!"

build/%.o: src/%.c
	mkdir -p build
	${CC} ${CFLAGS} -o $@ -c $^

build/libraylib.a:
	mkdir -p build
	make -C lib/raylib/src
	cp lib/raylib/src/libraylib.a $@

clean:
	@echo "Cleaning up all files.."
	rm -r build ${OUTPUT}

run: ${OUTPUT}
	./${OUTPUT}

test-mem-timing: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/mem_timing/mem_timing.gb

test-cpu-instrs: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/cpu_instrs/cpu_instrs.gb

test-instr-timing: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/instr_timing/instr_timing.gb

test-gameboy-doctor: ${OUTPUT}
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/01* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 01
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/02* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 02
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/03* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 03
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/04* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 04
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/05* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 05
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/06* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 06
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/07* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 07
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/08* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 08
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/09* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 09
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/10* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 10
	-./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/11* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs 11
