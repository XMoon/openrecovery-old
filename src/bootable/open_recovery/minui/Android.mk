LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -Os 
LOCAL_SRC_FILES := graphics.c events.c resources.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui_orcvr

include $(BUILD_STATIC_LIBRARY)
