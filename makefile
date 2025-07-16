CC = gcc
CFLAGS = -O3 -std=c99 -Wall -pedantic
LIBS = lib
INCLUDES = include

# Use different linker libraries and output names depending on the OS
ifeq ($(OS),Windows_NT)
	LFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm -mwindows
	OUTPUT = game.exe
else
	LFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
	OUTPUT = game
endif

${OUTPUT}: main.o lib/libraylib.a
	${CC} $^ -o ${OUTPUT} -L ${LIBS} ${LFLAGS}

main.o: main.c
	${CC} ${CFLAGS} -c -I ${INCLUDES} $^

lib/libraylib.a:
	make -C lib/raylib/src
	cp lib/raylib/src/libraylib.a lib/libraylib.a

clean:
	@echo "Cleaning up all files.."
	rm *.o ${OUTPUT}

run: ${OUTPUT}
	./${OUTPUT}
	rm *.o ${OUTPUT}

test: ${OUTPUT}
	./${OUTPUT} lib/gb-test-roms/cpu_instrs/individual/${TESTNUM}* | ./lib/gameboy-doctor/gameboy-doctor - cpu_instrs ${TESTNUM}
