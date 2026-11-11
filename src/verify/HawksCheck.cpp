#include "verify/Harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/IsometricMath.h"
#include "core/Rules.h"
#include "ecs/Simulation.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
namespace R = Core::Rules;
constexpr int kTickMs = 16;
// Long enough for every hawk to have flapped one spell (14 seconds at most)
// and glided in the next.
constexpr int kSeconds = 60;
// At the dat's 1.32 tiles a second a minute's flight covers about 79 tiles;
// half of that is plainly flying, not hovering.
constexpr int kLeastTiles = 30;
// The original draws a hawk 9 half tile heights above its ground.
constexpr int kLiftPx = 9 * Core::IsometricMath::TILE_HEIGHT / 2;

struct Watched {
    entt::entity e;
    int flapping = -1;
    int gliding = -1;
    double tiles = 0;
    bool flapped = false;
    bool glided = false;
};

} // namespace

// The hawks of a new map fly, as the dat's Fly task has them: each covers
// ground at its speed, drawn up at its height, flapping and gliding by turns,
// never leaving the map.
bool CheckHawks() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    const int side = sim.GetGrid().GetWidth();
    std::vector<Watched> hawks;
    auto view = reg.view<const C::Identity, const C::Transform>();
    for (auto e : view) {
        const R::UnitRules* rules = R::Unit(view.get<const C::Identity>(e).unit_id);
        if (rules == nullptr) continue;
        for (int t = 0; t < rules->task_count; ++t) {
            if (rules->tasks[t].action != R::TaskAction::Fly) continue;
            hawks.push_back({e, rules->tasks[t].moving_graphic, rules->tasks[t].proceeding_graphic});
        }
    }
    const char* verdict = nullptr;
    if (hawks.empty()) verdict = "no hawks on the map";
    int lifted = 0;
    for (int t = 0; t < kSeconds * 1000 / kTickMs && verdict == nullptr; ++t) {
        sim.Update(kTickMs);
        for (Watched& w : hawks) {
            if (!reg.valid(w.e)) continue;
            const auto& tr = reg.get<C::Transform>(w.e);
            const double dx = static_cast<double>(tr.position.x.ToFloat() - tr.previous_position.x.ToFloat());
            const double dy = static_cast<double>(tr.position.y.ToFloat() - tr.previous_position.y.ToFloat());
            w.tiles += std::sqrt(dx * dx + dy * dy);
            if (tr.position.x.ToInt() < 0 || tr.position.y.ToInt() < 0 || tr.position.x.ToInt() >= side ||
                tr.position.y.ToInt() >= side) {
                verdict = "a hawk flew off the map";
            }
            const auto* anim = reg.try_get<C::Animation>(w.e);
            const auto* sprite = reg.try_get<C::Sprite>(w.e);
            if (anim == nullptr || sprite == nullptr) continue;
            const int shown = anim->graphic[static_cast<int>(anim->shown)];
            w.flapped = w.flapped || shown == w.flapping;
            w.glided = w.glided || shown == w.gliding;
        }
    }
    double least = 1e9;
    int glided = 0;
    for (const Watched& w : hawks) {
        if (!reg.valid(w.e)) continue;
        least = std::min(least, w.tiles);
        glided += w.glided ? 1 : 0;
        const auto* sprite = reg.try_get<C::Sprite>(w.e);
        lifted += sprite != nullptr && sprite->lift_px == kLiftPx ? 1 : 0;
        if (verdict != nullptr) continue;
        if (w.tiles < kLeastTiles) verdict = "a hawk flaps where it is";
        else if (!w.flapped || !w.glided) verdict = "a hawk does not flap and glide by turns";
    }
    if (verdict == nullptr && lifted != static_cast<int>(hawks.size())) verdict = "a hawk is not drawn up at its height";
    std::printf("hawks=%s hawks=%zu least=%.1f glided=%d lifted=%d\n", verdict == nullptr ? "ok" : verdict,
                hawks.size(), least == 1e9 ? 0.0 : least, glided, lifted);
    return verdict == nullptr;
}

} // namespace Verify
