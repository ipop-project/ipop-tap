# build with "ndk-build"
# run ./setup.sh before building the first time

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := ipop-tap

LOCAL_CFLAGS += -D ANDROID --std=gnu99

LOCAL_C_INCLUDES := ../../src/ $(wildcard ../../lib/*/)

LOCAL_SRC_FILES := $(wildcard ../../src/*.c ../../lib/*/*.c)

include $(BUILD_EXECUTABLE)
