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

.PHONY: all loader sporekit coop res setup clean test test-captains-windows test-lifecycle-windows test-actions-windows test-vehicles-windows test-mirror-windows test-progress-windows test-relationships-windows test-social-windows test-blocked-update-windows test-debug-capabilities-windows
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
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -liphlpapi -lwinhttp

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

# native Linux unit tests for portable netcode
test:
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/net_tests mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	$(B)/host/net_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_tests mods/coop/net/adventure.cpp tests/adventure_tests.cpp
	$(B)/host/adventure_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_assets_tests mods/coop/net/adventure_assets.cpp mods/coop/net/dbpf.cpp updater/sha256.cpp tests/adventure_assets_tests.cpp
	$(B)/host/adventure_assets_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/adventure_world_tests tests/adventure_world_tests.cpp
	$(B)/host/adventure_world_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -Isdk/include -o $(B)/host/adventure_runtime_tests mods/coop/adventure.cpp mods/coop/net/adventure.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp tests/adventure_runtime_tests.cpp
	$(B)/host/adventure_runtime_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -Imods/coop -Isdk/include -o $(B)/host/adventure_flow_tests mods/coop/adventure_flow.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp tests/adventure_flow_tests.cpp
	$(B)/host/adventure_flow_tests
	python3 tests/test_uifind.py
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

ADVENTURE_ABI_HEADERS := sdk/include/sporekit.h sdk/include/sporekit_game.hpp mods/coop/game.h mods/coop/net/protocol.h

.PHONY: test-memory-windows
test-memory-windows: $(B)/host/readable_memory_tests.exe
$(B)/host/readable_memory_tests.exe: tests/readable_memory_tests.cpp sdk/include/sporekit_game.hpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Isdk/include tests/readable_memory_tests.cpp -o $@

ACTIONS_TEST_SRC := mods/coop/adventure_world_actions.cpp tests/adventure_actions_tests.cpp
VEHICLES_TEST_SRC := mods/coop/adventure_world_vehicles.cpp tests/adventure_vehicle_tests.cpp
RELATIONSHIPS_TEST_SRC := mods/coop/adventure_relationships.cpp tests/adventure_relationship_tests.cpp
SOCIAL_TEST_SRC := mods/coop/adventure_social.cpp tests/adventure_social_tests.cpp
MIRROR_TEST_SRC := tests/adventure_mirror_tests.cpp mods/coop/adventure_flow.cpp mods/coop/net/adventure.cpp mods/coop/net/session.cpp mods/coop/net/connection.cpp mods/coop/net/udp.cpp mods/coop/net/crc32.cpp

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

test-social-windows: $(B)/host/adventure_social_tests.exe
$(B)/host/adventure_social_tests.exe: $(SOCIAL_TEST_SRC) mods/coop/adventure_social.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(SOCIAL_TEST_SRC) -o $@

# Includes the actual game.cpp Update wrapper, without linking game.cpp twice.
# An explicit asInvoker manifest prevents installer autodetection on this fixture.
test-blocked-update-windows: $(B)/host/adventure_tick_dispatch_tests.exe
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
$(B)/host/adventure_mirror_tests.exe: $(MIRROR_TEST_SRC) mods/coop/adventure_world.cpp mods/coop/adventure_world.h mods/coop/adventure_world_actions.h mods/coop/adventure_world_lifecycle.h mods/coop/adventure_world_vehicles.h mods/coop/adventure_relationships.h mods/coop/net/adventure.h mods/coop/net/adventure_health.h mods/coop/net/session.h mods/coop/net/buffer.h $(ADVENTURE_ABI_HEADERS)
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include $(MIRROR_TEST_SRC) -o $@ -lws2_32

test-progress-windows: $(B)/host/adventure_progress_tests.exe
$(B)/host/adventure_progress_tests.exe: mods/coop/adventure_progress.cpp tests/adventure_progress_tests.cpp mods/coop/adventure_progress.h mods/coop/net/dbpf.h sdk/include/sporekit.h sdk/include/sporekit_game.hpp
	@mkdir -p $(B)/host
	$(CXX) $(CXXFLAGS) -Wextra -Werror -static -Imods/coop -Isdk/include mods/coop/adventure_progress.cpp tests/adventure_progress_tests.cpp -o $@

clean:
	rm -rf $(B)

-include $(KIT_OBJ:.o=.d) $(COOP_OBJ:.o=.d)

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
