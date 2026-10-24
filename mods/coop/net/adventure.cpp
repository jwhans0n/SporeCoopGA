#include "adventure.h"

#include <algorithm>
#include <cmath>

namespace ga {
namespace {
bool key_valid(const dbpf::Key& key) { return key.instance != 0 && key.type != 0; }
bool key_zero(const dbpf::Key& key) { return key.instance == 0 && key.type == 0 && key.group == 0; }
bool override_valid(float value) { return std::isfinite(value) && (value == 0 || (value >= 0.25f && value <= 10.f)); }
bool point_valid(const Point& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
bool near(const Point& a, const Point& b)
{
    double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
    return x * x + y * y + z * z <= double(kReviveDistance) * kReviveDistance;
}
bool source_valid(const Source& source)
{
    return source.kind <= SourceKind::OtherActor &&
           (source.kind != SourceKind::Captain || source.owner != kNoPlayer);
}
} // namespace

bool Difficulty::valid() const
{
    return death_mode <= DeathMode::AutomaticRevive && override_valid(enemy_health_override) &&
           override_valid(enemy_damage_override) &&
           (friendly_fire_percent == 0 || friendly_fire_percent == 25 ||
            friendly_fire_percent == 50 || friendly_fire_percent == 100);
}

float Difficulty::health_multiplier(size_t party_size) const
{
    if (!valid() || party_size == 0 || party_size > kPartyLimit)
        return 0;
    return enemy_health_override ? enemy_health_override : 1.f + 0.5f * float(party_size - 1);
}

float Difficulty::damage_multiplier() const
{
    return valid() ? (enemy_damage_override ? enemy_damage_override : 1.f) : 0;
}

void Difficulty::write(net::Writer& writer) const
{
    writer.u8(static_cast<uint8_t>(death_mode));
    writer.f32(enemy_health_override);
    writer.f32(enemy_damage_override);
    writer.u8(friendly_fire_percent);
}

bool Difficulty::read(net::Reader& reader)
{
    Difficulty fresh;
    fresh.death_mode = static_cast<DeathMode>(reader.u8());
    fresh.enemy_health_override = reader.f32();
    fresh.enemy_damage_override = reader.f32();
    fresh.friendly_fire_percent = reader.u8();
    if (!reader.ok() || !fresh.valid())
        return false;
    *this = fresh;
    return true;
}

bool Descriptor::valid() const
{
    if (!run_id || !key_valid(adventure) || !difficulty.valid() || roster.empty() || roster.size() > kPartyLimit ||
        (!key_zero(locked_captain) && !key_valid(locked_captain)) ||
        std::all_of(manifest_hash.begin(), manifest_hash.end(), [](uint8_t b) { return b == 0; }))
        return false;
    std::set<uint8_t> ids;
    for (uint8_t id : roster)
        if (id == kNoPlayer || !ids.insert(id).second)
            return false;
    return ids.count(0) != 0;
}

void Descriptor::write(net::Writer& writer) const
{
    writer.u32(static_cast<uint32_t>(run_id));
    writer.u32(static_cast<uint32_t>(run_id >> 32));
    writer.u32(adventure.instance); writer.u32(adventure.type); writer.u32(adventure.group);
    writer.raw(manifest_hash.data(), manifest_hash.size());
    difficulty.write(writer);
    writer.u32(locked_captain.instance); writer.u32(locked_captain.type); writer.u32(locked_captain.group);
    writer.u8(static_cast<uint8_t>(roster.size()));
    for (uint8_t player : roster)
        writer.u8(player);
}

bool Descriptor::read(net::Reader& reader)
{
    Descriptor fresh;
    fresh.run_id = reader.u32();
    fresh.run_id |= uint64_t(reader.u32()) << 32;
    fresh.adventure = {reader.u32(), reader.u32(), reader.u32()};
    if (!reader.raw(fresh.manifest_hash.data(), fresh.manifest_hash.size()) || !fresh.difficulty.read(reader))
        return false;
    fresh.locked_captain = {reader.u32(), reader.u32(), reader.u32()};
    uint8_t count = reader.u8();
    if (!reader.ok() || count == 0 || count > kPartyLimit)
        return false;
    for (uint8_t i = 0; i < count; i++)
        fresh.roster.push_back(reader.u8());
    if (!reader.ok() || !fresh.valid())
        return false;
    *this = std::move(fresh);
    return true;
}

void PreparationView::write(net::Writer& w) const
{
    auto number = [&](uint64_t n) { w.u32(uint32_t(n)); w.u32(uint32_t(n >> 32)); };
    auto key = [&](const dbpf::Key& k) { w.u32(k.instance); w.u32(k.type); w.u32(k.group); };
    number(run); number(revision); number(epoch); key(adventure); key(locked_captain);
    w.u8(loading); w.u8(uint8_t(members.size()));
    for (const auto& m : members) {
        w.u8(m.player); number(m.serial); w.u8(uint8_t(m.stage)); w.u8(m.progress);
        key(m.captain); w.u8(m.content); w.u8(m.validated); w.u8(m.ready);
    }
}

bool PreparationView::read(net::Reader& r)
{
    auto number = [&] { uint64_t lo = r.u32(); return lo | (uint64_t(r.u32()) << 32); };
    auto key = [&] { return dbpf::Key{r.u32(), r.u32(), r.u32()}; };
    PreparationView fresh;
    fresh.run = number(); fresh.revision = number(); fresh.epoch = number(); fresh.adventure = key(); fresh.locked_captain = key();
    uint8_t loading = r.u8(), count = r.u8();
    if (!fresh.run || !fresh.revision || !fresh.epoch || !key_valid(fresh.adventure) || loading > 1 || !count || count > kPartyLimit) return false;
    fresh.loading = loading;
    std::set<uint8_t> ids;
    for (uint8_t i = 0; i < count; ++i) {
        PreparationMember m;
        m.player = r.u8(); m.serial = number(); m.stage = PreparationStage(r.u8()); m.progress = r.u8();
        m.captain = key(); uint8_t content = r.u8(), validated = r.u8(), ready = r.u8();
        if (m.player == kNoPlayer || !ids.insert(m.player).second || !m.serial || m.stage > PreparationStage::Prepared ||
            m.progress > 100 || content > 1 || validated > 1 || ready > 1 ||
            (validated && !key_valid(m.captain)) || (ready && (!content || !validated)) ||
            (!key_zero(fresh.locked_captain) && validated && !(m.captain == fresh.locked_captain))) return false;
        m.content = content; m.validated = validated; m.ready = ready;
        fresh.members.push_back(m);
    }
    if (!r.ok() || !ids.count(0)) return false;
    *this = std::move(fresh); return true;
}

bool LaunchBarrier::clear_captain(uint64_t run, uint8_t player)
{
    Member* m = member(player);
    if (phase_ != LaunchPhase::Preparing || run != descriptor_.run_id || !m) return false;
    m->captain = m->ready = false; m->captain_key = {};
    return true;
}

Member* LaunchBarrier::member(uint8_t player)
{
    for (auto& member : members_)
        if (member.player == player)
            return &member;
    return nullptr;
}

bool LaunchBarrier::begin(const Descriptor& descriptor)
{
    if (!descriptor.valid() || descriptor.run_id <= newest_run_ ||
        phase_ == LaunchPhase::Loading || phase_ == LaunchPhase::Running)
        return false;
    descriptor_ = descriptor;
    newest_run_ = descriptor.run_id;
    members_.clear();
    for (uint8_t player : descriptor.roster) {
        Member m;
        m.player = player;
        members_.push_back(m);
    }
    phase_ = LaunchPhase::Preparing;
    return true;
}

bool LaunchBarrier::ack_content(uint64_t run, uint8_t player, const dbpf::Key& adventure, const Hash& hash)
{
    Member* m = member(player);
    if (phase_ != LaunchPhase::Preparing || run != descriptor_.run_id || !m || !m->connected ||
        !(adventure == descriptor_.adventure) || hash != descriptor_.manifest_hash)
        return false;
    m->content = true;
    return true;
}

bool LaunchBarrier::ack_captain(uint64_t run, uint8_t player, const dbpf::Key& captain)
{
    Member* m = member(player);
    if (phase_ != LaunchPhase::Preparing || run != descriptor_.run_id || !m || !m->connected ||
        !key_valid(captain) || (!key_zero(descriptor_.locked_captain) && !(captain == descriptor_.locked_captain)))
        return false;
    // First confirmation completes preparation. Only a changed confirmed choice invalidates
    // other members' readiness, otherwise a later peer's initial reply could erase it forever.
    if (m->captain && !(m->captain_key == captain))
        for (auto& member : members_)
            member.ready = false;
    m->captain_key = captain;
    m->captain = true;
    return true;
}

bool LaunchBarrier::set_ready(uint64_t run, uint8_t player, bool ready)
{
    Member* m = member(player);
    if (phase_ != LaunchPhase::Preparing || run != descriptor_.run_id || !m || !m->connected ||
        (ready && (!m->content || !m->captain)))
        return false;
    m->ready = ready;
    return true;
}

bool LaunchBarrier::all_ready() const
{
    return phase_ == LaunchPhase::Preparing && !members_.empty() &&
           std::all_of(members_.begin(), members_.end(), [](const Member& m) {
               return m.connected && m.content && m.captain && m.ready;
           });
}

bool LaunchBarrier::all_loaded() const
{
    return phase_ == LaunchPhase::Loading && !members_.empty() &&
           std::all_of(members_.begin(), members_.end(), [](const Member& m) { return m.connected && m.loaded; });
}

bool LaunchBarrier::begin_loading(uint64_t run)
{
    if (run != descriptor_.run_id || !all_ready())
        return false;
    phase_ = LaunchPhase::Loading;
    return true;
}

bool LaunchBarrier::ack_loaded(uint64_t run, uint8_t player)
{
    Member* m = member(player);
    if (phase_ != LaunchPhase::Loading || run != descriptor_.run_id || !m || !m->connected)
        return false;
    m->loaded = true;
    return true;
}

bool LaunchBarrier::commit(uint64_t run)
{
    if (run != descriptor_.run_id || !all_loaded())
        return false;
    phase_ = LaunchPhase::Running;
    return true;
}

bool LaunchBarrier::disconnect(uint64_t run, uint8_t player)
{
    Member* m = member(player);
    if (run != descriptor_.run_id || !m || !m->connected ||
        (phase_ != LaunchPhase::Preparing && phase_ != LaunchPhase::Loading))
        return false;
    m->connected = false;
    phase_ = LaunchPhase::Aborted;
    return true;
}

bool LaunchBarrier::abort(uint64_t run)
{
    if (!run || run != descriptor_.run_id || phase_ == LaunchPhase::Idle || phase_ == LaunchPhase::Aborted)
        return false;
    phase_ = LaunchPhase::Aborted;
    return true;
}

float resolve_damage(const Difficulty& difficulty, size_t party_size, const Source& source,
                     uint8_t victim_owner, float authored_damage)
{
    if (!difficulty.valid() || !source_valid(source) || !party_size || party_size > kPartyLimit ||
        !std::isfinite(authored_damage) || authored_damage <= 0)
        return 0;
    float factor = source.kind == SourceKind::HostileActor ? difficulty.damage_multiplier() : 1.f;
    if (source.kind == SourceKind::Captain && victim_owner != kNoPlayer && source.owner != victim_owner)
        factor *= float(difficulty.friendly_fire_percent) / 100.f;
    float result = authored_damage * factor;
    return std::isfinite(result) ? result : 0;
}

const Captain* Authority::captain(uint8_t player) const
{
    for (const auto& c : captains_)
        if (c.player == player)
            return &c;
    return nullptr;
}

Captain* Authority::mutable_captain(uint8_t player)
{
    for (auto& c : captains_)
        if (c.player == player)
            return &c;
    return nullptr;
}

bool Authority::current(uint64_t run) const
{
    return run && run == descriptor_.run_id && phase_ == RunPhase::Playing;
}

bool Authority::start(const Descriptor& descriptor, const std::vector<CaptainSeed>& seeds, double now)
{
    if (!descriptor.valid() || descriptor.run_id <= newest_run_ || phase_ == RunPhase::Playing ||
        !std::isfinite(now) || now < 0 || seeds.size() != descriptor.roster.size())
        return false;
    std::vector<Captain> fresh;
    std::set<uint8_t> ids;
    for (const auto& seed : seeds) {
        if (std::find(descriptor.roster.begin(), descriptor.roster.end(), seed.player) == descriptor.roster.end() ||
            !ids.insert(seed.player).second || !std::isfinite(seed.max_health) || seed.max_health <= 0 ||
            !point_valid(seed.position))
            return false;
        Captain c;
        c.player = seed.player;
        c.health = c.max_health = seed.max_health;
        c.position = seed.position;
        fresh.push_back(c);
    }
    descriptor_ = descriptor;
    newest_run_ = descriptor.run_id;
    captains_ = std::move(fresh);
    phase_ = RunPhase::Playing;
    now_ = now;
    act_ = 0;
    health_events_.clear();
    rewarded_.clear();
    objectives_.clear();
    revision_++;
    return true;
}

bool Authority::can_revive(const Captain& rescuer, const Captain& target) const
{
    return rescuer.player != target.player && rescuer.life == Life::Alive && target.life == Life::Downed &&
           near(rescuer.position, target.position);
}

void Authority::clear_revives()
{
    for (auto& c : captains_) {
        c.reviver = kNoPlayer;
        c.revive_started = 0;
    }
}

void Authority::interrupt_revives(uint8_t player)
{
    for (auto& c : captains_)
        if (c.reviver == player) {
            c.reviver = kNoPlayer;
            c.revive_started = 0;
        }
}

void Authority::check_survivors()
{
    if (phase_ == RunPhase::Playing && std::none_of(captains_.begin(), captains_.end(),
                                                  [](const Captain& c) { return c.life == Life::Alive; })) {
        phase_ = RunPhase::Failed;
        clear_revives();
    }
}

void Authority::restore(Captain& c)
{
    c.life = Life::Alive;
    c.health = c.max_health * kReviveHealth;
    c.protected_until = now_ + kGraceSeconds;
    c.reviver = kNoPlayer;
    c.revive_started = 0;
    revision_++;
}

bool Authority::tick(uint64_t run, double now)
{
    if (!current(run) || !std::isfinite(now) || now < now_)
        return false;
    now_ = now;
    // A last-captain lethal hit fails immediately in damage(), before any pending timer.
    // Holding close to a teammate is the manual action. One standing captain contributes
    // to one rescue, in stable roster order; extra teammates cannot accelerate the timer.
    for (auto& target : captains_) {
        if (target.life != Life::Downed || target.reviver != kNoPlayer)
            continue;
        for (const auto& rescuer : captains_) {
            if (!can_revive(rescuer, target)) continue;
            bool busy = std::any_of(captains_.begin(), captains_.end(), [&](const Captain& other) {
                return other.reviver == rescuer.player;
            });
            if (!busy) {
                target.reviver = rescuer.player;
                target.revive_started = now_;
                revision_++;
                break;
            }
        }
    }
    for (auto& c : captains_) {
        if (c.life != Life::Downed)
            continue;
        if (c.reviver != kNoPlayer) {
            const Captain* rescuer = captain(c.reviver);
            if (!rescuer || !can_revive(*rescuer, c)) {
                c.reviver = kNoPlayer;
                c.revive_started = 0;
                revision_++;
            } else if (now_ - c.revive_started >= kReviveSeconds) {
                restore(c);
                continue;
            }
        }
        if (descriptor_.difficulty.death_mode == DeathMode::AutomaticRevive && now_ - c.downed_at >= kAutomaticSeconds)
            restore(c);
    }
    return true;
}

bool Authority::position(uint64_t run, uint8_t player, const Point& point)
{
    Captain* c = mutable_captain(player);
    if (!current(run) || !c || c->life != Life::Alive || !point_valid(point))
        return false;
    c->position = point;
    for (auto& target : captains_)
        if (target.reviver == player && !can_revive(*c, target)) {
            target.reviver = kNoPlayer;
            target.revive_started = 0;
        }
    revision_++;
    return true;
}

float Authority::damage(uint64_t run, uint64_t event, uint8_t victim, const Source& source, float authored_damage)
{
    Captain* c = mutable_captain(victim);
    if (!current(run) || !event || !c || !source_valid(source) || !std::isfinite(authored_damage) || authored_damage <= 0 ||
        (source.kind == SourceKind::Captain && !captain(source.owner)) ||
        !health_events_.insert({event, victim}).second)
        return 0;
    if (c->life != Life::Alive || now_ < c->protected_until)
        return 0;
    float amount = resolve_damage(descriptor_.difficulty, descriptor_.roster.size(), source, victim, authored_damage);
    if (amount <= 0)
        return 0;
    float applied = std::min(c->health, amount);
    c->health -= applied;
    interrupt_revives(victim);
    if (c->health <= 0) {
        c->life = Life::Downed;
        c->downed_at = now_;
        if (descriptor_.difficulty.death_mode == DeathMode::AnyDeathFails) {
            phase_ = RunPhase::Failed;
            clear_revives();
        } else
            check_survivors();
    }
    revision_++;
    return applied;
}

float Authority::heal(uint64_t run, uint64_t event, uint8_t victim, float amount)
{
    Captain* c = mutable_captain(victim);
    if (!current(run) || !event || !c || !std::isfinite(amount) || amount <= 0 ||
        !health_events_.insert({event, victim}).second || c->life != Life::Alive)
        return 0;
    float applied = std::min(c->max_health - c->health, amount);
    c->health += applied;
    if (applied > 0)
        revision_++;
    return applied;
}

bool Authority::revive(uint64_t run, uint8_t rescuer_id, uint8_t target_id, bool active)
{
    Captain* rescuer = mutable_captain(rescuer_id);
    Captain* target = mutable_captain(target_id);
    if (!current(run) || !rescuer || !target || descriptor_.difficulty.death_mode == DeathMode::AnyDeathFails)
        return false;
    if (!active) {
        if (target->reviver != rescuer_id)
            return false;
        target->reviver = kNoPlayer;
        target->revive_started = 0;
        revision_++;
        return true;
    }
    if (!can_revive(*rescuer, *target) || (target->reviver != kNoPlayer && target->reviver != rescuer_id))
        return false;
    for (const auto& c : captains_)
        if (c.player != target_id && c.reviver == rescuer_id)
            return false;
    if (target->reviver == rescuer_id)
        return true; // repeated held-input does not restart the timer
    target->reviver = rescuer_id;
    target->revive_started = now_;
    revision_++;
    return true;
}

bool Authority::disconnect(uint64_t run, uint8_t player)
{
    Captain* c = mutable_captain(player);
    if (!current(run) || !c || c->life == Life::Disconnected)
        return false;
    c->life = Life::Disconnected;
    c->reviver = kNoPlayer;
    c->revive_started = 0;
    interrupt_revives(player);
    if (player == 0) {
        phase_ = RunPhase::Aborted;
        clear_revives();
    } else
        check_survivors();
    revision_++;
    return true;
}

bool Authority::finish(uint64_t run, bool success)
{
    if (!current(run))
        return false;
    phase_ = success ? RunPhase::Won : RunPhase::Failed;
    clear_revives();
    revision_++;
    return true;
}

bool Authority::abort(uint64_t run)
{
    if (!current(run))
        return false;
    phase_ = RunPhase::Aborted;
    clear_revives();
    revision_++;
    return true;
}

bool Authority::claim_reward(uint64_t run, uint8_t player)
{
    const Captain* c = captain(player);
    return run && run == descriptor_.run_id && (phase_ == RunPhase::Won || phase_ == RunPhase::Failed) &&
           c && c->life != Life::Disconnected && rewarded_.insert(player).second;
}

bool Authority::define_objective(uint64_t run, uint8_t act, uint32_t objective, uint32_t required)
{
    if (!current(run) || act != act_ || !required)
        return false;
    return objectives_.emplace(std::make_pair(act, objective), Objective{0, required, {}}).second;
}

bool Authority::advance_objective(uint64_t run, uint8_t act, uint32_t objective, uint64_t event, uint32_t amount)
{
    if (!current(run) || act != act_ || !event || !amount)
        return false;
    auto it = objectives_.find({act, objective});
    if (it == objectives_.end() || it->second.count >= it->second.required || !it->second.events.insert(event).second)
        return false;
    auto& goal = it->second;
    goal.count += std::min(amount, goal.required - goal.count);
    revision_++;
    return true;
}

uint32_t Authority::objective_count(uint8_t act, uint32_t objective) const
{
    auto it = objectives_.find({act, objective});
    return it == objectives_.end() ? 0 : it->second.count;
}

bool Authority::next_act(uint64_t run, uint8_t act)
{
    if (!current(run) || act > 7 || act != act_ + 1)
        return false;
    act_ = act;
    revision_++;
    return true;
}

} // namespace ga
