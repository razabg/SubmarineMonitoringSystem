CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g

BUILD = build
BIN   = $(BUILD)/sermon
OBJ   = $(BUILD)/serial.o $(BUILD)/sermon.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ)

$(BUILD)/%.o: %.cpp serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
