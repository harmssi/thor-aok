#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "ecs/Mortality.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "rendering/Renderer.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTickMs = 16;
// Long enough for a boar's dying graphic to play out.
constexpr int kDyingSeconds = 5;
constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;
// The hover hints for an animal (LANGUAGE.DLL): villagers selected, and
// nothing selected.
constexpr int kHuntHint = 3910;
constexpr int kClickVillagerHint = 3826;

// The hint the pointer shows over `at`, the screen centred on it.
int HintAt(ECS::Simulation& sim, const C::Vector2D& at) {
    Rendering::Renderer view;
    view.view_w = static_cast<float>(kFrameW);
    view.view_h = static_cast<float>(kFrameH);
    const SDL_FPoint p = Core::IsometricMath::WorldToScreen(at.x, at.y);
    view.camera_x = view.target_camera_x = p.x - view.view_w / 2.0f;
    view.camera_y = view.target_camera_y = p.y - view.view_h / 2.0f;
    auto& input = Core::InputManager::Get();
    input.Update(view, 0.0f, kFrameW, kFrameH);
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = kFrameW / 2;
    e.motion.y = kFrameH / 2;
    input.ProcessEvent(e, sim, view);
    Rendering::UIRenderer::SetWorldHint(-1);
    input.Hover(sim, view);
    return Rendering::UIRenderer::WorldHint();
}

} // namespace

// A wild boar killed lies as a carcass that the info panel shows as the boar
// it was, by its name and icon, with the food it holds; the pointer over it
// gives the hunt's hints, not a forage bush's.
bool CheckCarcassPanel() {
    // The whole map in sight, so the pointer finds the carcass wherever it
    // lies.
    Core::GameSetup setup = Core::DefaultSetup();
    setup.reveal = Core::Reveal::AllVisible;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    const int boar_id = cfg.GetUnitIdByName("BOARX");
    entt::entity boar = entt::null;
    auto view = reg.view<const C::Identity, const C::Transform>();
    for (auto e : view) {
        if (boar == entt::null && view.get<const C::Identity>(e).unit_id == boar_id) boar = e;
    }
    const char* verdict = nullptr;
    int food = 0, shown = -1, idle_hint = -1, villager_hint = -1;
    if (boar == entt::null) {
        verdict = "no boar on the map";
    } else {
        const C::Vector2D at = reg.get<C::Transform>(boar).position;
        ECS::Mortality::Kill(reg, sim.GetGrid(), boar);
        for (int t = 0; t < kDyingSeconds * 1000 / kTickMs; ++t) sim.Update(kTickMs);
        entt::entity carcass = entt::null;
        auto bodies = reg.view<const C::Corpse, const C::Resource, const C::Transform>();
        for (auto e : bodies) {
            const auto& p = bodies.get<const C::Transform>(e).position;
            if (p.x == at.x && p.y == at.y) carcass = e;
        }
        if (carcass == entt::null) {
            verdict = "a boar killed leaves no carcass holding its food";
        } else {
            food = reg.get<C::Resource>(carcass).current_amount;
            shown = Rendering::UIRenderer::PanelUnit(reg, carcass);
            const auto* ui = cfg.GetUnitInterface(shown);
            if (shown != boar_id || ui == nullptr || ui->name.empty()) {
                verdict = "a boar's carcass is not shown as the boar it was";
            } else if (food <= 0) {
                verdict = "a boar's carcass holds no food";
            } else {
                const C::Vector2D lies = reg.get<C::Transform>(carcass).position;
                idle_hint = HintAt(sim, lies);
                auto workers = reg.view<C::Worker, C::Owner, C::Selectable>();
                for (auto e : workers) {
                    if (workers.get<C::Owner>(e).player_id != 1) continue;
                    workers.get<C::Selectable>(e).is_selected = true;
                    break;
                }
                villager_hint = HintAt(sim, lies);
                if (idle_hint != kClickVillagerHint || villager_hint != kHuntHint) {
                    verdict = "the pointer over a boar's carcass does not give the hunt's hints";
                }
            }
        }
    }
    std::printf("carcass=%s shown=%d food=%d hints=%d/%d\n", verdict == nullptr ? "ok" : verdict, shown, food,
                idle_hint, villager_hint);
    return verdict == nullptr;
}

} // namespace Verify
