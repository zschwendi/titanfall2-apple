# A signed app bundle around one of the runtime's programs (issue 42; round-3 plan, 2.3).
#
# The kernel grants the x86 memory-ordering mode (thread_set_x86_64_compat) only to a program
# whose signature carries com.apple.developer.cross-architecture-support, and that entitlement
# is honoured only from an app bundle with a development provisioning profile embedded.  This
# wraps an already built program in such a bundle and signs it.
#
#     make app-sign APP_BIN=build/gate2/gate2        build/app/TFRecomp.app, signed
#     make app-run  APP_BIN=build/gate2/gate2 APP_ARGS="call 29"
#     make app-check                                 does the bundle get the mode? (exit 0: yes)
#
# Two ways to sign (APP_SIGN, below).  With a development identity (dev) the bundle carries the
# entitlement and an embedded profile, and the kernel grants the mode.  Ad hoc (adhoc) needs no
# Apple account; the lifted code must then order its own memory accesses (GUEST_MEM=tsob).
# Nothing here talks to Apple: the App ID, the entitlement and the profile are made beforehand in
# the developer account (app/signing.example.mk).
#
#     make app-sign APP_BIN=build/dxmt/load_probe APP_LIBS="build/dxmt/Frameworks/winemetal.dylib ..."
#                                                    the same, with libraries in Contents/Frameworks,
#                                                    each signed with the same identity before the bundle
#     make app-dxmt-check                            do DXMT's three libraries load in the bundle with the
#                                                    mode on? (picture plan, step 0c; exit 0: yes)
#
# Standalone too:  make -f app/app.mk app-sign APP_BIN=...

APP_BIN      ?= build/gate3/gate3
APP_ARGS     ?=
APP_LIBS     ?=
# APP_UI=1: a normal app with a window and a Dock icon (the owner's decision O5 of 11 October: the
# bundle becomes a normal app and keeps the probe's App ID); default: a background program (LSUIElement).
APP_UI       ?= 0
APP_DIR      ?= build/app/TFRecomp.app
# The signing identity.  APP_SIGN=dev (when APP_TEAM is set): an Apple Development identity and a
# development provisioning profile whose App ID carries com.apple.developer.cross-architecture-support
# (the paid Apple Developer Program); the program can then ask for the x86 memory-ordering mode
# (RT_TSO=require).  APP_SIGN=free: the same with com.apple.developer.cross-architecture-support-unmanaged,
# which a FREE Apple ID's Personal Team may use (untested so far); tools/signing/make_profile.sh gets the
# profile through Xcode's automatic signing and writes app/signing.local.mk.  APP_SIGN=adhoc (the
# default without a team): an ad-hoc signature, no Apple account at all; the mode is not available,
# so build the lifted code with software ordering instead (make gate3-build GUEST_MEM=tsob,
# APP_BIN=build/gate3-tsob/gate3, and run with RT_TSO=off).  Each Mac's own values go in
# app/signing.local.mk (git-ignored; app/signing.example.mk shows the three lines).
-include app/signing.local.mk
APP_ID       ?= local.tfrecomp.player
APP_TEAM     ?=
APP_PROFILE  ?=
APP_SIGN     ?= $(if $(APP_TEAM),dev,adhoc)
APP_XARCH    := com.apple.developer.cross-architecture-support$(if $(filter free,$(APP_SIGN)),-unmanaged)
# The identity is looked up by kind, so no account name is written in the repository.
ifeq ($(APP_SIGN),adhoc)
APP_IDENTITY := -
else
APP_IDENTITY ?= $(shell security find-identity -v -p codesigning | awk '/Apple Development/ {print $$2; exit}')
endif
APP_EXE      := $(APP_DIR)/Contents/MacOS/$(notdir $(APP_BIN))
APP_ENT      := $(dir $(APP_DIR))entitlements.plist

.PHONY: app-sign app-run app-check app-dxmt-check app-game

app-sign:
	@test -x "$(APP_BIN)" || { echo "app-sign: no program at $(APP_BIN) (build it first)"; exit 1; }
	@case "$(APP_SIGN)" in dev|free|adhoc) ;; *) echo "app-sign: APP_SIGN is dev, free or adhoc"; exit 1;; esac
	@test "$(APP_SIGN)" = adhoc || test -f "$(APP_PROFILE)" || { echo "app-sign: no profile at $(APP_PROFILE)"; \
	  echo "  (APP_PROFILE in app/signing.local.mk; or APP_SIGN=adhoc with a GUEST_MEM=tsob build)"; exit 1; }
	@test -n "$(APP_IDENTITY)" || { echo "app-sign: no 'Apple Development' identity in the keychain"; exit 1; }
	@rm -rf "$(APP_DIR)" && mkdir -p "$(APP_DIR)/Contents/MacOS"
	@cp "$(APP_BIN)" "$(APP_EXE)"
	@test "$(APP_SIGN)" = adhoc || cp "$(APP_PROFILE)" "$(APP_DIR)/Contents/embedded.provisionprofile"
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  '<key>CFBundleIdentifier</key><string>$(APP_ID)</string>' \
	  '<key>CFBundleName</key><string>TFRecomp</string>' \
	  '<key>CFBundleExecutable</key><string>$(notdir $(APP_BIN))</string>' \
	  '<key>CFBundlePackageType</key><string>APPL</string>' \
	  '<key>CFBundleVersion</key><string>1</string>' \
	  $(if $(filter 1,$(APP_UI)),,'<key>LSUIElement</key><true/>') \
	  '</dict></plist>' > "$(APP_DIR)/Contents/Info.plist"
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  $(if $(filter dev free,$(APP_SIGN)),'<key>com.apple.application-identifier</key><string>$(APP_TEAM).$(APP_ID)</string>' \
	  '<key>com.apple.developer.team-identifier</key><string>$(APP_TEAM)</string>' \
	  '<key>$(APP_XARCH)</key><true/>') \
	  '<key>com.apple.security.get-task-allow</key><true/>' \
	  '</dict></plist>' > "$(APP_ENT)"
	@set -e; for l in $(APP_LIBS); do \
	  test -f "$$l" || { echo "app-sign: no library at $$l"; exit 1; }; \
	  mkdir -p "$(APP_DIR)/Contents/Frameworks"; cp "$$l" "$(APP_DIR)/Contents/Frameworks/"; \
	  codesign --force --sign "$(APP_IDENTITY)" --timestamp=none "$(APP_DIR)/Contents/Frameworks/$$(basename $$l)"; \
	done
	@codesign --force --sign "$(APP_IDENTITY)" --entitlements "$(APP_ENT)" --timestamp=none "$(APP_DIR)"
	@codesign --verify --strict "$(APP_DIR)" && echo "app-sign: $(APP_EXE) signed"

app-run: app-sign
	RT_TSO=require "$(APP_EXE)" $(APP_ARGS)

# The mode must be granted inside the bundle (RT_TSO=require is fatal otherwise).
app-check: app-sign
	@RT_TSO=require "$(APP_EXE)" $(or $(APP_ARGS),registry) > build/app/check.log 2>&1 \
	  && echo "app-check: ok, x86 memory ordering granted in the bundle" \
	  || { echo "app-check: FAILED"; tail -5 build/app/check.log; exit 1; }

# Picture plan, step 0c: the probe of tools/dxmt (built by `DXMT_BUILD_KIND=assert sh
# tools/dxmt/load_probe.sh`) as the bundle's program, DXMT's three libraries beside it.  It asks
# for the mode on its own thread, loads the libraries from Contents/Frameworks, creates a device
# and one buffer.  Exit 3: the mode was refused; exit 2: a library did not load (library validation).
DXMT_STAGE ?= build/dxmt
app-dxmt-check:
	@$(MAKE) -f app/app.mk app-sign APP_BIN=$(DXMT_STAGE)/load_probe \
	  APP_LIBS="$(DXMT_STAGE)/Frameworks/winemetal.dylib $(DXMT_STAGE)/Frameworks/dxgi.dylib $(DXMT_STAGE)/Frameworks/d3d11.dylib"
	@codesign --verify --strict --deep "$(APP_DIR)"
	@"$(APP_DIR)/Contents/MacOS/load_probe" --tso > build/app/dxmt-check.log 2>&1; st=$$?; \
	  tail -3 build/app/dxmt-check.log; \
	  [ $$st -eq 0 ] && echo "app-dxmt-check: ok, the three libraries load in the bundle with the mode on" \
	  || { echo "app-dxmt-check: FAILED, exit $$st"; exit 1; }

# The game's driver in the bundle with DXMT's three libraries, as a normal app (APP_UI=1).  Signs only;
# it starts nothing.  How to run it is printed.  Build first: make gate3-build, and
# DXMT_BUILD_KIND=assert sh tools/dxmt/load_probe.sh (it stages the libraries under build/dxmt).
# Signed ad hoc, the bundle takes the GUEST_MEM=tsob build (it cannot have the hardware mode).
GAME_BIN ?= $(if $(filter adhoc,$(APP_SIGN)),build/gate3-tsob/gate3,build/gate3/gate3)
GAME_TSO := $(if $(filter adhoc,$(APP_SIGN)),off,require)
app-game:
	@$(MAKE) -f app/app.mk app-sign APP_BIN=$(GAME_BIN) APP_UI=1 \
	  APP_LIBS="$(DXMT_STAGE)/Frameworks/winemetal.dylib $(DXMT_STAGE)/Frameworks/dxgi.dylib $(DXMT_STAGE)/Frameworks/d3d11.dylib"
	@codesign --verify --strict --deep "$(APP_DIR)" && echo "app-game: signed; run from the checkout's root:"
	@echo "  RT_TSO=$(GAME_TSO) RT_D3D_BACKEND=dxmt RT_DXMT_DIR=$(abspath $(APP_DIR))/Contents/Frameworks RT_HOST_WINDOW=appkit GATE3_TIMEOUT=3600 GATE3_START=save:$(abspath build/gate3/saves/mission-600) GATE3_INPUT_SCRIPT=gate3/synthetic/continue.script GATE3_RUN_OUT=build/gate3/play $(APP_DIR)/Contents/MacOS/gate3 run"
