#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Resources.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTickMs = 16;
constexpr int kTreeClass = 15;
// The dat's corpse decay time for a tree's stump, in seconds.
constexpr int kStumpSeconds = 180;

// How many sprites of the stump's art stand at `at`.
int StumpsAt(entt::registry& reg, const C::Vector2D& at, int stump_slp) {
    int n = 0;
    auto view = reg.view<const C::Transform, const C::Sprite>();
    for (auto e : view) {
        const auto& p = view.get<const C::Transform>(e).position;
        n += p.x == at.x && p.y == at.y && view.get<const C::Sprite>(e).slp_id == stump_slp ? 1 : 0;
    }
    return n;
}

} // namespace

// A tree whose wood runs out: its tile is open at once, and its stump lies for
// the dat's decay time, three minutes, and is gone after it.
bool CheckStumps() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    entt::entity tree = entt::null;
    auto view = reg.view<const C::Identity, const C::Resource, const C::Transform>();
    for (auto e : view) {
        const auto* rules = Core::Rules::Unit(view.get<const C::Identity>(e).unit_id);
        if (tree == entt::null && rules != nullptr && rules->cls == kTreeClass) tree = e;
    }
    if (tree == entt::null) {
        verdict = "no tree on the map";
    } else {
        const C::Vector2D at = reg.get<C::Transform>(tree).position;
        const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
        const auto* rules = Core::Rules::Unit(reg.get<C::Identity>(tree).unit_id);
        const int stump_slp = Core::ConfigManager::Get().GetObjectArt(rules->dead_unit).slp_id;
        ECS::Resources::Exhaust(reg, grid, tree);
        if (grid.IsBlocked(x, y)) {
            verdict = "a used-up tree's tile stays blocked";
        } else if (StumpsAt(reg, at, stump_slp) != 1) {
            verdict = "a used-up tree leaves no stump";
        } else {
            for (int t = 0; t < (kStumpSeconds - 1) * 1000 / kTickMs; ++t) sim.Update(kTickMs);
            if (StumpsAt(reg, at, stump_slp) != 1) {
                verdict = "a stump is gone before its decay time";
            } else {
                for (int t = 0; t < 2 * 1000 / kTickMs; ++t) sim.Update(kTickMs);
                if (StumpsAt(reg, at, stump_slp) != 0) verdict = "a stump stays after its decay time";
            }
        }
    }
    std::printf("stumps=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
