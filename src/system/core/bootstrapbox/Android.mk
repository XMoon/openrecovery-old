LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

TOOLS := \
	cp \
	mkdir \
	mount \
	rm \
	rmdir \
	umount 

LOCAL_SRC_FILES:= \
	toolbox.c \
	copyfd.c \
	$(patsubst %,%.c,$(TOOLS))

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_OUT_UNSTRIPPED)/recovery/
LOCAL_MODULE:= bootstrapbox

# Including this will define $(intermediates).
#
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)


