# SporeCoop build (Linux host, llvm-mingw cross toolchain, 32-bit Windows targets)
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
CC  := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang
CXX := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang++
HOSTCXX ?= clang++

B := build
COMMON  := -O2 -g -Wall -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE
CFLAGS  := $(COMMON)
CXXFLAGS:= $(COMMON) -std=c++20
LDDLL   := -shared -static -Wl,--kill-at -Wl,--enable-stdcall-fixup

MINHOOK := third_party/minhook
MH_SRC  := $(MINHOOK)/src/hook.c $(MINHOOK)/src/buffer.c $(MINHOOK)/src/trampoline.c $(MINHOOK)/src/hde/hde32.c
MH_OBJ  := $(patsubst %.c,$(B)/obj/%.o,$(MH_SRC))

KIT_SRC := $(wildcard sporekit/*.cpp)
KIT_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(KIT_SRC))

COOP_SRC := $(wildcard mods/coop/*.cpp) $(wildcard mods/coop/net/*.cpp) $(wildcard updater/*.cpp)
COOP_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(COOP_SRC))

.PHONY: all loader sporekit coop res setup clean test test-captains-windows test-lifecycle-windows test-actions-windows test-vehicles-windows test-mirror-windows test-progress-windows test-relationships-windows test-targeting-windows test-social-windows test-blocked-update-windows test-debug-capabilities-windows
all: loader sporekit coop res setup

# game resources (icons, ...) for SporeCoop: Data/SporeCoop.package
UI_PNG := $(wildcard data/ui/*.png)
res: $(B)/SporeCoop.package
$(B)/SporeCoop.package: tools/mkpackage.py $(UI_PNG)
	@mkdir -p $(B)
	python3 tools/mkpackage.py $@ $(foreach f,$(UI_PNG),sporecoop!$(basename $(notdir $(f))).png=$(f))

loader: $(B)/steam_api.dll
sporekit: $(B)/sporekit.dll
coop: $(B)/coop.dll

$(B)/steam_api.dll: loader/loader.c loader/steam_api.def
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -shared -o $@ loader/loader.c loader/steam_api.def -Wl,--kill-at

$(B)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(MINHOOK)/include -c $< -o $@

$(B)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(MINHOOK)/include -Isdk/include -c $< -o $@ -MMD -MP

$(B)/sporekit.dll: $(KIT_OBJ) $(MH_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,$(B)/libsporekit.a

$(B)/coop.dll: $(COOP_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32 -lole32

# SporeCoopSetup.exe: install / update / uninstall (installer/, updater/)
WINDRES := $(LLVM_MINGW)/bin/i686-w64-mingw32-windres
COOP_VERSION = $(shell sed -n 's/^#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp)
setup: $(B)/SporeCoopSetup.exe
$(B)/setup.ico: data/ui/coop_icon.png tools/make_ico.py
	@mkdir -p $(B)
	python3 tools/make_ico.py $< $@
$(B)/obj/installer/setup_rc.o: installer/setup.rc installer/setup.manifest $(B)/setup.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -i installer/setup.rc -o $@ --include-dir installer
$(B)/SporeCoopSetup.exe: installer/setup.cpp updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp $(B)/obj/installer/setup_rc.o mods/coop/coop.cpp
	$(CXX) $(CXXFLAGS) -DSETUP_VERSION='"$(COOP_VERSION)"' -municode -mwindows -static -o $@ $(filter-out mods/coop/coop.cpp,$^) \
		-lwinhttp -lcomctl32 -lshell32 -lole32 -ladvapi32 -luser32 -lgdi32

# Portable editor transaction, durability, transport and owned-resource tests.
EDITOR_CORE_SRC := mods/coop/net/editor_document.cpp
SESSION_TRANSPORT_SRC := mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp
EDITOR_WIRE_SRC := mods/coop/net/editor_protocol.cpp $(EDITOR_CORE_SRC) updater/sha256.cpp
EDITOR_DELTA_SRC := mods/coop/net/editor_binary_delta.cpp updater/sha256.cpp
EDITOR_PROJECT_SRC := mods/coop/editor_project.cpp mods/coop/net/editor_binary_delta.cpp mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp $(EDITOR_WIRE_SRC)
EDITOR_SESSION_SRC := mods/coop/editor_session.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp $(EDITOR_WIRE_SRC)
EDITOR_RUNTIME_FIXTURE_SRC := tests/editor_runtime_windows_tests.cpp mods/coop/editor_store.cpp mods/coop/editor_project.cpp mods/coop/net/editor_binary_delta.cpp mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp $(EDITOR_SESSION_SRC)
PACKAGE_ADMISSION_SRC := mods/coop/package_admission.cpp
PACKAGE_ADMISSION_WINDOWS_SRC := $(PACKAGE_ADMISSION_SRC) mods/coop/package_admission_windows.cpp
ADVENTURE_ASSETS_NATIVE_FIXTURE_SRC := $(PACKAGE_ADMISSION_WINDOWS_SRC) tests/adventure_assets_native_windows_tests.cpp mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp updater/sha256.cpp
ADVENTURE_ABI_HEADERS := sdk/include/sporekit.h sdk/include/sporekit_game.hpp sdk/include/sporekit_build.hpp mods/coop/game.h mods/coop/net/protocol.h
.PHONY: test-editor test-editor-windows editor-portable-deps-missing
# The former inline portable builds are independent binaries. Capture all
# translation units in one stdout dependency pass; multi-input -MMD/-MF would
# overwrite earlier units' headers. Publish .d only after compilation succeeds.
define editor_portable_compile
@mkdir -p $(dir $@)
@set -eu; dep="$@.d.tmp"; \
trap 'rm -f "$$dep" "$@"' 0 1 2 3 15; \
$(HOSTCXX) $(1) -MM -MP -MT "$@" $(2) > "$$dep"; \
$(HOSTCXX) $(1) $(2) -o "$@"; \
mv -f "$$dep" "$@.d"; \
trap - 0 1 2 3 15
endef
define editor_portable_binary
$(B)/host/$(1): $(3) Makefile
	$$(call editor_portable_compile,$(2),$(3))
endef
EDITOR_INLINE_TEST_NAMES := editor_cast_tests editor_metrics_tests editor_stage_metrics_tests editor_authoring_extensions_tests editor_creation_png_carrier_tests editor_creation_zlib_tests editor_native_bindings_tests editor_native_pose_tests editor_native_terrain_tests editor_native_scale_tests editor_reopen_identity_tests editor_binary_delta_tests editor_document_tests editor_store_tests editor_protocol_tests editor_project_tests editor_session_tests editor_accounting_tests session_transport_tests
EDITOR_INLINE_TEST_BINS := $(addprefix $(B)/host/,$(EDITOR_INLINE_TEST_NAMES))
$(eval $(call editor_portable_binary,editor_cast_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,$(EDITOR_WIRE_SRC) tests/editor_cast_tests.cpp))
$(eval $(call editor_portable_binary,editor_metrics_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,tests/editor_metrics_tests.cpp))
$(eval $(call editor_portable_binary,editor_stage_metrics_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,tests/editor_stage_metrics_tests.cpp))
$(eval $(call editor_portable_binary,editor_authoring_extensions_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,$(EDITOR_WIRE_SRC) tests/editor_authoring_extensions_tests.cpp))
$(eval $(call editor_portable_binary,editor_creation_png_carrier_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror,tests/editor_creation_png_carrier_tests.cpp))
$(eval $(call editor_portable_binary,editor_creation_zlib_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror,tests/editor_creation_zlib_tests.cpp))
$(eval $(call editor_portable_binary,editor_native_bindings_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,tests/editor_native_bindings_tests.cpp))
$(eval $(call editor_portable_binary,editor_native_pose_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_CORE_SRC) tests/editor_native_pose_tests.cpp))
$(eval $(call editor_portable_binary,editor_native_terrain_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_CORE_SRC) tests/editor_native_terrain_tests.cpp))
$(eval $(call editor_portable_binary,editor_native_scale_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,$(EDITOR_WIRE_SRC) tests/editor_native_scale_tests.cpp))
$(eval $(call editor_portable_binary,editor_reopen_identity_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,tests/editor_reopen_identity_tests.cpp))
$(eval $(call editor_portable_binary,editor_binary_delta_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_DELTA_SRC) tests/editor_binary_delta_tests.cpp))
$(eval $(call editor_portable_binary,editor_document_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,$(EDITOR_CORE_SRC) tests/editor_document_tests.cpp))
$(eval $(call editor_portable_binary,editor_store_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,mods/coop/editor_store.cpp $(EDITOR_DELTA_SRC) tests/editor_store_tests.cpp))
$(eval $(call editor_portable_binary,editor_protocol_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_WIRE_SRC) tests/editor_protocol_tests.cpp))
$(eval $(call editor_portable_binary,editor_project_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include,$(EDITOR_PROJECT_SRC) tests/editor_project_tests.cpp))
$(eval $(call editor_portable_binary,editor_session_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_SESSION_SRC) tests/editor_session_tests.cpp))
$(eval $(call editor_portable_binary,editor_accounting_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(EDITOR_SESSION_SRC) tests/editor_accounting_tests.cpp))
$(eval $(call editor_portable_binary,session_transport_tests,-std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop,$(SESSION_TRANSPORT_SRC) tests/session_transport_tests.cpp))
# Existing binaries from the old recipe need a first dependency-producing
# rebuild too. This phony prerequisite disappears once the .d is present.
editor-portable-deps-missing:
$(foreach binary,$(EDITOR_INLINE_TEST_BINS),$(eval $(binary): $(if $(wildcard $(binary).d),,editor-portable-deps-missing)))
-include $(wildcard $(EDITOR_INLINE_TEST_BINS:=.d))
test-editor: $(B)/host/editor_native_terrain_rotations_tests $(B)/host/editor_native_complexity_ratio_tests $(EDITOR_INLINE_TEST_BINS) test-editor-pulse
	$(B)/host/editor_native_terrain_rotations_tests
	$(B)/host/editor_native_complexity_ratio_tests
	$(B)/host/editor_cast_tests
	$(B)/host/editor_metrics_tests
	$(B)/host/editor_stage_metrics_tests
	$(B)/host/editor_authoring_extensions_tests
	$(B)/host/editor_creation_png_carrier_tests
	$(B)/host/editor_creation_zlib_tests
	$(B)/host/editor_native_bindings_tests
	$(B)/host/editor_native_pose_tests
	$(B)/host/editor_native_terrain_tests
	$(B)/host/editor_native_scale_tests
	$(B)/host/editor_reopen_identity_tests
	$(B)/host/editor_binary_delta_tests
	$(B)/host/editor_document_tests
	$(B)/host/editor_store_tests
	$(B)/host/editor_protocol_tests
	$(B)/host/editor_project_tests
	$(B)/host/editor_session_tests
	$(B)/host/editor_accounting_tests
	$(B)/host/session_transport_tests

test-editor-windows: $(B)/host/package_admission_tests.exe $(B)/host/package_admission_windows_tests.exe $(B)/host/frame_metrics_tests.exe $(B)/host/editor_metrics_tests.exe $(B)/host/editor_native_bindings_tests.exe $(B)/host/editor_native_scale_tests.exe $(B)/host/editor_reopen_identity_tests.exe $(B)/host/editor_binary_delta_tests.exe $(B)/host/editor_document_tests.exe $(B)/host/editor_store_tests.exe $(B)/host/editor_protocol_tests.exe $(B)/host/editor_project_tests.exe $(B)/host/editor_session_tests.exe $(B)/host/session_transport_tests.exe $(B)/host/editor_native_save_tests.exe $(B)/host/adventure_editor_preview_tests.exe $(B)/host/editor_runtime_windows_tests.exe $(B)/host/adventure_assets_native_tests.exe

test-editor-windows: $(B)/host/editor_native_cancellation_tests.exe
test-editor-windows: $(B)/host/editor_cast_tests.exe
$(B)/host/editor_cast_tests.exe: tests/editor_cast_tests.cpp mods/coop/editor_native_cast.h mods/coop/editor_native_scale.h mods/coop/editor_native_authoring.h $(EDITOR_WIRE_SRC) mods/coop/net/editor_document.h mods/coop/net/editor_protocol.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(EDITOR_WIRE_SRC) tests/editor_cast_tests.cpp -o $@

test-editor-windows: $(B)/host/editor_stage_metrics_tests.exe
test-editor-windows: $(B)/host/editor_authoring_extensions_tests.exe $(B)/host/editor_creation_tests.exe $(B)/host/editor_creation_ui_tests.exe
test-editor-windows: $(B)/host/editor_creation_png_carrier_tests.exe $(B)/host/editor_creation_png_tests.exe $(B)/host/editor_creation_zlib_tests.exe

$(B)/host/editor_creation_zlib_tests.exe: tests/editor_creation_zlib_tests.cpp mods/coop/editor_creation_zlib.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static tests/editor_creation_zlib_tests.cpp -o $@

$(B)/host/editor_creation_png_carrier_tests.exe: tests/editor_creation_png_carrier_tests.cpp mods/coop/editor_creation_png_carrier.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static tests/editor_creation_png_carrier_tests.cpp -o $@

$(B)/host/editor_creation_png_tests.exe: tests/editor_creation_png_windows_tests.cpp mods/coop/editor_creation_png.cpp mods/coop/editor_creation_png.h mods/coop/editor_creation_png_carrier.h mods/coop/editor_creation_zlib.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_creation_png_windows_tests.cpp mods/coop/editor_creation_png.cpp -lole32 -o $@

$(B)/host/editor_creation_ui_tests.exe: tests/editor_creation_ui_windows_tests.cpp mods/coop/editor_creation_ui.cpp mods/coop/editor_creation_ui.h mods/coop/editor_creation.h mods/coop/editor_runtime.h mods/coop/coop_ui.h mods/coop/sui.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-unused-function -static -Imods/coop -Isdk/include tests/editor_creation_ui_windows_tests.cpp -o $@
test-editor-windows: $(B)/host/editor_native_pose_tests.exe
test-editor-windows: $(B)/host/editor_native_terrain_tests.exe $(B)/host/editor_native_complexity_tests.exe
test-editor-windows: $(B)/host/adventure_collect_tests.exe

$(B)/host/adventure_collect_tests.exe: tests/adventure_collect_windows_tests.cpp mods/coop/adventure_collect.cpp mods/coop/adventure_collect.h mods/coop/editor_saved_dependencies.h mods/coop/adventure_music.cpp mods/coop/adventure_music.h mods/coop/net/dbpf.cpp updater/sha256.cpp $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-unused-function -static -Imods/coop -Isdk/include tests/adventure_collect_windows_tests.cpp mods/coop/adventure_music.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp -o $@

$(B)/host/editor_native_cancellation_tests.exe: $(wildcard mods/coop/editor_native*.h) mods/coop/net/editor_terrain.h tests/editor_native_pose_prepare_fixture.h tests/editor_native_pose_prepare_staged_fixture.h mods/coop/editor_native_pose_prepare_staged_impl.h mods/coop/editor_native_pose_prepare_impl.h

# CPU-only Snapshot construction and all sidecar copy domains; no Native calls.
.PHONY: test-editor-snapshot-values-windows
test-editor-snapshot-values-windows: $(B)/host/editor_native_snapshot_values_tests.exe
test-editor-windows: $(B)/host/editor_native_snapshot_values_tests.exe
$(B)/host/editor_native_snapshot_values_tests.exe: tests/editor_native_snapshot_values_tests.cpp mods/coop/editor_native_snapshot_values.h mods/coop/editor_native.h mods/coop/net/editor_document.h mods/coop/net/editor_complexity.h mods/coop/editor_native_scale.h mods/coop/editor_native_cast.h mods/coop/editor_native_authoring.h mods/coop/editor_native_terrain.h mods/coop/net/editor_terrain.h sdk/include/sporekit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $< -o $@

$(B)/host/editor_native_terrain_tests.exe: $(EDITOR_CORE_SRC) tests/editor_native_terrain_tests.cpp mods/coop/editor_native_terrain.h mods/coop/editor_native_terrain_materialization.h mods/coop/net/editor_terrain.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(EDITOR_CORE_SRC) tests/editor_native_terrain_tests.cpp -o $@

$(B)/host/editor_native_complexity_tests.exe: tests/editor_native_complexity_windows_tests.cpp mods/coop/editor_native_complexity.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/editor_native_complexity_windows_tests.cpp -o $@

test-editor-windows: $(B)/host/editor_native_terrain_rotations_tests.exe $(B)/host/editor_native_complexity_ratio_tests.exe
$(B)/host/editor_native_terrain_rotations_tests: tests/editor_native_terrain_rotations_tests.cpp mods/coop/editor_native_terrain_rotations.h
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop tests/editor_native_terrain_rotations_tests.cpp -o $@
$(B)/host/editor_native_complexity_ratio_tests: tests/editor_native_complexity_ratio_tests.cpp mods/coop/editor_native_complexity_ratio.h
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop tests/editor_native_complexity_ratio_tests.cpp -o $@
$(B)/host/editor_native_terrain_rotations_tests.exe: tests/editor_native_terrain_rotations_tests.cpp mods/coop/editor_native_terrain_rotations.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_native_terrain_rotations_tests.cpp -o $@
$(B)/host/editor_native_complexity_ratio_tests.exe: tests/editor_native_complexity_ratio_tests.cpp mods/coop/editor_native_complexity_ratio.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_native_complexity_ratio_tests.cpp -o $@

# Inactive portable Native format/codec helpers, no game or Native API linkage.
EDITOR_REFPACK_SRC := mods/coop/editor_native_refpack.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp
EDITOR_POSE_FORMAT_SRC := mods/coop/editor_native_pose_format.cpp $(EDITOR_REFPACK_SRC)
EDITOR_POSE_FORMAT_DATA := $(wildcard tests/fixtures/editor_native_pose/*)
.PHONY: test-editor-pose-format
test-editor-pose-format: $(B)/host/editor_native_refpack_tests $(B)/host/editor_native_pose_format_tests
	$(B)/host/editor_native_refpack_tests
	$(B)/host/editor_native_pose_format_tests
test-editor: test-editor-pose-format
test-editor-windows: $(B)/host/editor_native_refpack_tests.exe $(B)/host/editor_native_pose_format_tests.exe
$(B)/host/editor_native_refpack_tests: tests/editor_native_refpack_tests.cpp mods/coop/editor_native_refpack.h $(EDITOR_REFPACK_SRC) $(EDITOR_POSE_FORMAT_DATA)
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O2 -g -Wall -Wextra -Werror -Imods/coop tests/editor_native_refpack_tests.cpp $(EDITOR_REFPACK_SRC) -o $@
$(B)/host/editor_native_pose_format_tests: tests/editor_native_pose_format_tests.cpp mods/coop/editor_native_pose_format.h mods/coop/editor_native_refpack.h mods/coop/net/editor_document.h $(EDITOR_POSE_FORMAT_SRC) $(EDITOR_POSE_FORMAT_DATA)
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O2 -g -Wall -Wextra -Werror -Imods/coop tests/editor_native_pose_format_tests.cpp $(EDITOR_POSE_FORMAT_SRC) -o $@
$(B)/host/editor_native_refpack_tests.exe: tests/editor_native_refpack_tests.cpp mods/coop/editor_native_refpack.h $(EDITOR_REFPACK_SRC) $(EDITOR_POSE_FORMAT_DATA)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_native_refpack_tests.cpp $(EDITOR_REFPACK_SRC) -o $@
$(B)/host/editor_native_pose_format_tests.exe: tests/editor_native_pose_format_tests.cpp mods/coop/editor_native_pose_format.h mods/coop/editor_native_refpack.h mods/coop/net/editor_document.h $(EDITOR_POSE_FORMAT_SRC) $(EDITOR_POSE_FORMAT_DATA)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_native_pose_format_tests.cpp $(EDITOR_POSE_FORMAT_SRC) -o $@

# Saved dependency values and immutable cache IO; no Native/Runtime linkage.
EDITOR_SAVED_DEPENDENCIES_SRC := mods/coop/editor_saved_dependencies.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp
EDITOR_SAVED_DEPENDENCIES_DATA := $(wildcard tests/fixtures/editor_saved_dependencies/*)
EDITOR_SAVED_DEPENDENCIES_HEADERS := mods/coop/editor_saved_dependencies.h tests/editor_saved_dependencies_fixture.h $(EDITOR_SAVED_DEPENDENCIES_DATA)
.PHONY: test-editor-saved-dependencies
test-editor-saved-dependencies: $(B)/host/editor_saved_dependencies_tests $(B)/host/editor_saved_dependencies_store_tests
	$(B)/host/editor_saved_dependencies_tests
	$(B)/host/editor_saved_dependencies_store_tests $(B)/saved-dependencies-tests
test-editor: test-editor-saved-dependencies
test-editor-windows: $(B)/host/editor_saved_dependencies_tests.exe $(B)/host/editor_saved_dependencies_store_tests.exe
$(B)/host/editor_saved_dependencies_tests: tests/editor_saved_dependencies_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) $(EDITOR_SAVED_DEPENDENCIES_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -g -Wall -Wextra -Werror -Imods/coop tests/editor_saved_dependencies_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) -o $@
$(B)/host/editor_saved_dependencies_store_tests: tests/editor_saved_dependencies_store_tests.cpp mods/coop/editor_saved_dependencies_store.cpp mods/coop/editor_saved_dependencies_store.h $(EDITOR_SAVED_DEPENDENCIES_SRC) $(EDITOR_SAVED_DEPENDENCIES_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -g -Wall -Wextra -Werror -pthread -Imods/coop tests/editor_saved_dependencies_store_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) -o $@
$(B)/host/editor_saved_dependencies_tests.exe: tests/editor_saved_dependencies_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) $(EDITOR_SAVED_DEPENDENCIES_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_saved_dependencies_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) -o $@
$(B)/host/editor_saved_dependencies_store_tests.exe: tests/editor_saved_dependencies_store_tests.cpp mods/coop/editor_saved_dependencies_store.cpp mods/coop/editor_saved_dependencies_store.h $(EDITOR_SAVED_DEPENDENCIES_SRC) $(EDITOR_SAVED_DEPENDENCIES_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_saved_dependencies_store_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) -o $@

# Real selected-Saved gate and codec, with controlled Native and filesystem endpoints.
.PHONY: test-editor-saved-native-windows
test-editor-saved-native-windows: $(B)/host/editor_saved_native_tests.exe
$(B)/host/editor_saved_native_tests.exe: tests/editor_saved_native_windows_tests.cpp mods/coop/editor_saved_native.cpp mods/coop/editor_saved_native.h mods/coop/editor_saved_dependencies_store.h mods/coop/editor_native_save.h mods/coop/editor_native.h mods/coop/adventure_collect.h mods/coop/adventure_assets.h $(EDITOR_SAVED_DEPENDENCIES_SRC) $(EDITOR_SAVED_DEPENDENCIES_HEADERS) $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-missing-field-initializers -static -Imods/coop -Isdk/include tests/editor_saved_native_windows_tests.cpp $(EDITOR_SAVED_DEPENDENCIES_SRC) -o $@

$(B)/host/editor_native_pose_tests.exe: $(EDITOR_CORE_SRC) tests/editor_native_pose_tests.cpp mods/coop/editor_native_pose.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(EDITOR_CORE_SRC) tests/editor_native_pose_tests.cpp -o $@

$(B)/host/editor_authoring_extensions_tests.exe: $(EDITOR_WIRE_SRC) tests/editor_authoring_extensions_tests.cpp mods/coop/net/editor_document.h mods/coop/net/editor_protocol.h mods/coop/net/editor_terrain.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(EDITOR_WIRE_SRC) tests/editor_authoring_extensions_tests.cpp -o $@

$(B)/host/editor_creation_tests.exe: tests/editor_creation_windows_tests.cpp mods/coop/editor_creation.cpp mods/coop/editor_creation.h mods/coop/editor_creation_ui.h mods/coop/editor_creation_native.cpp mods/coop/editor_creation_native.h mods/coop/editor_creation_png.cpp mods/coop/editor_creation_png.h mods/coop/editor_creation_png_carrier.h mods/coop/editor_creation_zlib.h mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-missing-field-initializers -static -Imods/coop -Isdk/include tests/editor_creation_windows_tests.cpp mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp -luser32 -lole32 -o $@

test-editor-windows: $(B)/host/editor_accounting_tests.exe

$(B)/host/editor_accounting_tests.exe: $(EDITOR_SESSION_SRC) tests/editor_accounting_tests.cpp mods/coop/editor_session.h mods/coop/net/session.h mods/coop/net/connection.h mods/coop/net/editor_protocol.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(EDITOR_SESSION_SRC) tests/editor_accounting_tests.cpp -o $@ -lws2_32

$(B)/host/editor_stage_metrics_tests.exe: tests/editor_stage_metrics_tests.cpp mods/coop/editor_stage_metrics.h mods/coop/net/editor_document.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_stage_metrics_tests.cpp -o $@

$(B)/host/editor_native_cancellation_tests.exe: tests/editor_native_cancellation_windows_tests.cpp tests/editor_native_service_pulse_fixture.h tests/editor_native_act_cache_fixture.h tests/editor_native_crew_assets_fixture.h tests/editor_native_recent_fixture.h tests/editor_native_pose_capture_fixture.h tests/editor_native_staged_capture_fixture.h tests/editor_native_terrain_materialization_fixture.h tests/editor_native_creation_grounding_fixture.h tests/editor_native_terrain_transition_fixture.h tests/editor_native_history_toolbar_fixture.h tests/editor_native_diagnostic_fixture.h tests/editor_native_view_fence_fixture.h tests/editor_native_water_fixture.h tests/editor_native_environment_fixture.h tests/editor_native_complexity_fixture.h mods/coop/editor_native_terrain_transition_impl.h mods/coop/editor_native.cpp mods/coop/editor_native.h mods/coop/editor_native_authoring.h mods/coop/editor_native_act_cache.h mods/coop/editor_native_scale.h mods/coop/editor_native_bindings.h mods/coop/editor_native_tools.h mods/coop/net/dbpf.cpp mods/coop/net/editor_document.cpp $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/editor_native_cancellation_windows_tests.cpp mods/coop/net/dbpf.cpp mods/coop/net/editor_document.cpp -o $@

$(B)/host/editor_metrics_tests.exe: tests/editor_metrics_tests.cpp mods/coop/editor_metrics.h mods/coop/net/editor_document.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/editor_metrics_tests.cpp -o $@
$(B)/host/frame_metrics_tests.exe: tests/frame_metrics_tests.cpp mods/coop/frame_metrics.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop tests/frame_metrics_tests.cpp -o $@
$(B)/host/editor_native_bindings_tests.exe: tests/editor_native_bindings_tests.cpp mods/coop/editor_native_bindings.h mods/coop/editor_native_tools.h mods/coop/editor_native.h mods/coop/editor_native_scale.h mods/coop/net/editor_document.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/editor_native_bindings_tests.cpp -o $@
$(B)/host/editor_native_scale_tests.exe: tests/editor_native_scale_tests.cpp mods/coop/editor_native_scale.h $(EDITOR_WIRE_SRC) mods/coop/net/editor_document.h mods/coop/net/editor_protocol.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(EDITOR_WIRE_SRC) tests/editor_native_scale_tests.cpp -o $@
$(B)/host/editor_reopen_identity_tests.exe: tests/editor_reopen_identity_tests.cpp mods/coop/editor_reopen_identity.h mods/coop/editor_native_bindings.h mods/coop/editor_native.h mods/coop/editor_native_scale.h mods/coop/net/editor_document.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/editor_reopen_identity_tests.cpp -o $@
$(B)/host/editor_binary_delta_tests.exe: $(EDITOR_DELTA_SRC) tests/editor_binary_delta_tests.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $^ -o $@
$(B)/host/editor_document_tests.exe: $(EDITOR_CORE_SRC) tests/editor_document_tests.cpp mods/coop/net/editor_document.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(filter %.cpp,$^) -o $@
$(B)/host/editor_store_tests.exe: mods/coop/editor_store.cpp $(EDITOR_DELTA_SRC) tests/editor_store_tests.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $^ -o $@
$(B)/host/editor_protocol_tests.exe: $(EDITOR_WIRE_SRC) tests/editor_protocol_tests.cpp mods/coop/net/editor_document.h mods/coop/net/editor_protocol.h mods/coop/net/editor_complexity.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(filter %.cpp,$^) -o $@
$(B)/host/editor_project_tests.exe: $(EDITOR_PROJECT_SRC) tests/editor_project_tests.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $^ -o $@
$(B)/host/editor_session_tests.exe: $(EDITOR_SESSION_SRC) tests/editor_session_tests.cpp tests/editor_complexity_session_fixture.h mods/coop/net/editor_document.h mods/coop/net/editor_protocol.h mods/coop/net/editor_complexity.h mods/coop/editor_session.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(filter %.cpp,$^) -o $@ -lws2_32
$(B)/host/session_transport_tests.exe: $(SESSION_TRANSPORT_SRC) tests/session_transport_tests.cpp mods/coop/net/session.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(SESSION_TRANSPORT_SRC) tests/session_transport_tests.cpp -o $@ -lws2_32
$(B)/host/editor_native_save_tests.exe: tests/editor_native_save_windows_tests.cpp mods/coop/editor_native_save.cpp mods/coop/editor_native_save.h mods/coop/editor_native.h mods/coop/editor_native_scale.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/editor_native_save_windows_tests.cpp -o $@
$(B)/host/adventure_editor_preview_tests.exe: tests/adventure_editor_preview_windows_tests.cpp mods/coop/adventure_native.cpp mods/coop/adventure_native.h mods/coop/editor_native.h mods/coop/editor_native_scale.h $(ADVENTURE_ABI_HEADERS) mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_editor_preview_windows_tests.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp -o $@ -lws2_32
$(B)/host/editor_runtime_windows_tests.exe: $(EDITOR_RUNTIME_FIXTURE_SRC) tests/editor_runtime_service_pulse_fixture.h tests/editor_runtime_pose_commit_fixture.h tests/editor_runtime_native_dirty_fixture.h mods/coop/editor_service_pulse.h mods/coop/editor_asset_diagnostic.h tests/editor_runtime_model_admission_fixture.h tests/editor_runtime_projection_scope_fixture.h tests/editor_runtime_terrain_transition_fixture.h tests/editor_runtime_capture_identity_fixture.h tests/editor_runtime_rejection_restore_fixture.h tests/editor_runtime_persistence_fixture.h tests/editor_runtime_complexity_fixture.h tests/editor_runtime_inverse_settlement_fixture.h tests/editor_runtime_staged_capture_fixture.h tests/editor_runtime_scene_alias_fixture.h tests/editor_runtime_completion_wake_fixture.h mods/coop/editor_runtime.cpp mods/coop/editor_runtime.h mods/coop/editor_metrics.h mods/coop/editor_stage_metrics.h mods/coop/editor_native.h mods/coop/editor_native_authoring.h mods/coop/editor_native_scale.h mods/coop/editor_reopen_identity.h mods/coop/editor_native_bindings.h mods/coop/editor_native_save.h mods/coop/adventure_native.h mods/coop/editor_store.h mods/coop/editor_project.h mods/coop/editor_placement_diagnostic.h mods/coop/editor_session.h $(wildcard mods/coop/net/editor_*.h) $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(EDITOR_RUNTIME_FIXTURE_SRC) -o $@ -lws2_32
$(B)/host/package_admission_tests.exe: $(PACKAGE_ADMISSION_SRC) tests/package_admission_tests.cpp mods/coop/package_admission.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(PACKAGE_ADMISSION_SRC) tests/package_admission_tests.cpp -o $@

$(B)/host/package_admission_windows_tests.exe: $(PACKAGE_ADMISSION_WINDOWS_SRC) tests/package_admission_windows_tests.cpp mods/coop/package_admission.h mods/coop/package_admission_windows.h updater/sha256.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(PACKAGE_ADMISSION_WINDOWS_SRC) updater/sha256.cpp tests/package_admission_windows_tests.cpp -o $@

$(B)/host/adventure_assets_native_tests.exe: $(ADVENTURE_ASSETS_NATIVE_FIXTURE_SRC) mods/coop/adventure_assets.cpp mods/coop/adventure_assets.h mods/coop/package_admission.h mods/coop/package_admission_windows.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(ADVENTURE_ASSETS_NATIVE_FIXTURE_SRC) -o $@ -lws2_32

# native Linux unit tests for portable netcode
test:
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Isdk/include tests/native_build_tests.cpp -o $(B)/host/native_build_tests
	$(B)/host/native_build_tests
	python3 tests/large_address_aware_tests.py
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop $(PACKAGE_ADMISSION_SRC) tests/package_admission_tests.cpp -o $(B)/host/package_admission_tests
	$(B)/host/package_admission_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/net_tests mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	$(B)/host/net_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/ui_pages_tests tests/ui_pages_tests.cpp
	$(B)/host/ui_pages_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/frame_metrics_tests tests/frame_metrics_tests.cpp
	$(B)/host/frame_metrics_tests
	$(MAKE) test-editor
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/adventure_hud_tests tests/adventure_hud_tests.cpp
	$(B)/host/adventure_hud_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_tests mods/coop/net/adventure.cpp tests/adventure_tests.cpp
	$(B)/host/adventure_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_assets_tests mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp tests/adventure_assets_tests.cpp
	$(B)/host/adventure_assets_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/adventure_music_tests mods/coop/adventure_music.cpp mods/coop/net/dbpf.cpp tests/adventure_music_tests.cpp
	$(B)/host/adventure_music_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_world_tests tests/adventure_world_tests.cpp
	$(B)/host/adventure_world_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/adventure_actor_motion_tests tests/adventure_actor_motion_tests.cpp
	$(B)/host/adventure_actor_motion_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop/net -o $(B)/host/adventure_control_policy_tests tests/adventure_control_policy_tests.cpp
	$(B)/host/adventure_control_policy_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -Isdk/include -o $(B)/host/adventure_runtime_tests mods/coop/adventure.cpp mods/coop/net/adventure.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp tests/adventure_runtime_tests.cpp
	$(B)/host/adventure_runtime_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include -o $(B)/host/adventure_flow_tests mods/coop/adventure_flow.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp tests/adventure_flow_tests.cpp
	$(B)/host/adventure_flow_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -o $(B)/host/adventure_pause_tests mods/coop/adventure_pause.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp tests/adventure_pause_tests.cpp
	$(B)/host/adventure_pause_tests
	python3 tests/test_uifind.py
	python3 tests/editor_performance_tool_tests.py
	python3 tests/editor_commit_performance_tool_tests.py
	python3 tests/editor_memory_accounting_tests.py
	python3 tests/editor_network_proxy_tests.py
	python3 tools/tests/coop_editor_lifecycle.py --self-test
	python3 tools/tests/coop_editor_save.py --self-test
	python3 tools/tests/coop_editor_creations.py --self-test
	python3 tools/tests/coop_editor_creation_save.py --self-test
	python3 tests/editor_preview_package_tests.py
	python3 tools/tests/editor_native_audit.py --self-test
	python3 tools/re_lint.py

# Optional Win32 ABI test with a fake native adapter. Run the .exe on Windows (no game).
test-captains-windows: $(B)/host/adventure_captains_tests.exe
$(B)/host/adventure_captains_tests.exe: mods/coop/adventure_captains.cpp mods/coop/net/adventure_assets.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp updater/sha256.cpp tests/adventure_captains_tests.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $^ -o $@ -lws2_32

test-lifecycle-windows: $(B)/host/adventure_lifecycle_tests.exe
$(B)/host/adventure_lifecycle_tests.exe: mods/coop/adventure_world_lifecycle.cpp tests/adventure_lifecycle_tests.cpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $^ -o $@

.PHONY: test-pause-windows
test-pause-windows: $(B)/host/adventure_pause_native_tests.exe
$(B)/host/adventure_pause_native_tests.exe: tests/adventure_pause_native_tests.cpp mods/coop/adventure_pause_native.cpp mods/coop/adventure_pause_native.h $(ADVENTURE_ABI_HEADERS) $(B)/obj/tests/native_fixture_rc.o
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_pause_native_tests.cpp $(B)/obj/tests/native_fixture_rc.o -o $@

.PHONY: test-memory-windows
test-memory-windows: $(B)/host/readable_memory_tests.exe
$(B)/host/readable_memory_tests.exe: tests/readable_memory_tests.cpp sdk/include/sporekit_game.hpp sdk/include/sporekit_build.hpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Wl,--large-address-aware -Isdk/include tests/readable_memory_tests.cpp -o $@

ACTIONS_TEST_SRC := mods/coop/adventure_world_actions.cpp tests/adventure_actions_tests.cpp
VEHICLES_TEST_SRC := mods/coop/adventure_world_vehicles.cpp tests/adventure_vehicle_tests.cpp
RELATIONSHIPS_TEST_SRC := mods/coop/adventure_relationships.cpp tests/adventure_relationship_tests.cpp
SOCIAL_TEST_SRC := mods/coop/adventure_social.cpp tests/adventure_social_tests.cpp
MIRROR_TEST_SRC := tests/adventure_mirror_tests.cpp mods/coop/adventure_flow.cpp mods/coop/adventure_targeting.cpp mods/coop/net/adventure.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp

test-actions-windows: $(B)/host/adventure_actions_tests.exe
$(B)/host/adventure_actions_tests.exe: $(ACTIONS_TEST_SRC) mods/coop/adventure_world_actions.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(ACTIONS_TEST_SRC) -o $@

test-vehicles-windows: $(B)/host/adventure_vehicle_tests.exe
$(B)/host/adventure_vehicle_tests.exe: $(VEHICLES_TEST_SRC) mods/coop/adventure_world_vehicles.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(VEHICLES_TEST_SRC) -o $@

test-relationships-windows: $(B)/host/adventure_relationship_tests.exe
$(B)/host/adventure_relationship_tests.exe: $(RELATIONSHIPS_TEST_SRC) mods/coop/adventure_relationships.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(RELATIONSHIPS_TEST_SRC) -o $@

test-targeting-windows: $(B)/host/adventure_targeting_tests.exe
$(B)/host/adventure_targeting_tests.exe: tests/adventure_targeting_tests.cpp mods/coop/adventure_targeting.cpp mods/coop/adventure_targeting.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_targeting_tests.cpp mods/coop/adventure_targeting.cpp -o $@

test-social-windows: $(B)/host/adventure_social_tests.exe
$(B)/host/adventure_social_tests.exe: $(SOCIAL_TEST_SRC) mods/coop/adventure_social.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(SOCIAL_TEST_SRC) -o $@

# Includes the actual game.cpp Update wrapper, without linking game.cpp twice.
# An explicit asInvoker manifest prevents installer autodetection on this fixture.
test-blocked-update-windows: $(B)/host/adventure_tick_dispatch_tests.exe
.PHONY: test-controls-windows
test-controls-windows: $(B)/host/adventure_control_tests.exe
$(B)/host/adventure_control_tests.exe: tests/adventure_control_tests.cpp mods/coop/adventure_control.cpp mods/coop/adventure_control.h $(ADVENTURE_ABI_HEADERS) $(B)/obj/tests/native_fixture_rc.o
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_control_tests.cpp $(B)/obj/tests/native_fixture_rc.o -o $@
$(B)/obj/tests/native_fixture_rc.o: tests/native_fixture.rc tests/native_fixture.manifest
	@mkdir -p $(dir $@)
	$(WINDRES) -i tests/native_fixture.rc -o $@ --include-dir tests
$(B)/host/adventure_tick_dispatch_tests.exe: tests/adventure_blocked_update_tests.cpp mods/coop/game.cpp $(ADVENTURE_ABI_HEADERS) $(B)/obj/tests/native_fixture_rc.o
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_blocked_update_tests.cpp $(B)/obj/tests/native_fixture_rc.o -o $@

# Includes actual debug.cpp; discard unrelated command implementations at link time.
test-debug-capabilities-windows: $(B)/host/adventure_debug_capability_tests.exe
$(B)/host/adventure_debug_capability_tests.exe: tests/adventure_debug_capability_tests.cpp mods/coop/debug.cpp mods/coop/world.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -static -Imods/coop -Isdk/include tests/adventure_debug_capability_tests.cpp -Wl,--gc-sections -o $@

# The mirror fixture includes world.cpp directly; list it as a dependency only.
test-mirror-windows: $(B)/host/adventure_mirror_tests.exe
$(B)/host/adventure_mirror_tests.exe: $(MIRROR_TEST_SRC) mods/coop/adventure_world.cpp mods/coop/adventure_world.h mods/coop/adventure_world_actions.h mods/coop/adventure_world_lifecycle.h mods/coop/adventure_world_vehicles.h mods/coop/adventure_relationships.h mods/coop/adventure_targeting.h mods/coop/net/adventure.h mods/coop/net/adventure_health.h mods/coop/net/adventure_actor_motion.h mods/coop/net/session.h mods/coop/net/buffer.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(MIRROR_TEST_SRC) -o $@ -lws2_32

test-progress-windows: $(B)/host/adventure_progress_tests.exe
$(B)/host/adventure_progress_tests.exe: mods/coop/adventure_progress.cpp tests/adventure_progress_tests.cpp mods/coop/adventure_progress.h mods/coop/net/dbpf.h sdk/include/sporekit.h sdk/include/sporekit_game.hpp sdk/include/sporekit_build.hpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include mods/coop/adventure_progress.cpp tests/adventure_progress_tests.cpp -o $@

.PHONY: test-results-windows
test-results-windows: $(B)/host/adventure_results_tests.exe
$(B)/host/adventure_results_tests.exe: mods/coop/adventure_results.cpp mods/coop/adventure_results.h tests/adventure_results_tests.cpp mods/coop/sui.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include mods/coop/adventure_results.cpp tests/adventure_results_tests.cpp -o $@

clean:
	rm -rf $(B)

-include $(KIT_OBJ:.o=.d) $(COOP_OBJ:.o=.d)

# Startup policy and controlled polling detours; never launches a game/window.
.PHONY: test-input test-input-windows
test-input: $(B)/host/sporekit_input_policy_tests
	$(B)/host/sporekit_input_policy_tests
test-input-windows: $(B)/host/sporekit_input_policy_tests.exe $(B)/host/sporekit_input_tests.exe
$(B)/host/sporekit_input_policy_tests: tests/sporekit_input_policy_tests.cpp sporekit/input_policy.h
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O2 -Wall -Wextra -Werror $< -o $@
$(B)/host/sporekit_input_policy_tests.exe: tests/sporekit_input_policy_tests.cpp sporekit/input_policy.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@
$(B)/host/sporekit_input_tests.exe: tests/sporekit_input_windows_tests.cpp sporekit/input.cpp sporekit/input_policy.h sporekit/core.h sdk/include/sporekit.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@ -luser32

# Actual mod manager with controlled Win32 loader/queue endpoints; no game calls.
.PHONY: test-mod-lifecycle-windows
test-mod-lifecycle-windows: $(B)/host/sporekit_mod_lifecycle_tests.exe
$(B)/host/sporekit_mod_lifecycle_tests.exe: tests/sporekit_mod_lifecycle_windows_tests.cpp sporekit/mods.cpp sporekit/core.h sdk/include/sporekit.h
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@

.PHONY: test-refpack-memo-windows
test-refpack-memo-windows: $(B)/host/sporekit_refpack_memo_tests.exe
$(B)/host/sporekit_refpack_memo_tests.exe: tests/sporekit_refpack_memo_windows_tests.cpp sporekit/refpack_memo.cpp sporekit/refpack_memo.h sporekit/refpack_memo_profile.h sporekit/core.h sdk/include/sporekit.h sdk/include/sporekit_game.hpp sdk/include/sporekit_build.hpp mods/coop/net/dbpf.cpp mods/coop/net/dbpf.h updater/sha256.cpp updater/sha256.h tests/fixtures/editor_native_pose/manifest.h tests/fixtures/editor_native_pose/r48.native.bin
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Isdk/include tests/sporekit_refpack_memo_windows_tests.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp -o $@

.PHONY: test-task-pump-metrics test-task-pump-metrics-windows
test-task-pump-metrics: $(B)/host/task_pump_metrics_tests
	$<
test-task-pump-metrics-windows: $(B)/host/task_pump_metrics_tests.exe
$(B)/host/task_pump_metrics_tests: tests/task_pump_metrics_tests.cpp sporekit/task_pump_metrics.h mods/coop/editor_service_pulse.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -Wall -Wextra -Werror -pthread $< -o $@
$(B)/host/task_pump_metrics_tests.exe: tests/task_pump_metrics_tests.cpp sporekit/task_pump_metrics.h mods/coop/editor_service_pulse.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@

.PHONY: test-outside-tasks test-outside-tasks-windows
test-outside-tasks: $(B)/host/outside_tasks_tests
	$<
test-outside-tasks-windows: $(B)/host/outside_tasks_tests.exe $(B)/host/outside_tasks_windows_tests.exe
$(B)/host/outside_tasks_tests: tests/outside_tasks_tests.cpp sporekit/outside_tasks.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -Wall -Wextra -Werror -pthread $< -o $@
$(B)/host/outside_tasks_tests.exe: tests/outside_tasks_tests.cpp sporekit/outside_tasks.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@
$(B)/host/outside_tasks_windows_tests.exe: tests/outside_tasks_windows_tests.cpp sporekit/console.cpp sporekit/outside_tasks.h sporekit/task_pump_metrics.h sporekit/core.h sdk/include/sporekit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -Wno-missing-field-initializers -static -ffunction-sections -fdata-sections $< -Wl,--gc-sections -lws2_32 -luser32 -o $@

# Preview 6 verified interaction adapters (the tests include their implementation).
INTERACTION_TEST_NET := mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp
.PHONY: test-explosives-windows test-pickups-windows
test-explosives-windows: $(B)/host/adventure_explosives_tests.exe
$(B)/host/adventure_explosives_tests.exe: tests/adventure_explosives_tests.cpp mods/coop/adventure_explosives.cpp mods/coop/adventure_explosives.h $(INTERACTION_TEST_NET) $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_explosives_tests.cpp $(INTERACTION_TEST_NET) -o $@ -lws2_32
test-pickups-windows: $(B)/host/adventure_pickups_tests.exe
$(B)/host/adventure_pickups_tests.exe: tests/adventure_pickups_tests.cpp mods/coop/adventure_pickups.cpp mods/coop/adventure_pickups.h $(INTERACTION_TEST_NET) $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include tests/adventure_pickups_tests.cpp $(INTERACTION_TEST_NET) -o $@ -lws2_32

# Passive shared-editor service cadence; deterministic controlled clocks/threads.
.PHONY: test-editor-pulse test-editor-pulse-windows
test-editor-pulse: $(B)/host/editor_service_pulse_tests
	$<
test-editor-pulse-windows: $(B)/host/editor_service_pulse_tests.exe
$(B)/host/editor_service_pulse_tests: tests/editor_service_pulse_tests.cpp mods/coop/editor_service_pulse.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -Wall -Wextra -Werror -pthread -Imods/coop $< -o $@
$(B)/host/editor_service_pulse_tests.exe: tests/editor_service_pulse_tests.cpp mods/coop/editor_service_pulse.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $< -o $@

# Read-only canonical asset pages fit the Kit console's fixed 8 KiB response.
EDITOR_ASSET_DIAGNOSTIC_SRC := tests/editor_asset_diagnostic_tests.cpp $(EDITOR_CORE_SRC) updater/sha256.cpp
EDITOR_ASSET_DIAGNOSTIC_HEADERS := mods/coop/editor_asset_diagnostic.h mods/coop/editor_native_cast.h mods/coop/net/editor_document.h mods/coop/net/protocol.h updater/sha256.h
.PHONY: test-editor-assets test-editor-assets-windows
test-editor: test-editor-assets
test-editor-assets: $(B)/host/editor_asset_diagnostic_tests
	$<
test-editor-assets-windows: $(B)/host/editor_asset_diagnostic_tests.exe
$(B)/host/editor_asset_diagnostic_tests: $(EDITOR_ASSET_DIAGNOSTIC_SRC) $(EDITOR_ASSET_DIAGNOSTIC_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -Wall -Wextra -Werror -Imods/coop $(EDITOR_ASSET_DIAGNOSTIC_SRC) -o $@
$(B)/host/editor_asset_diagnostic_tests.exe: $(EDITOR_ASSET_DIAGNOSTIC_SRC) $(EDITOR_ASSET_DIAGNOSTIC_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop $(EDITOR_ASSET_DIAGNOSTIC_SRC) -o $@

# Explicit diagnostic tree roots must belong to the current native window tree.
.PHONY: test-sui-tree-root test-sui-tree-root-windows
test-editor: test-sui-tree-root
test-sui-tree-root: $(B)/host/sui_tree_root_tests
	$<
test-sui-tree-root-windows: $(B)/host/sui_tree_root_tests.exe
test-editor-windows: $(B)/host/sui_tree_root_tests.exe
$(B)/host/sui_tree_root_tests: tests/sui_tree_root_tests.cpp mods/coop/sui_tree_root.h Makefile
	@mkdir -p $(dir $@)
	$(HOSTCXX) -std=c++20 -O2 -g -Wall -Wextra -Werror $< -o $@
$(B)/host/sui_tree_root_tests.exe: tests/sui_tree_root_tests.cpp mods/coop/sui_tree_root.h Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static $< -o $@
