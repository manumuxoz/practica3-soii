CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lpthread

TARGET = prod3_cons1
SRC = src/prod3_cons1.c
OBJ = $(SRC:.c=.o)
BIN_DIR = bin

.PHONY: all clean run

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(BIN_DIR)/$(TARGET) $(OBJ)
	rmdir $(BIN_DIR) 2>/dev/null || true

run: $(BIN_DIR)/$(TARGET)
	$(BIN_DIR)/$(TARGET) ../numeros1.txt ../numeros2.txt ../numeros3.txt
