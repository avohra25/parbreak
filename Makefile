CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
LDLIBS  += -lm

LIB_SRC  := $(wildcard src/*.c)
TEST_SRC := $(wildcard tests/*.c)

.PHONY: all test sanitize clean

all: build/libparbreak.a build/parbreak

build:
	mkdir -p build

build/libparbreak.a: $(LIB_SRC) include/parbreak.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(LIB_SRC)
	mv *.o build/
	ar rcs $@ build/*.o

build/test_parbreak: $(LIB_SRC) $(TEST_SRC) include/parbreak.h tests/test.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_SRC) $(TEST_SRC) -o $@ $(LDLIBS)

build/parbreak: $(LIB_SRC) cli/main.c include/parbreak.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_SRC) cli/main.c -o $@ $(LDLIBS)

test: build/test_parbreak build/parbreak
	./build/test_parbreak
	sh tests/cli_test.sh build/parbreak

sanitize:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer"

clean:
	rm -rf build *.o
