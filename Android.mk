
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sa
LOCAL_MODULE_TAGS := eng

LOCAL_SRC_FILES := \
    sa.c dev.c

LOCAL_STATIC_LIBRARIES := libc libm
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)
