CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

TARGET = minihttpd
SRC = src/main.c src/passiveTCP.c src/passivesock.c src/errexit.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
