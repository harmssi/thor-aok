#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Felling.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;

} // namespace

// A tree placed as a scenario or the editor places it draws as its standing
// graphic's pieces, the tree and its shadow; felled, it draws as the felled
// trunk, its dying graphic, with no standing piece left over it.
bool CheckFelledPlaced() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const int tree_id = Core::ConfigManager::Get().GetUnitIdByName("FOAK");
    const Core::Rules::UnitRules* rules = Core::Rules::Unit(tree_id);
    const char* verdict = nullptr;
    int ox = -1, oy = -1;
    for (int y = 2; y < grid.GetHeight() - 2 && ox < 0; ++y) {
        for (int x = 2; x < grid.GetWidth() - 2 && ox < 0; ++x) {
            if (grid.IsWalkable(x, y) && !grid.IsBlocked(x, y)) ox = x, oy = y;
        }
    }
    if (rules == nullptr || ox < 0) {
        verdict = "no oak or no open tile";
    } else {
        const entt::entity tree = ECS::ScenarioWorld::Place(reg, grid, tree_id, Core::PLAYER_GAIA,
                                                            {Core::Fixed32(ox), Core::Fixed32(oy)}, Core::Fixed32(0), 0);
        sim.Update(16);
        ECS::Felling::Fell(reg, tree);
        sim.Update(16);
        const int felled_slp = Core::ConfigManager::Get().GetGraphic(rules->dying_graphic).slp_id;
        if (tree == entt::null || !reg.valid(tree)) {
            verdict = "an oak placed is not there";
        } else if (reg.all_of<C::SpritePieces>(tree)) {
            verdict = "a placed tree felled still draws its standing pieces";
        } else if (reg.get<C::Sprite>(tree).slp_id != felled_slp) {
            verdict = "a placed tree felled is not drawn as the felled trunk";
        }
    }
    std::printf("felledplaced=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
