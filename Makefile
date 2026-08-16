PYTHON ?= python3
CC ?= cc
HOST_OS := $(shell uname -s)
DOS_DIR ?= reference/lemming1.pc
ASSET_OUT ?= generated/lemmings.lpd
# macOS keeps the hand-installed SDK; elsewhere `make playdate-sdk` provides a
# pinned copy under build/reference. Either default is overridable.
PLAYDATE_SDK_PATH ?= $(if $(filter Darwin,$(HOST_OS)),$(HOME)/PlaydateSDK,$(CURDIR)/build/reference/playdate-sdk)
PLAYDATE_RUN_ENV ?= $(if $(filter Linux,$(HOST_OS)),GDK_BACKEND=x11,)
ARM_TOOLCHAIN_PATH ?=
EFFECTS_ARCHIVE ?=
EFFECTS_DIR ?= playdate/effects
EFFECT_NAMES = chain changeop door electric explode fire glug letsgo mousepre ohno splat tenton thud ting yippee

.PHONY: verify-reference assets effects test host audio decompile playdate-sdk playdate playdate-test simulator simulator-test playdate-run release clean

verify-reference:
	$(PYTHON) preservation/verify_reference.py

assets: verify-reference
	$(PYTHON) -m tools.assetc build --dos-dir "$(DOS_DIR)" --out "$(ASSET_OUT)"

effects:
	@if [ -n "$(EFFECTS_ARCHIVE)" ]; then \
		$(PYTHON) -m tools.audio.import_effects "$(EFFECTS_ARCHIVE)" --out "$(EFFECTS_DIR)"; \
	fi
	@for name in $(EFFECT_NAMES); do \
		test -f "$(EFFECTS_DIR)/$$name.wav" || \
			(echo "Missing effect, run make effects with EFFECTS_ARCHIVE: $(EFFECTS_DIR)/$$name.wav"; exit 2); \
	done

test: assets
	$(PYTHON) -m unittest discover -s tools/assetc/tests -v
	$(PYTHON) -m unittest discover -s tools/audio/tests -v
	$(MAKE) -C playdate test

host: assets
	$(MAKE) -C playdate host ASSET_PACK="$(abspath $(ASSET_OUT))"

audio:
	@test -n "$(REFERENCE_PLAYER)" || (echo "Set REFERENCE_PLAYER=/path/to/lemmings.js"; exit 2)
	$(PYTHON) -m tools.audio.render --dos-dir "$(DOS_DIR)" --reference-bundle "$(REFERENCE_PLAYER)" --out generated/audio

decompile: verify-reference
	$(PYTHON) preservation/prepare_decomp.py

playdate-sdk:
	PLAYDATE_SDK_ROOT="$(PLAYDATE_SDK_PATH)" sh scripts/ensure_playdate_sdk.sh

playdate: assets effects playdate-sdk
	$(MAKE) -C playdate PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" ARM_TOOLCHAIN_PATH="$(ARM_TOOLCHAIN_PATH)" PRODUCT_PATH="$(abspath generated/Lemmings.pdx)"
	cd generated && zip -qrFS Lemmings.pdx.zip Lemmings.pdx
	@echo "Sideload generated/Lemmings.pdx.zip"

simulator: assets effects playdate-sdk
	$(MAKE) -C playdate simulator PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" ARM_TOOLCHAIN_PATH="$(ARM_TOOLCHAIN_PATH)" PRODUCT_PATH="$(abspath generated/Lemmings.pdx)"

playdate-test: assets effects playdate-sdk
	$(MAKE) -C playdate TEST_UNLOCK=1 PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" ARM_TOOLCHAIN_PATH="$(ARM_TOOLCHAIN_PATH)" PRODUCT_PATH="$(abspath generated/Lemmings-test.pdx)"
	# A test build previews the next patch release, so it carries that version
	# rather than the released one it was built from. Only the copy inside the
	# bundle is advanced; playdate/Source/pdxinfo is left for make release.
	$(PYTHON) tools/bump_playdate_version.py generated/Lemmings-test.pdx/pdxinfo
	cd generated && zip -qrFS Lemmings-test.pdx.zip Lemmings-test.pdx
	@echo "Sideload generated/Lemmings-test.pdx.zip"

simulator-test: assets effects playdate-sdk
	$(MAKE) -C playdate simulator TEST_UNLOCK=1 PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" ARM_TOOLCHAIN_PATH="$(ARM_TOOLCHAIN_PATH)" PRODUCT_PATH="$(abspath generated/Lemmings-test.pdx)"

playdate-run: simulator
	$(PLAYDATE_RUN_ENV) "$(PLAYDATE_SDK_PATH)/bin/PlaydateSimulator" "$(abspath generated/Lemmings.pdx)"

release: assets effects playdate-sdk
	$(MAKE) -C playdate release PLAYDATE_SDK_PATH="$(PLAYDATE_SDK_PATH)" ARM_TOOLCHAIN_PATH="$(ARM_TOOLCHAIN_PATH)" PRODUCT_PATH="$(abspath generated/Lemmings.pdx)"


clean:
	$(MAKE) -C playdate clean
