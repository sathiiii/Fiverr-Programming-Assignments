TARGET = cachesim
SRC = $(TARGET).c
CC = gcc
CFLAGS = -g -Wall -Wvla -Werror -fsanitize=address,undefined

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(TARGET) *.o *.a *.dylib *.dSYM