#---------------------------------------------------------------------------------
# wordPass - Nintendo DS homebrew (devkitARM + libnds)
#---------------------------------------------------------------------------------
.SUFFIXES:

# `make check` (native rule tests) and `make dict` run without devkitARM
NATIVE_GOALS := check dict pools variants guide guide-check
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
NITRO    := nitrofiles

ARCH := -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -Wextra -Wshadow -O2 -ffunction-sections -fdata-sections\
            $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lfilesystem -lfat -lnds9

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

.PHONY: $(BUILD) clean dict pools variants check guide guide-check

# The ROM filesystem (NitroFS): the ten languages, lang/<code>/{dict,pools,letters}.bin, copied from data/lang so the
# source of truth stays in data/ (the copy is not committed). English is compiled in and not in it.
ifneq ($(strip $(NITRO)),)
export NITRO_FILES := $(CURDIR)/$(NITRO)
endif

$(BUILD):
	@mkdir -p $@
	@if [ -n "$(strip $(NITRO))" ]; then mkdir -p $(NITRO)/lang && cp -ru data/lang/. $(NITRO)/lang/ && rm -f $(NITRO)/lang/manifest.txt; fi
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Regenerate the dictionary from a word list, marking COMMON words as the common tier:
#   make dict WORDS=tools/enable1.txt,tools/words/extra.txt COMMON=tools/common_words.txt
# WORDS takes several comma separated lists.  `make pools` rebuilds data/pools.bin (see tools/words/SOURCES.md).
WORDS  ?= tools/enable1.txt,tools/words/extra.txt,tools/words/more_open.txt
COMMON ?= tools/common_words.txt
dict:
	python3 tools/gen_dict.py $(WORDS) data/dict.bin $(COMMON)
	python3 tools/gen_dict.py --verify data/dict.bin $(WORDS) $(COMMON)

pools:
	python3 tools/gen_pools.py

variants:
	python3 tools/gen_dict.py --variants tools/words/us_only.txt tools/words/uk_only.txt data/variants.bin
	python3 tools/gen_dict.py --verify-variants data/variants.bin tools/words/us_only.txt tools/words/uk_only.txt

# Native rule tests: board.c, dict.c, pools.c, music.c, guide.c and font.c under gcc with the stubs in tools/check/stub,
# after settings.c on its own (stub fat.h, a scratch file for the card). The last line is CHECK OK.
#   make check
CHECK_SRC := tools/check/board_test.c tools/check/stubs.c source/board.c source/dict.c source/pools.c source/lang.c source/langload.c source/music.c source/guide.c source/font.c
LANG_SRC  := tools/check/lang_test.c tools/check/stubs.c source/dict.c source/pools.c
check:
	@mkdir -p $(BUILD)
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source -DCFG_PATH='"$(BUILD)/settings_test.cfg"' \
	    tools/check/settings_test.c source/settings.c -o $(BUILD)/settings_check
	$(BUILD)/settings_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(LANG_SRC) -o $(BUILD)/lang_check
	$(BUILD)/lang_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(CHECK_SRC) -o $(BUILD)/check
	$(BUILD)/check

# The written guide (docs/GUIDE.md). `make guide` retakes every picture in docs/img by driving the ROM in the headless
# emulator (tools/emu/screens.py: needs py-desmume, Pillow and the toolchain image for the symbol lookup, not devkitARM
# itself), then checks the guide against the source and the data files. `make guide-check` runs only the check (gcc only).
#   make guide [ROM=wordPass.nds]
ROM ?= wordPass.nds
guide:
	python3 tools/emu/screens.py $(ROM)
	python3 tools/check_guide.py

guide-check:
	python3 tools/check_guide.py

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(NITRO) $(TARGET).elf $(TARGET).nds

else

$(OUTPUT).nds: $(OUTPUT).elf $(GAME_ICON) $(wildcard $(NITRO_FILES)/lang/*/*.bin)
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
