// Exact executable identities accepted by the reviewed GOG 2024 native adapters.
#pragma once

#include <cstring>

namespace skg {
inline bool supported_gog2024(const char* sha256)
{
    // The second image differs only by IMAGE_FILE_LARGE_ADDRESS_AWARE at file
    // offset 0x136 (0x02 -> 0x22). Do not substitute a code-only fingerprint:
    // native adapters also depend on the original data and image layout.
    return sha256 &&
        (!std::strcmp(sha256, "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37") ||
         !std::strcmp(sha256, "47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab"));
}
} // namespace skg
