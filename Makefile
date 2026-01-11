CC = gcc
CFLAGS = -Wall -O2

SRC = A3.c
OBJ = A3.o
TARGET = A3

# Default target
all: $(TARGET)

# creating object file
$(OBJ): $(SRC)
	$(CC)  $(CFLAGS) -c $(SRC) -o $(OBJ)

# creating the executable
$(TARGET): $(OBJ)
	$(CC)  $(CFLAGS) $(OBJ) -o $(TARGET)  -lm

# Clean rule to remove object files and the executable
clean:
	rm -f $(OBJ) $(TARGET)


.PHONY: all clean
