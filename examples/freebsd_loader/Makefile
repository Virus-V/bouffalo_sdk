build_dirs += rtos_m0
build_dirs += loader_d0

CHIP ?= bl808

all: $(build_dirs)

$(build_dirs):
	$(MAKE) -C $@ ninja

clean::
	find . -name "build" | xargs rm -rf

.PHONY: $(build_dirs) clean

BL_SDK_BASE = ./../..

export BL_SDK_BASE

include $(BL_SDK_BASE)/project.build
