// Read-only reopen identity proof. No native calls, live mutation or save repair.
#pragma once
#include "editor_native_bindings.h"
namespace editor_reopen_probe {
inline bool dense(const editor_native::Snapshot& snapshot,
                  std::vector<editor_native::Binding>& sorted, std::string& error) {
    if (!editor_native::serialized_bindings(snapshot.bindings, sorted, error)) return false;
    auto original = snapshot.bindings;
    std::sort(original.begin(), original.end(), [](const auto& a, const auto& b) { return a.marker < b.marker; });
    // serialized_bindings normally compacts live sparse pools. This proof is
    // explicitly about serialized dense rows, so a hole is an error, not repair.
    if (original != sorted || sorted.size() != snapshot.document.placements.size()) {
        error = "Reopen requires complete dense serialized bindings"; return false;
    }
    for (const auto& binding : sorted) {
        auto found = snapshot.document.placements.find(binding.object);
        if (found == snapshot.document.placements.end() || found->second.object != binding.object) {
            error = "Reopen binding does not identify the observed placement"; return false;
        }
    }
    return true;
}

// The ONLY proof this provides is a bijection between serialized row identities.
// Bytes and observed authored fields remain unchanged. The caller must still
// validate observed bytes against the canonical document and compare opaque
// authored fields against saved bytes using the verified native adapter.
inline bool remap_reopened(const editor_native::Snapshot& saved,
                           const editor_native::Snapshot& observed,
                           editor_native::Snapshot& out, std::string& error) {
    constexpr size_t max_root = 16u << 20;
    if (saved.source == editor::ResourceKey{} || observed.source != saved.source ||
        saved.act != observed.act || saved.captain != observed.captain ||
        saved.captain_locked != observed.captain_locked || saved.bytes.empty() ||
        observed.bytes.empty() || saved.bytes.size() > max_root || observed.bytes.size() > max_root ||
        saved.document.classes != observed.document.classes || saved.document.acts != observed.document.acts ||
        saved.document.placements.size() != observed.document.placements.size()) {
        error = "Reopened native source, act, captain or roster differs from the saved snapshot"; return false;
    }
    std::vector<editor_native::Binding> expected, actual;
    if (!dense(saved, expected, error) || !dense(observed, actual, error)) return false;
    std::map<editor::ObjectId, editor::ObjectId> names;
    for (size_t i = 0; i < actual.size(); ++i) names.emplace(actual[i].object, expected[i].object);
    auto mapped = [&](editor::ObjectId id) {
        const auto found = names.find(id); return found == names.end() ? id : found->second;
    };
    auto candidate = observed;
    candidate.bindings = expected;
    candidate.document.placements.clear();
    for (const auto& [id, placement] : observed.document.placements) {
        auto renamed = placement;
        renamed.object = names.at(id);
        for (auto& [property, value] : renamed.properties) {
            (void)property;
            if (auto reference = std::get_if<editor::ObjectId>(&value)) *reference = mapped(*reference);
        }
        candidate.document.placements.emplace(renamed.object, std::move(renamed));
    }
    for (auto& reference : candidate.document.external_references) {
        reference.owner = mapped(reference.owner); reference.target = mapped(reference.target);
    }
    // Native projection has no tombstones. Reject unexpected collaboration
    // metadata rather than inventing a meaning for it in a reopened resource.
    if (!observed.document.tombstones.empty()) {
        error = "Reopened native projection unexpectedly contains tombstones"; return false;
    }
    out = std::move(candidate); error.clear(); return true;
}
} // namespace editor_reopen_probe
