export TARGET = native

include theos/makefiles/common.mk

TOOL_NAME = Nav
Nav_FILES = main.c LodePNG/lodepng.c
Nav_LIBRARIES = gps curl MagickWand

ADDITIONAL_CFLAGS = -I/usr/include/ImageMagick

include $(THEOS_MAKE_PATH)/tool.mk
include $(THEOS_MAKE_PATH)/aggregate.mk
