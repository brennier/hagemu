CC = gcc
CFLAGS = -O3 -std=c99 -Wall -pedantic
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
	@${CC} $^ -o $@ -L build ${LFLAGS} >/dev/null
	@echo sucessful!
	@echo ${OUTPUT} was sucessfully created!

build/raylib.h:
	@printf %s "Copying $(notdir $@) into build folder..."
	@cp lib/raylib/src/raylib.h $@
	@echo sucessfull!

build/%.o: src/%.c build/raylib.h
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -o $@ -I build -c $< >/dev/null
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

test: ${OUTPUT}
	@echo "Testing CPU instructions..."
	@./${OUTPUT} lib/gb-test-roms/cpu_instrs/cpu_instrs.gb
	@echo ""
	@echo "Testing instruction timing..."
	@./${OUTPUT} lib/gb-test-roms/instr_timing/instr_timing.gb
	@echo ""
	@echo "Testing memory timing..."
	@./${OUTPUT} lib/gb-test-roms/mem_timing/mem_timing.gb