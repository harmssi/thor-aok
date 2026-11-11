#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "rendering/EntityRenderer.h"

namespace Verify {

// What the dat draws on its ground layers lies under whatever stands or walks
// there: a stump, a soldier's body, a foundation. A villager and a tree stand.
bool CheckGroundLayer() {
    auto& cfg = Core::ConfigManager::Get();
    auto standing_slp = [&](const char* name) {
        const Core::Rules::UnitRules* rules = Core::Rules::Unit(cfg.GetUnitIdByName(name));
        return rules != nullptr ? cfg.GetGraphic(rules->standing_graphic).slp_id : -1;
    };
    const int stump = standing_slp("STUMP");
    const int body = standing_slp("ARCHR_D");
    const int villager = standing_slp("VMBAS");
    const int oak = cfg.GetObjectArt(cfg.GetUnitIdByName("FOAK")).slp_id;
    const char* verdict = nullptr;
    if (stump < 0 || body < 0 || villager < 0 || oak < 0) verdict = "a graphic is missing";
    else if (!Rendering::EntityRenderer::OnGround(stump)) verdict = "a stump is drawn standing, over who walks there";
    else if (!Rendering::EntityRenderer::OnGround(body)) verdict = "a body is drawn standing, over who walks there";
    else if (Rendering::EntityRenderer::OnGround(villager)) verdict = "a villager is drawn on the ground";
    else if (Rendering::EntityRenderer::OnGround(oak)) verdict = "a tree is drawn on the ground";
    std::printf("groundlayer=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
