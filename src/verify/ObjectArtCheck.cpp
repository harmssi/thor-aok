#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "physics/Grid.h"
#include "ecs/components/Transform.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Identity.h"
#include "ecs/Players.h"
#include "ecs/Buildings.h"
#include "core/Media.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Sprite.h"
#include "editor/EditorState.h"
#include "hal/SlpFile.h"
#include "scenario/Campaign.h"
#include "scenario/Scenario.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
namespace R = Core::Rules;

constexpr int kCivs = 13;
constexpr int kAges = 4;
const char* const kDemo = "../original_game_1999/GOODIES/DEMO.SCN";
const char* const kGraphicsDrs = "../assets/original/Data/graphics.drs";
const char* const kCampaigns[] = {
    "../assets/original/Campaign/cam1.cpn", "../assets/original/Campaign/cam2.cpn",
    "../assets/original/Campaign/cam3.cpn", "../assets/original/Campaign/cam4.cpn",
    "../assets/original/Campaign/cam8.cpn",
};
// The test map's side, and how far apart one owner's objects stand: room for
// the largest footprint.
constexpr int kMapSide = 240;
constexpr int kSpot = 6;
constexpr int kTicksForSprites = 1;

// Every object a game can hold: what the scenario editor offers, and what any
// scenario or campaign on the CD places, for any of its players or Gaia.
std::set<int> Objects(std::string& error) {
    std::set<int> ids;
    const auto& units = R::Get().units;
    for (int id = 0; id < static_cast<int>(units.size()); ++id) {
        if (units[static_cast<size_t>(id)].valid && !units[static_cast<size_t>(id)].hide_in_editor) ids.insert(id);
    }
    auto take = [&](const Scenario::File& f) {
        for (const auto& player : f.players) {
            for (const auto& unit : player.units) ids.insert(unit.unit_id);
        }
    };
    Scenario::File file;
    if (Scenario::Load(kDemo, -1, file, &error)) take(file);
    for (const char* path : kCampaigns) {
        Scenario::Campaign campaign;
        if (!Scenario::LoadCampaign(path, campaign, &error)) continue;
        for (size_t i = 0; i < campaign.scenarios.size(); ++i) {
            if (Scenario::Load(path, static_cast<int>(i), file, &error)) take(file);
        }
    }
    return ids;
}

bool Present(const HAL::DrsArchive& drs, int slp_id) {
    if (slp_id < 0) return false;
    const auto frames = HAL::SlpReadHeaders(drs, static_cast<uint32_t>(slp_id));
    return std::any_of(frames.begin(), frames.end(),
                       [](const HAL::SlpFrameHeader& f) { return f.width > 0 && f.height > 0; });
}

// Whether `entity`, or any piece of it, draws something: a sprite, or a piece
// of a composite (a ship's hull and sails), of art the archive holds; or, for
// a farm, the farmland laid on its ground, which is how a farm shows.
bool Drawn(entt::registry& reg, const Physics::Grid& grid, const HAL::DrsArchive& drs, entt::entity entity,
           Core::TerrainType ground_before) {
    auto draws = [&](entt::entity e) {
        if (const auto* sprite = reg.try_get<C::Sprite>(e);
            sprite != nullptr && !sprite->is_shadow && Present(drs, sprite->slp_id)) {
            return true;
        }
        if (const auto* pieces = reg.try_get<C::SpritePieces>(e)) {
            for (int i = 0; i < pieces->count; ++i) {
                if (Present(drs, pieces->piece[i].slp_id)) return true;
            }
        }
        return false;
    };
    if (draws(entity)) return true;
    auto parts = reg.view<const C::PartOf>();
    for (auto e : parts) {
        if (parts.get<const C::PartOf>(e).building == entity && draws(e)) return true;
    }
    const auto* identity = reg.try_get<C::Identity>(entity);
    const R::UnitRules* rules = identity != nullptr ? R::Unit(identity->unit_id) : nullptr;
    if (rules != nullptr && ECS::Buildings::LaysCrop(*rules)) {
        const auto& at = reg.get<C::Transform>(entity).position;
        return grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).terrain != ground_before;
    }
    return false;
}

// Whether the CD holds any art for `unit_id` as `owner` plays it: its own
// standing graphic or a piece of it, a building's art by name in any age, or
// its composition. Without any, it can draw nothing in the original either.
bool HasArt(const entt::registry& reg, const HAL::DrsArchive& drs, int unit_id, Core::PlayerId owner, int civ) {
    auto& cfg = Core::ConfigManager::Get();
    auto present = [&](int graphic) { return Present(drs, cfg.GetGraphic(graphic).slp_id); };
    const R::UnitRules* rules = ECS::Players::UnitOf(reg, owner, unit_id);
    const int graphic = rules != nullptr ? rules->standing_graphic : -1;
    if (present(graphic)) return true;
    for (const auto& delta : Core::Media::Deltas(graphic)) {
        if (present(delta.graphic >= 0 ? delta.graphic : graphic)) return true;
    }
    if (cfg.GetAnnexes(unit_id) != nullptr) return true;
    for (int age = 1; age <= kAges; ++age) {
        if (present(cfg.GetBuildingArt(unit_id, age, civ).body)) return true;
    }
    return false;
}

} // namespace

// Every object the scenario editor offers or a scenario or campaign on the CD
// places, put down through the scenario's own placement for each of the eight
// players and Gaia, in every civilisation and every age, draws something.
bool CheckObjectArt() {
    std::string error;
    const std::set<int> objects = Objects(error);
    HAL::DrsArchive drs;
    if (!HAL::DrsOpen(kGraphicsDrs, drs)) {
        std::printf("objectart=graphics.drs unread\n");
        return false;
    }
    std::vector<std::string> missing;
    std::set<std::pair<int, int>> seen;   // (unit, civ) reported once
    // Objects the CD holds no art for at all, which nothing can draw.
    std::vector<std::string> absent;
    std::set<int> absent_seen;
    int placed = 0;
    for (int age = 0; age < kAges; ++age) {
        // Two worlds an age give the thirteen civilisations eight seats.
        for (int first = 1; first <= kCivs; first += 8) {
            Scenario::File file = Editor::NewFile();
            file.width = file.height = kMapSide;
            file.terrain.assign(static_cast<size_t>(kMapSide * kMapSide), 0);
            file.elevation.assign(file.terrain.size(), 0);
            for (auto& player : file.players) player.units.clear();
            int civs[Scenario::kPlayers + 1] = {0};
            for (int p = 1; p <= Scenario::kPlayers; ++p) {
                civs[p] = 1 + (first - 1 + p - 1) % kCivs;
                file.players[p].active = true;
                file.players[p].civ = civs[p];
                file.players[p].starting_age = age;
            }
            Core::GameSetup setup = Core::DefaultSetup();
            Scenario::SetupFor(file, setup);
            ECS::Simulation sim;
            sim.InitFromFile(setup, file);
            auto& reg = sim.GetRegistry();
            auto& grid = sim.GetGrid();
            const int per_row = (grid.GetWidth() - 2 * kSpot) / kSpot;
            for (int owner = 0; owner <= Scenario::kPlayers; ++owner) {
                if (owner == 0 && age > 0) continue;   // Gaia has no ages
                const int civ = owner == 0 ? 0 : civs[owner];
                // Each owner's objects all at once, then a tick, as a
                // scenario's are when it starts: a unit's sprite is its
                // animation's.
                struct Put {
                    int id;
                    entt::entity e;
                    Core::TerrainType ground;
                };
                std::vector<Put> put;
                int slot = 0;
                for (int id : objects) {
                    const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
                    const C::Vector2D at{Core::Fixed32(kSpot + (slot % per_row) * kSpot) + half,
                                         Core::Fixed32(kSpot + (slot / per_row) * kSpot) + half};
                    ++slot;
                    const Core::TerrainType ground = grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).terrain;
                    const entt::entity e = ECS::ScenarioWorld::Place(reg, grid, id, static_cast<Core::PlayerId>(owner),
                                                                     at, Core::Fixed32(0), 0);
                    if (e != entt::null) put.push_back({id, e, ground});
                }
                for (int t = 0; t < kTicksForSprites; ++t) sim.Update(16);
                const int shown_age = ECS::Techs::Age(reg, static_cast<Core::PlayerId>(owner)) + 1;
                for (const Put& p : put) {
                    ++placed;
                    if (!reg.valid(p.e) || Drawn(reg, grid, drs, p.e, p.ground)) continue;
                    const std::string what = Core::ConfigManager::Get().GetUnitDatName(p.id) + "(" +
                                             std::to_string(p.id) + ") civ " + std::to_string(civ) + " age " +
                                             std::to_string(shown_age);
                    if (!HasArt(reg, drs, p.id, static_cast<Core::PlayerId>(owner), civ)) {
                        if (absent_seen.insert(p.id).second) absent.push_back(what);
                    } else if (seen.insert({p.id, civ}).second) {
                        missing.push_back(what);
                    }
                }
                for (const Put& p : put) ECS::ScenarioWorld::Remove(reg, grid, p.e);
            }
        }
    }
    // AOE_OBJECTART_ALL=1 lists every one.
    if (std::getenv("AOE_OBJECTART_ALL") != nullptr) {
        for (const std::string& m : missing) std::printf("  no art: %s\n", m.c_str());
        for (const std::string& m : absent) std::printf("  none on the CD: %s\n", m.c_str());
    }
    std::string list;
    for (size_t i = 0; i < missing.size() && i < 12; ++i) list += (i ? ", " : "") + missing[i];
    if (missing.empty()) {
        std::printf("objectart=ok objects=%zu placed=%d none_on_cd=%zu\n", objects.size(), placed, absent.size());
    } else {
        std::printf("objectart=%zu objects draw nothing: %s%s\n", missing.size(), list.c_str(),
                    missing.size() > 12 ? ", ..." : "");
    }
    return missing.empty();
}

} // namespace Verify
