CC = gcc
CFLAGS = -Wall -Wextra -pedantic -I./include/ -std=c17
OBJDIR = obj
BINDIR = bin
DBGDIR = debug
RELDIR = release
INSTALLDIR = /usr/local/bin
BIN = cgba

DBG_BINDIR = $(BINDIR)/$(DBGDIR)
REL_BINDIR = $(BINDIR)/$(RELDIR)

DBG_OBJDIR = $(OBJDIR)/$(DBGDIR)
REL_OBJDIR = $(OBJDIR)/$(RELDIR)

DBGBIN = $(DBG_BINDIR)/$(BIN)
RELBIN = $(REL_BINDIR)/$(BIN)

vpath %.c src/ src/cpu/

SRC = $(notdir $(wildcard src/*.c src/*/*.c))
DBGOBJS = $(patsubst %.c, $(DBG_OBJDIR)/%.o, $(SRC))
RELOBJS = $(patsubst %.c, $(REL_OBJDIR)/%.o, $(SRC))

# file dependencies, created by gcc
DBGDEPENDS = $(patsubst %.o, %.d, $(DBGOBJS))
RELDEPENDS = $(patsubst %.o, %.d, $(RELOBJS))

.PHONY: all debug install clean

all: CFLAGS += -O3 -flto=auto
all: $(RELBIN)

debug: CFLAGS += -g -DDEBUG
debug: $(DBGBIN)

# required directories
$(DBG_BINDIR) $(DBG_OBJDIR) $(REL_BINDIR) $(REL_OBJDIR):
	mkdir -p $@/

# regular build
$(RELBIN): $(RELOBJS) | $(REL_BINDIR)
	$(CC) $(CFLAGS) $^ -o $@

# debug build
$(DBGBIN): $(DBGOBJS) | $(DBG_BINDIR)
	$(CC) $(CFLAGS) $^ -o $@

-include $(RELDEPENDS) $(DBGDEPENDS)

clean:
	rm -rf $(OBJDIR)/ $(BINDIR)/

install: all
	cp $(RELBIN) $(INSTALLDIR)

# object files (plus dependency files from -MMD -MP)
.SECONDEXPANSION:
%.o: $$(*F).c Makefile | $$(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
