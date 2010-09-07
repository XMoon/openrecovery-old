ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_ARCH),arm)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c

LOCAL_CFLAGS := -Os 
LOCAL_MODULE := libmtdutils_orcvr

include $(BUILD_STATIC_LIBRARY)

#open recovery flash_image

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flash_image.c
LOCAL_CFLAGS := -Os 
LOCAL_MODULE := flash_image-or
LOCAL_MODULE_TAGS := eng
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libmtdutils_orcvr libcutils libc
include $(BUILD_EXECUTABLE)

#open recovery erase_image

include $(CLEAR_VARS)
LOCAL_SRC_FILES := erase_image.c
LOCAL_CFLAGS := -Os 
LOCAL_MODULE := erase_image-or
LOCAL_MODULE_TAGS := eng
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libmtdutils_orcvr libcutils libc
include $(BUILD_EXECUTABLE)

#open recovery dump_image

include $(CLEAR_VARS)
LOCAL_SRC_FILES := dump_image.c
LOCAL_CFLAGS := -Os 
LOCAL_MODULE := dump_image-or
LOCAL_MODULE_TAGS := eng
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libmtdutils_orcvr libcutils libc
include $(BUILD_EXECUTABLE)

endif	# TARGET_ARCH == arm
endif	# !TARGET_SIMULATOR
