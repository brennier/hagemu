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

${OUTPUT}: build/libhagemu.a build/hagemu.h build/main.o build/text.o
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

build/text.o: src/hagemu_app/text.c
	@mkdir -p build
	@printf %s "Compiling $(notdir $<) into object code..."
	@${CC} ${CFLAGS} -c $< -o $@ -I build
	@echo sucessfull!

web:
	@sh lib/emsdk/emsdk install latest
	@sh lib/emsdk/emsdk activate latest
	@mkdir -p web_build
	@cp src/hagemu_core/hagemu_core.h web_build/hagemu.h
	@cp lib/emsdk/upstream/emscripten/cache/sysroot/include/emscripten.h web_build
	@source lib/emsdk/emsdk_env.sh && emcc -DPLATFORM_WEB -O3 -flto -lidbfs.js src/hagemu_core/*.c src/hagemu_app/*.c -o web_build/main.html -I web_build -s USE_SDL=3 -s ASYNCIFY
	@rm web_build/main.html
	@cp src/hagemu_app/index.html web_build/index.html
	@rm web_build/*.h
	@cd web_build && python -m http.server 8000

clean:
	@echo Cleaning up build files and executables...
	@rm -rf build web_build ${OUTPUT}

test: ${OUTPUT}
	./${OUTPUT} roms/test.gb
