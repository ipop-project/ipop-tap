
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := svpn

LOCAL_CFLAGS += -D DROID_BUILD

LOCAL_LDLIBS := -L. -lssl -lcrypto

LOCAL_C_INCLUDES := /home/pierre/projects/external/openssl-android/include/ \
                   ../../src/

LOCAL_SRC_FILES := ../../src/tap.c ../../src/peerlist.c ../../src/translator.c \
                   ../../src/encryption.c ../../src/headers.c ../../src/svpn.c \
                   ../../src/dtls.c ../../src/bss_fifo.c

include $(BUILD_EXECUTABLE)
