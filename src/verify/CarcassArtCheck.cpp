#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Mortality.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTickMs = 16;
// Long enough for a dying graphic to play out.
constexpr int kDyingSeconds = 5;

void Run(ECS::Simulation& sim, int ms) {
    for (int t = 0; t < ms / kTickMs; ++t) sim.Update(kTickMs);
}

// What lies at `at` from the dead: the carcass or body there, or null.
entt::entity LyingAt(entt::registry& reg, const C::Vector2D& at) {
    entt::entity found = entt::null;
    auto view = reg.view<const C::Corpse, const C::Transform>();
    for (auto e : view) {
        const auto& p = view.get<const C::Transform>(e).position;
        if (p.x == at.x && p.y == at.y) found = e;
    }
    return found;
}

// Kills the first `name` on the map and plays out its dying: where it fell.
bool Fell(ECS::Simulation& sim, const char* name, C::Vector2D& at) {
    auto& reg = sim.GetRegistry();
    const int id = Core::ConfigManager::Get().GetUnitIdByName(name);
    auto view = reg.view<const C::Identity, const C::Transform>();
    for (auto e : view) {
        if (view.get<const C::Identity>(e).unit_id != id) continue;
        at = view.get<const C::Transform>(e).position;
        ECS::Mortality::Kill(reg, sim.GetGrid(), e);
        Run(sim, kDyingSeconds * 1000);
        return true;
    }
    return false;
}

// Whether the sprite shows the last frame of `graphic`.
bool LastFrameOf(const C::Sprite& sprite, int graphic) {
    const auto art = Core::ConfigManager::Get().GetGraphic(graphic);
    return sprite.slp_id == art.slp_id && sprite.current_frame == art.frame_count - 1;
}

} // namespace

// A wild boar killed lies as it fell, the last frame of its dying graphic,
// while it holds food; its food gone, its dead unit lies in its place for the
// dat's decay time, rotting through its graphic's frames, and is gone after
// it. A sheep's carcass lies as the sheep fell too, and leaves nothing, its
// dead unit's decay time being none.
bool CheckCarcassArt() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;
    C::Vector2D at{};
    const Core::Rules::UnitRules* boar = Core::Rules::Unit(cfg.GetUnitIdByName("BOARX"));
    const Core::Rules::UnitRules* sheep = Core::Rules::Unit(cfg.GetUnitIdByName("SHEEPG"));
    int lies_s = 0;
    if (boar == nullptr || sheep == nullptr || !Fell(sim, "BOARX", at)) {
        verdict = "no boar on the map";
    } else {
        const entt::entity carcass = LyingAt(reg, at);
        const Core::Rules::UnitRules* dead = Core::Rules::Unit(boar->dead_unit);
        lies_s = dead != nullptr ? ECS::Mortality::DecayMs(*dead) / 1000 : 0;
        const int bones = dead != nullptr ? cfg.GetGraphic(dead->standing_graphic).slp_id : -1;
        if (carcass == entt::null || !reg.all_of<C::Resource>(carcass)) {
            verdict = "a boar leaves no carcass";
        } else if (!LastFrameOf(reg.get<C::Sprite>(carcass), boar->dying_graphic)) {
            verdict = "a boar's carcass is not drawn as the boar fell";
        } else {
            reg.get<C::Resource>(carcass).current_amount = 0;
            Run(sim, 2 * kTickMs);
            const entt::entity body = LyingAt(reg, at);
            if (body == entt::null || reg.get<C::Sprite>(body).slp_id != bones || reg.all_of<C::Resource>(body)) {
                verdict = "a boar's carcass with its food gone leaves no remains";
            } else if (reg.get<C::Sprite>(body).current_frame != 0) {
                verdict = "a boar's remains start rotted";
            } else {
                Run(sim, lies_s * 1000 * 3 / 4);
                if (!reg.valid(body) || reg.get<C::Sprite>(body).current_frame == 0) {
                    verdict = "a boar's remains do not rot as they lie";
                } else {
                    Run(sim, lies_s * 1000 / 4 + 1000);
                    if (LyingAt(reg, at) != entt::null) verdict = "a boar's remains stay past their decay time";
                }
            }
        }
    }
    if (verdict == nullptr) {
        if (!Fell(sim, "SHEEPG", at)) {
            verdict = "no sheep on the map";
        } else {
            const entt::entity carcass = LyingAt(reg, at);
            if (carcass == entt::null || !LastFrameOf(reg.get<C::Sprite>(carcass), sheep->dying_graphic)) {
                verdict = "a sheep's carcass is not drawn as the sheep fell";
            } else {
                reg.get<C::Resource>(carcass).current_amount = 0;
                Run(sim, 4 * kTickMs);
                if (LyingAt(reg, at) != entt::null) verdict = "a sheep eaten up leaves remains";
            }
        }
    }
    std::printf("carcassart=%s lies=%d\n", verdict == nullptr ? "ok" : verdict, lies_s);
    return verdict == nullptr;
}

} // namespace Verify
