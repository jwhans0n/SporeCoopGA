// Verified native save and editor-preview lifecycle, separate from project WAL.
#pragma once
#include <functional>
#include <string>
#include "net/editor_document.h"
#include "sporekit.h"

namespace editor_native_save {
struct Ctx {
    // Owns the shared draft, first Save As wizard and recovery native context.
    std::function<bool()> shared_active;
    // Called only for this module's pending native operation. The saved resource
    // has passed native completion and an independent persisted-record readback.
    std::function<void(editor::ResourceKey)> saved;
    std::function<void()> cancelled;
    std::function<void(const std::string&)> error;
    // Direct native Done/hammer returns are group requests during owned co-op
    // preview. Explicit playtest(false) performs the authorized group transition.
    std::function<bool()> owned_preview;
    std::function<void()> return_requested;
};
bool init(const SkHost*, SkMod*, Ctx);
void shutdown();
bool available();
bool busy();
// Entry points below must be called outside Present. Save As must establish a
// fresh native identity before saving so the original adventure stays intact.
bool save_as(std::string& error);
bool save(std::string& error);
// Exact completed native record, including Save-derived terrain properties and
// opaque authoring data that the live working resource may not yet contain.
bool read_saved(const editor::ResourceKey&, std::vector<uint8_t>& bytes, std::string& error);
bool playtest(bool on, std::string& error);
// Scheduling only is safe inside Present; native completion polling runs outside.
void tick();
} // namespace editor_native_save
