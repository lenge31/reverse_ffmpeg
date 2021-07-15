LOCAL_PATH := $(call my-dir)

#########
include $(CLEAR_VARS)
LOCAL_MODULE := v4l2_app
# LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)/v4l2
LOCAL_SRC_FILES := src/v4l2_app.c
# LOCAL_SHARED_LIBRARIES := libc
include $(BUILD_EXECUTABLE)

#########
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg_app
# LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)/v4l2
# LOCAL_C_INCLUDES := $(LOCAL_PATH)/ffmpeg/include
LOCAL_CFLAGS := -I$(LOCAL_PATH)/ffmpeg/include

LOCAL_LDFLAGS := -L$(LOCAL_PATH)/ffmpeg/lib \
		-Wl,--start-group -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lavcodec -Wl,--end-group \
		-Lprebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/lib/gcc/arm-linux-androideabi/4.9/ -lgcc
#		-Wl,--whole-archive -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lavcodec -Wl,--no-whole-archive
LOCAL_SRC_FILES := src/ffmpeg_app.c
include $(BUILD_EXECUTABLE)

#########
include $(CLEAR_VARS)
LOCAL_MODULE := linux_test
# LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)/v4l2
LOCAL_SRC_FILES := src/linux_test.c
include $(BUILD_EXECUTABLE)

#########
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg_threads
# LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)/v4l2
# LOCAL_C_INCLUDES := $(LOCAL_PATH)/ffmpeg/include
LOCAL_CFLAGS := -I$(LOCAL_PATH)/ffmpeg/include -g

LOCAL_LDFLAGS := -L$(LOCAL_PATH)/ffmpeg/lib \
		-Wl,--start-group -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lavcodec -Wl,--end-group \
		-Lprebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/lib/gcc/arm-linux-androideabi/4.9/ -lgcc
#		-Wl,--whole-archive -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lavcodec -Wl,--no-whole-archive
LOCAL_SRC_FILES := src/ffmpeg_threads.c
LOCAL_SHARED_LIBRARIES := libcutils
include $(BUILD_EXECUTABLE)
