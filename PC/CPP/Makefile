CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++20
#LDFLAGS = -lsqlite3 -lpthread

OBJS = main.o HardwareTester.o TestLogger.o
TARGET = mthw_tester

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

PHONY: clean

clean:
	rm *.o