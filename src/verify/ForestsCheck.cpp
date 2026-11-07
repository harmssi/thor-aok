#include "verify/Harness.h"

#include <cstdio>
#include <string>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTreeClass = 15;

} // namespace

// Every random map, as its script lays it: each tile of ground that carries
// trees -- forest, a script's base forest among it -- has its tree, and no
// tree stands on ground land units could not walk once it is used up, such
// as a pond laid over a forest clearing.
bool CheckForests() {
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    int maps = 0, bare = 0, drowned = 0;
    for (int m = 0; m < Core::kMapScriptCount && verdict == "ok"; ++m) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.map_script = Core::kMapScripts[m];
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        const auto& grid = sim.GetGrid();
        const int w = grid.GetWidth();
        std::vector<uint8_t> treed(static_cast<size_t>(w * grid.GetHeight()), 0);
        auto trees = reg.view<const C::Identity, const C::Resource, const C::Transform>();
        for (auto e : trees) {
            const auto* rules = Core::Rules::Unit(trees.get<const C::Identity>(e).unit_id);
            if (rules == nullptr || rules->cls != kTreeClass) continue;
            const auto& at = trees.get<const C::Transform>(e).position;
            const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
            treed[static_cast<size_t>(y * w + x)] = 1;
            if (!grid.LandPassable(grid.GetTile(x, y).terrain)) ++drowned;
        }
        for (int y = 0; y < grid.GetHeight(); ++y) {
            for (int x = 0; x < w; ++x) {
                const auto& art = cfg.GetTerrainArt(grid.GetTile(x, y).terrain);
                if (art.tree_unit_id >= 0 && !treed[static_cast<size_t>(y * w + x)]) ++bare;
            }
        }
        ++maps;
        if (bare > 0) verdict = std::string(Core::kMapScripts[m]) + " has forest ground with no tree";
        else if (drowned > 0) verdict = std::string(Core::kMapScripts[m]) + " has a tree standing where nobody can walk";
    }
    std::printf("forests=%s maps=%d bare=%d drowned=%d\n", verdict.c_str(), maps, bare, drowned);
    return verdict == "ok";
}

} // namespace Verify
