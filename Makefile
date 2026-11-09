CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

TARGET = minihttpd
SRC = src/main.c src/passiveTCP.c src/passivesock.c src/errexit.c src/mime.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
