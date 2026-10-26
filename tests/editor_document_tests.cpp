// Portable transaction tests. No game, renderer, profile or network required.
#include "net/editor_document.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace editor;

static int failures, checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static constexpr ObjectId a{10, 1}, b{11, 1}, c{12, 1}, captain{13, 1};
static constexpr ObjectId class_a{1, 1}, class_b{2, 1}, act_a{3, 1}, act_b{4, 1};
static constexpr FieldKey pos{Field::Position, 0}, scale{Field::Scale, 0}, flag{Field::Property, 100}, text{Field::Property, 101}, ref{Field::Property, 102};
static Scope field_scope(ObjectId id, FieldKey f) { return {ScopeKind::Field, id, f}; }

static State seed()
{
    State s; auto& d = s.document;
    d.project[0] = 7; d.project[15] = 9; d.epoch = 1; d.next_id = 14;
    d.classes = {class_a, class_b}; d.acts = {act_a, act_b};
    d.schema[100] = {ValueKind::Boolean};
    d.schema[101] = {ValueKind::Text, 0, 0, 128, true, false};
    d.schema[102] = {ValueKind::Reference, 0, 0, 0, true, true};
    d.schema[103] = {ValueKind::Number, 0, 1000};
    d.schema[104] = {ValueKind::Resource, 0, 0, 0, true, true};
    d.schema[105] = {ValueKind::Integer, -100, 100};
    for (auto id : {a, b, c, captain}) {
        Placement p; p.object = id; p.class_id = id == c ? class_b : class_a;
        p.act_id = id == b ? act_b : act_a; p.protected_object = id == captain;
        p.position = {float(id.id), 0, 0}; p.properties[100] = false;
        p.properties[101] = std::string("Original"); p.properties[102] = ObjectId{};
        d.placements.emplace(id, p);
    }
    return s;
}

struct Fixture {
    Coordinator core;
    Time now = 100;
    std::array<uint64_t, 4> sequence{};
    std::array<Peer, 4> peers{{{0, 1}, {1, 1}, {2, 1}, {3, 1}}};
    explicit Fixture(Limits l = {}, State s = seed()) : core(l) {
        std::string error; CHECK(core.reset(std::move(s), 1, error));
        for (auto p : peers) CHECK(core.connect(p));
    }
    const Document& doc() const { return core.state().document; }
    Request request(unsigned p, Intent intent = Intent::Forward) {
        Request r; r.project = doc().project; r.epoch = doc().epoch;
        r.operation = {peers[p], ++sequence[p], doc().epoch}; r.base_revision = doc().revision;
        r.intent = intent; r.summary = "Test gesture"; return r;
    }
    std::vector<LeaseToken> grant(unsigned p, std::vector<Scope> scopes = {{}}, Time ttl = 1000) {
        auto r = core.acquire(peers[p], scopes, now, ttl); CHECK(r.accepted());
        std::vector<LeaseToken> tokens; for (const auto& lease : r.leases) tokens.push_back({lease.scope, lease.token}); return tokens;
    }
    void release_all(unsigned p) {
        std::vector<LeaseToken> tokens;
        for (const auto& lease : core.leases()) if (lease.owner == peers[p]) tokens.push_back({lease.scope, lease.token});
        if (!tokens.empty()) CHECK(core.release(peers[p], tokens));
    }
    Decision forward(unsigned p, std::vector<Mutation> mutations, const Hooks& hooks = {}) {
        auto tokens = grant(p); auto r = request(p); r.leases = tokens; r.mutations = std::move(mutations);
        auto result = core.submit(r, now, hooks); release_all(p); return result;
    }
    Decision history(unsigned p, Intent intent) {
        auto tokens = grant(p); auto r = request(p, intent); r.leases = tokens;
        auto result = core.submit(r, now); release_all(p); return result;
    }
    Mutation set(ObjectId id, FieldKey f, std::optional<Value> v) const {
        Mutation m; m.target = id; m.field = f; m.value = std::move(v); m.expected = field_revision(doc().placements.at(id), f); return m;
    }
    Mutation erase(ObjectId id) const { Mutation m; m.kind = MutationKind::Delete; m.target = id; m.expected = doc().placements.at(id).revision; return m; }
};

static void test_validation()
{
    std::string error; CHECK(valid(seed().document, {}, error));
    auto invalid = seed(); invalid.document.project = {}; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].scale = std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].rotation.w = 0; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].properties[100] = 1.f; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].properties[9000] = false; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].properties[102] = ObjectId{999, 1}; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.placements[a].properties[101] = std::string("\xC0\x80", 2); CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.schema[101].maximum = std::numeric_limits<double>::infinity(); CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.next_id = 10; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.tombstones[a] = 0; CHECK(!valid(invalid.document, {}, error));
    invalid = seed(); invalid.document.external_references.push_back({{200, 1}, {999, 1}}); CHECK(!valid(invalid.document, {}, error));
    Limits tiny; tiny.document_bytes = 100; CHECK(!valid(seed().document, tiny, error));
}

static void test_create_clone_delete()
{
    Fixture f;
    Mutation create; create.kind = MutationKind::Create; create.initial.class_id = class_a; create.initial.act_id = act_a;
    auto result = f.forward(0, {create}); CHECK(result.accepted() && result.change && result.change->allocated.size() == 1);
    auto id = result.change->allocated.front(); CHECK(id == (ObjectId{14, 1}) && f.doc().next_id == 15);
    CHECK(result.change->patch.objects[0].after->revision == 1 && result.change->inverse.objects[0].expected == 1);
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = id; clone.expected = 1;
    result = f.forward(0, {clone}); CHECK(result.accepted());
    auto clone_id = result.change->allocated.front(); CHECK(clone_id != id && f.doc().placements.at(clone_id).class_id == class_a);
    CHECK(result.change->patch.dependencies.size() == 1 && result.change->patch.dependencies[0].object == id);
    result = f.forward(0, {f.erase(clone_id)}); CHECK(result.accepted() && !f.doc().placements.contains(clone_id));
    CHECK(f.doc().tombstones.at(clone_id) == 3);
    CHECK(f.history(0, Intent::Undo).accepted() && f.doc().placements.at(clone_id).revision == 4);
    CHECK(f.history(0, Intent::Redo).accepted() && !f.doc().placements.contains(clone_id));
    auto before = f.doc(); clone.target = captain; clone.expected = before.placements.at(captain).revision;
    CHECK(f.forward(0, {clone}).code == Code::Protected && f.doc() == before);
    CHECK(f.forward(0, {f.erase(captain)}).code == Code::Protected && f.doc() == before);
    create.initial.protected_object = true;
    CHECK(f.forward(0, {create}).code == Code::Invalid && f.doc() == before);
}

static void test_clone_final_pose()
{
    Fixture f;
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = a;
    clone.clone_pose = Pose{{40, 50, 60}, {0, 0, 1, 0}, 2.5f};
    auto result = f.forward(0, {clone}); CHECK(result.accepted());
    auto id = result.change->allocated.front(); const auto& p = f.doc().placements.at(id);
    CHECK(p.position == clone.clone_pose->position && p.rotation == clone.clone_pose->rotation && p.scale == 2.5f);
    CHECK(p.properties == f.doc().placements.at(a).properties && f.core.state().history.size() == 1);
    auto replica = seed(); std::string error; CHECK(apply_committed(replica, result, error)); CHECK(replica == f.core.state());
    auto undo = f.history(0, Intent::Undo); CHECK(undo.accepted() && !f.doc().placements.contains(id));
    CHECK(apply_committed(replica, undo, error));
    auto redo = f.history(0, Intent::Redo); CHECK(redo.accepted() && f.doc().placements.at(id).position == clone.clone_pose->position);
    CHECK(apply_committed(replica, redo, error)); CHECK(replica == f.core.state());
    auto before = f.doc(); clone.clone_pose->position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!f.forward(0, {clone}).accepted() && f.doc() == before);
    clone.clone_pose->position.x = 1; clone.clone_pose->scale = 0;
    CHECK(!f.forward(0, {clone}).accepted() && f.doc() == before);
    clone.clone_pose->scale = 1; clone.clone_pose->rotation = {};
    clone.kind = MutationKind::Delete;
    CHECK(f.forward(0, {clone}).code == Code::Invalid && f.doc() == before);
}

static void test_concurrent_fields_and_batches()
{
    Fixture f; std::array<FieldKey, 4> fields{pos, scale, flag, text};
    std::array<Value, 4> values{Vec3{22, 3, 4}, 2.f, true, std::string("Guest name")};
    std::array<Request, 4> requests;
    for (unsigned p = 0; p < 4; ++p) {
        requests[p] = f.request(p); requests[p].leases = f.grant(p, {field_scope(a, fields[p])});
        requests[p].mutations = {f.set(a, fields[p], values[p])};
    }
    for (unsigned p : {3u, 1u, 0u, 2u}) CHECK(f.core.submit(requests[p], f.now).accepted());
    CHECK(f.doc().revision == 4);
    for (unsigned p = 0; p < 4; ++p) { CHECK(value(f.doc().placements.at(a), fields[p]) == std::optional<Value>(values[p])); f.release_all(p); }
    CHECK(f.doc().placements.at(a).field_revisions.at(text) == 1 && f.doc().placements.at(a).field_revisions.at(flag) == 4);
    auto before = f.doc(); auto bad = f.set(b, scale, -1.f);
    CHECK(f.forward(0, {f.set(b, pos, Vec3{50, 0, 0}), bad}).code == Code::Unsupported && f.doc() == before);
    auto same = f.set(b, pos, Vec3{50, 0, 0});
    CHECK(f.forward(0, {same, same}).code == Code::Invalid && f.doc() == before);
    auto stale = f.set(a, pos, Vec3{99, 0, 0}); stale.expected = 0;
    CHECK(f.forward(1, {stale}).code == Code::Conflict && f.doc() == before);
    CHECK(f.forward(1, {f.set(a, {Field::Property, 999}, true)}).code == Code::Unsupported);
    auto r = f.request(0); r.leases = f.grant(0); r.mutations = {f.set(b, pos, Vec3{60, 0, 0})};
    r.dependencies = {{c, CheckKind::Object, {}, 1}};
    CHECK(f.core.submit(r, f.now).code == Code::Conflict && f.doc() == before); f.release_all(0);
    // Removing an optional property retains its field revision: stale re-adds fail.
    CHECK(f.forward(0, {f.set(a, text, std::nullopt)}).accepted());
    auto readd = f.set(a, text, std::string("Stale")); readd.expected = 0;
    CHECK(f.forward(1, {readd}).code == Code::Conflict);
}

static void test_reference_integrity()
{
    Fixture f;
    CHECK(f.forward(0, {f.set(a, ref, b)}).accepted());
    auto before = f.doc(); CHECK(f.forward(0, {f.erase(b)}).code == Code::Referenced && f.doc() == before);
    auto clear = f.set(a, ref, ObjectId{});
    CHECK(f.forward(0, {clear, f.erase(b)}).accepted());
    CHECK(!f.doc().placements.contains(b) && std::get<ObjectId>(*value(f.doc().placements.at(a), ref)) == ObjectId{});
    CHECK(f.history(0, Intent::Undo).accepted());
    CHECK(f.doc().placements.contains(b) && std::get<ObjectId>(*value(f.doc().placements.at(a), ref)) == b);
    CHECK(f.history(0, Intent::Redo).accepted() && !f.doc().placements.contains(b));
    State s = seed(); s.document.external_references.push_back({{300, 1}, a}); s.document.next_id = 301; Fixture goals({}, s);
    before = goals.doc(); CHECK(goals.forward(0, {goals.erase(a)}).code == Code::Referenced && goals.doc() == before);
    auto wrong = goals.set(a, pos, Vec3{1, 0, 0}); wrong.target.generation++;
    CHECK(goals.forward(0, {wrong}).code == Code::MissingObject && goals.doc() == before);
}

static void test_leases()
{
    Fixture f; auto held = f.grant(1, {field_scope(b, pos)}, 100);
    auto result = f.core.acquire(f.peers[0], {field_scope(a, pos), field_scope(b, pos)}, f.now, 100);
    CHECK(result.code == Code::Conflict && f.core.leases().size() == 1);
    auto own = f.grant(0, {field_scope(a, pos)}, 100);
    auto old_expiry = f.core.leases()[1].expires;
    auto invalid = own; invalid.push_back({field_scope(c, pos), 99999});
    CHECK(f.core.renew(f.peers[0], invalid, f.now, 1000).code == Code::LeaseExpired && f.core.leases()[1].expires == old_expiry);
    CHECK(!f.core.release(f.peers[0], invalid) && f.core.leases().size() == 2);
    auto request = f.request(0); request.leases = own; request.mutations = {f.set(a, pos, Vec3{1, 0, 0})};
    f.now = 200; CHECK(f.core.submit(request, f.now).code == Code::LeaseExpired);
    auto newer = f.grant(0, {field_scope(a, pos)}, 100); CHECK(newer[0].token != own[0].token);
    auto stale = f.request(0); stale.leases = own; stale.mutations = request.mutations;
    CHECK(f.core.submit(stale, f.now).code == Code::LeaseExpired);
    CHECK(f.core.submit(request, f.now).code == Code::LeaseExpired); // Original decision retained.
    f.release_all(0);
    CHECK(f.core.acquire(f.peers[0], {{ScopeKind::Class, class_a, {}}}, f.now, 100).accepted());
    CHECK(f.core.acquire(f.peers[1], {field_scope(b, flag)}, f.now, 100).code == Code::Conflict);
    CHECK(f.core.acquire(f.peers[1], {field_scope(c, flag)}, f.now, 100).accepted());
    CHECK(f.core.acquire(f.peers[2], {{ScopeKind::Act, act_b, {}}}, f.now, 100).code == Code::Conflict);
    f.release_all(0); f.release_all(1);
    auto peer0 = f.peers[0]; CHECK(f.core.connect({0, 2}));
    CHECK(f.core.submit(stale, f.now).code == Code::StalePeer);
    CHECK(!f.core.connect(peer0));
    f.peers[0] = {0, 2}; CHECK(f.history(0, Intent::Undo).code == Code::NothingToUndo);
    f.core.disconnect(f.peers[0]); CHECK(!f.core.connect(f.peers[0]));
    CHECK(f.core.acquire(f.peers[0], {{}}, f.now, 100).code == Code::StalePeer);
}

static void test_undo_chronology_and_conflict()
{
    Fixture f;
    CHECK(f.forward(0, {f.set(a, pos, Vec3{1, 0, 0})}).accepted());
    CHECK(f.forward(0, {f.set(b, pos, Vec3{2, 0, 0})}).accepted());
    CHECK(f.forward(1, {f.set(b, pos, Vec3{3, 0, 0})}).accepted());
    auto before = f.doc(); auto history = f.core.state().history;
    CHECK(f.history(0, Intent::Undo).code == Code::Conflict);
    CHECK(f.doc() == before && f.core.state().history == history);
    CHECK(field_revision(f.doc().placements.at(a), pos) == history[0].undo.fields[0].expected);
    CHECK(value(f.doc().placements.at(a), pos) == history[0].undo.fields[0].before);
    CHECK(f.doc().placements.at(a).position.x == 1); // Older independent action was not skipped to.
    CHECK(f.history(1, Intent::Undo).accepted());
    // Same value restored at a newer revision still cannot silently rebase Undo.
    CHECK(f.history(0, Intent::Undo).code == Code::Conflict);

    Fixture order;
    for (auto [id, x] : std::array<std::pair<ObjectId, float>, 3>{{{a, 1}, {b, 2}, {c, 3}}}) CHECK(order.forward(0, {order.set(id, pos, Vec3{x, 0, 0})}).accepted());
    CHECK(order.history(0, Intent::Undo).accepted() && order.doc().placements.at(c).position.x == 12);
    CHECK(order.history(0, Intent::Undo).accepted() && order.doc().placements.at(b).position.x == 11);
    CHECK(order.history(0, Intent::Redo).accepted() && order.doc().placements.at(b).position.x == 2 && order.doc().placements.at(c).position.x == 12);
    CHECK(order.history(0, Intent::Redo).accepted() && order.doc().placements.at(c).position.x == 3);
    CHECK(order.core.state().history.size() == 3); // Undo/Redo are linked transitions, not gestures.
}

static void test_own_multistep_and_structural_history()
{
    Fixture f;
    for (float x : {1.f, 2.f, 3.f}) CHECK(f.forward(0, {f.set(a, pos, Vec3{x, 0, 0})}).accepted());
    for (float x : {2.f, 1.f, 10.f}) { CHECK(f.history(0, Intent::Undo).accepted()); CHECK(f.doc().placements.at(a).position.x == x); }
    CHECK(f.history(0, Intent::Undo).code == Code::NothingToUndo);
    for (float x : {1.f, 2.f, 3.f}) { CHECK(f.history(0, Intent::Redo).accepted()); CHECK(f.doc().placements.at(a).position.x == x); }
    CHECK(f.history(0, Intent::Redo).code == Code::NothingToRedo);
    // Restoring logical versions must survive persistence, not rely on a private
    // stack of transient native history pointers.
    auto saved = f.core.state(); std::string error; CHECK(f.core.reset(saved, 2, error));
    for (auto p : f.peers) CHECK(f.core.connect(p));
    CHECK(f.history(0, Intent::Undo).accepted() && f.doc().placements.at(a).position.x == 2);
    CHECK(f.history(0, Intent::Undo).accepted() && f.doc().placements.at(a).position.x == 1);

    Fixture structural;
    Mutation create; create.kind = MutationKind::Create; create.initial.class_id = class_a; create.initial.act_id = act_a;
    auto created = structural.forward(0, {create}); CHECK(created.accepted()); auto id = created.change->allocated[0];
    CHECK(structural.forward(0, {structural.set(id, pos, Vec3{2, 4, 6})}).accepted());
    CHECK(structural.forward(0, {structural.set(id, text, std::string("New name"))}).accepted());
    CHECK(structural.forward(0, {structural.erase(id)}).accepted());
    for (int i = 0; i < 4; ++i) CHECK(structural.history(0, Intent::Undo).accepted());
    CHECK(!structural.doc().placements.contains(id));
    for (int i = 0; i < 4; ++i) CHECK(structural.history(0, Intent::Redo).accepted());
    CHECK(!structural.doc().placements.contains(id));
    CHECK(structural.history(0, Intent::Undo).accepted());
    CHECK(structural.doc().placements.at(id).position == (Vec3{2, 4, 6}));
    CHECK(std::get<std::string>(*value(structural.doc().placements.at(id), text)) == "New name");

    // An unrelated other-author field change must survive own Undo and keep the
    // older whole-object deletion inverse blocked.
    Fixture cross; auto original = cross.forward(0, {create}); id = original.change->allocated[0];
    CHECK(cross.forward(0, {cross.set(id, pos, Vec3{1, 2, 3})}).accepted());
    CHECK(cross.forward(1, {cross.set(id, scale, 2.f)}).accepted());
    CHECK(cross.history(0, Intent::Undo).accepted());
    auto before = cross.doc(); auto history = cross.core.state().history;
    CHECK(cross.history(0, Intent::Undo).code == Code::Conflict && cross.doc() == before && cross.core.state().history == history);
    CHECK(cross.doc().placements.at(id).scale == 2.f);
}

static void test_redo_and_author_isolation()
{
    Fixture f;
    CHECK(f.forward(0, {f.set(a, pos, Vec3{1, 0, 0})}).accepted());
    CHECK(f.history(0, Intent::Undo).accepted());
    CHECK(f.forward(1, {f.set(b, pos, Vec3{2, 0, 0})}).accepted());
    CHECK(f.core.state().history[0].redo_available);
    CHECK(f.history(0, Intent::Redo).accepted());
    CHECK(f.doc().placements.at(a).position.x == 1 && f.doc().placements.at(b).position.x == 2);
    CHECK(f.history(0, Intent::Undo).accepted());
    CHECK(f.forward(1, {f.set(a, pos, Vec3{7, 0, 0})}).accepted());
    auto before = f.doc(); auto history = f.core.state().history;
    CHECK(f.history(0, Intent::Redo).code == Code::Conflict && f.doc() == before && f.core.state().history == history);
    CHECK(f.forward(0, {f.set(c, pos, Vec3{9, 0, 0})}).accepted());
    CHECK(!f.core.state().history[0].redo_available && !f.core.state().history[0].applied);
    CHECK(f.history(0, Intent::Redo).code == Code::NothingToRedo);
}

static void test_dedup_retention_and_epoch()
{
    Limits limits; limits.decisions = 3; limits.history = 2; Fixture f(limits);
    auto r = f.request(0); r.leases = f.grant(0); r.mutations = {f.set(a, pos, Vec3{1, 0, 0})};
    int applications = 0; Hooks hooks; hooks.apply = [&](const auto&, const auto&, const auto&, auto&) { applications++; return true; };
    auto accepted = f.core.submit(r, f.now, hooks); CHECK(accepted.accepted()); f.release_all(0);
    CHECK(f.core.submit(r, 9000, hooks) == accepted && applications == 1); // Dedup before lease/time checks.
    auto changed_retry = r; changed_retry.mutations[0].value = Vec3{999, 0, 0}; CHECK(f.core.submit(changed_retry, f.now, hooks) == accepted);
    CHECK(f.forward(0, {f.set(a, pos, Vec3{2, 0, 0})}).accepted());
    CHECK(f.forward(0, {f.set(a, pos, Vec3{3, 0, 0})}).accepted());
    CHECK(f.forward(0, {f.set(a, pos, Vec3{4, 0, 0})}).accepted());
    auto before = f.doc(); CHECK(f.core.submit(r, f.now).code == Code::ExpiredOperation && f.doc() == before);
    CHECK(f.core.state().history.size() == 2 && f.core.state().history_truncated.contains(f.peers[0]));
    CHECK(f.history(0, Intent::Undo).accepted());
    CHECK(f.history(0, Intent::Undo).accepted());
    CHECK(f.history(0, Intent::Undo).code == Code::HistoryUnavailable);
    auto saved = f.core.state(); std::string error;
    CHECK(f.core.reset(saved, 2, error)); for (auto p : f.peers) CHECK(f.core.connect(p));
    f.sequence[0] = 0;
    CHECK(f.forward(0, {f.set(b, pos, Vec3{44, 0, 0})}).accepted()); // Sequence 1 is new in epoch 2.
    before = f.doc(); CHECK(f.core.submit(r, f.now).code == Code::StaleEpoch && f.doc() == before);
    auto stale_project = f.request(0); stale_project.project[0]++; CHECK(f.core.submit(stale_project, f.now).code == Code::StaleProject);
    auto corrupt = f.core.state(); corrupt.history.back().transition_revision = UINT64_MAX;
    CHECK(!f.core.reset(corrupt, 3, error) && f.doc() == before);

    Fixture reorder;
    auto leases = reorder.grant(0);
    auto high = reorder.request(0); high.operation.sequence = 20; high.leases = leases; high.mutations = {reorder.set(a, pos, Vec3{20, 0, 0})};
    auto low = reorder.request(0); low.operation.sequence = 2; low.leases = leases; low.mutations = {reorder.set(b, pos, Vec3{2, 0, 0})};
    CHECK(reorder.core.submit(high, reorder.now).accepted() && reorder.core.submit(low, reorder.now).accepted());
}

static void test_hooks_and_atomicity()
{
    for (int stage = 0; stage < 4; ++stage) {
        Fixture f; auto before = f.doc(); auto tokens = f.grant(0); auto r = f.request(0); r.leases = tokens; r.mutations = {f.set(a, pos, Vec3{1, 0, 0})};
        int applied = 0, rolled_back = 0, persisted = 0; Hooks hooks;
        hooks.validate = [&](const Document&, const Document&, const Change&, std::string& error) { if (stage == 0) { error = "Native rejects"; return false; } return true; };
        hooks.apply = [&](const Document& old_doc, const Document& next, const Change& change, std::string&) {
            CHECK(f.core.applying() && old_doc == before && next.revision == 1 && change.revision == 1);
            CHECK(f.core.submit(r, f.now).code == Code::Busy); applied++; return stage != 1;
        };
        hooks.rollback = [&](const Document&, const Document&, const Change&, std::string&) { rolled_back++; return stage != 3; };
        hooks.persist = [&](const State& next, const Change& change, std::string& error) {
            persisted++; CHECK(f.doc() == before && next.document.revision == 1 && next.history.size() == 1);
            CHECK(next.decisions.at(r.operation).accepted() && change.inverse.fields[0].expected == 1);
            error = "Disk full"; return false;
        };
        auto decision = f.core.submit(r, f.now, hooks);
        CHECK(f.doc() == before && f.core.state().history.empty());
        if (stage == 0) CHECK(decision.code == Code::NativeRejected && applied == 0 && rolled_back == 0 && persisted == 0);
        if (stage == 1) CHECK(decision.code == Code::NativeFailed && applied == 1 && rolled_back == 1 && persisted == 0);
        if (stage == 2) CHECK(decision.code == Code::PersistenceFailed && applied == 1 && rolled_back == 1 && persisted == 1 && !f.core.frozen());
        if (stage == 3) CHECK(decision.code == Code::Frozen && rolled_back == 1 && f.core.frozen());
        CHECK(f.core.submit(r, f.now, hooks) == decision);
    }
    Fixture f; auto before = f.doc(); Hooks hooks;
    hooks.apply = [](const auto&, const auto&, const auto&, auto&) -> bool { throw std::runtime_error("Native exception"); };
    hooks.rollback = [](const auto&, const auto&, const auto&, auto&) { return true; };
    CHECK(f.forward(0, {f.set(a, pos, Vec3{1, 0, 0})}, hooks).code == Code::NativeFailed && f.doc() == before && !f.core.frozen());
    hooks = {}; hooks.persist = [](const auto&, const auto&, auto&) { return false; };
    CHECK(f.forward(0, {f.set(a, pos, Vec3{2, 0, 0})}, hooks).code == Code::PersistenceFailed && f.doc() == before);
    hooks.persist = [](const auto&, const auto&, auto&) -> bool { throw std::runtime_error("I/O exception"); };
    CHECK(f.forward(0, {f.set(a, pos, Vec3{3, 0, 0})}, hooks).code == Code::Frozen && f.doc() == before && f.core.frozen());
}

static void test_four_author_adversity()
{
    Limits limits; limits.history = 12; limits.decisions = 32; Fixture f(limits);
    uint32_t random = 0x51A2F7;
    auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    std::array<ObjectId, 3> ids{a, b, c};
    for (unsigned i = 0; i < 300; ++i) {
        unsigned p = next() % 4; auto id = ids[next() % ids.size()];
        auto tokens = f.grant(p, {field_scope(id, pos)});
        auto r = f.request(p); r.leases = tokens;
        auto edit = f.set(id, pos, Vec3{float(i + 100), float(p), 0});
        bool stale = i > 5 && (next() % 4 == 0); if (stale) edit.expected = 0;
        r.mutations = {edit}; auto before = f.doc(); auto decision = f.core.submit(r, f.now);
        CHECK(decision.accepted() == !stale);
        CHECK(f.core.submit(r, f.now) == decision);
        CHECK(f.doc().revision == before.revision + (decision.accepted() ? 1 : 0));
        if (!decision.accepted()) CHECK(f.doc() == before);
        std::string error; CHECK(valid(f.doc(), limits, error));
        CHECK(f.core.state().history.size() <= limits.history && f.core.state().decisions.size() <= limits.decisions);
        f.release_all(p);
    }
}

static void test_replica_and_nonce_recovery()
{
    Fixture host; State replica = seed(); std::string error;
    int applied = 0, persisted = 0; Hooks hooks;
    hooks.apply = [&](const Document&, const Document&, const Change&, std::string&) { applied++; return true; };
    hooks.persist = [&](const State&, const Change&, std::string&) { persisted++; return true; };
    for (float x : {1.f, 2.f, 3.f}) {
        auto decision = host.forward(0, {host.set(a, pos, Vec3{x, 0, 0})}); CHECK(decision.accepted());
        CHECK(apply_committed(replica, decision, error, hooks)); CHECK(replica.document == host.doc());
        CHECK(apply_committed(replica, decision, error, hooks));
    }
    CHECK(applied == 3 && persisted == 3);
    for (int i = 0; i < 3; ++i) { auto d = host.history(0, Intent::Undo); CHECK(apply_committed(replica, d, error, hooks)); CHECK(replica.history == host.core.state().history); }
    for (int i = 0; i < 3; ++i) { auto d = host.history(0, Intent::Redo); CHECK(apply_committed(replica, d, error, hooks)); CHECK(replica.history == host.core.state().history); }
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = a; clone.expected = host.doc().placements.at(a).revision;
    auto d = host.forward(0, {clone}); CHECK(d.change->clone_sources.at(d.change->allocated.front()) == a);
    CHECK(apply_committed(replica, d, error, hooks) && replica.document == host.doc());
    auto next = host.forward(1, {host.set(b, pos, Vec3{20, 0, 0})}); auto corrupt = next;
    corrupt.change->inverse.fields[0].after = Vec3{999, 0, 0}; auto before = replica;
    int previous_applied = applied; CHECK(!apply_committed(replica, corrupt, error, hooks) && replica == before && applied == previous_applied);
    CHECK(apply_committed(replica, next, error, hooks));
    auto missing = host.forward(1, {host.set(b, scale, 2.f)});
    auto ahead = host.forward(2, {host.set(c, pos, Vec3{30, 0, 0})}); before = replica;
    CHECK(!apply_committed(replica, ahead, error, hooks) && replica == before);
    CHECK(apply_committed(replica, missing, error, hooks) && apply_committed(replica, ahead, error, hooks));
    auto saved = host.core.state(); saved.document.epoch = UINT64_C(0xFFF0000000000000);
    CHECK(host.core.reset(saved, UINT64_C(0x1234), error)); // Epochs are random nonces, not ordered counters.
    CHECK(host.core.state().document.epoch == 0x1234 && !host.core.state().history.empty());
}

int main()
{
    test_validation(); test_create_clone_delete(); test_clone_final_pose(); test_concurrent_fields_and_batches(); test_reference_integrity(); test_leases();
    test_undo_chronology_and_conflict(); test_own_multistep_and_structural_history(); test_redo_and_author_isolation(); test_dedup_retention_and_epoch(); test_hooks_and_atomicity(); test_four_author_adversity(); test_replica_and_nonce_recovery();
    std::printf("editor_document: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
