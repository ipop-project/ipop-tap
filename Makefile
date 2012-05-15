# build with: make
# run with:   sudo make exec
CC=gcc
CFLAGS=-Wall
SRC_DIR=src
BIN_DIR=bin
LIBS=-lpthread -lcrypto -lssl

all: build

build: clean
	mkdir -p bin
	${CC} ${CFLAGS} ${SRC_DIR}/*.c -I${SRC_DIR} ${LIBS} -o "${BIN_DIR}/svpn"
	cp certs/* bin

clean:
	rm -rf bin

exec:
	cd bin; ./svpn
