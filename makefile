CFLAGS = -O3 -std=c99 -Wall -pedantic
SOURCES = $(wildcard src/hagemu_core/*.c)
OBJECTS = $(patsubst src/hagemu_core/%.c,build/%.o,$(SOURCES))
INCLUDES = -I src/hagemu_core -I src/hagemu_app

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lSDL3 -lhagemu -lopengl32 -lgdi32 -lwinmm
# Maybe add -mwindows later
	OUTPUT = hagemu.exe
else
	LFLAGS = -lSDL3 -lhagemu -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = hagemu
endif

${OUTPUT}: build/libhagemu.a build/main.o build/text.o build/file.o build/web.o
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

build/%.o: src/hagemu_core/%.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ ${INCLUDES}
	@echo sucessfull!

build/main.o: src/hagemu_app/main.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ ${INCLUDES}
	@echo sucessfull!

build/text.o: src/hagemu_app/text.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ ${INCLUDES}
	@echo sucessfull!

build/file.o: src/hagemu_app/file.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ ${INCLUDES}
	@echo sucessfull!

build/web.o: src/hagemu_app/web.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ ${INCLUDES}
	@echo sucessfull!

web:
	@mkdir -p web_build
	@sh src/emsdk/emsdk install latest
	@sh src/emsdk/emsdk activate latest
	@source src/emsdk/emsdk_env.sh && emcc -O3 -flto -lidbfs.js src/hagemu_core/*.c src/hagemu_app/*.c -o web_build/hagemu.js ${INCLUDES} -I src/emsdk/upstream/emscripten/cache/sysroot/include/ -s USE_SDL=3 -s ASYNCIFY -s EXPORTED_RUNTIME_METHODS='["HEAPU8","UTF8ToString"]'
	@cd web_build && python -m http.server 8000

clean:
	@echo Cleaning up build files and executables...
	@rm -rf build ${OUTPUT}

test: ${OUTPUT}
	./${OUTPUT} roms/test.gb
