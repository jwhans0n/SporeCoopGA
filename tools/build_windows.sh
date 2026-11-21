#!/usr/bin/env bash
# Build loader, SporeKit and coop.dll on a Windows host (Git Bash) without make.
# Product resources and libraries match the Makefile. Set FORCE=1 after header changes. Needs the llvm-mingw Windows toolchain
# (https://github.com/mstorsjo/llvm-mingw/releases, llvm-mingw-<ver>-ucrt-x86_64.zip):
#   LLVM_MINGW=/c/path/to/llvm-mingw-20261006-ucrt-x86_64 tools/build_windows.sh [loader|sporekit|coop|all]
set -euo pipefail
cd "$(dirname "$0")/.."
: "${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw folder}"
CC="$LLVM_MINGW/bin/i686-w64-mingw32-clang"
CXX="$LLVM_MINGW/bin/i686-w64-mingw32-clang++"
WINDRES="$LLVM_MINGW/bin/i686-w64-mingw32-windres"
B=build
COMMON="-O2 -g -Wall -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE"
LDDLL="-shared -static -Wl,--kill-at -Wl,--enable-stdcall-fixup"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
what="${1:-all}"

# compile <c|cpp> <src...>: objects into build/obj, in parallel, only when the source is newer
compile() {
  local lang=$1; shift
  local pids=() src obj
  for src in "$@"; do
    obj="$B/obj/${src%.*}.o"
    mkdir -p "$(dirname "$obj")"
    if [[ -f "$obj" && "$obj" -nt "$src" && -z "${FORCE:-}" ]]; then continue; fi
    if [[ $lang == c ]]; then
      "$CC" $COMMON -Ithird_party/minhook/include -c "$src" -o "$obj" &
    else
      "$CXX" $COMMON -std=c++20 -Ithird_party/minhook/include -Isdk/include -c "$src" -o "$obj" &
    fi
    pids+=($!)
    if (( ${#pids[@]} >= JOBS )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
}
objs() { for s in "$@"; do echo "$B/obj/${s%.*}.o"; done; }

mkdir -p "$B"
ver=$(sed -n 's/^#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp)
ver_num=$(echo "$ver" | awk -F. '{printf "%d,%d,%d,%d", $1, $2, $3, $4}')
printf '#define SC_VER_NUM %s\n#define SC_VER_STR "%s"\n' "$ver_num" "$ver" > "$B/version.h"
resource() {
  local source=$1 target=$2
  mkdir -p "$(dirname "$target")"
  "$WINDRES" -i "$source" -o "$target" -I"$B" -Iinstaller
}
if [[ $what == all || $what == loader ]]; then
  resource loader/loader.rc "$B/obj/loader/loader_rc.o"
  "$CC" $COMMON -shared -o "$B/steam_api.dll" loader/loader.c loader/steam_api.def "$B/obj/loader/loader_rc.o" -Wl,--kill-at
  echo "built $B/steam_api.dll"
fi
if [[ $what == all || $what == sporekit ]]; then
  MH=(third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde32.c)
  KIT=(sporekit/*.cpp)
  compile c "${MH[@]}"
  compile cpp "${KIT[@]}"
  resource sporekit/sporekit.rc "$B/obj/sporekit/sporekit_rc.o"
  "$CXX" $LDDLL -o "$B/sporekit.dll" $(objs "${KIT[@]}") $(objs "${MH[@]}") "$B/obj/sporekit/sporekit_rc.o" \
    -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,"$B/libsporekit.a"
  echo "built $B/sporekit.dll"
fi
if [[ $what == all || $what == coop ]]; then
  COOP=(mods/coop/*.cpp mods/coop/net/*.cpp updater/*.cpp)
  compile cpp "${COOP[@]}"
  resource mods/coop/coop.rc "$B/obj/mods/coop/coop_rc.o"
  "$CXX" $LDDLL -o "$B/coop.dll" $(objs "${COOP[@]}") "$B/obj/mods/coop/coop_rc.o" -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32 -lole32
  echo "built $B/coop.dll"
fi
