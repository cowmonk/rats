# config.mk: build configuration for RITS

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS  = -static
PREFIX   = /usr/local
SBIN_DIR = /sbin
BIN_DIR  = /bin
