CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lpthread

TARGET1 = prod3_cons1_sleeps_a
TARGET2 = prod3_cons1_sleeps_b
SRC1 = src/prod3_cons1_sleeps_a.c
SRC2 = src/prod3_cons1_sleeps_b.c
OBJ1 = $(SRC1:.c=.o)
OBJ2 = $(SRC2:.c=.o)
BIN_DIR = bin

.PHONY: all clean run-a run-b

all: $(BIN_DIR)/$(TARGET1) $(BIN_DIR)/$(TARGET2)

$(BIN_DIR)/$(TARGET1): $(OBJ1) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/$(TARGET2): $(OBJ2) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(BIN_DIR)/$(TARGET1) $(BIN_DIR)/$(TARGET2) $(OBJ1) $(OBJ2)
	rmdir $(BIN_DIR) 2>/dev/null || true

# ejecucion para cada variante
run-a: $(BIN_DIR)/$(TARGET1)
	$(BIN_DIR)/$(TARGET1) ../numeros1.txt ../numeros2.txt ../numeros3.txt

run-b: $(BIN_DIR)/$(TARGET2)
	$(BIN_DIR)/$(TARGET2) ../numeros1.txt ../numeros2.txt ../numeros3.txt
