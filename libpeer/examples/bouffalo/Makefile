SDK_DEMO_PATH ?= $(abspath .)

ifeq ($(strip $(BL_SDK_BASE)),)
$(error Set BL_SDK_BASE to the Bouffalo SDK directory)
endif

wifi6_cmake_definition := -DCMAKE_C_FLAGS=-I$(SDK_DEMO_PATH)/config

export BL_SDK_BASE

include $(BL_SDK_BASE)/project.build
