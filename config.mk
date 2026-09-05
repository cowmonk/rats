# config.mk: build configuration for RATS

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wpedantic -Werror -O2
LDFLAGS  = -static
PREFIX   = /usr/local
SBIN_DIR = /sbin
BIN_DIR  = /bin
