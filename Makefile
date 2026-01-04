# Compiler settings
CXX = g++
CXXFLAGS = -Wall -Wextra -pthread -Iinclude

# Source files and Output name
SRC = src/proxy.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = proxy_server

# The Default "Build" Rule
all: $(TARGET)

# How to link the object files into the final executable
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ)

# How to compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up (delete old builds)
clean:
	rm -f $(TARGET) src/*.o

.PHONY: all clean