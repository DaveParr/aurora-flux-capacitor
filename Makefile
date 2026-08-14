include config.mk

.PHONY: build flash libdaisy

build:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make build PROJECT=hello-aurora)
endif
	$(MAKE) -C $(PROJECT) TARGET=$(PROJECT)-$(FIRMWARE_VERSION)

flash:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
ifndef MOUNT
	$(error MOUNT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
	@test -d "$(MOUNT)" || (echo "Error: $(MOUNT) is not mounted or does not exist."; exit 1)
	cp "$(PROJECT)/build/$(PROJECT)-$(FIRMWARE_VERSION).bin" "$(MOUNT)/"
	sync
	@echo "Flashed $(PROJECT)-$(FIRMWARE_VERSION).bin to $(MOUNT)/"

libdaisy:
	$(MAKE) -C lib/Aurora-SDK/libs/libDaisy GCC_PATH=$(GCC_PATH)
	$(MAKE) -C lib/Aurora-SDK/libs/DaisySP GCC_PATH=$(GCC_PATH)
