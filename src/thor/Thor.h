#pragma once
#include <SDL2/SDL.h>

namespace ECS { class Simulation; }
namespace Rendering { class Renderer; }

// The AYN Thor's second screen (Thor AoK): with it, the interface leaves the
// top screen to the map and a command deck on the bottom screen shows the
// stockpiles, the mini-map, the selection and its commands
// (android/app/src/main/java/org/openaok/game/ThorDeck.java). The deck reads a
// snapshot the engine writes once a frame and sends commands back, which the
// engine runs on its own thread through the code a click on the panel runs.
// Without a second screen nothing here does anything.
namespace Thor {

// Whether the deck is showing: the top screen then draws no interface.
bool Dual();
void SetDual(bool on);

// Commands from the deck (ThorBridge.java has the same numbers).
enum Command : int {
    kCommand = 1,       // a = command grid slot 1..15
    kMinimap = 2,       // a, b = point of the mini-map picture, 0..65535 each
    kIdleVillager = 3,
    kIdleMilitary = 4,
    kTownCenter = 5,
    kAgeUp = 6,         // selects the Town Center and researches the next age
    kMenu = 7,
    kCancelQueue = 8,   // a = queue slot (QueueSlot::slot)
    kDelete = 9,
    kObjectives = 10,
    kTechTree = 11,
};

// Once a frame, after the world is drawn: runs the deck's commands and
// writes the snapshot (and now and then the mini-map's picture).
void Frame(ECS::Simulation& sim, Rendering::Renderer& renderer, SDL_Renderer* sdl_r, int win_w,
           int win_h);
} // namespace Thor
