
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := svpn

LOCAL_CFLAGS += -D DROID_BUILD

LOCAL_LDLIBS := -L/home/pierre/projects/external/openssl-android/libs/armeabi \
                -lcrypto

LOCAL_C_INCLUDES := /home/pierre/projects/external/openssl-android/include/ \
                   ../../src/

LOCAL_SRC_FILES := ../../src/tap.c ../../src/peerlist.c ../../src/translator.c \
                   ../../src/encryption.c ../../src/headers.c ../../src/svpn.c

include $(BUILD_EXECUTABLE)
