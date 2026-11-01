#---------------------------------------------------------------------------------
# wordPass - Nintendo DS homebrew (devkitARM + libnds)
#---------------------------------------------------------------------------------
.SUFFIXES:

# `make check` (native rule tests) and `make dict` run without devkitARM
NATIVE_GOALS := check dict
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out $(NATIVE_GOALS),$(MAKECMDGOALS)),)
NATIVE := 1
endif
endif

ifndef NATIVE
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
endif

GAME_TITLE     := wordPass
GAME_SUBTITLE1 := crack the pass, save the library
GAME_SUBTITLE2 := homebrew

ifndef NATIVE
include $(DEVKITARM)/ds_rules
endif

TARGET   := wordPass
BUILD    := build
SOURCES  := source
INCLUDES := include
DATA     := data
GRAPHICS :=
AUDIO    :=
ICON     :=
NITRO    :=

ARCH := -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -Wextra -Wshadow -O2 -ffunction-sections -fdata-sections\
            $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lfat -lnds9

LIBDIRS := $(LIBNDS) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))\
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CC)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CFILES:.c=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir))\
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include)\
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

icons := $(wildcard *.bmp)
ifneq (,$(findstring icon.bmp,$(icons)))
  export GAME_ICON := $(CURDIR)/icon.bmp
endif

.PHONY: $(BUILD) clean dict check

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Regenerate the dictionary from a word list, marking COMMON words as the common tier:
#   make dict WORDS=tools/enable1.txt COMMON=tools/common_words.txt
WORDS  ?= tools/enable1.txt
COMMON ?= tools/common_words.txt
dict:
	python3 tools/gen_dict.py $(WORDS) data/dict.bin $(COMMON)
	python3 tools/gen_dict.py --verify data/dict.bin $(WORDS) $(COMMON)

# Native rule tests: board.c, dict.c and music.c under gcc with the stubs in tools/check/stub.
#   make check
CHECK_SRC := tools/check/board_test.c tools/check/stubs.c source/board.c source/dict.c source/music.c
check:
	@mkdir -p $(BUILD)
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(CHECK_SRC) -o $(BUILD)/check
	$(BUILD)/check

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

$(OUTPUT).nds: $(OUTPUT).elf $(GAME_ICON)
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
