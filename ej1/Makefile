CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lpthread

TARGET = prod_cons
SRC = src/prod_cons.c
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
	$(BIN_DIR)/$(TARGET) ../numeros.txt
