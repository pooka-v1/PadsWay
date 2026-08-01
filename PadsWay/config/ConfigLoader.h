#pragma once
#include "../input/ControllerConfig.h"
#include "../input/ComponentTypes.h"
#include "../ui/PadLayout.h"
#include <vector>
#include <string>
#include <unordered_map>

// Loads all controller configs from a JSON file.
std::vector<ControllerConfig> loadControllerConfigs(const std::string& path);

// Returns a pointer to the best-matching config, or nullptr if not found.
// Matches on VID+PID (required). Optional discriminators add to a score;
// entries that declare a discriminator but don't match are skipped entirely.
//   connection  (+2): "usb"/"bt" — transport type
//   product_name(+2): partial case-insensitive match against the device's HID name
//   sourceName  (+1): exact match, used only by the wizard for re-pair
const ControllerConfig* findConfig(const std::vector<ControllerConfig>& configs,
                                   uint16_t vid, uint16_t pid,
                                   const std::string& connection   = "",
                                   const std::string& sourceName   = "",
                                   const std::string& productName  = "");

// Persists one controller entry's stick, trigger and gyro/accel calibration (deadzone/max, see
// ARCHITECTURE.md "Calibracion de entrada") plus per-axis invert flags, preserving every other
// field in the file untouched. imu's invert fields (gyro_x_invert...accel_z_invert) come from
// `imu` itself and are written alongside deadzone/max — CalibrationPanel now owns those too, not
// just the wizard. Stick axis invert doesn't live on StickCalibration (it's on the whole-axis
// `axes` map, keyed by HID source name, not by logical stick axis), so it's passed separately as
// (HID source name, invert) pairs and merged into ctrl["axes"][key]["invert"]; entries whose HID
// key doesn't exist in this file are skipped. Matches the entry by source_name (exact, unlike
// vid+pid which duplicate across compat-mode fallbacks — see findConfig's productName/connection
// discriminators).
// Throws std::runtime_error if the file can't be written or sourceName isn't found.
void saveCalibration(const std::string& path, const std::string& sourceName,
                     const StickCalibration& leftStick, const StickCalibration& rightStick,
                     const TriggerCalibration& triggerL, const TriggerCalibration& triggerR,
                     const ImuConfig& imu,
                     const std::vector<std::pair<std::string, bool>>& axisInverts);

// Loads macro library from a JSON file (name -> execution string).
// Returns an empty map if the file does not exist.
std::unordered_map<std::string, std::string> loadMacroLibrary(const std::string& path);

// Saves macro library to a JSON file. Entries are written in the order supplied.
// Throws std::runtime_error if the file cannot be written.
void saveMacroLibrary(const std::string& path,
                      const std::vector<std::pair<std::string, std::string>>& macros);

// Which kind of virtual controller the engine plugs into ViGEm.
//   Xbox      -> XInput device: universally supported, but games reading XInput
//                cap at 4 pads.
//   DualShock -> DS4 device read through DirectInput: works with old games and
//                emulators, and is not subject to the XInput 4-pad cap.
// The engine's internal GamepadState stays Xbox-shaped either way; the DS4 adapter
// only remaps at the very last step (A->Cross, B->Circle, ...).
enum class VirtualOutputType { Xbox, DualShock };

struct VirtualPadConfig {
    // Per-type virtual identity. Xbox uses a custom VID/PID. DS4 must use a REAL DS4 id
    // (a fake PID on Sony's VID won't enumerate → ViGEm 0xE0000007): 054C:05C4 is DS4 v1,
    // which ViGEm emulates natively and does NOT clash with a physical DS4 v2 (054C:09CC).
    uint16_t                 xboxVid                = 0x5650;   // defaults if file is missing
    uint16_t                 xboxPid                = 0x0001;
    uint16_t                 directVid              = 0x054C;   // Sony VID
    uint16_t                 directPid              = 0x05C4;   // DualShock 4 v1 (ViGEm native DS4 id)
    VirtualOutputType        outputType             = VirtualOutputType::Xbox;  // ViGEm target type
    std::string              logLevel               = "info";   // trace/debug/info/warn/error
    std::string              locale                 = "en";
    float                    fontSize               = 17.0f;    // ImGui font size in pixels
    std::vector<std::string> acceptedXboxButtons    = {"a","b","x","y","l1","r1","select","start","home","l3","r3"};
    float                    stickSelectThreshold   = 0.85f;    // normalized [0,1]
    int                      stickHoldMs            = 2000;     // ms held at tope to select direction
    // Gyro/accel hold-to-arm thresholds for the Mapper (Component System half-axis reassignment).
    // Deliberately well above the gyro widget's own 0.12 deadzone — a held gesture should be
    // unmistakable, not a casual jostle. Accel (orientation, sustains while tilted) is what
    // actually completes the hold for pitch/roll; gyro (angular velocity, decays to 0 at rest)
    // is checked as a fallback for pitch/roll and is the only source for yaw (cw/ccw).
    float                    gyroSelectThreshold    = 0.6f;
    float                    accelSelectThreshold   = 0.5f;
    bool                     console                = false;    // open a console window for live logs (set "console": true)
};

// Loads virtual pad identity config from a JSON file.
// Returns defaults if the file does not exist.
VirtualPadConfig loadVirtualPadConfig(const std::string& path);

// Persists only the virtual output type into virtualpad.json, preserving every
// other field already present in the file (vid/pid, locale, console, font_size...).
void saveVirtualPadOutputType(const std::string& path, VirtualOutputType outputType);

// ── Game profiles ──────────────────────────────────────────────────────────

// A game profile declares controller-agnostic button overrides.
// Keys are virtual Xbox output names (a, b, x, y, l1, r1, l2, r2, l3, r3,
// start, select, home, dpad_up/down/left/right). The profile applies to any
// controller: overrides for virtual buttons the controller does not produce
// are silently ignored.
//
// Merge semantics per section:
//   buttons / axes            — per-key override on top of the base config.
//   axis_actions / dpad_remap — whole-section replace: if present in the JSON
//                               (has* flag true) the base section is discarded,
//                               absent keys mean default behaviour.
//   trigger_actions           — per-side replace ("l2"/"r2"); a JSON null side
//                               resets that trigger to default behaviour.
struct GameProfile {
    std::string profile_name;
    std::unordered_map<std::string, ButtonAction> buttons;  // virtual Xbox name -> action
    std::unordered_map<std::string, AxisMapping>  axes;     // virtual axis name (right_x, left_y…) -> new mapping

    bool hasAxisActions = false;
    std::unordered_map<std::string, HalfAxisAction> axis_actions;  // "left_x_pos"… -> action

    bool hasGyroActions = false;
    std::unordered_map<std::string, HalfAxisAction> gyro_actions;   // "x_pos"… (gyro's own axes)

    bool hasAccelActions = false;
    std::unordered_map<std::string, HalfAxisAction> accel_actions;  // "x_pos"… (accel's own axes)

    bool hasDpadRemap = false;
    std::unordered_map<std::string, std::string>  dpadRemap;   // dir -> virtual short name
    std::unordered_map<std::string, ButtonAction> dpadActions; // dir -> keyboard/mouse/macro action
    std::unordered_map<std::string, std::string>  dpadSlots;   // dir -> stick slot ("right_x_pos"…)

    bool hasTriggerL = false, hasTriggerR = false;
    ButtonAction triggerLAction, triggerRAction;
    bool triggerLHasAction = false, triggerRHasAction = false;
    std::vector<TriggerRange> triggerLRanges, triggerRRanges;

    std::vector<std::string> context_bots;                  // bots to start when this profile is active
};

// Loads a game profile JSON. Returns a profile with an empty name if the
// file does not exist or has no profile_name field.
GameProfile loadGameProfile(const std::string& path);

// Returns a copy of base with the profile's button overrides applied.
// Resolves each virtual Xbox name to the bit that produces it on this controller.
// Overrides for virtual buttons not produced by this controller are silently ignored.
ControllerConfig applyProfile(const ControllerConfig& base, const GameProfile& profile);

// Rebuilds pc's base layer (buttons, dpad, triggers, analog dirs, gyro, accel) from cfg.
// Touchpad is left as parsed (not profile-overridable).
// Call after applyProfile() to ensure the Component System reflects profile overrides.
void rebuildPhysicalControllerFromConfig(PhysicalController& pc, const ControllerConfig& cfg);

// ── Component System ─────────────────────────────────────────────────────────

// Builds a PhysicalController from one controllers.json entry.
// Runs in parallel with ControllerConfig — does not affect existing behaviour.
std::vector<PhysicalController> loadPhysicalControllers(const std::string& path);

// ── Pad layouts ─────────────────────────────────────────────────────────────

// Loads all pad layouts from a JSON file.
// Returns an empty vector if the file does not exist.
std::vector<PadLayout> loadPadLayouts(const std::string& path);

// Returns a pointer to the layout with the given id, or nullptr if not found.
const PadLayout* findLayout(const std::vector<PadLayout>& layouts, const std::string& id);

// Serialises all pad layouts back to a JSON file.
// Throws std::runtime_error if the file cannot be written.
void savePadLayouts(const std::string& path, const std::vector<PadLayout>& layouts);
