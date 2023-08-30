#
# Makefile for NES APU emulator demo
#

##################################################
# Compiler
##################################################

CC			:= gcc
CFLAGS		:= -Wall -Wextra --std=gnu17

##################################################
# Linker
##################################################

LDFLAGS		:= `sdl2-config --cflags --libs` -lSDL2_image -lm

##################################################
# Directories
##################################################

SRC			:= ./src
OBJ			:= ./obj

##################################################
# Files
##################################################

SRCS		:= $(wildcard $(SRC)/*.c)
OBJS		:= $(patsubst $(SRC)/%.c,$(OBJ)/%.o,$(SRCS))
DEPS		:= $(patsubst $(SRC)/%.c,$(OBJ)/%.d,$(SRCS))

##################################################
# OS handling
##################################################

ifeq ($(OS),Windows_NT)
	FOUND_OS := Windows
else
	FOUND_OS := $(shell uname)
endif

ifeq ($(FOUND_OS), Windows)
	APP		:= ./apu_emu_demo.exe
	LDFLAGS	+= -lmingw32
endif
ifeq ($(FOUND_OS), Linux)
	APP		:= ./apu_emu_demo
endif

##################################################
# Other flags
##################################################

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CFLAGS += -DDEBUG -g
else
	CFLAGS += -O2
endif

##################################################
# Rules
##################################################

.PHONY: all clean

all: $(APP)

$(APP): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJ):
	mkdir -p $@

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(APP) $(OBJ) audio_out.wav

-include $(DEPS)
