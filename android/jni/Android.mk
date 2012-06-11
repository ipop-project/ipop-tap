
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := svpn

LOCAL_CFLAGS += -D DROID_BUILD --std=gnu99

LOCAL_LDLIBS := -L. -lssl -lcrypto

LOCAL_C_INCLUDES := /home/pierre/projects/external/openssl-android/include/ \
                   ../../src/

LOCAL_SRC_FILES := ../../src/bss_fifo.c ../../src/headers.c \
                   ../../src/peerlist.c ../../src/socket_utils.c \
                   ../../src/svpn.c ../../src/tap.c ../../src/translator.c \
                   ../../src/hsearch.c ../../src/hsearch_r.c

include $(BUILD_EXECUTABLE)
