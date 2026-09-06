CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR  = ../../../Shared/ProtocolTLV
UART_DIR = ../uartTransport

INCLUDES = -I$(TLV_DIR) -I$(UART_DIR)

BIN = comm_test
OBJ = comm_test.o communication.o serial.o tlv.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) -lpthread

comm_test.o: comm_test.cpp communication.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

communication.o: communication.cpp communication.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all clean
