CC = gcc
CFLAGS = -O3 -std=c99 -Wall -pedantic
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,build/%.o,$(SOURCES))

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
# Maybe add -mwindows later
	OUTPUT = hagemu.exe
else
	LFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = hagemu
endif

${OUTPUT}: build/libraylib.a ${OBJECTS}
	@printf %s "Linking object files..."
	@${CC} $^ -o $@ -L build ${LFLAGS} >/dev/null
	@echo sucessful!
	@echo ${OUTPUT} was sucessfully created!

build/raylib.h:
	@mkdir -p build
	@printf %s "Copying $(notdir $@) into build folder..."
	@cp lib/raylib/src/raylib.h $@
	@echo sucessfull!

build/%.o: src/%.c build/raylib.h makefile
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -o $@ -I build -c $< >/dev/null
	@echo sucessfull!

build/libraylib.a: makefile
	@mkdir -p build
	@printf %s "Building Raylib..."
	@make -C lib/raylib/src PLATFORM=PLATFORM_DESKTOP -B >/dev/null
	@cp lib/raylib/src/libraylib.a $@
	@echo sucessful!

build/libraylib_web.a: makefile
	@printf %s "Building Raylib using emcc..."
	@make -C lib/raylib/src PLATFORM=PLATFORM_WEB -B >/dev/null
	@cp lib/raylib/src/libraylib.web.a build/libraylib_web.a
	@echo sucessful!

web: makefile build/raylib.h build/libraylib_web.a
	sh lib/emsdk/emsdk install latest
	sh lib/emsdk/emsdk activate latest
	source lib/emsdk/emsdk_env.sh && emcc -O3 -flto src/*.c -o build/main.html -I build -L build -lraylib_web -s USE_GLFW=3 -s ASYNCIFY
	rm build/main.html
	cp src/emsdk_index.html build/index.html
	cd build && python -m http.server 8000

clean:
	@echo Cleaning up build files and executables...
	@rm -rf build ${OUTPUT}
	@make clean -C lib/raylib/src

test: ${OUTPUT}
	./${OUTPUT} roms/dmg-acid2.gb
