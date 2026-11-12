CXX ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -pedantic
TARGET := smetki
SOURCE := main.cpp
INFO_FILE := info.txt

.PHONY: all clean

all: $(TARGET) $(INFO_FILE)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

$(INFO_FILE):
	@touch $(INFO_FILE)

clean:
	rm -f $(TARGET)
