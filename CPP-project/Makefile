CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -static -static-libgcc -static-libstdc++ -pthread

TARGET = bench

all: $(TARGET)

$(TARGET): bench.cpp order_book.hpp order_book_map.hpp order_book_vector.hpp order_book_direct.hpp
	$(CXX) $(CXXFLAGS) bench.cpp -o $(TARGET)

verify: $(TARGET)
	./$(TARGET) --verify

bench-a: $(TARGET)
	./$(TARGET) --impl A

bench-b: $(TARGET)
	./$(TARGET) --impl B

bench-c: $(TARGET)
	./$(TARGET) --impl C

ifeq ($(OS),Windows_NT)
    CLEAN_CMD = cmd /c del /f /q $(TARGET) $(TARGET).exe 2>nul || exit 0
else
    CLEAN_CMD = rm -f $(TARGET) $(TARGET).exe
endif

clean:
	$(CLEAN_CMD)
