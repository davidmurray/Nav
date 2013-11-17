export TARGET = native

include theos/makefiles/common.mk

TOOL_NAME = Nav
Nav_FILES = main.c

include $(THEOS_MAKE_PATH)/tool.mk
