# build with: make
# run with:   sudo make exec
MINGWCC=i686-w64-mingw32-gcc
CFLAGS=-Wall -Werror --std=gnu99
CFLAGS_DEPLOY=-O3
CFLAGS_DEBUG=-g -O0 -D DEBUG # Include debug symbols, disable optimizations, etc
SRC_DIR=src
STATIC_LIB_DIR=lib# root directory of statically linked libraries
BIN_DIR=bin
LIBS=-lpthread # flags for dynamically linked libraries
LINUX_FLAGS=-D LINUX
WIN32_FLAGS=-D WIN32
WIN32_LDFLAGS=-lws2_32 -lntdll -liphlpapi -static-libgcc

# create the call to CC needed for a base build
CC_BUILD=$(CC) $(CFLAGS) $(SRC_DIR)/*.c -I$(SRC_DIR) \
         $(STATIC_LIB_DIR)/*/*.c \
		 $(foreach dir,$(wildcard $(STATIC_LIB_DIR)/*),-I$(dir)) $(LIBS) \
         -o "$(BIN_DIR)/ipop-tap"

MINGWCC_BUILD=$(MINGWCC) $(CFLAGS) $(SRC_DIR)/*.c -I$(SRC_DIR) \
         $(STATIC_LIB_DIR)/*/*.c \
		 $(foreach dir,$(wildcard $(STATIC_LIB_DIR)/*),-I$(dir)) \
         -lpthreadGC2

all: build

build: init
	$(CC_BUILD) $(CFLAGS_DEPLOY) $(LINUX_FLAGS)

debug: init
	$(CC_BUILD) $(CFLAGS_DEBUG)

win32: init
	$(MINGWCC_BUILD) $(WIN32_FLAGS) $(WIN32_LDFLAGS) -o "$(BIN_DIR)/ipop-tap.exe"

win32_dll: init
	$(MINGWCC_BUILD) $(WIN32_FLAGS) $(WIN32_LDFLAGS) -shared -o "$(BIN_DIR)/ipoptap.dll" \
            -Wl,--output-def,"$(BIN_DIR)/ipoptap.def",--out-implib,"$(BIN_DIR)/libipoptap.a"

clean:
	rm -rf bin

init: clean
	mkdir -p bin
