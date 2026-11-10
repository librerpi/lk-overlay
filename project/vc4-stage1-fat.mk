GLOBAL_DEFINES += STAGE1_SD_FAT=1
MODULES += lib/fs/fat-boot

include project/vc4-stage1-sdonly.mk
