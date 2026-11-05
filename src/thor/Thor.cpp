#include "thor/Thor.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"
#include "rendering/HudLayout.h"
#include "rendering/Renderer.h"
#include "rendering/UIRenderer.h"

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace Core {
// The deck's commands go through what the panel's buttons and the game
// commands run (InputManager names it a friend).
struct ThorAccess {
    static void Run(InputManager& in, ECS::Simulation& sim, const Rendering::UIRenderer::Command& c) {
        in.RunCommand(sim, c, 1);
    }
    static void IdleVillager(InputManager& in, ECS::Simulation& sim, Rendering::Renderer& r) {
        in.NextIdleVillager(sim, r);
    }
    static void GoTo(InputManager& in, ECS::Simulation& sim, Rendering::Renderer& r, const char* name) {
        in.GoToBuilding(sim, r, name);
    }
    static void LookAt(InputManager& in, Rendering::Renderer& r, float wx, float wy) { in.LookAt(r, wx, wy); }
};
} // namespace Core

namespace Thor {

namespace {

using UI = Rendering::UIRenderer;

std::atomic<bool> g_dual{false};

// The snapshot's layout; ThorBridge.java reads it by the same numbers.
enum Header : int {
    kVersion, kInGame, kWood, kFood, kGold, kStone, kPop, kPopCap, kAge, kFlags,
    kSelected, kHp, kMaxHp, kSelSlp, kSelFrame, kSelUnit, kQueueDone, kQueueTotal,
    kCommands, kQueue, kMapSerial, kMapWidth, kMapHeight, kView, kHeader = kView + 8,
};
constexpr int kCommandInts = 8;   // slot, kind, slp, frame, enabled, affordable, label, name string
constexpr int kQueueInts = 5;     // slot, id, count, slp, frame
constexpr int kFlagPlacing = 1, kFlagAgeResearching = 2;
// The mini-map's picture for the deck: the map diamond, twice as wide as tall.
constexpr int kMapW = 512, kMapH = 256;
constexpr uint32_t kMapEveryMs = 300;

struct Shared {
    std::mutex lock;
    std::vector<int> snap = std::vector<int>(kHeader);
    uint32_t written = 0;               // SDL ticks of the last snapshot
    std::vector<int> pixels;             // ARGB, kMapW x kMapH
    std::string age, next_age, name;
    std::vector<std::string> help;       // each command's rollover help, by index
    std::vector<std::string> labels;     // each command's name for its tile, by index
    struct Pending { int type, a, b; };
    std::vector<Pending> commands;
};

Shared& S() {
    static Shared shared;
    return shared;
}

// The deck's mini-map: drawn by the panel's own code into a texture and read
// back, now and then.
struct Minimap {
    SDL_Texture* target = nullptr;
    SDL_Renderer* owner = nullptr;
    uint32_t drawn = 0;
    int serial = 0;
    std::vector<int> buffer = std::vector<int>(kMapW * kMapH);
};
Minimap& M() {
    static Minimap minimap;
    return minimap;
}

void DrawMinimap(ECS::Simulation& sim, Rendering::Renderer& renderer, SDL_Renderer* sdl_r, int win_w, int win_h) {
    Minimap& m = M();
    const uint32_t now = SDL_GetTicks();
    if (m.drawn != 0 && now - m.drawn < kMapEveryMs) return;
    m.drawn = now == 0 ? 1 : now;
    if (m.owner != sdl_r) {
        m.target = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kMapW, kMapH);
        m.owner = sdl_r;
    }
    if (m.target == nullptr) return;
    SDL_Texture* prev = SDL_GetRenderTarget(sdl_r);
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(sdl_r, &sx, &sy);
    SDL_SetRenderTarget(sdl_r, m.target);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    UI::DrawMinimapIn(sim, renderer, sdl_r, SDL_FRect{0.0f, 0.0f, kMapW, kMapH},
                      SDL_FRect{0.0f, 0.0f, static_cast<float>(win_w), static_cast<float>(win_h)});
    const bool read = SDL_RenderReadPixels(sdl_r, nullptr, SDL_PIXELFORMAT_ARGB8888, m.buffer.data(),
                                           kMapW * 4) == 0;
    SDL_SetRenderTarget(sdl_r, prev);
    SDL_RenderSetScale(sdl_r, sx, sy);
    if (!read) return;
    ++m.serial;
    std::lock_guard<std::mutex> hold(S().lock);
    S().pixels = m.buffer;
}

// A point of the window, as a point of the deck's mini-map picture.
SDL_FPoint OnMinimap(ECS::Simulation& sim, const Rendering::Renderer& renderer, float x, float y) {
    const float zoom = renderer.camera_zoom > 0.0f ? renderer.camera_zoom : 1.0f;
    const auto world = Core::IsometricMath::ScreenToWorld(renderer.camera_x + x / zoom, renderer.camera_y + y / zoom);
    return UI::MinimapProject(SDL_FRect{0.0f, 0.0f, kMapW, kMapH}, world.first, world.second,
                              sim.GetGrid().GetWidth(), sim.GetGrid().GetHeight());
}

// A button's name for its tile: what it makes or researches, or its own
// words without the help's markup and hotkey.
std::string TileLabel(const UI::Command& c) {
    if (c.kind == UI::CommandKind::Produce && c.unit_id >= 0) return Core::ConfigManager::Get().GetUnitName(c.unit_id);
    if (c.kind == UI::CommandKind::Research && c.tech_id >= 0) return Core::Rules::TechLabel(c.tech_id);
    const int id = c.kind == UI::CommandKind::Caption ? c.caption : c.name_string;
    if (id < 0) return {};
    std::string out;
    bool tag = false;
    for (char ch : Core::Strings::Get(id)) {
        if (ch == '<') tag = true;
        else if (ch == '>') tag = false;
        else if (!tag) out += ch;
    }
    // The name alone: the help goes on after a line break, two spaces or the hotkey.
    for (const char* end : {"\n", "  ", " ("}) {
        if (const size_t cut = out.find(end); cut != std::string::npos) out.resize(cut);
    }
    return out;
}

void RunCommands(ECS::Simulation& sim, Rendering::Renderer& renderer, int win_w, int win_h) {
    std::vector<Shared::Pending> todo;
    {
        std::lock_guard<std::mutex> hold(S().lock);
        todo.swap(S().commands);
    }
    if (todo.empty()) return;
    auto& input = Core::InputManager::Get();
    auto& registry = sim.GetRegistry();
    const Core::PlayerId me = Core::LocalPlayer();
    const int panel_w = UI::PanelSpace(win_w, win_h).w;
    UI::Command commands[UI::kCommandSlots];
    for (const auto& c : todo) {
        switch (c.type) {
        case kCommand: {
            const int n = UI::BuildCommands(sim, panel_w, win_h, commands, UI::kCommandSlots);
            for (int i = 0; i < n; ++i) {
                if (commands[i].slot != c.a || commands[i].kind == UI::CommandKind::Caption) continue;
                Core::ThorAccess::Run(input, sim, commands[i]);
                break;
            }
            break;
        }
        case kMinimap: {
            float wx = 0.0f, wy = 0.0f;
            if (UI::MinimapUnproject(SDL_FRect{0.0f, 0.0f, kMapW, kMapH}, c.a * kMapW / 65536.0f,
                                     c.b * kMapH / 65536.0f, sim.GetGrid().GetWidth(),
                                     sim.GetGrid().GetHeight(), wx, wy)) {
                Core::ThorAccess::LookAt(input, renderer, wx, wy);
            }
            break;
        }
        case kIdleVillager: Core::ThorAccess::IdleVillager(input, sim, renderer); break;
        case kIdleMilitary: input.NextIdleMilitary(sim, renderer); break;
        case kTownCenter: Core::ThorAccess::GoTo(input, sim, renderer, "Town Center"); break;
        case kAgeUp: {
            const int next = ECS::Techs::AgeTech(registry, me, ECS::Techs::Age(registry, me) + 1);
            if (next < 0) break;
            Core::ThorAccess::GoTo(input, sim, renderer, "Town Center");
            const int n = UI::BuildCommands(sim, panel_w, win_h, commands, UI::kCommandSlots);
            for (int i = 0; i < n; ++i) {
                if (commands[i].kind == UI::CommandKind::Research && commands[i].tech_id == next) {
                    Core::ThorAccess::Run(input, sim, commands[i]);
                    break;
                }
            }
            break;
        }
        case kMenu: UI::RequestDialog(Rendering::kGameMenu); break;
        case kObjectives: UI::RequestDialog(Rendering::kObjectives); break;
        case kTechTree: UI::RequestDialog(Rendering::kTechTree); break;
        case kCancelQueue:
            if (const entt::entity building = UI::SelectedBuilding(sim); building != entt::null) {
                Core::CommandQueue::Get().PushCommand(Core::CancelTrainCommand{building, c.a, me});
            }
            break;
        case kDelete: {
            UI::Command remove;
            remove.kind = UI::CommandKind::Delete;
            Core::ThorAccess::Run(input, sim, remove);
            break;
        }
        default: break;
        }
    }
}

} // namespace

bool Dual() { return g_dual.load(std::memory_order_relaxed); }
void SetDual(bool on) { g_dual.store(on); }

void Frame(ECS::Simulation& sim, Rendering::Renderer& renderer, SDL_Renderer* sdl_r, int win_w, int win_h) {
    if (!Dual()) return;
    RunCommands(sim, renderer, win_w, win_h);
    DrawMinimap(sim, renderer, sdl_r, win_w, win_h);

    auto& registry = sim.GetRegistry();
    const Core::PlayerId me = Core::LocalPlayer();
    const ECS::Players::Player* player = ECS::Players::Find(registry, me);
    std::vector<int> snap(kHeader, 0);
    snap[kVersion] = 1;
    snap[kInGame] = 1;
    if (player != nullptr) {
        snap[kWood] = ECS::Players::Stock(*player, Core::ResourceType::Wood);
        snap[kFood] = ECS::Players::Stock(*player, Core::ResourceType::Food);
        snap[kGold] = ECS::Players::Stock(*player, Core::ResourceType::Gold);
        snap[kStone] = ECS::Players::Stock(*player, Core::ResourceType::Stone);
    }
    snap[kPop] = ECS::Population::Used(registry, me);
    snap[kPopCap] = ECS::Population::Capacity(registry, me);
    const int age = ECS::Techs::Age(registry, me);
    snap[kAge] = age;
    const int next_age = ECS::Techs::AgeTech(registry, me, age + 1);
    int unit = 0;
    if (Core::InputManager::Get().PlacingBuilding(unit)) snap[kFlags] |= kFlagPlacing;
    if (player != nullptr && next_age >= 0 && next_age < static_cast<int>(player->techs.size()) &&
        player->techs[static_cast<size_t>(next_age)].state == ECS::Players::TechState::Researching) {
        snap[kFlags] |= kFlagAgeResearching;
    }
    const UI::Inspected seen = UI::Inspect(sim);
    snap[kSelected] = seen.selected;
    snap[kHp] = seen.hp;
    snap[kMaxHp] = seen.max_hp;
    snap[kSelSlp] = static_cast<int>(seen.icon_slp);
    snap[kSelFrame] = seen.icon_frame;
    snap[kSelUnit] = seen.unit_id;
    snap[kQueueDone] = seen.queue_done;
    snap[kQueueTotal] = seen.queue_total;

    const int panel_w = UI::PanelSpace(win_w, win_h).w;
    UI::Command commands[UI::kCommandSlots];
    const int n = UI::BuildCommands(sim, panel_w, win_h, commands, UI::kCommandSlots);
    snap[kCommands] = n;
    for (int i = 0; i < n; ++i) {
        const UI::Command& c = commands[i];
        const int at[kCommandInts] = {c.slot, static_cast<int>(c.kind), static_cast<int>(c.icon_slp), c.icon_frame,
                                      c.enabled ? 1 : 0, c.affordable ? 1 : 0, c.label,
                                      c.kind == UI::CommandKind::Caption ? c.caption : c.name_string};
        snap.insert(snap.end(), at, at + kCommandInts);
    }
    UI::QueueSlot slots[UI::kMaxQueueSlots];
    const int queued = UI::BuildQueueSlots(sim, panel_w, win_h, slots, UI::kMaxQueueSlots);
    snap[kQueue] = queued;
    for (int i = 0; i < queued; ++i) {
        uint32_t slp = 0;
        int frame = -1;
        UI::QueueIcon(sim, slots[i], slp, frame);
        const int at[kQueueInts] = {slots[i].slot, slots[i].id, slots[i].count, static_cast<int>(slp), frame};
        snap.insert(snap.end(), at, at + kQueueInts);
    }

    snap[kMapSerial] = M().serial;
    snap[kMapWidth] = kMapW;
    snap[kMapHeight] = kMapH;
    const SDL_FPoint corners[4] = {OnMinimap(sim, renderer, 0, 0), OnMinimap(sim, renderer, win_w, 0),
                                   OnMinimap(sim, renderer, win_w, win_h), OnMinimap(sim, renderer, 0, win_h)};
    for (int i = 0; i < 4; ++i) {
        snap[kView + i * 2] = static_cast<int>(corners[i].x);
        snap[kView + i * 2 + 1] = static_cast<int>(corners[i].y);
    }

    // The words change seldom; the help only while it can be read.
    static uint32_t help_at = 0;
    std::vector<std::string> help, labels;
    for (int i = 0; i < n; ++i) labels.push_back(TileLabel(commands[i]));
    const uint32_t now = SDL_GetTicks();
    const bool fresh_help = now - help_at > 250;
    if (fresh_help) {
        help_at = now;
        for (int i = 0; i < n; ++i) help.push_back(UI::CommandHelp(sim, commands[i]));
    }
    std::lock_guard<std::mutex> hold(S().lock);
    Shared& s = S();
    s.snap.swap(snap);
    s.written = now == 0 ? 1 : now;
    s.age = Core::Rules::TechLabel(ECS::Techs::AgeTech(registry, me, age));
    s.next_age = next_age >= 0 ? Core::Rules::TechLabel(next_age) : std::string();
    s.name = seen.name;
    if (fresh_help) s.help.swap(help);
    s.labels.swap(labels);
}

} // namespace Thor

#ifdef __ANDROID__
// JNI for org.openaok.game.ThorBridge.
extern "C" {

JNIEXPORT void JNICALL Java_org_openaok_game_ThorBridge_setDualScreen(JNIEnv*, jclass, jboolean on) {
    Thor::SetDual(on == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_org_openaok_game_ThorBridge_pushCommand(JNIEnv*, jclass, jint type, jint a, jint b) {
    std::lock_guard<std::mutex> hold(Thor::S().lock);
    if (Thor::S().commands.size() < 64) Thor::S().commands.push_back({type, a, b});
}

// out[0] = age of the snapshot in ms, then the snapshot; the ints written, 0 = none yet.
JNIEXPORT jint JNICALL Java_org_openaok_game_ThorBridge_snapshot(JNIEnv* env, jclass, jintArray out) {
    std::lock_guard<std::mutex> hold(Thor::S().lock);
    const auto& s = Thor::S();
    if (s.written == 0) return 0;
    const jsize room = env->GetArrayLength(out);
    const jsize n = static_cast<jsize>(s.snap.size()) + 1;
    if (n > room) return 0;
    const jint age = static_cast<jint>(SDL_GetTicks() - s.written);
    env->SetIntArrayRegion(out, 0, 1, &age);
    env->SetIntArrayRegion(out, 1, n - 1, s.snap.data());
    return n;
}

// The deck's mini-map as ARGB, kMapW x kMapH; the pixels written, 0 = none yet.
JNIEXPORT jint JNICALL Java_org_openaok_game_ThorBridge_minimap(JNIEnv* env, jclass, jintArray out) {
    std::lock_guard<std::mutex> hold(Thor::S().lock);
    const auto& px = Thor::S().pixels;
    if (px.empty() || env->GetArrayLength(out) < static_cast<jsize>(px.size())) return 0;
    env->SetIntArrayRegion(out, 0, static_cast<jsize>(px.size()), px.data());
    return static_cast<jint>(px.size());
}

// An interface icon as ARGB; wh = {w, h}; the pixels, 0 when there is none.
JNIEXPORT jint JNICALL Java_org_openaok_game_ThorBridge_icon(JNIEnv* env, jclass, jint slp, jint frame,
                                                             jintArray out, jintArray wh) {
    auto& cache = HAL::GraphicCache::Get();
    const HAL::DrsArchive* drs = cache.InterfaceArchive();
    HAL::SlpFrame f;
    if (drs == nullptr || slp <= 0 || frame < 0 ||
        !HAL::SlpDecodeFrame(*drs, static_cast<uint32_t>(slp), static_cast<uint32_t>(frame), f)) {
        return 0;
    }
    const jsize n = static_cast<jsize>(f.width * f.height);
    if (n == 0 || env->GetArrayLength(out) < n) return 0;
    std::vector<jint> argb(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        if (!f.alpha[i]) continue;
        const uint8_t index = f.player[i] ? f.player[i] : f.indexes[i];
        const HAL::PaletteColor c = cache.GetPaletteColor(index);
        argb[i] = static_cast<jint>(0xFF000000u | (c.r << 16) | (c.g << 8) | c.b);
    }
    env->SetIntArrayRegion(out, 0, n, argb.data());
    const jint size[2] = {static_cast<jint>(f.width), static_cast<jint>(f.height)};
    env->SetIntArrayRegion(wh, 0, 2, size);
    return n;
}

// Words for the deck: which 0 the age, 1 the next age, 2 the selection's name,
// 3 a command's rollover help (id = its index), 4 a LANGUAGE string (id),
// 5 a command's tile name (id = its index).
JNIEXPORT jstring JNICALL Java_org_openaok_game_ThorBridge_text(JNIEnv* env, jclass, jint which, jint id) {
    std::string words;
    {
        std::lock_guard<std::mutex> hold(Thor::S().lock);
        const auto& s = Thor::S();
        if (which == 0) words = s.age;
        else if (which == 1) words = s.next_age;
        else if (which == 2) words = s.name;
        else if (which == 3 && id >= 0 && id < static_cast<jint>(s.help.size())) words = s.help[id];
        else if (which == 5 && id >= 0 && id < static_cast<jint>(s.labels.size())) words = s.labels[id];
    }
    if (which == 4 && id >= 0) words = Core::Strings::Get(id);
    return env->NewStringUTF(words.c_str());
}

} // extern "C"
#endif
