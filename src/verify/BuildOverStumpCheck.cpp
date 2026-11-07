#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Felling.h"
#include "ecs/Resources.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr Core::PlayerId kPlayer = 1;
// Room for two houses side by side, a tile apart.
constexpr int kHalf = 4;

int CorpsesAt(entt::registry& reg, int x, int y) {
    int n = 0;
    auto view = reg.view<const C::Corpse, const C::Transform>();
    for (auto e : view) {
        const auto& p = view.get<const C::Transform>(e).position;
        n += Physics::TileOf(p.x) == x && Physics::TileOf(p.y) == y ? 1 : 0;
    }
    return n;
}

} // namespace

// A tree used up leaves a stump, which the dat lets a building be laid over:
// a house can be placed there, and its laying clears the stump. A felled tree
// still holding wood keeps its ground: no house can be placed over it.
bool CheckBuildOverStump() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;
    int ox = 0, oy = 0;
    for (int y = kHalf; y < grid.GetHeight() - kHalf && ox == 0; ++y) {
        for (int x = kHalf; x < grid.GetWidth() - kHalf && ox == 0; ++x) {
            bool clear = true;
            for (int dy = -kHalf; dy <= kHalf && clear; ++dy) {
                for (int dx = -kHalf; dx <= kHalf && clear; ++dx) clear = grid.IsWalkable(x + dx, y + dy);
            }
            if (clear) ox = x, oy = y;
        }
    }
    const int house = cfg.GetUnitIdByName("HOUS");
    const int tree = cfg.GetUnitIdByName("FOAK");
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int sx = ox - 2, fx = ox + 2;
        const entt::entity used = ECS::ScenarioWorld::Place(reg, grid, tree, Core::PLAYER_GAIA,
                                                            {Core::Fixed32(sx), Core::Fixed32(oy)}, Core::Fixed32(0), 0);
        const entt::entity felled = ECS::ScenarioWorld::Place(reg, grid, tree, Core::PLAYER_GAIA,
                                                              {Core::Fixed32(fx), Core::Fixed32(oy)}, Core::Fixed32(0), 0);
        ECS::Felling::Fell(reg, felled);
        ECS::Resources::Exhaust(reg, grid, used);
        if (CorpsesAt(reg, sx, oy) != 1) {
            verdict = "a used-up tree leaves no stump";
        } else if (ECS::Buildings::CanPlace(grid, house, fx, oy)) {
            verdict = "a house can be placed over a felled tree still holding wood";
        } else if (!ECS::Buildings::CanPlace(grid, house, sx, oy)) {
            verdict = "a house cannot be placed over a stump";
        } else {
            ECS::Buildings::Place(reg, grid, house, kPlayer, sx, oy, false);
            if (CorpsesAt(reg, sx, oy) != 0) verdict = "a house laid over a stump leaves the stump";
        }
    }
    std::printf("buildstump=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
