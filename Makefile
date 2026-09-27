CC := gcc
CFLAGS := -Werror -Wall -Wextra
LDFLAGS := -lj1939

LIBNAME := libj1939.a

SRC := src/*.c
OBJ := $(SRC:.c=.o)

.PHONY: clean test  lib

lib: $(LIBNAME)

$(LIBNAME): $(OBJ)
	ar rcs $@ $^

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.a *.o src/*.o

test:
	# Run test

