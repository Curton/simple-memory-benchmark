CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c18 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lrt

TARGET = test_mem_bandwidth
SOURCE = test_mem_bandwidth.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

run-large: $(TARGET)
	./$(TARGET) 1024

run-small: $(TARGET)
	./$(TARGET) 16

.PHONY: all clean run run-large run-small 