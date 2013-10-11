# build with: make
# run with:   sudo make exec
CC=gcc
MINGWCC=i686-w64-mingw32-gcc
CFLAGS=-Wall --std=gnu99
CFLAGS_DEPLOY=-O3
CFLAGS_DEBUG=-g -O0 -D DEBUG # Include debug symbols, disable optimizations, etc
SRC_DIR=src
STATIC_LIB_DIR=lib# root directory of statically linked libraries
BIN_DIR=bin
LIBS=-lpthread # flags for dynamically linked libraries

# create the call to CC needed for a base build
CC_BUILD=$(CC) $(CFLAGS) $(SRC_DIR)/*.c -I$(SRC_DIR) \
         $(STATIC_LIB_DIR)/*/*.c \
		 $(foreach dir,$(wildcard $(STATIC_LIB_DIR)/*),-I$(dir)) $(LIBS) \
         -o "$(BIN_DIR)/ipop-tap"

MINGWCC_BUILD=$(MINGWCC) $(CFLAGS) $(SRC_DIR)/*.c -I$(SRC_DIR) \
         $(STATIC_LIB_DIR)/*/*.c \
		 $(foreach dir,$(wildcard $(STATIC_LIB_DIR)/*),-I$(dir)) \
         -o "$(BIN_DIR)/ipop-tap" -lws2_32 -lntdll

all: build

build: init
	$(CC_BUILD) $(CFLAGS_DEPLOY)

debug: init
	$(CC_BUILD) $(CFLAGS_DEBUG)

win32: init
	$(MINGWCC_BUILD) $(CFLAGS_DEBUG) -D WIN32

clean:
	rm -rf bin

init: clean
	mkdir -p bin
