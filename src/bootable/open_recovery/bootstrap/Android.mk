ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_ARCH),arm)

#open recovery boostrap
#for Milestone (development of the bootstrap version)
#but mainly Milestone XT720 etc.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := bootstrap.c
LOCAL_CFLAGS := -Os 
LOCAL_MODULE := bootstrap-or
LOCAL_MODULE_TAGS := eng

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_OUT_UNSTRIPPED)/recovery/
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

include $(BUILD_EXECUTABLE)


endif	# TARGET_ARCH == arm
endif	# !TARGET_SIMULATOR
