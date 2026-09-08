CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g

UART_DIR = ../uartTransport

INCLUDES = -I$(UART_DIR)

BUILD = build
BIN   = $(BUILD)/gateway
OBJ   = $(BUILD)/gateway.o $(BUILD)/serial.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) -lpthread

$(BUILD)/gateway.o: gateway.cpp $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
