CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g

UART_DIR = ../uartTransport

INCLUDES = -I$(UART_DIR)

BIN = gateway
OBJ = gateway.o serial.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) -lpthread

gateway.o: gateway.cpp $(UART_DIR)/serial.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all clean
