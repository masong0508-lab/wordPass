.SUFFIXES:
TARGET := wordPass-cjk-preview
BUILD := build
SOURCES := source
INCLUDES := source generated
GAME_TITLE := WordPass CJK
GAME_SUBTITLE1 := Chinese and Japanese practice
GAME_SUBTITLE2 := Experimental homebrew
ARCH := -march=armv5te -mtune=arm946e-s
CFLAGS := -g -Wall -Wextra -Wshadow -O2 -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -DARM9
LDFLAGS = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lnds9
LIBDIRS := $(LIBNDS)
include $(DEVKITARM)/ds_rules
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CC)
export OFILES := cjk_core.o main.o
.PHONY: $(BUILD) clean
$(BUILD):
	@test -f generated/words_generated.h -a -f generated/glyphs_generated.h
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds
else
$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d
endif
