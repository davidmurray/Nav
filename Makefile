export TARGET = native

include theos/makefiles/common.mk

TOOL_NAME = Nav
Nav_FILES = main.c
Nav_LIBRARIES = gps curl

include $(THEOS_MAKE_PATH)/tool.mk
