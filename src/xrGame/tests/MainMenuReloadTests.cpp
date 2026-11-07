// Execute the production ReloadUI/restart methods with deterministic dependencies.
// This is an ownership/control-flow test, not a Lua factory or live input-stack test.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iterator>
#include <limits.h>
#include <type_traits>
#include <string>
#include <vector>

#if !defined(_MSC_VER) && defined(__EXCEPTIONS)
#error This regression must run with C++ exceptions disabled
#endif

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
    std::exit(1); } } while (false)
// Supply platform decorations and malloc backing for the actual registry/allocator
// headers, without linking an initialized engine or changing their algorithms.
#define XRCORE_API
#define IC inline
#define ICF inline
#ifndef _MSC_VER
#define __fastcall
#endif
#ifdef _WIN32
#define XR_PLATFORM_WINDOWS
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#else
#define XR_PLATFORM_POSIX
#endif
#include "xrEngine/pure.h"
xrMemory Memory;
int allocatorCalls = 0;
void* xrMemory::mem_alloc(size_t size)
{
    ++allocatorCalls;
    void* memory = std::malloc(size);
    CHECK(memory != nullptr);
    return memory;
}
void xrMemory::mem_free(void* memory) { std::free(memory); }

#define FALSE false
#define TRUE true
using u32 = std::uint32_t;
#define VERIFY(condition) ((void)0) // Match the original Release build, not a debug abort.
constexpr int MAIN_MENU_RELOADED = 1;
std::vector<std::string> events;

struct Counts
{
    int destroyed = 0;
    int hidden = 0;
    int shown = 0;
    int messages = 0;
};
struct IFactoryObject { virtual ~IFactoryObject() = default; };
struct CUIDialogWnd
{
    Counts& counts;
    const char* name;
    bool shown = false;
    bool m_bWorkInPause = false;
    CUIDialogWnd(Counts& counts, const char* name) : counts(counts), name(name) {}
    virtual ~CUIDialogWnd() { ++counts.destroyed; events.emplace_back(std::string(name) + ":destroy"); }
    bool IsShown() const { return shown; }
    void HideDialog() { ++counts.hidden; shown = false; events.emplace_back(std::string(name) + ":hide"); }
    void ShowDialog(bool hideIndicators)
    {
        CHECK(hideIndicators);
        CHECK(m_bWorkInPause);
        ++counts.shown; shown = true; events.emplace_back(std::string(name) + ":show");
    }
    void SendMessage(CUIDialogWnd* sender, int message, void* data)
    {
        CHECK(sender == this && message == MAIN_MENU_RELOADED && data == nullptr);
        ++counts.messages; events.emplace_back(std::string(name) + ":notify");
    }
};
// A cross-cast between distinct bases, as with script-created dialogs.
struct FactoryDialog final : CUIDialogWnd, virtual IFactoryObject
{
    FactoryDialog(Counts& counts, const char* name) : CUIDialogWnd(counts, name) {}
};
struct FactoryPadding { virtual ~FactoryPadding() = default; };
struct NonDialog final : FactoryPadding, virtual IFactoryObject
{
    Counts& counts;
    explicit NonDialog(Counts& counts) : counts(counts) {}
    ~NonDialog() override { ++counts.destroyed; events.emplace_back("candidate:destroy"); }
};
template<class T> T smart_cast(IFactoryObject* object) { return dynamic_cast<T>(object); }
struct Factory
{
    IFactoryObject* next = nullptr;
    void (*onCreate)() = nullptr;
    int creates = 0;
    int destroys = 0;
    IFactoryObject* Create()
    {
        ++creates; events.emplace_back("create");
        if (onCreate) onCreate();
        auto* result = next; next = nullptr; return result;
    }
    void Destroy(IFactoryObject* object)
    {
        CHECK(object != nullptr); ++destroys; events.emplace_back("factory:destroy"); xr_delete(object);
    }
} factory;
#define NEW_INSTANCE(classId) factory.Create()
#define DEL_INSTANCE(object) { factory.Destroy(object); object = nullptr; }
void Msg(const char*) { events.emplace_back("error"); }
struct Flags
{
    unsigned bits = 0;
    bool test(unsigned mask) const { return (bits & mask) != 0; }
    void set(unsigned mask, bool value) { bits = value ? bits | mask : bits & ~mask; }
};
// Dependencies record effects; Activate, ReloadUI and cleanup are production code.
template<class T>
struct ObservedRegistry : MessageRegistry<T>
{
    template<class Object> int Count(Object* object)
    {
        int& calls = [&]() -> int& {
            if constexpr (std::is_same_v<T, pureFrame>) return object->frameCalls;
            else return object->renderCalls;
        }();
        const int before = calls;
        this->Process();
        return calls - before;
    }
};
struct IGame_Level : pureFrame, pureRender
{
    // Fixture identities; the actual engine allocator/constructor is separately
    // compiled and checked by the retained production-source harness.
    virtual ~IGame_Level() = default;
    inline static u64 nextIdentity = 0;
    const u64 identity = ++nextIdentity;
    u64 LifetimeId() const { return identity; }
    bool bReady = true;
    int frameCalls = 0, renderCalls = 0;
    void OnFrame() override { ++frameCalls; }
    void OnRender() override { ++renderCalls; }
};
IGame_Level* g_pGameLevel = nullptr;
bool singlePlayer = true;
bool IsGameTypeSingle() { return singlePlayer; }
bool b_shniaganeed_pp = false;
bool bShowPauseString = false;
bool g_bBenchmark = false;
struct Environment { bool isDedicatedServer = false; bool Sound = true; } GEnv;
struct DeviceFixture
{
    u32 dwFrame = 100;
    ObservedRegistry<pureFrame> seqFrame;
    ObservedRegistry<pureRender> seqRender;
    bool paused = false;
    bool canPause = true;
    int soundDepth = 0;
    int emitterPauseId = 0;
    int soundAcquires = 0, soundReleases = 0, timerReleases = 0;
    bool Paused() const { return paused; }
    void Pause(bool on, bool timer, bool sound, const char*)
    {
        if (g_bBenchmark || GEnv.isDedicatedServer) return;
        if (on)
        {
            if (!paused) bShowPauseString = true;
            if (timer && canPause) paused = true;
            if (sound && GEnv.Sound)
            {
                ++soundAcquires; ++soundDepth;
                if (!emitterPauseId) emitterPauseId = soundDepth;
            }
        }
        else
        {
            if (timer && paused) { paused = false; ++timerReleases; }
            if (sound && soundDepth > 0)
            {
                ++soundReleases;
                if (emitterPauseId == soundDepth) emitterPauseId = 0;
                --soundDepth;
            }
        }
    }
    int resets = 0;
    void Reset() { ++resets; }
    void PreCache(int, bool) {}
} Device;
struct CursorFixture
{
    bool visible = false;
    bool IsVisible() const { return visible; }
    void Show() { visible = true; }
    void Hide() { visible = false; }
} cursor;
CursorFixture& GetUICursor() { return cursor; }
struct ConsoleFixture
{
    bool bVisible = false;
    void Hide() { bVisible = false; }
    void Show() { bVisible = true; }
    void Execute(const char*) {}
} console;
ConsoleFixture* Console = &console;
struct CCameraManager { static void ResetPP() {} };
void ResetEnvironment()
{
    Device = {}; GEnv = {}; console = {}; cursor = {};
    g_pGameLevel = nullptr; singlePlayer = true;
    bShowPauseString = false; g_bBenchmark = false; b_shniaganeed_pp = false;
}
class CMainMenu : public pureFrame, public pureRender
{
public:
    enum { flRestoreConsole = 1 << 0, flRestorePause = 1 << 1, flRestorePauseStr = 1 << 2,
        flActive = 1 << 3, flNeedChangeCapture = 1 << 4, flRestoreCursor = 1 << 5,
        flGameSaveScreenshot = 1 << 6, flNeedVidRestart = 1 << 7, flNeedUIRestart = 1 << 8,
        preservedFlag = flRestoreConsole };
    int frameCalls = 0, renderCalls = 0;
    void OnFrame() override { ++frameCalls; }
    void OnRender() override { ++renderCalls; }
    // Effects owned by the current activation, including a partially completed one.
    struct ActivationEffects
    {
        u64 levelLifetimeId{}; // Compare only with the current live level; retain no pointer.
        bool started{};
        bool soundPaused{};
        bool timerPaused{};
        bool consoleHidden{};
        bool levelFrameRemoved{};
        bool levelRenderRemoved{};
        bool menuRenderAdded{};
    } m_activation;
    Flags m_Flags;
    u32 m_screenshotFrame = u32(-1), m_deactivated_frame = 0;
    CUIDialogWnd* m_startDialog = nullptr;
    std::vector<CUIDialogWnd*> holder;
    // The sentinel models the existing input stack's permanent bottom receiver.
    int sentinel = 0;
    std::vector<void*> input{&sentinel};
    int releases = 0;
    ~CMainMenu() { xr_delete(m_startDialog); }
    void CleanInternals() { holder.clear(); GetUICursor().Hide(); events.emplace_back("clean"); }
    void IR_Release()
    {
        ++releases; events.emplace_back("release");
        auto found = std::find(input.rbegin(), input.rend(), this);
        if (found != input.rend()) input.erase(std::next(found).base());
    }
    void Activate(bool active);
    bool IsActive() const { return m_Flags.test(flActive); }
    bool ReloadUI();
    void UpdateUIRestart();
    void DeactivateUI();
};
#include "../MainMenuReload.inl"

void Prepare(CMainMenu& menu, Counts& old, bool active, bool shown = true)
{
    menu.m_Flags.set(CMainMenu::preservedFlag, true);
    if (active)
    {
        menu.m_startDialog = xr_new<FactoryDialog>(old, "old");
        menu.m_startDialog->shown = shown;
        menu.holder.push_back(menu.m_startDialog);
        menu.m_Flags.set(CMainMenu::flActive | CMainMenu::flNeedUIRestart, true);
        menu.input.push_back(&menu);
    }
    else
        menu.m_Flags.set(CMainMenu::flActive | CMainMenu::flNeedChangeCapture, true);
}
void CheckFailure(const CMainMenu& menu)
{
    CHECK(menu.m_startDialog == nullptr);
    CHECK(menu.holder.empty());
    CHECK(menu.m_Flags.bits == CMainMenu::preservedFlag);
    CHECK(menu.releases == 1);
    CHECK(std::find(menu.input.begin(), menu.input.end(), &menu) == menu.input.end());
}
void Failure(bool active, bool nonDialog, bool frameRestart, bool covered = false)
{
    Counts old, candidate;
    int coveringReceiver = 0;
    {
        CMainMenu menu;
        Prepare(menu, old, active);
        if (covered) menu.input.push_back(&coveringReceiver);
        if (nonDialog) factory.next = xr_new<NonDialog>(candidate);
        if (frameRestart) menu.UpdateUIRestart();
        else CHECK(!menu.ReloadUI());
        CHECK(old.messages == 0);
        CheckFailure(menu);
        CHECK(factory.creates == 1);
        CHECK(factory.destroys == (nonDialog ? 1 : 0));
        CHECK(candidate.destroyed == (nonDialog ? 1 : 0));
        CHECK(old.destroyed == (active ? 1 : 0));
        CHECK(old.hidden == (active ? 1 : 0));
        CHECK(old.messages == 0);
        CHECK(menu.input.size() == (covered ? 2 : 1));
        if (covered) CHECK(menu.input.back() == &coveringReceiver);
        // A cleared restart cannot retry, notify, or release again next frame.
        menu.UpdateUIRestart();
        CHECK(factory.creates == 1 && menu.releases == 1);
    }
    CHECK(old.destroyed == (active ? 1 : 0));
    CHECK(candidate.destroyed == (nonDialog ? 1 : 0));
}
void Valid(bool replace, bool frameRestart, bool shown = true)
{
    Counts old, candidate;
    {
        CMainMenu menu;
        Prepare(menu, old, replace, shown);
        if (!replace) menu.m_Flags.set(CMainMenu::flActive | CMainMenu::flNeedChangeCapture, true);
        auto* dialog = xr_new<FactoryDialog>(candidate, "new");
        factory.next = dialog;
        CHECK(static_cast<void*>(factory.next) != static_cast<void*>(static_cast<CUIDialogWnd*>(dialog)));
        const unsigned flags = menu.m_Flags.bits;
        if (frameRestart) menu.UpdateUIRestart();
        else CHECK(menu.ReloadUI());
        CHECK(menu.m_startDialog == static_cast<CUIDialogWnd*>(dialog));
        CHECK(menu.m_Flags.bits == (frameRestart ? flags & ~CMainMenu::flNeedUIRestart : flags));
        CHECK(menu.releases == 0 && factory.destroys == 0 && factory.creates == 1);
        CHECK(old.destroyed == (replace ? 1 : 0));
        CHECK(old.hidden == (replace && shown ? 1 : 0));
        CHECK(candidate.destroyed == 0 && candidate.shown == 1);
        CHECK(candidate.messages == (frameRestart ? 1 : 0));
        std::vector<std::string> expected;
        if (replace && shown) expected.emplace_back("old:hide");
        if (replace) expected.emplace_back("clean");
        expected.emplace_back("create");
        if (replace) expected.emplace_back("old:destroy");
        expected.emplace_back("new:show");
        if (frameRestart) expected.emplace_back("new:notify");
        CHECK(events == expected);
        if (frameRestart)
        {
            menu.UpdateUIRestart();
            CHECK(factory.creates == 1 && candidate.messages == 1);
        }
    }
    CHECK(candidate.destroyed == 1);
    CHECK(old.destroyed == (replace ? 1 : 0));
}
void Recovery()
{
    Counts old, candidate, rejected;
    {
        CMainMenu menu;
        Prepare(menu, old, true);
        factory.next = xr_new<NonDialog>(rejected);
        CHECK(!menu.ReloadUI());
        CheckFailure(menu);
        factory.next = xr_new<FactoryDialog>(candidate, "new");
        menu.m_Flags.set(CMainMenu::flActive | CMainMenu::flNeedChangeCapture, true);
        CHECK(menu.ReloadUI());
        CHECK(candidate.shown == 1 && candidate.destroyed == 0);
        CHECK(old.destroyed == 1 && rejected.destroyed == 1);
        CHECK(factory.destroys == 1 && factory.creates == 2);
    }
    CHECK(candidate.destroyed == 1 && old.destroyed == 1 && rejected.destroyed == 1);
}
void CheckRestored(CMainMenu& menu, IGame_Level* level, bool prepaused, int soundDepth)
{
    CHECK(!menu.IsActive());
    CHECK(Device.soundDepth == soundDepth);
    CHECK(Device.emitterPauseId == (soundDepth ? 1 : 0));
    CHECK(Device.Paused() == prepaused);
    CHECK(Device.seqRender.Count(&menu) == 0);
    if (level)
    {
        CHECK(Device.seqFrame.Count(level) == 1);
        CHECK(Device.seqRender.Count(level) == 1);
    }
}
void ActivationFailure(bool completed, bool nonDialog, bool frameRestart, bool prepaused = false)
{
    IGame_Level level;
    g_pGameLevel = &level;
    Device.seqFrame.Add(&level); Device.seqRender.Add(&level);
    if (prepaused) Device.Pause(true, true, true, "external pause");
    const int previousSound = Device.soundDepth;
    Counts old, rejected;
    {
        CMainMenu menu;
        if (completed)
        {
            factory.next = xr_new<FactoryDialog>(old, "old");
            menu.Activate(true);
            CHECK(menu.IsActive());
            CHECK(Device.seqRender.Count(&menu) == 1);
            CHECK(Device.seqFrame.Count(&level) == 0);
            CHECK(Device.seqRender.Count(&level) == 0);
            menu.input.push_back(&menu);
            menu.m_Flags.set(CMainMenu::flNeedChangeCapture, false);
        }
        if (nonDialog) factory.next = xr_new<NonDialog>(rejected);
        if (!completed) menu.Activate(true);
        else if (frameRestart)
        {
            menu.m_Flags.set(CMainMenu::flNeedUIRestart, true);
            menu.UpdateUIRestart();
        }
        else CHECK(!menu.ReloadUI()); // Exact method called by the active reset callback.
        CheckRestored(menu, &level, prepaused, previousSound);
        CHECK(menu.m_startDialog == nullptr);
        CHECK(menu.releases == 1);
        CHECK(menu.input.size() == 1);
        CHECK(old.destroyed == (completed ? 1 : 0));
        CHECK(old.messages == 0);
        CHECK(rejected.destroyed == (nonDialog ? 1 : 0));
        const int releases = Device.soundReleases;
        menu.Activate(false);
        CHECK(Device.soundReleases == releases && menu.releases == 1);
        CheckRestored(menu, &level, prepaused, previousSound);
    }
}
void RepeatedActivation()
{
    IGame_Level level;
    g_pGameLevel = &level;
    Device.seqFrame.Add(&level); Device.seqRender.Add(&level);
    Counts old, rejected, good;
    {
        CMainMenu menu;
        for (int i = 0; i < 3; ++i)
        {
            factory.next = xr_new<FactoryDialog>(old, "old");
            menu.Activate(true);
            CHECK(Device.seqRender.Count(&menu) == 1);
            factory.next = xr_new<NonDialog>(rejected);
            CHECK(!menu.ReloadUI());
            CheckRestored(menu, &level, false, 0);
        }
        factory.next = xr_new<FactoryDialog>(good, "new");
        menu.Activate(true);
        CHECK(Device.seqRender.Count(&menu) == 1);
        menu.Activate(false);
        CheckRestored(menu, &level, false, 0);
        CHECK(Device.soundAcquires == 4 && Device.soundReleases == 4);
        CHECK(old.destroyed == 3 && rejected.destroyed == 3);
    }
    CHECK(good.destroyed == 1);
}
void ValidActivationClose(bool prepaused)
{
    IGame_Level level;
    g_pGameLevel = &level;
    Device.seqFrame.Add(&level); Device.seqRender.Add(&level);
    if (prepaused) Device.Pause(true, true, true, "external pause");
    bShowPauseString = false;
    cursor.visible = true; Console->bVisible = true;
    Counts candidate;
    {
        CMainMenu menu;
        factory.next = xr_new<FactoryDialog>(candidate, "new");
        menu.Activate(true);
        CHECK(menu.IsActive() && Device.seqRender.Count(&menu) == 1);
        menu.input.push_back(&menu);
        menu.Activate(false);
        CheckRestored(menu, &level, prepaused, prepaused ? 1 : 0);
        CHECK(!bShowPauseString && cursor.visible && Console->bVisible);
        CHECK(!menu.m_Flags.test(CMainMenu::flNeedChangeCapture));
        CHECK(menu.releases == 1 && menu.input.size() == 1);
        CHECK(candidate.hidden == 1 && candidate.destroyed == 0);
        CHECK(Device.timerReleases == (prepaused ? 0 : 1));
        menu.Activate(false);
        CHECK(menu.releases == 1);
    }
    CHECK(candidate.destroyed == 1);
}
void MembershipRestoration(int mode)
{
    IGame_Level replacement;
    auto* level = xr_new<IGame_Level>();
    g_pGameLevel = level;
    if (mode != 1) Device.seqFrame.Add(level);
    if (mode != 1 && mode != 2) Device.seqRender.Add(level);
    if (mode == 1) level->bReady = false;
    Counts old;
    {
        CMainMenu menu;
        factory.next = xr_new<FactoryDialog>(old, "old");
        menu.Activate(true);
        if (mode == 0) // Another path has already registered the current level.
        {
            Device.seqFrame.Add(level); Device.seqRender.Add(level);
        }
        else if (mode == 3 || mode == 4)
        {
            // The recorded level is freed: ASan detects any stale dereference.
            xr_delete(level);
            g_pGameLevel = mode == 3 ? &replacement : nullptr;
            if (g_pGameLevel)
            {
                Device.seqFrame.Add(g_pGameLevel); Device.seqRender.Add(g_pGameLevel);
            }
        }
        else if (mode == 5 || mode == 6)
        {
            // A new, not-yet-loaded level occupies the same allocation.
            level->~IGame_Level();
            new (level) IGame_Level();
            level->bReady = mode == 6;
        }
        CHECK(!menu.ReloadUI());
        CHECK(Device.seqRender.Count(&menu) == 0);
        if (mode == 3)
        {
            CHECK(Device.seqFrame.Count(&replacement) == 1);
            CHECK(Device.seqRender.Count(&replacement) == 1);
        }
        else if (level)
        {
            CHECK(Device.seqFrame.Count(level) == ((mode == 1 || mode == 5 || mode == 6) ? 0 : 1));
            CHECK(Device.seqRender.Count(level) == ((mode == 1 || mode == 2 || mode == 5 || mode == 6) ? 0 : 1));
        }
        CHECK(Device.soundDepth == 0 && !Device.Paused());
    }
    xr_delete(level);
}
void ChangedGameType(bool wasSingle)
{
    IGame_Level level;
    g_pGameLevel = &level; singlePlayer = wasSingle;
    Device.seqFrame.Add(&level); Device.seqRender.Add(&level);
    Counts old;
    CMainMenu menu;
    factory.next = xr_new<FactoryDialog>(old, "old");
    menu.Activate(true);
    singlePlayer = !wasSingle;
    CHECK(!menu.ReloadUI());
    CheckRestored(menu, &level, false, 0);
}
void NoTimerAcquired()
{
    Device.canPause = false;
    Counts old;
    CMainMenu menu;
    factory.next = xr_new<FactoryDialog>(old, "old");
    menu.Activate(true);
    CHECK(!Device.Paused());
    Device.paused = true; // Another owner pauses after the rejected menu request.
    CHECK(!menu.ReloadUI());
    CHECK(Device.Paused() && Device.timerReleases == 0);
    CHECK(Device.soundDepth == 0);
}
void InitialFailureRestoresUI()
{
    Counts rejected;
    CMainMenu menu;
    cursor.visible = true; Console->bVisible = true; bShowPauseString = false;
    menu.m_Flags.set(CMainMenu::flNeedVidRestart, true);
    factory.next = xr_new<NonDialog>(rejected);
    menu.Activate(true);
    CHECK(cursor.visible && Console->bVisible && !bShowPauseString);
    CHECK(Device.resets == 0 && menu.m_Flags.test(CMainMenu::flNeedVidRestart));
    CHECK(Device.soundDepth == 0 && Device.soundAcquires == 1 && Device.soundReleases == 1);
}
void InitialRetries()
{
    Counts rejected, valid;
    CMainMenu menu;
    for (int i = 0; i < 3; ++i)
    {
        factory.next = xr_new<NonDialog>(rejected);
        menu.Activate(true);
        CHECK(Device.soundDepth == 0 && Device.emitterPauseId == 0);
    }
    CHECK(rejected.destroyed == 3);
    factory.next = xr_new<FactoryDialog>(valid, "new");
    menu.Activate(true); menu.Activate(false);
    CHECK(Device.soundAcquires == 4 && Device.soundReleases == 4);
    CHECK(Device.seqRender.Count(&menu) == 0 && !Device.Paused());
}
void FailureBypassesCloseGuards()
{
    Counts old;
    CMainMenu menu;
    factory.next = xr_new<FactoryDialog>(old, "old");
    menu.Activate(true);
    menu.m_screenshotFrame = Device.dwFrame;
    menu.m_Flags.set(CMainMenu::flGameSaveScreenshot, true);
    CHECK(!menu.ReloadUI());
    CHECK(!menu.IsActive() && Device.soundDepth == 0 && !Device.Paused());
    CHECK(Device.seqRender.Count(&menu) == 0);
}
void PauseDuringFactory()
{
    Counts candidate;
    CMainMenu menu;
    factory.next = xr_new<FactoryDialog>(candidate, "new");
    factory.onCreate = [] { Device.Pause(true, true, false, "factory callback pause"); };
    menu.Activate(true);
    menu.Activate(false);
    CHECK(Device.Paused());
    CHECK(Device.timerReleases == 0);
    CHECK(Device.soundDepth == 0);
}
void MenuCleanupDuringRegistryProcess()
{
    IGame_Level level;
    g_pGameLevel = &level;
    Device.seqFrame.Add(&level); Device.seqRender.Add(&level);
    Counts old;
    CMainMenu menu;
    struct Driver : pureFrame
    {
        CMainMenu& menu;
        Counts& old;
        bool ran = false;
        Driver(CMainMenu& menu, Counts& old) : menu(menu), old(old) {}
        void OnFrame() override
        {
            if (ran) return;
            ran = true;
            factory.next = xr_new<FactoryDialog>(old, "old");
            menu.Activate(true);
            CHECK(!Device.seqFrame.Contains(g_pGameLevel));
            CHECK(!menu.ReloadUI());
            CHECK(Device.seqFrame.Contains(g_pGameLevel));
        }
    } driver(menu, old);
    Device.seqFrame.Add(&driver, REG_PRIORITY_HIGH);
    Device.seqFrame.Process();
    CHECK(driver.ran && level.frameCalls == 1);
    Device.seqFrame.Remove(&driver);
    CheckRestored(menu, &level, false, 0);
    CHECK(old.destroyed == 1 && menu.releases == 1);
}
struct RegistryProbe
{
    MessageRegistry<RegistryProbe>* registry = nullptr;
    RegistryProbe* other = nullptr;
    void (*action)(RegistryProbe*) = nullptr;
    int calls = 0;
    static void OnPure(RegistryProbe* self)
    {
        ++self->calls;
        if (self->action) self->action(self);
    }
};
void ActualRegistryMembership()
{
    MessageRegistry<RegistryProbe> registry;
    const auto& readOnly = registry;
    RegistryProbe first, removed;
    CHECK(!readOnly.Contains(&first));
    registry.Add(&first, 10);
    registry.Add(&removed, 5);
    const int allocationCount = allocatorCalls;
    CHECK(readOnly.Contains(&first) && readOnly.Contains(&removed));
    CHECK(allocatorCalls == allocationCount);
    first.registry = &registry; first.other = &removed;
    first.action = [](RegistryProbe* self)
    {
        self->registry->Remove(self->other);
        // Invalid entry is still physically present until Process calls Resort.
        CHECK(!self->registry->Contains(self->other));
        self->registry->Remove(self);
        CHECK(!self->registry->Contains(self));
    };
    registry.Process();
    CHECK(first.calls == 1 && removed.calls == 0);
    CHECK(!readOnly.Contains(&first) && !readOnly.Contains(&removed));
    registry.Add(&removed);
    if (!readOnly.Contains(&removed)) registry.Add(&removed);
    registry.Process();
    CHECK(removed.calls == 1);
}
void ActualRegistryReregisterInProcess()
{
    MessageRegistry<RegistryProbe> registry;
    RegistryProbe first, other;
    first.registry = &registry; first.other = &other;
    first.action = [](RegistryProbe* self)
    {
        self->registry->Remove(self->other);
        CHECK(!self->registry->Contains(self->other));
        if (!self->registry->Contains(self->other)) self->registry->Add(self->other, 5);
        if (!self->registry->Contains(self->other)) self->registry->Add(self->other, 5);
        CHECK(self->registry->Contains(self->other));
        self->action = nullptr;
    };
    registry.Add(&first, 10); registry.Add(&other, 5);
    registry.Process();
    CHECK(other.calls == 1);
    registry.Process();
    CHECK(other.calls == 2);
}
struct Case { const char* name; void (*run)(); };
const Case cases[] = {
    {"level-address-reused-ready", [] { MembershipRestoration(6); }},
    {"menu-cleanup-during-process", MenuCleanupDuringRegistryProcess},
    {"pause-during-factory", PauseDuringFactory},
    {"valid-activation-close", [] { ValidActivationClose(false); }},
    {"prepaused-valid-close", [] { ValidActivationClose(true); }},
    {"level-reregistered", [] { MembershipRestoration(0); }},
    {"level-unregistered", [] { MembershipRestoration(1); }},
    {"level-partial-membership", [] { MembershipRestoration(2); }},
    {"level-replaced", [] { MembershipRestoration(3); }},
    {"level-destroyed", [] { MembershipRestoration(4); }},
    {"level-address-reused-loading", [] { MembershipRestoration(5); }},
    {"game-type-was-single", [] { ChangedGameType(true); }},
    {"game-type-was-multi", [] { ChangedGameType(false); }},
    {"no-timer-acquired", NoTimerAcquired},
    {"initial-ui-restore", InitialFailureRestoresUI},
    {"initial-retries", InitialRetries},
    {"failure-bypasses-close-guards", FailureBypassesCloseGuards},
    {"actual-registry", ActualRegistryMembership},
    {"registry-reregister-processing", ActualRegistryReregisterInProcess},
    {"activation-null-initial", [] { ActivationFailure(false, false, false); }},
    {"activation-non-dialog-initial", [] { ActivationFailure(false, true, false); }},
    {"activation-null-reset", [] { ActivationFailure(true, false, false); }},
    {"activation-non-dialog-reset", [] { ActivationFailure(true, true, false); }},
    {"activation-null-frame", [] { ActivationFailure(true, false, true); }},
    {"activation-non-dialog-frame", [] { ActivationFailure(true, true, true); }},
    {"activation-prepaused-initial", [] { ActivationFailure(false, true, false, true); }},
    {"activation-prepaused-active", [] { ActivationFailure(true, true, false, true); }},
    {"repeated-activation", RepeatedActivation},
    {"null-initial", [] { Failure(false, false, false); }},
    {"null-active", [] { Failure(true, false, false); }},
    {"non-dialog-initial", [] { Failure(false, true, false); }},
    {"non-dialog-active", [] { Failure(true, true, false); }},
    {"frame-null", [] { Failure(true, false, true); }},
    {"frame-non-dialog", [] { Failure(true, true, true); }},
    {"covered-capture", [] { Failure(true, true, false, true); }},
    {"valid-initial", [] { Valid(false, false); }},
    {"valid-replace", [] { Valid(true, false); }},
    {"valid-hidden-replace", [] { Valid(true, false, false); }},
    {"frame-valid", [] { Valid(true, true); }},
    {"recovery", Recovery},
};
int main(int argc, char** argv)
{
    bool matched = false;
    for (const auto& test : cases)
    {
        if (argc > 1 && test.name != std::string(argv[1])) continue;
        matched = true; events.clear(); factory = {}; ResetEnvironment();
        test.run();
        std::printf("PASS %s\n", test.name);
    }
    CHECK(matched);
}
