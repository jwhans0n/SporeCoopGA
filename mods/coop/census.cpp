// Census of the shared objects (census.h, docs/06-testing.md#testing-census).
#include "census.h"

#include <cstdio>
#include <cstring>

#include "game.h"
#include "modes.h"
#include "npc.h"

namespace census {

static const SkHost* H;
static SkMod* M;
static Ctx C;

#define SAY(...) H->out_printf(out, __VA_ARGS__)

constexpr uint32_t kCreatureNoun = 0x18EB45Eu; // cCreatureAnimal (docs: SporeCoop::NounCast)

static void creature_rows(std::vector<Row>& rows)
{
    struct U {
        std::vector<Row>* rows;
        void* av;
    } u{&rows, game::avatar()};
    game::for_each_object(
        [](void* o, uint32_t noun, void* p) {
            auto* u = (U*)p;
            if (noun != kCreatureNoun || !game::object_alive(o) || u->rows->size() >= 2000)
                return true;
            Row r;
            r.kind = 'c';
            r.key = game::species_key(o).instance;
            if (!r.key && o != u->av) // a creature object without a species: not placed in the world
                return true;
            r.alive = game::is_dead(o) ? 0 : 1;
            r.hp = game::health(o);
            r.ai = game::is_puppet(o) ? 0 : 1;
            r.pos = game::position(o);
            int pid = npc::proxy_id(o), hid = npc::host_id(o);
            if (o == u->av)
                r.own = "avatar";
            else if (pid >= 0) {
                r.own = "proxy";
                r.id = pid;
            } else if (hid >= 0) {
                r.own = "npc";
                r.id = hid;
            } else
                r.own = r.ai ? "local" : "puppet";
            u->rows->push_back(r);
            return true;
        },
        &u);
}

static int c_census(void*, int, const char* const*, SkOut* out)
{
    uint32_t mode = 0, world = 0;
    if (C.place)
        C.place(mode, world);
    bool conn = C.sess && C.sess->connected();
    const char* role = !conn ? "none" : C.sess->role() == net::Role::Host ? "host" : "guest";
    SAY("census v1 role=%s me=%u mode=%s modeid=%08X world=%08X t=%.1f\n", role, conn ? C.sess->my_id() : 0,
        modes::mode_name(mode), mode, world, H->time_seconds());
    std::vector<Row> rows;
    if (mode == modes::kCreature)
        creature_rows(rows);
    else if (mode == modes::kCiv)
        civ::census_rows(rows);
    std::string buf;
    for (auto& r : rows) { // a console answer is built from pieces of at most 8 KB
        char b[256];
        snprintf(b, sizeof b, "obj %c%d key=%08X alive=%d hp=%.1f ai=%d own=%s owner=%d pos=(%.1f %.1f %.1f)\n", r.kind,
                 r.id, r.key, r.alive, r.hp, r.ai, r.own, r.owner, r.pos.x, r.pos.y, r.pos.z);
        if (buf.size() + strlen(b) > 7000) {
            SAY("%s", buf.c_str());
            buf.clear();
        }
        buf += b;
    }
    if (!buf.empty())
        SAY("%s", buf.c_str());
    SAY("end objects=%u\n", (unsigned)rows.size());
    return 0;
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    H->command_add(M, "coop.census",
                   "coop.census - the shared objects of this mode as the game has them, with net ids (tests: "
                   "tools/tests/census.py)",
                   c_census, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace census
