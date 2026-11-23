#---------------------------------------------------------------------------------
# wordPass - Nintendo DS homebrew (devkitARM + libnds)
#---------------------------------------------------------------------------------
.SUFFIXES:

# `make check` (native rule tests) and `make dict` run without devkitARM
NATIVE_GOALS := check dict pools variants guide guide-check songs music-render music-check bot bot-run
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
# An opt-in engineering build packages only small experimental Russian/
# Ukrainian resource fixtures and exposes separate DEMO menu choices.
# The regular ROM keeps its 11-language picker and settings format.
ifeq ($(WP_CYR_PREVIEW),1)
CFLAGS += -DWP_CYR_PREVIEW
endif
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
# The standard 11-language ROM must not compile or link any experimental
# Russian/Ukrainian WPD4 gameplay, dictionary, ledger or Daily modules.
# Native make check names its test sources explicitly, independently of this
# ROM object list. The opt-in DS preview retains all eight modules.
CYR_ONLY_CFILES := dict_v4.c pools_v2.c tiles_v2.c script_board.c \
                   script_rules.c script_preview.c script_ledger.c script_daily.c
ifneq ($(WP_CYR_PREVIEW),1)
CFILES := $(filter-out $(CYR_ONLY_CFILES),$(CFILES))
endif
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

.PHONY: $(BUILD) clean dict pools variants check guide guide-check songs music-render music-check bot bot-run

# The ROM filesystem (NitroFS): the ten languages, lang/<code>/{dict,pools,letters}.bin, copied from data/lang so the
# source of truth stays in data/ (the copy is not committed). English is compiled in and not in it.
ifneq ($(strip $(NITRO)),)
export NITRO_FILES := $(CURDIR)/$(NITRO)
endif

$(BUILD):
	@mkdir -p $@
	@if [ -n "$(strip $(NITRO))" ]; then rm -rf $(NITRO)/lang && mkdir -p $(NITRO)/lang && cp -r data/lang/. $(NITRO)/lang/ && rm -f $(NITRO)/lang/manifest.txt $(NITRO)/lang/NOTICE; fi
	@if [ "$(WP_CYR_PREVIEW)" = "1" ]; then \
	    if [ -n "$(strip $(CYR_EXTERNAL_DIR))" ]; then \
	        python3 tools/lang/install_cyr_preview.py --input-root "$(CYR_EXTERNAL_DIR)" --output-root "$(NITRO)/cyrpreview"; \
	    else python3 tools/lang/build_preview.py $(NITRO)/cyrpreview; fi; \
	else rm -rf $(NITRO)/cyrpreview; fi
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
CHECK_SRC := tools/check/board_test.c tools/check/stubs.c source/board.c source/dict.c source/pools.c source/lang.c source/langload.c source/music.c source/songs.c source/guide.c source/font.c source/ui.c source/ui_strings.c
LANG_SRC  := tools/check/lang_test.c tools/check/stubs.c source/dict.c source/pools.c
check:
	@mkdir -p $(BUILD)
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source -DCFG_PATH='"$(BUILD)/settings_test.cfg"' \
	    tools/check/settings_test.c source/settings.c -Wl,--wrap=rename,--wrap=fwrite,--wrap=remove -o $(BUILD)/settings_check
	$(BUILD)/settings_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(LANG_SRC) -o $(BUILD)/lang_check
	$(BUILD)/lang_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source tools/check/ui_test.c source/ui.c source/ui_strings.c -o $(BUILD)/ui_check
	$(BUILD)/ui_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source tools/check/font_utf8_test.c source/font.c -o $(BUILD)/font_utf8_check
	$(BUILD)/font_utf8_check
	python3 tools/lang/dict_v4.py --selftest $(BUILD)
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/dict_v4_test.c source/dict_v4.c -o $(BUILD)/dict_v4_check
	$(BUILD)/dict_v4_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin
	python3 tools/lang/tiles_v2.py --selftest $(BUILD)
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/tiles_v2_test.c source/tiles_v2.c source/dict_v4.c -o $(BUILD)/tiles_v2_check
	$(BUILD)/tiles_v2_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin $(BUILD)/wpl2_ru.bin $(BUILD)/wpl2_uk.bin
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/script_board_test.c source/script_board.c source/tiles_v2.c source/dict_v4.c source/pools_v2.c -o $(BUILD)/script_board_check
	$(BUILD)/script_board_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin $(BUILD)/wpl2_ru.bin $(BUILD)/wpl2_uk.bin
	python3 tools/lang/pools_v2.py --selftest $(BUILD)
	python3 tools/lang/check_cyr_seeds.py
	python3 tools/lang/audit_sourced_cyr.py --out $(BUILD)/cyr-editorial-preview
	python3 tools/lang/fetch_cyr_upstream.py --selftest
	python3 tools/lang/import_cyr_hunspell.py --selftest $(BUILD)/cyr-source-intake
	python3 tools/lang/install_cyr_preview.py --selftest $(BUILD)/cyr-pack-install
	python3 tools/lang/build_preview.py $(BUILD)/cyrpreview
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/pools_v2_test.c source/pools_v2.c source/dict_v4.c -o $(BUILD)/pools_v2_check
	$(BUILD)/pools_v2_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin $(BUILD)/wpp2_ru.bin $(BUILD)/wpp2_uk.bin
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/script_pass_test.c source/script_board.c source/pools_v2.c source/tiles_v2.c source/dict_v4.c -o $(BUILD)/script_pass_check
	$(BUILD)/script_pass_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin $(BUILD)/wpl2_ru.bin $(BUILD)/wpl2_uk.bin $(BUILD)/wpp2_ru.bin $(BUILD)/wpp2_uk.bin
	# Verify expanded source-backed 161-word test pools in the actual C board parser.
	$(BUILD)/script_pass_check $(BUILD)/cyr-source-intake/ru/dict.bin $(BUILD)/cyr-source-intake/uk/dict.bin $(BUILD)/cyr-source-intake/ru/tiles.bin $(BUILD)/cyr-source-intake/uk/tiles.bin $(BUILD)/cyr-source-intake/ru/pools.bin $(BUILD)/cyr-source-intake/uk/pools.bin
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/script_fire_test.c source/script_rules.c source/script_board.c source/pools_v2.c source/tiles_v2.c source/dict_v4.c -o $(BUILD)/script_fire_check
	$(BUILD)/script_fire_check $(BUILD)/wpd4_ru.bin $(BUILD)/wpd4_uk.bin $(BUILD)/wpl2_ru.bin $(BUILD)/wpl2_uk.bin $(BUILD)/wpp2_ru.bin $(BUILD)/wpp2_uk.bin
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -DWP_CYR_PREVIEW -I source tools/check/script_preview_test.c source/script_preview.c source/script_rules.c source/script_board.c source/script_ledger.c source/script_daily.c source/pools_v2.c source/tiles_v2.c source/dict_v4.c -o $(BUILD)/script_preview_check
	$(BUILD)/script_preview_check $(BUILD)/cyrpreview/ru/dict.bin $(BUILD)/cyrpreview/uk/dict.bin $(BUILD)/cyrpreview/ru/tiles.bin $(BUILD)/cyrpreview/uk/tiles.bin $(BUILD)/cyrpreview/ru/pools.bin $(BUILD)/cyrpreview/uk/pools.bin
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/script_ledger_test.c source/script_ledger.c -o $(BUILD)/script_ledger_check
	$(BUILD)/script_ledger_check
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I source tools/check/script_daily_test.c source/script_daily.c -o $(BUILD)/script_daily_check
	$(BUILD)/script_daily_check
	gcc -std=gnu11 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(CHECK_SRC) -o $(BUILD)/check
	$(BUILD)/check
	gcc -std=gnu11 -O1 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(BOT_SRC) -lm -o $(BUILD)/bot_check
	$(BUILD)/bot_check --selftest
	python3 tools/music/build_songs.py --check || (echo 'source/songs.c is not what tools/music/songs_src.py generates: run make songs'; exit 1)
	python3 tools/music/render.py --stats > /dev/null
	python3 tools/check/save_sites.py
	python3 tools/check/elf_symbols_test.py
	python3 tools/check/elf_abi_test.py
	python3 tools/check/rom_budget_test.py
	python3 tools/check/music_render_test.py
	python3 tools/lang/gen_ui.py --check || (echo 'source/ui_strings.[ch] are not what tools/lang/gen_ui.py generates from ui_strings.tsv: run python3 tools/lang/gen_ui.py'; exit 1)
	python3 tools/check/ui_slot_sanity.py
	python3 tools/lang/check_translation_quality.py
	python3 tools/lang/check_translation_quality.py --selftest

# The balance bot (tools/check/bot.c): plays the real rules code headlessly with a competent and a weak player model and
# prints per-shelf tables (docs/BALANCE.md). `make bot` builds build/bot; `make bot-run` runs English and the other
# languages in parallel into build/bot-<code>.md (about ten minutes). `make check` only runs its --selftest.
BOT_SRC := tools/check/bot.c tools/check/stubs.c source/board.c source/dict.c source/pools.c source/lang.c source/langload.c source/music.c source/songs.c source/ui.c source/ui_strings.c
BOT_LANGS ?= en es de fr fi
bot:
	@mkdir -p $(BUILD)
	gcc -std=gnu11 -O2 -Wall -Wextra -Wshadow -I tools/check/stub -I source $(BOT_SRC) -lm -o $(BUILD)/bot

bot-run: bot
	@for l in $(BOT_LANGS); do ( $(BUILD)/bot --lang $$l $(BOT_ARGS) > $(BUILD)/bot-$$l.md ) & done; wait; cat $(addprefix $(BUILD)/bot-,$(addsuffix .md,$(BOT_LANGS)))

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

# source/songs.c is generated from tools/music/analysis/*.json and tools/music/songs_src.py (deterministic; commit the result)
songs:
	python3 tools/music/build_songs.py

# docs/music/*.wav and *.mid: a software render of the same song tables (no DS needed to listen)
music-render:
	python3 tools/music/render.py

# docs/music is tracked (about 13 MB, deterministic, two seconds to render): this fails when it differs from a fresh render.
music-check:
	python3 tools/music/render_check.py

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
