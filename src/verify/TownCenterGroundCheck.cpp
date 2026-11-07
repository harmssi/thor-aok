#include "verify/Harness.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <utility>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
namespace R = Core::Rules;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kWalkSeconds = 40;
constexpr Core::PlayerId kPlayer = 1;

} // namespace

// A Town Center, which the dat gives no obstruction, keeps units off only its
// back, the annex that has one: the rest of its footprint is ground units
// walk, and a villager sent there gets there. Nothing else is built on any of
// it.
bool CheckTownCenterGround() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const int town_centre = cfg.GetUnitIdByName("RTWC");
    const char* verdict = nullptr;
    entt::entity tc = entt::null;
    auto buildings = reg.view<const C::Identity, const C::Owner, const C::Building>();
    for (auto e : buildings) {
        if (tc == entt::null && buildings.get<const C::Identity>(e).unit_id == town_centre &&
            buildings.get<const C::Owner>(e).player_id == kPlayer) {
            tc = e;
        }
    }
    const R::UnitRules* rules = R::Unit(town_centre);
    int solid = 0, open = 0;
    double off = 0;
    if (tc == entt::null || rules == nullptr) {
        verdict = "no Town Center";
    } else {
        const C::Vector2D origin = reg.get<C::Transform>(tc).position;
        const int ox = Physics::TileOf(origin.x), oy = Physics::TileOf(origin.y);
        // The tiles of its pieces that keep units off, from the dat.
        std::set<std::pair<int, int>> kept_off;
        for (int i = 0; i < rules->annex_count; ++i) {
            const R::UnitRules* piece = R::Unit(rules->annexes[i].unit);
            if (piece == nullptr || piece->obstruction == R::kOpenGround) continue;
            const C::Vector2D d = ECS::Buildings::FromDatAxes(rules->annexes[i].dx, rules->annexes[i].dy);
            ECS::Buildings::ForEachFootprintTile(rules->annexes[i].unit, Physics::TileOf(origin.x + d.x),
                                                 Physics::TileOf(origin.y + d.y),
                                                 [&](int x, int y) { kept_off.insert({x, y}); });
        }
        // The open tile farthest from the back: where the villager is sent.
        int far_x = ox, far_y = oy, far = -1;
        ECS::Buildings::ForEachFootprintTile(town_centre, ox, oy, [&](int x, int y) {
            const bool back = kept_off.count({x, y}) != 0;
            if (!grid.IsBlocked(x, y)) verdict = "a Town Center's ground can be built on";
            if (back) {
                ++solid;
                if (grid.IsWalkable(x, y)) verdict = "units walk through a Town Center's back";
            } else {
                ++open;
                if (!grid.IsWalkable(x, y)) verdict = "units are kept off a Town Center's open ground";
                if (x + y > far) far = x + y, far_x = x, far_y = y;
            }
        });
        if (verdict == nullptr && (solid == 0 || open == 0)) verdict = "the dat gives a Town Center no open ground";
        if (verdict == nullptr && ECS::Buildings::CanPlace(grid, cfg.GetUnitIdByName("HOUS"), far_x, far_y)) {
            verdict = "a house can be placed on a Town Center";
        }
        if (verdict == nullptr) {
            entt::entity villager = entt::null;
            auto workers = reg.view<const C::Worker, const C::Owner>();
            for (auto e : workers) {
                if (villager == entt::null && workers.get<const C::Owner>(e).player_id == kPlayer) villager = e;
            }
            if (villager == entt::null) {
                verdict = "no villager";
            } else {
                Core::CommandQueue::Get().PushCommand(
                    Core::MoveCommand{villager, Core::Fixed32(far_x), Core::Fixed32(far_y), kPlayer});
                for (int t = 0; t < kWalkSeconds * kTicksPerSecond; ++t) sim.Update(16);
                const auto& at = reg.get<C::Transform>(villager).position;
                off = std::hypot(static_cast<double>(at.x.ToFloat()) - far_x,
                                 static_cast<double>(at.y.ToFloat()) - far_y);
                if (off > 0.5) verdict = "a villager sent onto a Town Center's open ground stops short";
            }
        }
    }
    std::printf("tcground=%s open=%d back=%d off=%.2f\n", verdict == nullptr ? "ok" : verdict, open, solid, off);
    return verdict == nullptr;
}

} // namespace Verify
