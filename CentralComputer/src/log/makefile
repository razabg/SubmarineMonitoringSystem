CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g

BIN = log_test
OBJ = log_test.o log.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

log_test.o: log_test.cpp log.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

log.o: log.cpp log.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ) *.log

.PHONY: all clean
