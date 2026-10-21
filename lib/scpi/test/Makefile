CXX ?= g++
CXXFLAGS ?= -Wall -Wextra -std=c++14 -I../include -O2

SRCS = test_scpi_host.cpp ../src/scpi_parser.cpp ../src/scpi_error_queue.cpp
TARGET = test_scpi_host

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
