// Adventure content preparation. Game operations run outside the render frame.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/adventure_assets.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure_assets {

using Key = dbpf::Key;

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    // Called outside the frame. Must return the scenario and its complete resource dependency
    // closure, normalizing server references in an owned scenario copy when needed. No SDK
    // layouts are guessed here: the build-specific runtime supplies the verified collector.
    std::function<bool(const Key&, std::vector<dbpf::Record>&, std::string&)> collect;
    std::function<void(uint64_t run, const Key&, const std::string& hash)> prepared;
    std::function<void(uint8_t peer, uint64_t run, const Key&, const std::string& hash)> peer_prepared;
    std::function<void(uint64_t run, const std::string&)> failed;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
// Host: collect and register content. Guest: expect only this run/key from the host.
bool prepare(uint64_t run, const Key& scenario);
bool share_with(uint8_t peer);
void on_message(uint8_t from, net::Reader& payload); // reads its own subkind, range 64..71
void tick(double now);
void reset(); // transfers stop; registered packages remain valid for live game resources
void release(); // only after native scenario resources have been released, outside the frame
// Additional verified creation records, outside the frame; does not replace a scenario transfer.
bool register_records(const std::vector<dbpf::Record>& records, std::string& error);
std::string status();
uint8_t progress();

} // namespace adventure_assets
