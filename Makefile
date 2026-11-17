CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
SRCS = $(wildcard src/*.c)
TARGET = minihttpd

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET) test/test_http test/test_mime
