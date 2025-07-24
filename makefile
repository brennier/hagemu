CC = gcc
CFLAGS = -O3 -std=c99 -Wall -pedantic
LIBS = lib
OBJ = build/main.o build/mmu.o build/clock.o build/cpu.o

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
# Maybe add -mwindows later
	OUTPUT = hagemu.exe
else
	LFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = hagemu
endif

${OUTPUT}: build/libraylib.a ${OBJ}
	@printf %s "Linking object files..."
	@${CC} $^ -o $@ -L ${LIBS} ${LFLAGS} >/dev/null
	@echo sucessful!
	@echo ${OUTPUT} was sucessfully created!

build/%.o: src/%.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $^) into object code..."
	@${CC} ${CFLAGS} -o $@ -c $^ >/dev/null
	@echo sucessfull!

build/libraylib.a:
	@mkdir -p build
	@printf %s "Building Raylib..."
	@make -C lib/raylib/src >/dev/null
	@cp lib/raylib/src/libraylib.a $@
	@echo sucessful!

clean:
	@echo Cleaning up build files and executables...
	@rm -rf build ${OUTPUT}

run: ${OUTPUT}
	./${OUTPUT}

test-mem-timing: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/mem_timing/mem_timing.gb

test-cpu-instrs: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/cpu_instrs/cpu_instrs.gb

test-instr-timing: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/instr_timing/instr_timing.gb