#pragma once
#include <cstdint>

namespace ECS::Components {

// A bird on the wing, as the dat's Fly task keeps a hawk: it flies from one
// point of the map to the next, flapping for a spell, then gliding for one,
// by turns (ecs/Animals).
struct Flier {
    enum class Phase : uint8_t { Picking, Flapping, Gliding };
    Phase phase = Phase::Picking;
    int32_t spell_ms = 0;   // until it changes from flapping to gliding or back
};

} // namespace ECS::Components
