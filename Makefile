CC = gcc
CFLAGS = -Wall -Wextra -pedantic -I./include/ -std=c17
OBJ_DIR = obj
BIN_DIR = bin
INSTALL_DIR = /usr/local/bin
BIN = cgba

vpath %.c src/

SRC = $(notdir $(wildcard src/*.c))
OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC))

# file dependencies, created by gcc
DEPENDS = $(patsubst %.o, %.d, $(OBJS))

.PHONY: all install clean

all: CFLAGS += -O3 -flto=auto
all: $(BIN_DIR)/$(BIN)

# required directories
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@/

# regular build
$(BIN_DIR)/$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

-include $(DEPENDS)

clean:
	rm -rf $(OBJ_DIR)/ $(BIN_DIR)/

install: all
	cp $(BIN_DIR)/$(BIN) $(INSTALL_DIR)

# object files (plus dependency files from -MMD -MP)
.SECONDEXPANSION:
%.o: $$(*F).c Makefile | $$(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
