CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g

BIN     = sermon
OBJ     = serial.o sermon.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c serial.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all clean
