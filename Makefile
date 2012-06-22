# build with: make
# run with:   sudo make exec
CC=gcc
CFLAGS=-Wall --std=gnu99
CFLAGS_DEPLOY=-O3
CFLAGS_DEBUG=-g -O0 -D DEBUG # Include debug symbols, disable optimizations, etc
SRC_DIR=src
SRC_AC_DIR=src_ac# directory for the animal_control daemon source
STATIC_LIB_DIR=lib# root directory of statically linked libraries
BIN_DIR=bin
LIBS=-lpthread -lcrypto -lssl # flags for dynamically linked libraries

# create the call to CC needed for a base build
CC_BUILD=$(CC) $(CFLAGS) $(SRC_DIR)/*.c -I$(SRC_DIR) \
         $(STATIC_LIB_DIR)/*/*.c -I$(wildcard $(STATIC_LIB_DIR)/*) $(LIBS) \
         -o "$(BIN_DIR)/svpn"
CC_AC_BUILD=$(CC) $(CFLAGS) $(SRC_AC_DIR)/*.c -I$(SRC_AC_DIR) -o \
            "$(BIN_DIR)/animal_control"

all: build

build: init
	$(CC_BUILD) $(CFLAGS_DEPLOY)
	$(CC_AC_BUILD) $(CFLAGS_DEPLOY)

debug: init
	$(CC_BUILD) $(CFLAGS_DEBUG)
	$(CC_AC_BUILD) $(CFLAGS_DEBUG)

clean:
	rm -rf bin

init: clean
	mkdir -p bin
