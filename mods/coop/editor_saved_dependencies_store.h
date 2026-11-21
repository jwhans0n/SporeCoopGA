// Immutable receipt files in an explicitly supplied isolated cache directory.
// No Native save/profile database access, directory scan, or historical fallback.
#pragma once
#include "editor_saved_dependencies.h"
#include <filesystem>

namespace editor_saved_dependencies {
enum class Publication {NotPublished,Published,Unchanged,Uncertain};
// Complete encode, flushed temporary and no-replace atomic install. Prior files
// are never overwritten. Uncertain means target visibility preceded a failed
// durability acknowledgement; callers must not announce readiness from it.
Publication publish(const std::filesystem::path& directory,const Receipt&,std::string&);
bool load(const std::filesystem::path& directory,const Domain&,Key selected,
          std::span<const uint8_t> genuine_raw,Receipt&,std::string&);
}
