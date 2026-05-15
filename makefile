TARGET = hagemu
CFLAGS = -O3 -std=c99 -Wall -pedantic
LFLAGS = $(shell pkg-config --libs sdl3)
INCLUDES = -I src/hagemu_core -I src/hagemu_app $(shell pkg-config --cflags sdl3)

SOURCE_DIR = src
BUILD_DIR  = build
SOURCES = $(shell find $(SOURCE_DIR) -name "*.c" | grep -v "$(SOURCE_DIR)/emsdk/")
OBJECTS = $(patsubst $(SOURCE_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))

$(TARGET): $(OBJECTS)
	@printf %s "Linking together the final executable..."
	@$(CC) $(CFLAGS) $(LFLAGS) $^ -o $@ >/dev/null
	@echo successful!
	@echo $(TARGET) was successfully created!

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(@D)
	@printf %s "Compiling $< into object code..."
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	@echo successful!

.PHONY: clean test

clean:
	@echo Cleaning up build files and executables...
	@rm -rf $(BUILD_DIR) $(TARGET)

test: $(TARGET)
	./$(TARGET) roms/test.gb

#####--- WebAssembly Build ---#####
# Recursively calls this makefile with different arguments

WEB_BUILD_DIR = $(BUILD_DIR)/web
WEB_TARGET    = web_build/hagemu.js
EMSDK         = src/emsdk/emsdk
EMFLAGS       = -sUSE_SDL=3 -flto -lidbfs.js -sASYNCIFY \
                -sEXPORTED_RUNTIME_METHODS='["HEAPU8","UTF8ToString","ccall","cwrap"]'

web:
	@printf %s "Setting up the emsdk environment..."
	@$(EMSDK) install latest >/dev/null
	@$(EMSDK) activate latest >/dev/null
	@echo successful!
	@sh -c "EMSDK_QUIET=1 source src/emsdk/emsdk_env.sh && $(MAKE) \
		CC=emcc \
		TARGET=$(WEB_TARGET) \
		BUILD_DIR=$(WEB_BUILD_DIR) \
		LFLAGS='$(LFLAGS) $(EMFLAGS)'"
	@cd web_build && python -m http.server 8000
