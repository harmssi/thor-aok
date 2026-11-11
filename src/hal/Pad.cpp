#include "hal/Pad.h"

#include <algorithm>
#include <cmath>

#include "hal/Pointer.h"
#include "hal/TextInput.h"
#include "hal/Window.h"
#ifdef __ANDROID__
#include "thor/Thor.h"
#endif

#ifdef __SWITCH__
// SDL's own way in for a key (events/SDL_keyboard_c.h), which its Switch port
// uses for a USB keyboard. SDL is linked in whole on the Switch.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
#endif

namespace HAL::Pad {

namespace {

// The buttons as SDL's Switch port numbers them (joystick/switch).
enum Button : int {
    kA = 0,
    kB = 1,
    kX = 2,
    kY = 3,
    kL = 6,
    kR = 7,
    kZL = 8,
    kZR = 9,
    kPlus = 10,
    kMinus = 11,
    kLeft = 12,
    kUp = 13,
    kRight = 14,
    kDown = 15,
};

// How far a stick leans before it counts, of its full reach: a stick at rest
// is seldom quite at its middle.
constexpr float kDeadZone = 0.15f;
// The pointer's speed with the stick pushed all the way, in screen heights a
// second.
constexpr float kPointerSpeed = 1.4f;
constexpr Uint32 kRumbleMs = 30;

struct State {
    bool enabled = false;
    bool opened = false;
    SDL_Joystick* joystick = nullptr;
    SDL_GameController* pad = nullptr;   // Android: the handheld's own controller
    bool zr = false;   // Android, held: the D-pad sets a group instead of picking it
    float x = -1.0f;   // the pointer, window pixels; below zero until placed
    float y = -1.0f;
    bool zl = false;   // held: the right stick zooms
    Uint64 last = 0;   // SDL's performance counter at the last Update
};
State g;

void Key(SDL_Scancode key, bool down) {
#ifdef __SWITCH__
    SDL_SendKeyboardKey(down ? SDL_PRESSED : SDL_RELEASED, key);
#elif defined(__ANDROID__)
    // Shift is the state the game asks SDL for; any other key is an event of its own.
    if (key == SDL_SCANCODE_LSHIFT) {
        SDL_SetModState(static_cast<SDL_Keymod>(down ? SDL_GetModState() | KMOD_LSHIFT : SDL_GetModState() & ~KMOD_LSHIFT));
        return;
    }
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = key;
    e.key.keysym.sym = SDL_GetKeyFromScancode(key);
    e.key.keysym.mod = static_cast<Uint16>((g.zr ? KMOD_LCTRL : 0) | (g.zl ? KMOD_LSHIFT : 0));
    SDL_PushEvent(&e);
#else
    (void)key;
    (void)down;
#endif
}

#ifdef __ANDROID__
// A handheld's buttons (SDL's game controller names; the AYN Thor reports its
// buttons by their letters) as the Switch's above. The game's files from the HD
// edition carry no hotkey table, so what the Switch does with a hotkey is the
// deck's command here, and the D-pad is the control groups: up, right, down,
// left pick groups 1 to 4, with ZR held they set them.
int ButtonOf(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return kA;
    case SDL_CONTROLLER_BUTTON_B: return kB;
    case SDL_CONTROLLER_BUTTON_X: return kX;
    case SDL_CONTROLLER_BUTTON_Y: return kY;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kL;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kR;
    case SDL_CONTROLLER_BUTTON_START: return kPlus;
    case SDL_CONTROLLER_BUTTON_BACK: return kMinus;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return kUp;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kRight;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kLeft;
    default: return -1;
    }
}

// True when the button was the deck's command or a control group.
bool DeckButton(int button) {
    switch (button) {
    case kX: Thor::Push(Thor::kIdleVillager); return true;
    case kL: Thor::Push(Thor::kTownCenter); return true;
    case kR: Thor::Push(Thor::kIdleMilitary); return true;
    case kPlus: Thor::Push(Thor::kMenu); return true;
    case kUp: Key(SDL_SCANCODE_1, true); Key(SDL_SCANCODE_1, false); return true;
    case kRight: Key(SDL_SCANCODE_2, true); Key(SDL_SCANCODE_2, false); return true;
    case kDown: Key(SDL_SCANCODE_3, true); Key(SDL_SCANCODE_3, false); return true;
    case kLeft: Key(SDL_SCANCODE_4, true); Key(SDL_SCANCODE_4, false); return true;
    default: return false;
    }
}
#endif

// The key a button presses, or SDL_SCANCODE_UNKNOWN.
SDL_Scancode KeyOf(int button) {
    switch (button) {
    case kX: return SDL_SCANCODE_PERIOD;
    case kY: return SDL_SCANCODE_ESCAPE;
    case kL: return SDL_SCANCODE_H;
    case kR: return SDL_SCANCODE_COMMA;
    case kZL: return SDL_SCANCODE_LSHIFT;
    case kZR: return SDL_SCANCODE_HOME;
    case kPlus: return SDL_SCANCODE_F10;
    case kMinus: return SDL_SCANCODE_RETURN;
    case kUp: return SDL_SCANCODE_F3;
    case kDown: return SDL_SCANCODE_SPACE;
    case kLeft: return SDL_SCANCODE_KP_MINUS;
    case kRight: return SDL_SCANCODE_KP_PLUS;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

void Open() {
    if (g.opened) return;
    g.opened = true;
#ifdef __ANDROID__
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
    for (int i = 0; i < SDL_NumJoysticks() && g.pad == nullptr; ++i) {
        if (SDL_IsGameController(i)) g.pad = SDL_GameControllerOpen(i);
    }
    if (g.pad == nullptr) g.opened = false;   // none yet: looked for again next frame
#else
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0 || SDL_NumJoysticks() < 1) return;
    g.joystick = SDL_JoystickOpen(0);
#endif
}

// A stick's lean, -1 to 1 each way: nothing inside the dead zone, and beyond
// it the square of how far, so that a small push moves finely.
void Lean(int axis_x, int axis_y, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
#ifdef __ANDROID__
    if (g.pad == nullptr) return;
    // 0, 1 the left stick and 2, 3 the right, as SDL's controller axes are numbered too.
    const float ax = SDL_GameControllerGetAxis(g.pad, static_cast<SDL_GameControllerAxis>(axis_x)) / 32767.0f;
    const float ay = SDL_GameControllerGetAxis(g.pad, static_cast<SDL_GameControllerAxis>(axis_y)) / 32767.0f;
#else
    if (g.joystick == nullptr) return;
    const float ax = SDL_JoystickGetAxis(g.joystick, axis_x) / 32767.0f;
    const float ay = SDL_JoystickGetAxis(g.joystick, axis_y) / 32767.0f;
#endif
    const float length = std::hypot(ax, ay);
    if (length <= kDeadZone) return;
    const float reach = std::min(1.0f, (length - kDeadZone) / (1.0f - kDeadZone));
    const float scale = reach * reach / length;
    x = std::clamp(ax * scale, -1.0f, 1.0f);
    y = std::clamp(ay * scale, -1.0f, 1.0f);
}

} // namespace

void Enable() { g.enabled = true; }

bool Enabled() { return g.enabled; }

bool Handle(const SDL_Event& event) {
#ifdef __ANDROID__
    if (!g.enabled || (event.type != SDL_CONTROLLERBUTTONDOWN && event.type != SDL_CONTROLLERBUTTONUP)) return false;
    const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
    const int button = ButtonOf(event.cbutton.button);
    if (button < 0) return true;
    if (button != kA && button != kB && button != kY && button != kMinus) {
        if (down) DeckButton(button);
        return true;
    }
#else
    if (!g.enabled || (event.type != SDL_JOYBUTTONDOWN && event.type != SDL_JOYBUTTONUP)) return false;
    const bool down = event.type == SDL_JOYBUTTONDOWN;
    const int button = event.jbutton.button;
#endif
    if (button == kA || button == kB) {
        const Uint8 mouse = button == kA ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
        if (down) {
            Pointer::Press(mouse);
        } else {
            Pointer::Release(mouse);
        }
        return true;
    }
    // On a screen with a name to type, - brings up the keyboard; elsewhere it
    // is Enter, which opens the chat line.
    if (button == kMinus && down && TextInput::ShowForFields()) return true;
    if (button == kZL) g.zl = down;
    const SDL_Scancode key = KeyOf(button);
    if (key == SDL_SCANCODE_UNKNOWN) return true;
    // Shift is held as long as ZL is; every other key is pressed and let go
    // as its button goes down.
    if (key == SDL_SCANCODE_LSHIFT) {
        Key(key, down);
    } else if (down) {
        Key(key, true);
        Key(key, false);
    }
    return true;
}

void Update() {
    if (!g.enabled) return;
    Open();
    const Uint64 now = SDL_GetPerformanceCounter();
    const float dt = g.last == 0 ? 0.0f
        : std::min(0.1f, static_cast<float>(now - g.last) / static_cast<float>(SDL_GetPerformanceFrequency()));
    g.last = now;
#ifdef __ANDROID__
    // The triggers are axes here: ZL held is Shift (and the right stick's zoom), ZR sets groups.
    if (g.pad != nullptr) {
        const bool zl = SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
        if (zl != g.zl) {
            g.zl = zl;
            Key(SDL_SCANCODE_LSHIFT, zl);
        }
        g.zr = SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
    }
#endif
    Window* window = MainWindow();
    if (window == nullptr || window->GetSDLWindow() == nullptr) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window->GetSDLWindow(), &w, &h);
    if (w <= 0 || h <= 0) return;
    // The pointer starts in the middle of the screen, and goes on from wherever
    // a finger or a mouse has since put it.
    if (g.x < 0.0f) {
        g.x = w / 2.0f;
        g.y = h / 2.0f;
        Pointer::Move(g.x, g.y, false);
    }
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    if (static_cast<int>(g.x) != mx || static_cast<int>(g.y) != my) {
        g.x = static_cast<float>(mx);
        g.y = static_cast<float>(my);
    }
    float lx = 0.0f, ly = 0.0f;
    Lean(0, 1, lx, ly);
    if (lx == 0.0f && ly == 0.0f) return;
    const float step = kPointerSpeed * static_cast<float>(h) * dt;
    g.x = std::clamp(g.x + lx * step, 0.0f, static_cast<float>(w - 1));
    g.y = std::clamp(g.y + ly * step, 0.0f, static_cast<float>(h - 1));
    Pointer::Move(g.x, g.y, false);
}

void Scroll(float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (g.enabled && !g.zl) Lean(2, 3, x, y);
}

float Zoom() {
    if (!g.enabled || !g.zl) return 0.0f;
    float x = 0.0f, y = 0.0f;
    Lean(2, 3, x, y);
    return -y;
}

void Rumble() {
    if (g.pad != nullptr) SDL_GameControllerRumble(g.pad, 0x6000, 0x6000, kRumbleMs);
    if (g.joystick != nullptr) SDL_JoystickRumble(g.joystick, 0x6000, 0x6000, kRumbleMs);
}

} // namespace HAL::Pad
