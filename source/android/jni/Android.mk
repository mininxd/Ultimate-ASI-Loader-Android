LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := ASILoader
LOCAL_SRC_FILES := ../ASILoader.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_CPPFLAGS  := -std=c++17 -fexceptions -frtti -O3 -Wall
LOCAL_LDLIBS    := -llog -ldl

include $(BUILD_SHARED_LIBRARY)
