################################################################################
#
# aic8800-sdio-firmware
#
################################################################################

AIC8800_SDIO_FIRMWARE_VERSION = c56f910044cc854d6c553bcb9a644f3bca5a4c38
AIC8800_SDIO_FIRMWARE_SITE = $(call github,lxowalle,aic8800-sdio-firmware,$(AIC8800_SDIO_FIRMWARE_VERSION))
AIC8800_SDIO_FIRMWARE_LICENSE = PROPRIETARY
AIC8800_SDIO_FIRMWARE_REDISTRIBUTE = NO

define AIC8800_SDIO_FIRMWARE_INSTALL_TARGET_CMDS
	mkdir -pv $(TARGET_DIR)/usr/lib/firmware/aic8800_sdio/
	rsync -r --verbose --copy-dirlinks --copy-links --hard-links ${@D}/* $(TARGET_DIR)/usr/lib/firmware/aic8800_sdio/
	ln -sfn aic8800_and_aic8800D80 $(TARGET_DIR)/usr/lib/firmware/aic8800_sdio/aic8800
endef

$(eval $(generic-package))
