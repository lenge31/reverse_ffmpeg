#
# Copyright (C) 2007 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and 
# limitations under the License.
#

TARGET_PLATFORM := sc9630
TARGET_HARDWARE := sc9832a
TARGET_BOARD := m0032

PLATDIR := device/sprd/scx35l
PLATCOMM := $(PLATDIR)/common
BOARDDIR := $(PLATDIR)/$(TARGET_BOARD)
ROOTDIR := $(BOARDDIR)/rootdir
ROOTCOMM := $(PLATCOMM)/rootdir

include $(APPLY_PRODUCT_REVISION)
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_SEPARATED_DT := true

STORAGE_INTERNAL := emulated
STORAGE_PRIMARY := internal

VOLTE_SERVICE_ENABLE := true

#use sprd's four(wifi bt gps fm) integrated one chip
USE_SPRD_WCN := true 

# copy media_codec.xml before calling device.mk,
# because we want to use our file, not the common one
PRODUCT_COPY_FILES += $(BOARDDIR)/media_codecs.xml:system/etc/media_codecs.xml

# Jiahao add for auto-pack pac file
$(call inherit-product, $(BOARDDIR)/modem_bins/modem_bins.mk)
$(call inherit-product, $(BOARDDIR)/custom_files/custom_files.mk)

# SPRD:resolve the primary card can't be recorgnized {@
ifndef STORAGE_ORIGINAL
  STORAGE_ORIGINAL := false
endif

ifndef ENABLE_OTG_USBDISK
  ENABLE_OTG_USBDISK := true
endif
# @}
# SPRD: add for low-memory.set before calling device.mk @{
#PRODUCT_RAM := low
PRODUCT_RAM := high
# @}
# include general common configs
$(call inherit-product, $(PLATCOMM)/device.mk)
$(call inherit-product, $(PLATCOMM)/emmc/emmc_device.mk)
$(call inherit-product, $(PLATCOMM)/proprietories.mk)

DEVICE_PACKAGE_OVERLAYS := $(BOARDDIR)/overlay $(PLATCOMM)/overlay
# Remove video wallpaper feature
PRODUCT_VIDEO_WALLPAPERS := none
BUILD_FPGA := false
PRODUCT_AAPT_CONFIG := hdpi xhdpi normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Set default USB interface
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
	persist.sys.usb.config=mass_storage

PRODUCT_PROPERTY_OVERRIDES += \
	keyguard.no_require_sim=true \
	ro.com.android.dataroaming=false \
	ro.msms.phone_count=2 \
        ro.modem.l.count=2 \
	persist.msms.phone_count=2 \
	persist.radio.multisim.config=dsds \
	persist.msms.phone_default=0 \
        persist.sys.modem.diag=,gser \
        sys.usb.gser.count=8 \
        ro.modem.external.enable=0 \
        persist.sys.support.vt=true \
        persist.modem.l.cs=0 \
        persist.modem.l.ps=1 \
        persist.modem.l.rsim=1 \
        persist.radio.ssda.mode=csfb \
        persist.radio.ssda.testmode=9 \
        persist.radio.ssda.testmode1=10 \
        persist.support.oplpnn=true \
        persist.support.cphsfirst=false \
        lmk.autocalc=false \
        use_brcm_fm_chip=true \
        ro.wcn.gpschip=ge2


#
#	ro.msms.phone_count=2 \
#        ro.modem.l.count=2 \
#	persist.msms.phone_count=2 \
#	persist.radio.multisim.config=dsds \
#
#
        
ifeq ($(strip $(VOLTE_SERVICE_ENABLE)), true)
PRODUCT_PROPERTY_OVERRIDES += persist.sys.volte.enable=true
endif

PRODUCT_PROPERTY_OVERRIDES += ro.ge2.mode=gps_beidou

# board-specific modules
PRODUCT_PACKAGES += \
        sensors.sc8830 \
        fm.$(TARGET_PLATFORM) \
        ValidationTools \
        ims \
        download \
		gnss_download

#		LuoShuli 2018/01/30
#				
#

#[[ for autotest
        PRODUCT_PACKAGES += autotest
#]]

PRODUCT_PACKAGES += wpa_supplicant \
	wpa_supplicant.conf \
	wpa_supplicant_overlay.conf \
	hostapd


PRODUCT_PACKAGES +=	\
			AmapAutoLite			\
			FactoryTest_XJ_M3		\
			Fmt_XJ_M3				\
			kwplay_m3				\
			Launcher_XJ_M3			\
			PopupInfo_XJ_M3			\
			RadarDog_XJ_M3			\
			RecorderReplay_XJ_M3	\
			ScreenSaver_XJ_M3		\
			SpreadwinCamera_XJ_M3	\
			SystemSetting_XJ_M3		\
			Weather_XJ_M3			\
			baidume_simple_XJ_M3	\
			GPSTest_XJ_M3			\
			Music_XJ_M3				\
			SimManager_XJ_M3		\
			SpreadwinWeiXin_XJ_M3	\
			spreadwinComplain_XJ_M3	\
			AmapMonitor_XJ_M3		\
			yunzhisheng_XJ_M3		\
			Application_XJ_M3		\
			SpreadwinLogger_XJ_M3	\
			cameaTest \
			ffmpeg_threads

			


PRODUCT_PACKAGES +=	\
			bootupanimation
			
			
# board-specific files
PRODUCT_COPY_FILES += \
	$(BOARDDIR)/slog_modem_$(TARGET_BUILD_VARIANT).conf:system/etc/slog_modem.conf \
	$(ROOTDIR)/prodnv/PCBA.conf:prodnv/PCBA.conf \
	$(ROOTDIR)/prodnv/BBAT.conf:prodnv/BBAT.conf \
	$(ROOTDIR)/system/usr/keylayout/sprd-gpio-keys.kl:system/usr/keylayout/sprd-gpio-keys.kl \
	$(ROOTDIR)/system/usr/keylayout/sprd-eic-keys.kl:system/usr/keylayout/sprd-eic-keys.kl \
	$(ROOTDIR)/root/init.$(TARGET_BOARD).2342.rc:root/init.$(TARGET_BOARD).rc \
	$(ROOTDIR)/root/init.recovery.$(TARGET_BOARD).2342.rc:root/init.recovery.$(TARGET_BOARD).rc \
	$(ROOTDIR)/system/etc/audio_params/tiny_hw.xml:system/etc/tiny_hw.xml \
	$(ROOTDIR)/system/etc/audio_params/codec_pga.xml:system/etc/codec_pga.xml \
	$(ROOTDIR)/system/etc/audio_params/audio_hw.xml:system/etc/audio_hw.xml \
	$(ROOTDIR)/system/etc/audio_params/audio_para:system/etc/audio_para \
	$(ROOTDIR)/system/etc/audio_params/audio_policy.conf:system/etc/audio_policy.conf \
	$(ROOTCOMM)/root/ueventd.sc8830.rc:root/ueventd.$(TARGET_BOARD).rc \
	$(ROOTCOMM)/system/usr/idc/focaltech_ts.idc:system/usr/idc/focaltech_ts.idc \
	$(ROOTCOMM)/system/usr/idc/msg2138_ts.idc:system/usr/idc/msg2138_ts.idc \
	$(ROOTCOMM)/system/usr/idc/goodix_ts.idc:system/usr/idc/goodix_ts.idc \
	frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
	frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
	frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
	frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
	frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
	frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml\
	frameworks/native/data/etc/android.hardware.camera.flash.xml:system/etc/permissions/android.hardware.camera.flash.xml \
    frameworks/native/data/etc/android.software.midi.xml:system/etc/permissions/android.software.midi.xml \
	frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml
#	hardware/broadcom/libbt/conf/bcm/firmware/bcm4343s/bcm4343.hcd:system/vendor/firmware/bcm4343.hcd




$(call inherit-product-if-exists, vendor/sprd/open-source/common_packages.mk)
$(call inherit-product-if-exists, vendor/sprd/open-source/plus_special_packages.mk)
$(call inherit-product, vendor/sprd/partner/shark/bluetooth/device-shark-bt.mk)
$(call inherit-product, vendor/sprd/gps/GreenEye2/device-sprd-gps.mk)
ifeq ($(strip $(USE_SPRD_WCN)),true)
#connectivity configuration
CONNECTIVITY_HW_CONFIG := $(TARGET_BOARD)
CONNECTIVITY_HW_CHISET := $(shell grep BOARD_SPRD_WCNBT $(BOARDDIR)/BoardConfig.mk)
$(call inherit-product, vendor/sprd/open-source/res/connectivity/device-sprd-wcn.mk)
endif 

# JiaHao - ignore dynamic permission check for certain application
DEFAULT_RUNTIME_PERMISSION_FILES := vendor/sprd/operator/cmcc/permission/sprd-app-perms.xml
ifneq ($(wildcard $(DEFAULT_RUNTIME_PERMISSION_FILES)),)
PRODUCT_COPY_FILES += \
    $(DEFAULT_RUNTIME_PERMISSION_FILES):system/etc/sprd-app-perms.xml
endif

# add security build info
#$(call inherit-product, vendor/sprd/open-source/security_support.mk)

WCN_EXTENSION := true

PRODUCT_REVISION := multiuser
include $(APPLY_PRODUCT_REVISION)

CHIPRAM_DEFCONFIG := m0032
KERNEL_DEFCONFIG := m0032_defconfig
DTS_DEFCONFIG := sprd-scx35l_m0032
UBOOT_DEFCONFIG := m0032

# Overrides
PRODUCT_NAME := m0032
PRODUCT_DEVICE := $(TARGET_BOARD)
PRODUCT_MODEL := A800
PRODUCT_BRAND := GoodView
PRODUCT_MANUFACTURER := SPRD

PRODUCT_LOCALES := zh_CN zh_TW en_US

# Config for using dreamcamera
PRODUCT_USE_DREAMCAM := true

#config selinux policy
BOARD_SEPOLICY_DIRS += $(PLATCOMM)/sepolicy

SPRD_BUILD_VERNO := A800_HW1.0_V1.4
