CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g

COMMON_DIR = ../common
INCLUDES   = -I$(COMMON_DIR)

BUILD = build
BIN   = $(BUILD)/log_test
OBJ   = $(BUILD)/log_test.o $(BUILD)/log.o $(BUILD)/time_utils.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ)

$(BUILD)/log_test.o: log_test.cpp log.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/log.o: log.cpp log.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/time_utils.o: $(COMMON_DIR)/time_utils.cpp $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) test_output

.PHONY: all clean
