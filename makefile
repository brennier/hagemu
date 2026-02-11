CFLAGS = -O3 -std=c99 -Wall -pedantic
SOURCES = $(wildcard src/hagemu_core/*.c)
OBJECTS = $(patsubst src/hagemu_core/%.c,build/%.o,$(SOURCES))

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lSDL3 -lhagemu -lopengl32 -lgdi32 -lwinmm
# Maybe add -mwindows later
	OUTPUT = hagemu.exe
else
	LFLAGS = -lSDL3 -lhagemu -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = hagemu
endif

${OUTPUT}: build/libhagemu.a build/hagemu.h build/libraylib.a build/raylib.h build/main.o
	@printf %s "Linking together the final executable..."
	@${CC} $^ -o $@ -L build ${LFLAGS} >/dev/null
	@echo sucessful!
	@echo ${OUTPUT} was sucessfully created!

build/libhagemu.a: ${OBJECTS}
	@mkdir -p build
	@printf %s "Linking together the emulator core..."
	@ar rcs $@ $^ -o $@
	@ranlib $@
	@echo sucessful!

build/hagemu.h: src/hagemu_core/hagemu_core.h
	@mkdir -p build
	@printf %s "Copying $(notdir $@) into build folder..."
	@cp $^ $@
	@echo sucessfull!

build/raylib.h: lib/raylib/src/raylib.h
	@mkdir -p build
	@printf %s "Copying $(notdir $@) into build folder..."
	@cp $^ $@
	@echo sucessfull!

build/%.o: src/hagemu_core/%.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ -I build
	@echo sucessfull!

build/main.o: src/hagemu_app/main.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ -I build
	@echo sucessfull!

build/libraylib.a: lib/raylib/src/libraylib.a
	@mkdir -p build
	@printf %s "Copying $(notdir $@) into build folder..."
	@cp $^ $@
	@echo sucessfull!

lib/raylib/src/libraylib.a:
	@printf %s "Building Raylib..."
	@make -C lib/raylib/src PLATFORM=PLATFORM_DESKTOP -B
	@echo sucessful!

build/libraylib_web.a:
	@sh lib/emsdk/emsdk install latest
	@sh lib/emsdk/emsdk activate latest
	@mkdir -p build
	@printf %s "Building Raylib using emcc..."
	@source lib/emsdk/emsdk_env.sh && make -C lib/raylib/src PLATFORM=PLATFORM_WEB -B >/dev/null
	@cp lib/raylib/src/libraylib.web.a build/libraylib_web.a
	@echo sucessful!

web: build/raylib.h build/libraylib_web.a
	@sh lib/emsdk/emsdk install latest
	@sh lib/emsdk/emsdk activate latest
	@cp lib/emsdk/upstream/emscripten/cache/sysroot/include/emscripten.h build
	@source lib/emsdk/emsdk_env.sh && emcc -DPLATFORM_WEB -O3 -flto -lidbfs.js src/*.c -o build/main.html -I build -L build -lraylib_web -s USE_GLFW=3 -s ASYNCIFY
	@rm build/main.html
	@cp src/index.html build/index.html
	@cd build && python -m http.server 8000

clean:
	@echo Cleaning up build files and executables...
	@rm -rf build ${OUTPUT}

test: ${OUTPUT}
	./${OUTPUT} roms/test.gb
