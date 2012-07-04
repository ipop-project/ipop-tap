#!/bin/sh

GIT_REPO_DIR="`mktemp -d`"
GIT_REPO_URL='https://github.com/guardianproject/openssl-android.git'
DEST_INCLUDE_DIR="openssl-include"

LOG_PREFIX="`tput bold`[OpenSSL]`tput sgr0`"

echo "$LOG_PREFIX Cloning Git Repository"
git clone --quiet "$GIT_REPO_URL" "$GIT_REPO_DIR"

echo "$LOG_PREFIX Copying include directory"
cp -r "$GIT_REPO_DIR/include" "$DEST_INCLUDE_DIR"

echo "$LOG_PREFIX Deleting Git Repository"
rm -rf "$GIR_REPO_DIR"
