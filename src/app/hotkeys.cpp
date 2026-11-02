// Hotkeys for the campaign clock (Stop / Pause / Start), set in Options.
// A low-level keyboard hook sees the keys while the game has focus; the keys still reach the game.
#include "app.h"

namespace {

struct HotKey {
    const wchar_t* iniKey;
    UINT vk;
    int mods;  // 1 = Ctrl, 2 = Shift, 4 = Alt
};

HotKey gKeys[HK_COUNT] = {
    {L"CampaignStop", VK_F9, 0},
    {L"CampaignPause", VK_F10, 0},
    {L"CampaignStart", VK_F11, 0},
};

HHOOK gHook = nullptr;
int gCapture = -1;  // index of the hotkey being set, -1 = none
bool gDown[256] = {};

bool isModifier(UINT vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
    }
    return false;
}

int currentMods() {
    return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 0) | ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 2 : 0) |
           ((GetAsyncKeyState(VK_MENU) & 0x8000) ? 4 : 0);
}

// Hotkeys act while the game or one of this app's windows is in front.
bool targetInFront() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (fg == findGame()) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

LRESULT CALLBACK keyProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        UINT vk = k->vkCode & 0xFF;
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        bool repeat = down && gDown[vk];
        gDown[vk] = down;
        if (gCapture >= 0 && !isModifier(vk)) {  // setting a hotkey: the key is taken, not passed on
            if (down && !repeat) PostMessageW(hMain, WM_KEY_CAPTURED, vk, currentMods());
            return 1;
        }
        if (down && !repeat && gCapture < 0 && gHotkeysOn) {
            int mods = currentMods();
            for (int i = 0; i < HK_COUNT; ++i)
                if (gKeys[i].vk && gKeys[i].vk == vk && gKeys[i].mods == mods && targetInFront()) {
                    PostMessageW(hMain, WM_HOTKEY_ACT, i, 0);
                    break;
                }
        }
    }
    return CallNextHookEx(gHook, code, wp, lp);
}

std::wstring keyText(UINT vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    switch (vk) {  // keys that share scan codes with the numpad need the extended bit
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
            sc |= 0x100;
    }
    wchar_t b[64] = L"";
    if (sc && GetKeyNameTextW((LONG)(sc << 16), b, 64) > 0) return b;
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    return L"Key " + std::to_wstring(vk);
}

void saveKey(int i) {
    std::wstring v = std::to_wstring(gKeys[i].vk) + L"," + std::to_wstring(gKeys[i].mods);
    WritePrivateProfileStringW(L"hotkeys", gKeys[i].iniKey, v.c_str(), iniPath().c_str());
}

void endCapture() {
    int i = gCapture;
    gCapture = -1;
    if (i >= 0) SetWindowTextW(hKeyBtn[i], hotkeyName(i).c_str());
}

}  // namespace

bool gHotkeysOn = true;

void loadHotkeys() {
    gHotkeysOn = GetPrivateProfileIntW(L"hotkeys", L"Enabled", 1, iniPath().c_str()) != 0;
    for (auto& k : gKeys) {
        wchar_t b[32];
        GetPrivateProfileStringW(L"hotkeys", k.iniKey, L"", b, 32, iniPath().c_str());
        unsigned vk = 0;
        int mods = 0;
        if (*b && swscanf(b, L"%u,%d", &vk, &mods) >= 1 && vk < 256) {
            k.vk = vk;
            k.mods = mods & 7;
        }
    }
}

void installKeyHook() {
    if (!gHook) gHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyProc, GetModuleHandleW(nullptr), 0);
}

void removeKeyHook() {
    if (gHook) UnhookWindowsHookEx(gHook);
    gHook = nullptr;
}

std::wstring hotkeyName(int i) {
    const HotKey& k = gKeys[i];
    if (!k.vk) return L"Not set";
    std::wstring s;
    if (k.mods & 1) s += L"Ctrl+";
    if (k.mods & 2) s += L"Shift+";
    if (k.mods & 4) s += L"Alt+";
    return s + keyText(k.vk);
}

void toggleHotkeys() {
    gHotkeysOn = !gHotkeysOn;
    WritePrivateProfileStringW(L"hotkeys", L"Enabled", gHotkeysOn ? L"1" : L"0", iniPath().c_str());
    InvalidateRect(hKeysChk, nullptr, FALSE);
    refreshCampaign(true);
}

void beginKeyCapture(int i) {
    endCapture();
    gCapture = i;
    SetWindowTextW(hKeyBtn[i], L"Press a key...");
}

// Esc cancels, Delete or Backspace removes the hotkey.
void keyCaptured(UINT vk, int mods) {
    int i = gCapture;
    if (i < 0) return;
    if (vk != VK_ESCAPE) {
        bool clear = (vk == VK_DELETE || vk == VK_BACK) && !mods;
        gKeys[i].vk = clear ? 0 : vk;
        gKeys[i].mods = clear ? 0 : mods;
        for (int j = 0; j < HK_COUNT; ++j)  // one action per key
            if (j != i && !clear && gKeys[j].vk == vk && gKeys[j].mods == mods) {
                gKeys[j].vk = 0;
                gKeys[j].mods = 0;
                saveKey(j);
                SetWindowTextW(hKeyBtn[j], hotkeyName(j).c_str());
            }
        saveKey(i);
    }
    endCapture();
    refreshCampaign(true);
}

void campaignAction(int i) {
    uint32_t now = nowTick();
    switch (i) {
        case HK_STOP: g.stopCampaign(now); break;
        case HK_PAUSE: g.toggleCampaignPause(now); break;
        case HK_START:
            if (g.cmpClock != Tracker::Clock::Stopped) return;  // Start only follows a Stop
            startCampaignRun();
            break;
        default: return;
    }
    saveData();
    refreshCampaign(true);
    ovSyncFooter();
    if (hOverlay) InvalidateRect(hOverlay, nullptr, FALSE);
}
