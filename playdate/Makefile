CC ?= cc
CXX ?= c++
PYTHON ?= python3
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CXXFLAGS ?= -std=c++14 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti
# Keep host tests separate from the SDK's fixed device/simulator object tree.
BUILD = build-host
CORE_C = core/lp_pack.c core/lp_game.c core/lp_music.c core/lp_adlib.c core/lp_progress.c
CORE_CPP = core/lp_dbopl.cpp core/dbopl_core.cpp
CORE_OBJECTS = $(CORE_C:%.c=$(BUILD)/%.o) $(CORE_CPP:%.cpp=$(BUILD)/%.o)
ASSET_PACK ?= ../generated/lemmings.lpd
ADLIB_IMAGE ?= ../generated/adlib.bin
HOST_OS := $(shell uname -s)
PLAYDATE_SDK_PATH ?= $(if $(filter Darwin,$(HOST_OS)),$(HOME)/PlaydateSDK,$(abspath ../build/reference/playdate-sdk))
FONT_RESOURCES ?= $(PLAYDATE_SDK_PATH)/Resources/Fonts
PRODUCT_PATH ?= ../generated/Lemmings.pdx
ARM_TOOLCHAIN_PATH ?=
EFFECTS_DIR ?= effects
# TEST_UNLOCK=1 exposes every level on device and stops the build recording
# progress. It stages a marker the itch packager refuses, so such a build
# cannot be published. Never set by the release target.
TEST_UNLOCK ?= 0
TEST_MARKER = Source/TEST_UNLOCK.txt
ifeq ($(TEST_UNLOCK),1)
UDEFS += -DLP_TEST_UNLOCK
endif

ifneq ($(strip $(ARM_TOOLCHAIN_PATH)),)
export PATH := $(ARM_TOOLCHAIN_PATH):$(PATH)
endif

.PHONY: all host test stage playdate simulator release clean check-test-flag
all: playdate

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -Icore -I../third_party/dbopl -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -Wno-error -Wno-unused-but-set-variable -Wno-unreachable-code \
		-Icore -I../third_party/dbopl -c $< -o $@

host: $(CORE_OBJECTS) $(BUILD)/host/main.o
	$(CXX) $(CORE_OBJECTS) $(BUILD)/host/main.o -lm -o $(BUILD)/lemmings-host
	$(BUILD)/lemmings-host "$(ASSET_PACK)"

test: $(CORE_OBJECTS) $(BUILD)/tests/test_game.o $(BUILD)/tests/test_adlib.o \
		$(BUILD)/tests/test_dbopl.o $(BUILD)/tests/test_progress.o
	$(CXX) $(CORE_OBJECTS) $(BUILD)/tests/test_game.o -lm -o $(BUILD)/test-game
	$(BUILD)/test-game
	$(CC) $(CFLAGS) $(BUILD)/core/lp_progress.o $(BUILD)/tests/test_progress.o \
		-o $(BUILD)/test-progress
	$(BUILD)/test-progress
	$(CXX) $(CORE_OBJECTS) $(BUILD)/tests/test_adlib.o -lm -o $(BUILD)/test-adlib
	cd .. && $(PYTHON) -m tools.audio.trace --adlib reference/lemming1.pc/adlib.dat --track music:5 --steps 4609 --out playdate/$(BUILD)/music5.lpr
	$(BUILD)/test-adlib "$(ADLIB_IMAGE)" $(BUILD)/music5.lpr
	$(CXX) $(BUILD)/core/lp_dbopl.o $(BUILD)/core/dbopl_core.o \
		$(BUILD)/tests/test_dbopl.o -lm -o $(BUILD)/test-dbopl
	cd .. && $(PYTHON) -m tools.audio.trace --adlib reference/lemming1.pc/adlib.dat \
		--track music:5 --samples 49716 --rate 49716 --out playdate/$(BUILD)/music5-1s.lpr
	$(BUILD)/test-dbopl $(BUILD)/music5-1s.lpr
	for track in $$(seq 0 20); do \
		cd .. && $(PYTHON) -m tools.audio.trace --adlib reference/lemming1.pc/adlib.dat \
			--track music:$$track --samples 49716 --rate 49716 \
			--out playdate/$(BUILD)/music-smoke.lpr >/dev/null && \
		cd playdate && $(BUILD)/test-dbopl $(BUILD)/music-smoke.lpr --smoke || exit 1; \
	done

stage:
	mkdir -p Source/assets
	cp "$(ASSET_PACK)" Source/assets/lemmings.lpd
	mkdir -p Source/audio
	cp "$(ADLIB_IMAGE)" Source/audio/adlib.bin
	mkdir -p Source/licenses
	cp ../third_party/dbopl/COPYING Source/licenses/DBOPL-GPL-2.0-or-later.txt
	rm -rf Source/fonts
	mkdir -p Source/fonts
	cp "$(FONT_RESOURCES)/Cuberick/font-Cuberick-Bold.fnt" Source/fonts/
	cp "$(FONT_RESOURCES)/Cuberick/font-Cuberick-Bold-table-12-15.png" Source/fonts/
	rm -f Source/licenses/Nuked-OPL3-LGPL-2.1.txt
	rm -rf Source/music
	rm -rf Source/sfx
	rm -rf Source/effects
	test -d "$(EFFECTS_DIR)" || (echo "Missing effects directory, run make effects with EFFECTS_ARCHIVE: $(EFFECTS_DIR)"; exit 2)
	mkdir -p Source/effects
	cp "$(EFFECTS_DIR)"/*.wav Source/effects/
	rm -f "$(TEST_MARKER)"
ifeq ($(TEST_UNLOCK),1)
	printf 'Test build: every level is selectable and progress is never saved.\nThis marker keeps the build out of a published package.\n' > "$(TEST_MARKER)"
endif

# The SDK object tree carries no record of the flags it was built with, so a
# TEST_UNLOCK change must discard it. Otherwise a release build could silently
# reuse objects compiled with the test flag, or the reverse.
check-test-flag:
	@mkdir -p build
	@if [ "$$(cat build/.test-unlock 2>/dev/null)" != "$(TEST_UNLOCK)" ]; then \
		echo "TEST_UNLOCK=$(TEST_UNLOCK) differs from the last build; clearing SDK objects"; \
		rm -rf build; mkdir -p build; \
	fi
	@echo "$(TEST_UNLOCK)" > build/.test-unlock

playdate: check-test-flag stage
	rm -f Source/pdex.dylib Source/pdex.so Source/pdex.dll
	$(MAKE) -f Playdate.mk device PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" PRODUCT="$(PRODUCT_PATH)" UDEFS="$(UDEFS)"

simulator: check-test-flag stage
	rm -f Source/pdex.bin Source/pdex.elf
	rm -f "$(PRODUCT_PATH)/pdex.bin"
	$(MAKE) -f Playdate.mk simulator PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" PRODUCT="$(PRODUCT_PATH)" UDEFS="$(UDEFS)"

release:
	$(PYTHON) ../tools/bump_playdate_version.py Source/pdxinfo
	$(MAKE) playdate PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" PRODUCT_PATH="$(PRODUCT_PATH)" ASSET_PACK="$(ASSET_PACK)"
	cd "$(dir $(abspath $(PRODUCT_PATH)))" && zip -qrFS Lemmings.pdx.zip Lemmings.pdx

clean:
	rm -rf $(BUILD) Source/assets
