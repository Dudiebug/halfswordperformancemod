#include "menu.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <vector>

namespace hsp {
using namespace ue;

namespace {
constexpr const char* kHudClass = "UI_HUD_C";
constexpr int32_t kZOrder = 1000;      // over the HUD's own widgets
constexpr float kWidth = 640;

// Engine value types as UMG functions take them (UE 5.4: FVector2D is two doubles).
struct V2 { double x = 0, y = 0; };
struct Anchors { V2 min, max; };
struct Margin { float l = 0, t = 0, r = 0, b = 0; };
struct Color { float r = 0, g = 0, b = 0, a = 1; };
struct ChildSize { float value = 1; uint8_t rule = 0; uint8_t pad[3] = {}; };   // FSlateChildSize: rule 0 Automatic, 1 Fill
static_assert(sizeof(Anchors) == 32 && sizeof(Margin) == 16 && sizeof(Color) == 16 && sizeof(ChildSize) == 8, "UMG value types");

enum : uint8_t { kVisible = 0, kCollapsed = 1, kHitTestInvisible = 3, kSelfHitTestInvisible = 4 };   // ESlateVisibility
enum : uint8_t { kFill = 0, kLeft = 1, kCenter = 2, kRight = 3 };                                     // E(Horizontal|Vertical)Alignment

constexpr Color kPanel{0.025f, 0.022f, 0.02f, 0.94f};
constexpr Color kRowIdle{0.10f, 0.10f, 0.10f, 1};
constexpr Color kRowSelected{0.55f, 0.42f, 0.18f, 1};
constexpr Color kTitle{0.95f, 0.80f, 0.45f, 1};
constexpr Color kText{0.92f, 0.90f, 0.86f, 1};
constexpr Color kDim{0.60f, 0.58f, 0.55f, 1};
constexpr Color kOn{0.50f, 0.88f, 0.50f, 1};
constexpr Color kOff{0.75f, 0.55f, 0.50f, 1};

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

double NowMs() { return double(GetTickCount64()); }

UObject* Cdo(const char* cls) {
    UObject* c = FindObject(cls, "Class");
    return c ? DefaultObject(reinterpret_cast<UClass*>(c)) : nullptr;
}

bool Has(UObject* o, const char* fn) { return o && FindFunction(ClassOf(o), fn); }
bool IsClass(UObject* o, const char* cls) {
    UClass* c = FindClass(cls);
    return o && c && IsA(o, c);
}

UObject* CallObj(UObject* o, const char* fn) {
    if (!Has(o, fn)) return nullptr;
    Call c(o, fn);
    return c.invoke() ? c.get<UObject*>("ReturnValue") : nullptr;
}
bool CallBool(UObject* o, const char* fn) {
    if (!Has(o, fn)) return false;
    Call c(o, fn);
    return c.invoke() && c.get<bool>("ReturnValue");
}

uint8_t GetVis(UObject* w) {
    Call c(w, "GetVisibility");
    return c.invoke() ? c.get<uint8_t>("ReturnValue") : kVisible;
}
void SetVis(UObject* w, uint8_t v) { if (w) Call(w, "SetVisibility").set<uint8_t>("InVisibility", v).invoke(); }

// A user widget's own children are its tree's root; a panel's are its slots; a content widget (Border, Button) has one.
std::vector<UObject*> Children(UObject* w) {
    std::vector<UObject*> out;
    if (UObject** tree = Field<UObject*>(w, "WidgetTree"); tree && *tree)
        if (UObject** root = Field<UObject*>(*tree, "RootWidget"); root && *root) out.push_back(*root);
    if (Has(w, "GetChildrenCount")) {
        Call n(w, "GetChildrenCount");
        const int32_t count = n.invoke() ? n.get<int32_t>("ReturnValue") : 0;
        for (int32_t i = 0; i < count && i < 256; ++i) {
            Call at(w, "GetChildAt");
            at.set<int32_t>("Index", i);
            if (at.invoke()) if (UObject* c = at.get<UObject*>("ReturnValue")) out.push_back(c);
        }
    } else if (UObject* c = CallObj(w, "GetContent")) {
        out.push_back(c);
    }
    return out;
}

// Offsets inside the engine structs the text setters take, from the runtime reflection (never hard-coded).
struct TextLayout { bool ok = false; int32_t fontSize = -1, color = -1, rule = -1; };
const TextLayout& Layout() {
    static TextLayout l;
    static bool tried = false;
    if (tried) return l;
    tried = true;
    FProperty* size = StructMember(FindStruct("SlateFontInfo"), "Size");
    UObject* sc = FindStruct("SlateColor");
    FProperty* color = StructMember(sc, "SpecifiedColor");
    FProperty* rule = StructMember(sc, "ColorUseRule");
    if (size && PropertySize(size) == 4) l.fontSize = PropertyOffset(size);
    if (color && rule && PropertySize(color) == int32_t(sizeof(Color)) && PropertySize(rule) == 1) {
        l.color = PropertyOffset(color);
        l.rule = PropertyOffset(rule);
    }
    l.ok = l.fontSize >= 0 && l.color >= 0;
    HSP_LOG("menu: text layout: font size +%d, colour +%d, rule +%d", l.fontSize, l.color, l.rule);
    return l;
}

class Builder {
public:
    Builder(UObject* tree, UObject* fontFrom) : tree_(tree), fontFrom_(fontFrom) {}
    UObject* Make(const char* cls) {
        UClass* c = FindClass(cls);
        UObject* o = c ? SpawnObject(c, tree_) : nullptr;
        if (!o && failed_++ < 3) HSP_LOG("menu: could not make a %s", cls);
        return o;
    }
    UObject* Text(const std::string& s, float size, Color color, uint8_t justify = 0) {
        UObject* t = Make("TextBlock");
        if (!t) return nullptr;
        Font(t, size);
        Ink(t, color);
        Call(t, "SetJustification").set<uint8_t>("InJustification", justify).invoke();
        SetVis(t, kHitTestInvisible);
        Set(t, s);
        return t;
    }
    void Font(UObject* t, float size) {
        const TextLayout& l = Layout();
        void* src = FieldPtr(fontFrom_ ? fontFrom_ : t, "Font");
        Call c(t, "SetFont");
        uint8_t* p = static_cast<uint8_t*>(c.paramPtr("InFontInfo"));
        if (!src || !p || l.fontSize < 0) return;
        c.setFrom("InFontInfo", src);   // a deep copy by the engine: the font info holds shared pointers
        std::memcpy(p + l.fontSize, &size, sizeof size);
        c.invoke();
    }
    static void Ink(UObject* t, Color color) {
        const TextLayout& l = Layout();
        Call c(t, "SetColorAndOpacity");
        uint8_t* p = static_cast<uint8_t*>(c.paramPtr("InColorAndOpacity"));
        if (!p || l.color < 0) return;
        std::memcpy(p + l.color, &color, sizeof color);
        p[l.rule] = 0;   // ESlateColorStylingMode::UseColor_Specified
        c.invoke();
    }
    static void Set(UObject* t, const std::string& s) { if (t) Call(t, "SetText").set("InText", MakeText(s)).invoke(); }
    static UObject* AddTo(UObject* box, const char* fn, UObject* child) {
        if (!box || !child) return nullptr;
        Call c(box, fn);
        c.set<UObject*>("Content", child);
        return c.invoke() ? c.get<UObject*>("ReturnValue") : nullptr;
    }
    static void Pad(UObject* slot, Margin m) { if (slot) Call(slot, "SetPadding").set("InPadding", m).invoke(); }
    static void HAlign(UObject* slot, uint8_t a) { if (slot) Call(slot, "SetHorizontalAlignment").set<uint8_t>("InHorizontalAlignment", a).invoke(); }
    static void VAlign(UObject* slot, uint8_t a) { if (slot) Call(slot, "SetVerticalAlignment").set<uint8_t>("InVerticalAlignment", a).invoke(); }
private:
    UObject* tree_;
    UObject* fontFrom_;
    int failed_ = 0;
};

const char* const kLabels[] = {
    "Armour follows the body (leader pose)",
    "Tighter armour bounds",
    "No armour shadows on far fighters",
    "Far means farther than",
    "Armour in ray traced reflections and light",
    "Blood and dents (vertex paint)",
    "Cheaper blood (looks the same)",
    "Weapons on the floor stop ticking",
    "Still corpses stop ticking",
    "Tighter body bounds",
    "Head and skeleton follow the body",
    "Skip the game mode's empty tick",
    "Physics on more threads (same result)",
    "Frame time counters (F10)",
    "Mod fixes (compare with Ctrl + F10)",
    "Record a 30 s profile (Ctrl + Shift + F10)",
    "Close (F9 or Esc)",
};
static_assert(sizeof kLabels / sizeof kLabels[0] == 17, "one label per row");
}  // namespace

UObject* Menu::PlayerController(UObject* gameMode) {
    static UObject* statics = nullptr;
    if (!statics) statics = Cdo("GameplayStatics");
    if (!statics) return nullptr;
    Call pc(statics, "GetPlayerController");
    pc.set<UObject*>("WorldContextObject", gameMode).set<int32_t>("PlayerIndex", 0);
    return pc.invoke() ? pc.get<UObject*>("ReturnValue") : nullptr;
}

bool Menu::Attach(UObject* gameMode) {
    (void)gameMode;
    if (hud_ && ObjectAt(hudIndex_) == hud_ && CallBool(hud_, "IsInViewport")) return built_ || Build();
    hud_ = nullptr;
    hudIndex_ = -1;
    canvas_ = nullptr;
    built_ = false;
    w_ = Widgets{};
    if (!FindClass(kHudClass)) return false;
    for (UObject* o : FindInstances(kHudClass))
        if (CallBool(o, "IsInViewport")) { hud_ = o; hudIndex_ = ObjectIndex(o); break; }
    return hud_ && Build();
}

bool Menu::Build() {
    built_ = true;   // one try per HUD: a failed build is not repeated every frame
    UObject** tree = Field<UObject*>(hud_, "WidgetTree");
    if (!tree || !*tree) { HSP_LOG("menu: UI_HUD has no widget tree"); return false; }
    // The HUD's first canvas (for the panel) and first text (its typeface), breadth first.
    UObject* font = nullptr;
    std::vector<UObject*> queue = Children(hud_);
    for (size_t i = 0; i < queue.size() && i < 400 && (!canvas_ || !font); ++i) {
        UObject* x = queue[i];
        if (!canvas_ && IsClass(x, "CanvasPanel")) canvas_ = x;
        if (!font && IsClass(x, "TextBlock")) font = x;
        for (UObject* c : Children(x)) queue.push_back(c);
    }
    if (!canvas_) { HSP_LOG("menu: UI_HUD has no canvas to put the menu on"); return false; }
    Builder b(*tree, font);

    UObject* panel = b.Make("Border");
    UObject* width = b.Make("SizeBox");
    UObject* col = b.Make("VerticalBox");
    if (!panel || !width || !col) return false;
    Call(panel, "SetBrushColor").set("InBrushColor", kPanel).invoke();
    Call(panel, "SetPadding").set("InPadding", Margin{26, 20, 26, 18}).invoke();
    Call(width, "SetWidthOverride").set<float>("InWidthOverride", kWidth).invoke();
    Call(panel, "SetContent").set<UObject*>("Content", width).invoke();
    Call(width, "SetContent").set<UObject*>("Content", col).invoke();
    UObject* slot = Builder::AddTo(canvas_, "AddChildToCanvas", panel);
    if (!slot) { HSP_LOG("menu: could not add the panel to UI_HUD's canvas"); return false; }
    Call(slot, "SetAnchors").set("InAnchors", Anchors{{0.5, 0.5}, {0.5, 0.5}}).invoke();
    Call(slot, "SetAlignment").set("InAlignment", V2{0.5, 0.5}).invoke();
    Call(slot, "SetAutoSize").set<bool>("InbAutoSize", true).invoke();
    Call(slot, "SetZOrder").set<int32_t>("InZOrder", kZOrder).invoke();
    SetVis(panel, kCollapsed);
    w_.root = panel;

    Builder::Pad(Builder::AddTo(col, "AddChildToVerticalBox", b.Text("Half Sword Performance Mod", 22, kTitle)), {0, 0, 0, 2});
    w_.stats = b.Text("", 12, kDim);
    Builder::Pad(Builder::AddTo(col, "AddChildToVerticalBox", w_.stats), {0, 0, 0, 12});
    for (int r = 0; r < kRows; ++r) {
        UObject* button = b.Make("Button");
        UObject* row = b.Make("HorizontalBox");
        UObject* label = b.Text(kLabels[r], 15, kText);
        UObject* value = b.Text("", 15, kDim, 2);
        if (!button || !row || !label || !value) return false;
        SetVis(row, kHitTestInvisible);
        Call(button, "SetBackgroundColor").set("InBackgroundColor", kRowIdle).invoke();
        UObject* inButton = Builder::AddTo(button, "SetContent", row);
        Builder::Pad(inButton, {12, 5, 12, 5});
        Builder::HAlign(inButton, kFill);
        UObject* ls = Builder::AddTo(row, "AddChildToHorizontalBox", label);
        if (ls) Call(ls, "SetSize").set("InSize", ChildSize{1, 1}).invoke();
        Builder::VAlign(ls, kCenter);
        UObject* vs = Builder::AddTo(row, "AddChildToHorizontalBox", value);
        Builder::VAlign(vs, kCenter);
        Builder::Pad(vs, {16, 0, 0, 0});
        Builder::Pad(Builder::AddTo(col, "AddChildToVerticalBox", button), {0, r == kFixes || r == kClose ? 10.f : 2.f, 0, 0});
        w_.button[r] = button;
        w_.value[r] = value;
    }
    w_.status = b.Text("", 12, kDim);
    Builder::Pad(Builder::AddTo(col, "AddChildToVerticalBox", w_.status), {0, 12, 0, 0});
    Builder::Pad(Builder::AddTo(col, "AddChildToVerticalBox",
                                b.Text("Click a row, or Up / Down to choose and Enter / Left / Right to change. Changes are saved.", 12, kDim)),
                 {0, 4, 0, 0});
    for (int r = 0; r < kRows; ++r) shown_[r].clear();
    shownStats_.clear();
    shownStatus_.clear();
    shownSelected_ = -1;
    w_.ready = true;
    HSP_LOG("menu: built on %s (font from %s)", ObjectName(hud_).c_str(), font ? ObjectName(font).c_str() : "the engine default");
    return true;
}

void Menu::Open(UObject* gameMode) {
    if (!Attach(gameMode) || !w_.ready) {
        Status("");
        if (!noHudLogged_) {
            noHudLogged_ = true;
            if (hud_) HSP_LOG("menu: could not build the menu on %s (see the menu: line above)", ObjectName(hud_).c_str());
            else HSP_LOG("menu: no game HUD on screen; the menu opens during a fight");
        }
        return;
    }
    UObject* pc = PlayerController(gameMode);
    bool cursor = false;
    cursorWasUp_ = pc && GetBool(pc, "bShowMouseCursor", cursor) && cursor;
    // The HUD takes no clicks (HitTestInvisible hides its children from the mouse too): while the menu is up its parents
    // are made SelfHitTestInvisible, so the clicks reach the menu.
    hudVis_ = GetVis(hud_);
    canvasVis_ = GetVis(canvas_);
    if (hudVis_ == kHitTestInvisible) SetVis(hud_, kSelfHitTestInvisible);
    if (canvasVis_ == kHitTestInvisible) SetVis(canvas_, kSelfHitTestInvisible);
    SetVis(w_.root, kVisible);
    if (pc && !cursorWasUp_) {
        static UObject* wbl = Cdo("WidgetBlueprintLibrary");
        if (wbl)
            Call(wbl, "SetInputMode_UIOnlyEx").set<UObject*>("PlayerController", pc).set<UObject*>("InWidgetToFocus", nullptr)
                .set<uint8_t>("InMouseLockMode", 0).set<bool>("bFlushInput", true).invoke();
        SetBool(pc, "bShowMouseCursor", true);
    }
    open_ = true;
    shownSelected_ = -1;
    nextInfoMs_ = 0;
    HSP_LOG("menu: open (HUD visibility %u, canvas %u, cursor was %s)", unsigned(hudVis_), unsigned(canvasVis_), cursorWasUp_ ? "up" : "hidden");
}

void Menu::Close(UObject* gameMode) {
    open_ = false;
    if (hud_ && ObjectAt(hudIndex_) == hud_) {
        SetVis(w_.root, kCollapsed);
        if (hudVis_ == kHitTestInvisible) SetVis(hud_, kHitTestInvisible);
        if (canvasVis_ == kHitTestInvisible) SetVis(canvas_, kHitTestInvisible);
    }
    if (!cursorWasUp_)
        if (UObject* pc = PlayerController(gameMode)) {
            static UObject* wbl = Cdo("WidgetBlueprintLibrary");
            if (wbl) Call(wbl, "SetInputMode_GameOnly").set<UObject*>("PlayerController", pc).set<bool>("bFlushInput", true).invoke();
            SetBool(pc, "bShowMouseCursor", false);
        }
    HSP_LOG("menu: closed");
}

void Menu::Status(const std::string& line) { status_ = line; }

MenuAction Menu::Change(int row, int dir, core::Settings& s) {
    switch (row) {
    case kLeader:
        s.armorLeaderPose = !s.armorLeaderPose;
        if (!s.armorLeaderPose) s.armorBounds = false;   // bounds from the body need the body's pose
        break;
    case kBounds:
        if (!s.armorLeaderPose) { Status("Tighter bounds need \"Armour follows the body\" on."); return MenuAction::None; }
        s.armorBounds = !s.armorBounds;
        break;
    case kShadow: s.shadowTrim = !s.shadowTrim; break;
    case kShadowMeters: s.shadowTrimMeters = core::NextTrimMeters(s.shadowTrimMeters, dir); break;
    case kPaint: s.vertexPaint = !s.vertexPaint; break;
    case kBlood: s.cheaperBlood = !s.cheaperBlood; break;
    case kWeapons: s.idleWeapons = !s.idleWeapons; break;
    case kArmorRt: s.armorRayTracing = !s.armorRayTracing; break;
    case kCorpses: s.corpseSleep = !s.corpseSleep; break;
    case kBodyBounds: s.bodyBounds = !s.bodyBounds; break;
    case kHead: s.headLeaderPose = !s.headLeaderPose; break;
    case kSkipTick: s.skipEmptyGameModeTick = !s.skipEmptyGameModeTick; break;
    case kIslands: s.physicsIslands = !s.physicsIslands; break;
    case kOverlay: return MenuAction::ToggleOverlay;
    case kFixes: return MenuAction::ToggleFixes;
    case kProfile: return MenuAction::ToggleProfile;
    default: return MenuAction::None;
    }
    Status(SaveSettings(s) ? "Saved to HSPerf.ini." : "Changed, but HSPerf.ini could not be written (see HSPerf.log).");
    HSP_LOG("menu: %s -> %s", kLabels[row], row == kShadowMeters ? (std::to_string(int(s.shadowTrimMeters)) + " m").c_str() : "toggled");
    return MenuAction::SettingsChanged;
}

void Menu::Refresh(const core::Settings& s, const MenuInfo& info, bool statsDue) {
    auto onOff = [](bool v) { return v ? "on" : "off"; };
    std::string v[kRows];
    v[kLeader] = onOff(s.armorLeaderPose);
    v[kBounds] = s.armorLeaderPose ? onOff(s.armorBounds) : "needs leader pose";
    v[kShadow] = onOff(s.shadowTrim);
    v[kShadowMeters] = std::to_string(int(s.shadowTrimMeters)) + " m";
    v[kPaint] = onOff(s.vertexPaint);
    v[kBlood] = onOff(s.cheaperBlood);
    v[kWeapons] = onOff(s.idleWeapons);
    v[kArmorRt] = s.armorRayTracing ? "on (as the game ships)" : "off";
    v[kCorpses] = onOff(s.corpseSleep);
    v[kBodyBounds] = onOff(s.bodyBounds);
    v[kHead] = onOff(s.headLeaderPose);
    v[kSkipTick] = onOff(s.skipEmptyGameModeTick);
    v[kIslands] = onOff(s.physicsIslands);
    v[kOverlay] = onOff(info.overlay);
    v[kFixes] = info.fixesActive ? "on" : "off (as the game ships)";
    v[kProfile] = info.recording ? "recording, click to stop" : "start";
    for (int r = 0; r < kRows; ++r) {
        if (v[r] == shown_[r]) continue;
        shown_[r] = v[r];
        Builder::Set(w_.value[r], v[r]);
        const bool good = r == kArmorRt ? !s.armorRayTracing : v[r] == "on" || r == kShadowMeters || (r == kProfile && info.recording);
        Builder::Ink(w_.value[r], (r == kProfile && !info.recording) ? kDim : good ? kOn : kOff);
    }
    if (selected_ != shownSelected_) {
        for (int r = 0; r < kRows; ++r)
            if (r == selected_ || r == shownSelected_)
                Call(w_.button[r], "SetBackgroundColor").set("InBackgroundColor", r == selected_ ? kRowSelected : kRowIdle).invoke();
        shownSelected_ = selected_;
    }
    if (statsDue && info.stats != shownStats_) { shownStats_ = info.stats; Builder::Set(w_.stats, info.stats); }
    if (status_ != shownStatus_) { shownStatus_ = status_; Builder::Set(w_.status, status_); }
}

MenuAction Menu::Tick(UObject* gameMode, core::Settings& s, const MenuInfo& info) {
    // Keys, polled from the OS (the game gets none while the menu is open). A press is a key going down.
    const int vks[8] = {s.menuKey, VK_ESCAPE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_LBUTTON};
    bool press[8] = {}, release[8] = {};
    const bool focus = GameHasFocus();
    for (int i = 0; i < 8; ++i) {
        const bool down = focus && vks[i] && (GetAsyncKeyState(vks[i]) & 0x8000);
        press[i] = down && !keys_[i];
        release[i] = !down && keys_[i];
        keys_[i] = down;
    }
    if (press[0]) {
        if (open_) Close(gameMode); else Open(gameMode);
        return MenuAction::None;
    }
    if (!open_) return MenuAction::None;
    // The game swapped or dropped its HUD (a respawn, a new round, back to the main menu): the panel went with it.
    const bool due = NowMs() >= nextInfoMs_;   // the stats line and the HUD check run 4 times a second
    if (due) nextInfoMs_ = NowMs() + 250;
    if (ObjectAt(hudIndex_) != hud_ || (due && !CallBool(hud_, "IsInViewport"))) {
        HSP_LOG("menu: the HUD left the screen; closing");
        hud_ = nullptr;
        Close(gameMode);
        return MenuAction::None;
    }
    if (release[1]) { Close(gameMode); return MenuAction::None; }   // on release, so the game never sees the Esc
    MenuAction act = MenuAction::None;
    if (press[2]) selected_ = (selected_ + kRows - 1) % kRows;
    if (press[3]) selected_ = (selected_ + 1) % kRows;
    int hovered = -1;
    for (int r = 0; r < kRows; ++r)
        if (CallBool(w_.button[r], "IsHovered")) { hovered = r; break; }
    if (hovered >= 0) selected_ = hovered;
    int pick = -1, dir = 1;
    if (press[4]) { pick = selected_; dir = -1; }
    if (press[5] || press[6]) pick = selected_;
    if (press[7] && hovered >= 0) pick = hovered;
    if (pick == kClose) { Close(gameMode); return MenuAction::None; }
    if (pick >= 0) act = Change(pick, dir, s);
    Refresh(s, info, due);
    return act;
}

}  // namespace hsp
