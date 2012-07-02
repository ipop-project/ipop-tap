# build with "ndk-build"
# run ./setup.sh before building the first time

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := svpn

LOCAL_CFLAGS += -D DROID_BUILD --std=gnu99

LOCAL_LDLIBS := -L. -lssl -lcrypto

LOCAL_C_INCLUDES := openssl-include/ ../../src/ $(wildcard ../../lib/*/)

LOCAL_SRC_FILES := $(wildcard ../../src/*.c ../../lib/*/*.c)

include $(BUILD_EXECUTABLE)
