// =============================================================================
//  老陈截图 LaoChenShot  —  纯 Win32 + GDI+ 桌面截图工具
//  零第三方依赖：只用 Windows 自带的 user32 / gdi32 / gdiplus / comctl32 / shell32 等
//
//  功能：
//    · 全局热键（可在设置里自定义）：区域截图 / 全屏截图 / 贴图（剪贴板）
//    · 智能窗口与控件识别、放大镜 + 色值、8 向拖拽调整选区、方向键微调
//    · 标注：矩形、椭圆、箭头、画笔、荧光笔、马赛克、文字（支持中文输入法）、序号
//    · 撤销、复制到剪贴板（同时写入 PNG 格式）、另存为 PNG/JPG/BMP、自动保存
//    · 贴图（钉在桌面）：拖动、滚轮缩放、Ctrl+滚轮调透明度、右键菜单
//    · 托盘常驻、单实例、开机自启、Per-Monitor V2 高 DPI、多显示器
// =============================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <imm.h>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cwchar>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
using namespace Gdiplus;

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define IDI_APP 101

// ----------------------------------------------------------------------------
//  常量与全局
// ----------------------------------------------------------------------------
static const wchar_t* APP_NAME   = L"老陈截图";
static const wchar_t* APP_ID     = L"LaoChenShot";
static const wchar_t* APP_VER    = L"1.2.0";
static const wchar_t* CLS_MAIN   = L"LaoChenShotMain";
static const wchar_t* CLS_OVER   = L"LaoChenShotOverlay";
static const wchar_t* CLS_PIN    = L"LaoChenShotPin";
static const wchar_t* CLS_SET    = L"LaoChenShotSettings";

#define WM_TRAY (WM_APP + 1)
enum { IDM_REGION = 1001, IDM_FULL, IDM_PINCLIP, IDM_SETTINGS, IDM_OPENDIR, IDM_ABOUT, IDM_EXIT };
enum { IDT_REGION = 11, IDT_FULL = 12 };

struct Hotkey { UINT mod; UINT vk; };
static const Hotkey DEFAULT_HK[3] = {
    { MOD_CONTROL | MOD_ALT, 'A' },   // 区域截图
    { MOD_CONTROL | MOD_ALT, 'F' },   // 全屏截图
    { MOD_CONTROL | MOD_ALT, 'P' },   // 贴图（剪贴板）
};
static const wchar_t* HK_NAMES[3] = { L"区域截图", L"全屏截图", L"贴图（剪贴板）" };

struct Settings {
    Hotkey hk[3] = { DEFAULT_HK[0], DEFAULT_HK[1], DEFAULT_HK[2] };
    std::wstring saveDir;
    bool autoSave  = false;
    bool magnifier = true;
    bool autoStart = false;
    int  upscale   = 1;      // 0 原始像素  1 自动（小图 2 倍）  2 始终 2 倍  3 始终 3 倍
    bool sharpen   = true;   // 放大后锐化文字边缘
};

static Settings      g_cfg;
static HINSTANCE     g_hInst;
static HWND          g_hMain, g_hOverlay, g_hSettings;
static HICON         g_hIconSm, g_hIconLg, g_hIconBig;
static UINT          g_msgTaskbarCreated;
static float         g_scale = 1.0f;
static HFONT         g_uiFont, g_uiFontBold;
static ULONG_PTR     g_gdipToken;
static NOTIFYICONDATAW g_nid;
static std::vector<HWND> g_pins;

// ---- 配色主题：紫罗兰 → 青 ----
static const Color ACCENT(255, 108, 92, 231);      // #6C5CE7 紫罗兰（主色）
static const Color ACCENT2(255, 0, 184, 212);      // #00B8D4 青（渐变辅色）
static const Color OK_GREEN(255, 34, 197, 94);     // #22C55E 完成
static const Color DANGER(255, 255, 107, 107);     // #FF6B6B 退出
static const Color PANEL(244, 27, 29, 40);         // 深色玻璃面板
static const Color PANEL_HOVER(255, 52, 56, 76);
static const Color ICON_FG(255, 228, 231, 242);
static const Color ICON_DIM(255, 105, 110, 130);
static const Color SEP_C(255, 62, 66, 86);
static const COLORREF BG_RGB = RGB(246, 247, 251);
static HBRUSH g_bgBrush;
static Color PALETTE[8] = {
    Color(255, 235, 59, 59),  Color(255, 255, 149, 0), Color(255, 255, 214, 0),  Color(255, 52, 199, 89),
    Color(255, 0, 122, 255),  Color(255, 175, 82, 222), Color(255, 30, 30, 30),  Color(255, 255, 255, 255),
};

static inline int   S(float v)  { return (int)lroundf(v * g_scale); }
static inline float SF(float v) { return v * g_scale; }

// ----------------------------------------------------------------------------
//  小工具
// ----------------------------------------------------------------------------
static const wchar_t* UiFontName() {
    static std::wstring name;
    if (name.empty()) {
        FontFamily ff(L"Microsoft YaHei UI");
        name = ff.IsAvailable() ? L"Microsoft YaHei UI" : L"Segoe UI";
    }
    return name.c_str();
}

static void RoundRectPath(GraphicsPath& p, const RectF& r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d < 1) { p.AddRectangle(r); return; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void FillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p; RoundRectPath(p, r, rad);
    SolidBrush b(c); g.FillPath(&b, &p);
}

static void SoftShadow(Graphics& g, const RectF& r, float rad) {
    for (int i = 1; i <= 6; ++i) {
        float k = SF((float)i);
        RectF s(r.X - k, r.Y - k + SF(2), r.Width + 2 * k, r.Height + 2 * k);
        FillRound(g, s, rad + k, Color(9, 0, 0, 0));
    }
}

static std::wstring TimeStampName(const wchar_t* ext) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[96];
    swprintf(buf, 96, L"老陈截图_%04d%02d%02d_%02d%02d%02d_%03d.%s",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ext);
    return buf;
}

static std::wstring DefaultSaveDir() {
    wchar_t p[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, p);
    return std::wstring(p) + L"\\老陈截图";
}

static std::wstring ConfigPath() {
    wchar_t p[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, p);
    std::wstring dir = std::wstring(p) + L"\\LaoChenShot";
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring ini = dir + L"\\config.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 写入 UTF-16 BOM，让 WritePrivateProfileStringW 以 Unicode 保存（支持中文路径）
        HANDLE f = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD n; const WORD bom = 0xFEFF; WriteFile(f, &bom, 2, &n, NULL); CloseHandle(f); }
    }
    return ini;
}

static bool IsExtendedVk(UINT vk) {
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_NUMLOCK: case VK_DIVIDE: case VK_SNAPSHOT:
        return true;
    }
    return false;
}

static std::wstring KeyName(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk == VK_SNAPSHOT) return L"PrintScreen";
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    if (IsExtendedVk(vk)) lp |= (1 << 24);
    wchar_t b[64] = {};
    if (GetKeyNameTextW(lp, b, 64) > 0) return b;
    return L"?";
}

static std::wstring HotkeyStr(const Hotkey& h) {
    if (!h.vk) return L"未设置";
    std::wstring s;
    if (h.mod & MOD_CONTROL) s += L"Ctrl+";
    if (h.mod & MOD_SHIFT)   s += L"Shift+";
    if (h.mod & MOD_ALT)     s += L"Alt+";
    if (h.mod & MOD_WIN)     s += L"Win+";
    return s + KeyName(h.vk);
}

static bool GetEncoderClsid(const wchar_t* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* info = (ImageCodecInfo*)buf.data();
    GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; return true; }
    return false;
}

static bool SaveBitmapFile(Bitmap* bmp, const std::wstring& path) {
    std::wstring ext;
    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) { ext = path.substr(dot + 1); for (auto& c : ext) c = (wchar_t)towlower(c); }
    CLSID clsid;
    if (ext == L"jpg" || ext == L"jpeg") {
        if (!GetEncoderClsid(L"image/jpeg", &clsid)) return false;
        EncoderParameters ep; ULONG q = 100;
        ep.Count = 1; ep.Parameter[0].Guid = EncoderQuality; ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1; ep.Parameter[0].Value = &q;
        return bmp->Save(path.c_str(), &clsid, &ep) == Ok;
    }
    if (ext == L"bmp") { if (!GetEncoderClsid(L"image/bmp", &clsid)) return false; return bmp->Save(path.c_str(), &clsid, NULL) == Ok; }
    if (!GetEncoderClsid(L"image/png", &clsid)) return false;
    return bmp->Save(path.c_str(), &clsid, NULL) == Ok;
}

static HGLOBAL EncodePngToHGlobal(Bitmap* bmp) {
    IStream* st = nullptr;
    if (FAILED(CreateStreamOnHGlobal(NULL, FALSE, &st))) return NULL;
    CLSID clsid; HGLOBAL hg = NULL;
    if (GetEncoderClsid(L"image/png", &clsid) && bmp->Save(st, &clsid, NULL) == Ok) GetHGlobalFromStream(st, &hg);
    st->Release();
    return hg;
}

static bool OpenClipboardRetry(HWND owner) {
    for (int i = 0; i < 10; ++i) { if (OpenClipboard(owner)) return true; Sleep(30); }
    return false;
}

static bool CopyBitmapToClipboard(Bitmap* bmp) {
    UINT w = bmp->GetWidth(), h = bmp->GetHeight();
    BitmapData bd; Rect r(0, 0, (INT)w, (INT)h);
    if (bmp->LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return false;
    size_t row = (size_t)w * 4, sz = sizeof(BITMAPINFOHEADER) + row * h;
    HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hDib) { bmp->UnlockBits(&bd); return false; }
    BYTE* p = (BYTE*)GlobalLock(hDib);
    auto* bih = (BITMAPINFOHEADER*)p; ZeroMemory(bih, sizeof(*bih));
    bih->biSize = sizeof(*bih); bih->biWidth = (LONG)w; bih->biHeight = (LONG)h; bih->biPlanes = 1;
    bih->biBitCount = 32; bih->biCompression = BI_RGB; bih->biSizeImage = (DWORD)(row * h);
    for (UINT y = 0; y < h; ++y)
        memcpy(p + sizeof(*bih) + (size_t)(h - 1 - y) * row, (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, row);
    GlobalUnlock(hDib);
    bmp->UnlockBits(&bd);
    HGLOBAL hPng = EncodePngToHGlobal(bmp);
    if (!OpenClipboardRetry(g_hMain)) { GlobalFree(hDib); if (hPng) GlobalFree(hPng); return false; }
    EmptyClipboard();
    SetClipboardData(CF_DIB, hDib);
    if (hPng) SetClipboardData(RegisterClipboardFormatW(L"PNG"), hPng);
    CloseClipboard();
    return true;
}

static void CopyTextToClipboard(const std::wstring& s) {
    if (!OpenClipboardRetry(g_hMain)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard();
}

static bool EnsureSaveDir() {
    if (g_cfg.saveDir.empty()) g_cfg.saveDir = DefaultSaveDir();
    int r = SHCreateDirectoryExW(NULL, g_cfg.saveDir.c_str(), NULL);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

static std::wstring AutoSaveBitmap(Bitmap* bmp) {
    if (!EnsureSaveDir()) return L"";
    std::wstring path = g_cfg.saveDir + L"\\" + TimeStampName(L"png");
    return SaveBitmapFile(bmp, path) ? path : L"";
}

static bool SaveAsDialog(HWND owner, Bitmap* bmp, std::wstring* outPath = nullptr) {
    EnsureSaveDir();
    wchar_t file[MAX_PATH * 2] = {};
    lstrcpynW(file, TimeStampName(L"png").c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG 图片 (*.png)\0*.png\0JPEG 图片 (*.jpg)\0*.jpg\0BMP 图片 (*.bmp)\0*.bmp\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrInitialDir = g_cfg.saveDir.c_str();
    ofn.lpstrTitle = L"保存截图";
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    std::wstring path = file;
    static const wchar_t* exts[3] = { L"png", L"jpg", L"bmp" };
    std::wstring want = exts[std::min<DWORD>(std::max<DWORD>(ofn.nFilterIndex, 1), 3) - 1];
    size_t dot = path.find_last_of(L'.'); size_t slash = path.find_last_of(L"\\/");
    std::wstring cur = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? path.substr(dot + 1) : L"";
    for (auto& c : cur) c = (wchar_t)towlower(c);
    bool known = (cur == L"png" || cur == L"jpg" || cur == L"jpeg" || cur == L"bmp");
    if (!known) path += L"." + want;
    else if (cur != want && !(want == L"jpg" && cur == L"jpeg")) path = path.substr(0, dot + 1) + want;
    if (!SaveBitmapFile(bmp, path)) { MessageBoxW(owner, L"保存失败，请检查路径或磁盘权限。", APP_NAME, MB_ICONERROR); return false; }
    if (outPath) *outPath = path;
    return true;
}

// ----------------------------------------------------------------------------
//  设置读写 / 开机自启 / 热键
// ----------------------------------------------------------------------------
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static bool IsAutoStart() {
    HKEY k; bool r = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        r = RegQueryValueExW(k, APP_ID, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return r;
}

static void SetAutoStart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, APP_ID, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, APP_ID);
    }
    RegCloseKey(k);
}

static const wchar_t* HK_KEYS[3] = { L"Region", L"Full", L"PinClip" };

static void LoadSettings() {
    std::wstring ini = ConfigPath();
    for (int i = 0; i < 3; ++i) {
        std::wstring k = HK_KEYS[i];
        g_cfg.hk[i].mod = GetPrivateProfileIntW(L"Hotkey", (k + L"Mod").c_str(), DEFAULT_HK[i].mod, ini.c_str());
        g_cfg.hk[i].vk  = GetPrivateProfileIntW(L"Hotkey", (k + L"Key").c_str(), DEFAULT_HK[i].vk, ini.c_str());
    }
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"General", L"SaveDir", DefaultSaveDir().c_str(), buf, MAX_PATH * 2, ini.c_str());
    g_cfg.saveDir   = buf[0] ? buf : DefaultSaveDir();
    g_cfg.autoSave  = GetPrivateProfileIntW(L"General", L"AutoSave", 0, ini.c_str()) != 0;
    g_cfg.magnifier = GetPrivateProfileIntW(L"General", L"Magnifier", 1, ini.c_str()) != 0;
    g_cfg.upscale   = std::max(0, std::min(3, (int)GetPrivateProfileIntW(L"Quality", L"Upscale", 1, ini.c_str())));
    g_cfg.sharpen   = GetPrivateProfileIntW(L"Quality", L"Sharpen", 1, ini.c_str()) != 0;
    g_cfg.autoStart = IsAutoStart();
}

static void WriteIni(const wchar_t* sec, const wchar_t* key, const std::wstring& v, const std::wstring& ini) {
    WritePrivateProfileStringW(sec, key, v.c_str(), ini.c_str());
}

static void SaveSettings() {
    std::wstring ini = ConfigPath();
    for (int i = 0; i < 3; ++i) {
        std::wstring k = HK_KEYS[i];
        WriteIni(L"Hotkey", (k + L"Mod").c_str(), std::to_wstring(g_cfg.hk[i].mod), ini);
        WriteIni(L"Hotkey", (k + L"Key").c_str(), std::to_wstring(g_cfg.hk[i].vk), ini);
    }
    WriteIni(L"General", L"SaveDir", g_cfg.saveDir, ini);
    WriteIni(L"General", L"AutoSave", g_cfg.autoSave ? L"1" : L"0", ini);
    WriteIni(L"General", L"Magnifier", g_cfg.magnifier ? L"1" : L"0", ini);
    WriteIni(L"Quality", L"Upscale", std::to_wstring(g_cfg.upscale), ini);
    WriteIni(L"Quality", L"Sharpen", g_cfg.sharpen ? L"1" : L"0", ini);
}

static std::wstring RegisterAllHotkeys() {
    std::wstring failed;
    for (int i = 0; i < 3; ++i) UnregisterHotKey(g_hMain, i + 1);
    for (int i = 0; i < 3; ++i) {
        if (!g_cfg.hk[i].vk) continue;
        if (!RegisterHotKey(g_hMain, i + 1, g_cfg.hk[i].mod | MOD_NOREPEAT, g_cfg.hk[i].vk))
            failed += L"  · " + std::wstring(HK_NAMES[i]) + L"：" + HotkeyStr(g_cfg.hk[i]) + L"\n";
    }
    return failed;
}

// ----------------------------------------------------------------------------
//  托盘
// ----------------------------------------------------------------------------
static void TrayUpdateTip() {
    std::wstring tip = std::wstring(APP_NAME) + L"\n区域截图：" + HotkeyStr(g_cfg.hk[0]);
    g_nid.uFlags = NIF_TIP;
    lstrcpynW(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayAdd() {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_hIconSm;
    lstrcpynW(g_nid.szTip, APP_NAME, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    TrayUpdateTip();
}

static void TrayBalloon(const std::wstring& title, const std::wstring& text) {
    g_nid.uFlags = NIF_INFO;
    lstrcpynW(g_nid.szInfoTitle, title.c_str(), ARRAYSIZE(g_nid.szInfoTitle));
    lstrcpynW(g_nid.szInfo, text.c_str(), ARRAYSIZE(g_nid.szInfo));
    g_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ----------------------------------------------------------------------------
//  贴图窗口（钉在桌面）
// ----------------------------------------------------------------------------
struct Pin {
    Bitmap* bmp = nullptr;
    float zoom = 1.0f;
    int alpha = 255;
    bool active = false;
    DWORD badgeTick = 0;
    std::wstring badge;
};

static void CreatePin(Bitmap* bmp, POINT pos) {
    Pin* p = new Pin; p->bmp = bmp;
    HWND hw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, CLS_PIN, L"贴图", WS_POPUP,
                              pos.x, pos.y, (int)bmp->GetWidth(), (int)bmp->GetHeight(), NULL, NULL, g_hInst, p);
    if (!hw) { delete bmp; delete p; return; }
    SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA);
    ShowWindow(hw, SW_SHOW);
    SetForegroundWindow(hw);
    g_pins.push_back(hw);
}

static void PinResize(HWND hw, Pin* p, float nz, POINT anchor) {
    nz = std::max(0.1f, std::min(10.0f, nz));
    RECT wr; GetWindowRect(hw, &wr);
    float rx = (anchor.x - wr.left) / (float)std::max(1L, wr.right - wr.left);
    float ry = (anchor.y - wr.top) / (float)std::max(1L, wr.bottom - wr.top);
    int nw = std::max(8, (int)lroundf(p->bmp->GetWidth() * nz));
    int nh = std::max(8, (int)lroundf(p->bmp->GetHeight() * nz));
    int nx = anchor.x - (int)lroundf(rx * nw), ny = anchor.y - (int)lroundf(ry * nh);
    p->zoom = nz;
    SetWindowPos(hw, NULL, nx, ny, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
    p->badge = std::to_wstring((int)lroundf(nz * 100)) + L"%";
    p->badgeTick = GetTickCount();
    SetTimer(hw, 1, 1200, NULL);
    InvalidateRect(hw, NULL, FALSE);
}

static LRESULT CALLBACK PinProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    Pin* p = (Pin*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return DefWindowProcW(hw, msg, wp, lp);
    case WM_NCHITTEST:
        return HTCAPTION;                       // 整个窗口都可拖动
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_SIZEALL)); return TRUE;
    case WM_ACTIVATE:
        if (p) { p->active = LOWORD(wp) != WA_INACTIVE; InvalidateRect(hw, NULL, FALSE); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        int cw = std::max(1L, rc.right), ch = std::max(1L, rc.bottom);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP mb = CreateCompatibleBitmap(hdc, cw, ch);
        HGDIOBJ old = SelectObject(mdc, mb);
        {
            Graphics g(mdc);
            bool one = fabsf(p->zoom - 1.0f) < 0.001f;
            g.SetInterpolationMode(one ? InterpolationModeNearestNeighbor : InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.DrawImage(p->bmp, RectF(0, 0, (REAL)cw, (REAL)ch), 0, 0,
                        (REAL)p->bmp->GetWidth(), (REAL)p->bmp->GetHeight(), UnitPixel);
            g.SetPixelOffsetMode(PixelOffsetModeDefault);
            Pen border(p->active ? ACCENT : Color(255, 170, 170, 185), p->active ? 2.0f : 1.0f);
            g.DrawRectangle(&border, 0, 0, cw - 1, ch - 1);
            if (GetTickCount() - p->badgeTick < 1100 && !p->badge.empty()) {
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                Font f(UiFontName(), SF(13), FontStyleBold, UnitPixel);
                RectF m; g.MeasureString(p->badge.c_str(), -1, &f, PointF(0, 0), &m);
                RectF box((cw - m.Width) / 2 - SF(10), (ch - m.Height) / 2 - SF(5), m.Width + SF(20), m.Height + SF(10));
                FillRound(g, box, SF(6), Color(225, 108, 92, 231));
                SolidBrush wb(Color(255, 255, 255, 255));
                g.DrawString(p->badge.c_str(), -1, &f, PointF(box.X + SF(10), box.Y + SF(5)), &wb);
            }
        }
        BitBlt(hdc, 0, 0, cw, ch, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old); DeleteObject(mb); DeleteDC(mdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_TIMER:
        KillTimer(hw, 1); InvalidateRect(hw, NULL, FALSE); return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            p->alpha = std::max(40, std::min(255, p->alpha + (delta > 0 ? 20 : -20)));
            SetLayeredWindowAttributes(hw, 0, (BYTE)p->alpha, LWA_ALPHA);
            p->badge = L"不透明度 " + std::to_wstring(p->alpha * 100 / 255) + L"%";
            p->badgeTick = GetTickCount(); SetTimer(hw, 1, 1200, NULL);
            InvalidateRect(hw, NULL, FALSE);
        } else {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            PinResize(hw, p, p->zoom * (delta > 0 ? 1.1f : 1.0f / 1.1f), pt);
        }
        return 0;
    }
    case WM_NCLBUTTONDBLCLK:
        DestroyWindow(hw); return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
        if (ctrl && wp == 'C') { CopyBitmapToClipboard(p->bmp); return 0; }
        if (ctrl && wp == 'S') { SaveAsDialog(hw, p->bmp); return 0; }
        if (wp == '1') { RECT wr; GetWindowRect(hw, &wr); PinResize(hw, p, 1.0f, POINT{ wr.left, wr.top }); return 0; }
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 1, L"复制\tCtrl+C");
        AppendMenuW(m, MF_STRING, 2, L"另存为…\tCtrl+S");
        AppendMenuW(m, MF_STRING, 3, L"原始大小\t1");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, 4, L"关闭\tEsc / 双击");
        AppendMenuW(m, MF_STRING, 5, L"关闭全部贴图");
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, NULL);
        DestroyMenu(m);
        switch (cmd) {
        case 1: CopyBitmapToClipboard(p->bmp); break;
        case 2: SaveAsDialog(hw, p->bmp); break;
        case 3: { RECT wr; GetWindowRect(hw, &wr); PinResize(hw, p, 1.0f, POINT{ wr.left, wr.top }); break; }
        case 4: DestroyWindow(hw); break;
        case 5: { auto all = g_pins; for (HWND h : all) DestroyWindow(h); break; }
        }
        return 0;
    }
    case WM_NCDESTROY:
        g_pins.erase(std::remove(g_pins.begin(), g_pins.end(), hw), g_pins.end());
        if (p) { delete p->bmp; delete p; }
        SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
        return DefWindowProcW(hw, msg, wp, lp);
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void PinClipboard() {
    if (!IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB)) {
        TrayBalloon(APP_NAME, L"剪贴板里没有图片，先复制一张图片再贴图。");
        return;
    }
    if (!OpenClipboardRetry(g_hMain)) return;
    Bitmap* copy = nullptr;
    HBITMAP hb = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (hb) {
        Bitmap src(hb, NULL);
        UINT w = src.GetWidth(), h = src.GetHeight();
        if (w && h) {
            copy = new Bitmap((INT)w, (INT)h, PixelFormat32bppARGB);
            Graphics g(copy);
            g.DrawImage(&src, Rect(0, 0, (INT)w, (INT)h), 0, 0, (INT)w, (INT)h, UnitPixel);
        }
    }
    CloseClipboard();
    if (!copy) return;
    POINT pt; GetCursorPos(&pt);
    pt.x -= (LONG)copy->GetWidth() / 2; pt.y -= (LONG)copy->GetHeight() / 2;
    CreatePin(copy, pt);
}

// ----------------------------------------------------------------------------
//  截图编辑层（全屏遮罩）
// ----------------------------------------------------------------------------
enum Tool { T_NONE = -1, T_RECT = 0, T_ELLIPSE, T_ARROW, T_PEN, T_MARKER, T_MOSAIC, T_TEXT, T_NUMBER };
enum Btn  { B_RECT = 0, B_ELLIPSE, B_ARROW, B_PEN, B_MARKER, B_MOSAIC, B_TEXT, B_NUMBER,
            B_UNDO, B_TRIM, B_PIN, B_SAVE, B_CANCEL, B_DONE, B_COUNT };
static const wchar_t* BTN_TIPS[B_COUNT] = {
    L"矩形（按住 Shift 画正方形）", L"椭圆（按住 Shift 画正圆）", L"箭头（按住 Shift 锁定 45°）", L"画笔",
    L"荧光笔", L"马赛克", L"文字（支持中文输入）", L"序号标记",
    L"撤销  Ctrl+Z", L"一键裁边：去掉四周空白和细边框  T", L"钉在桌面", L"保存  Ctrl+S", L"退出  Esc", L"完成  Enter / 双击",
};
enum Drag { DR_NONE = 0, DR_PENDING, DR_CREATE, DR_MOVE, DR_RESIZE, DR_SHAPE };
enum Finish { FIN_COPY, FIN_SAVE, FIN_PIN };

struct Shape {
    int type = T_RECT;
    Color color;
    float width = 2;
    std::vector<PointF> pts;
    std::wstring text;
    int number = 0;
};

struct WinInfo { RECT r; std::vector<RECT> kids; };

struct Overlay {
    int vx = 0, vy = 0, w = 0, h = 0;
    HDC baseDC = 0, dimDC = 0, backDC = 0;
    HBITMAP baseBmp = 0, dimBmp = 0, backBmp = 0;
    HGDIOBJ oldBase = 0, oldDim = 0, oldBack = 0;
    BYTE* baseBits = nullptr; BYTE* dimBits = nullptr;
    std::unique_ptr<Bitmap> gdipBase, mosaicBmp;
    std::vector<WinInfo> wins;

    bool hasSel = false; RECT sel = {};
    std::vector<RECT> trimUndo;      // 裁边前的选区，可撤销
    bool hasHover = false; RECT hover = {};
    int drag = DR_NONE; POINT dragStart = {}; RECT selAtDrag = {}; int handle = -1;

    int tool = T_NONE, colorIdx = 0, sizeIdx = 1;
    std::vector<Shape> shapes;
    bool editingText = false;

    POINT mouse = {};
    int hoverBtn = -1, hoverProp = -1;
    RECT tbRect = {}, btn[B_COUNT] = {};
    bool propVisible = false; RECT propRect = {}; RECT propItems[11] = {}; int propCount = 0;
    bool tbBelow = true;

    ~Overlay() {
        gdipBase.reset(); mosaicBmp.reset();
        if (baseDC) { SelectObject(baseDC, oldBase); DeleteDC(baseDC); }
        if (dimDC)  { SelectObject(dimDC, oldDim);   DeleteDC(dimDC); }
        if (backDC) { SelectObject(backDC, oldBack); DeleteDC(backDC); }
        if (baseBmp) DeleteObject(baseBmp);
        if (dimBmp)  DeleteObject(dimBmp);
        if (backBmp) DeleteObject(backBmp);
    }
};
static Overlay* g_ov = nullptr;

static HBITMAP MakeDIB(int w, int h, BYTE** bits) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)bits, NULL, 0);
}

static inline LONG RW(const RECT& r) { return r.right - r.left; }
static inline LONG RH(const RECT& r) { return r.bottom - r.top; }
static inline long long Area(const RECT& r) { return (long long)RW(r) * RH(r); }

static RECT MonRectAt(Overlay* o, POINT cp) {
    POINT sp = { cp.x + o->vx, cp.y + o->vy };
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(sp, MONITOR_DEFAULTTONEAREST), &mi);
    RECT r = mi.rcMonitor; OffsetRect(&r, -o->vx, -o->vy);
    return r;
}

static BOOL CALLBACK EnumKidProc(HWND hw, LPARAM lp) {
    auto* pr = (std::pair<Overlay*, WinInfo*>*)lp;
    if (!IsWindowVisible(hw)) return TRUE;
    RECT r; GetWindowRect(hw, &r);
    if (RW(r) < 6 || RH(r) < 6) return TRUE;
    OffsetRect(&r, -pr->first->vx, -pr->first->vy);
    RECT c; if (IntersectRect(&c, &r, &pr->second->r)) pr->second->kids.push_back(c);
    return TRUE;
}

static BOOL CALLBACK EnumWinProc(HWND hw, LPARAM lp) {
    auto* o = (Overlay*)lp;
    if (!IsWindowVisible(hw) || IsIconic(hw)) return TRUE;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hw, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    RECT r;
    if (FAILED(DwmGetWindowAttribute(hw, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hw, &r);
    if (RW(r) < 8 || RH(r) < 8) return TRUE;
    OffsetRect(&r, -o->vx, -o->vy);
    RECT clip = { 0, 0, o->w, o->h }, c;
    if (!IntersectRect(&c, &r, &clip)) return TRUE;
    WinInfo wi; wi.r = c;
    o->wins.push_back(wi);
    std::pair<Overlay*, WinInfo*> pr(o, &o->wins.back());
    EnumChildWindows(hw, EnumKidProc, (LPARAM)&pr);
    return TRUE;
}

static void UpdateHover(Overlay* o) {
    o->hasHover = false;
    POINT p = o->mouse;
    for (auto& wi : o->wins) {
        if (!PtInRect(&wi.r, p)) continue;
        RECT best = wi.r;
        for (auto& k : wi.kids) if (PtInRect(&k, p) && Area(k) < Area(best)) best = k;
        o->hover = best; o->hasHover = true;
        return;
    }
    o->hover = MonRectAt(o, p); o->hasHover = true;
}

static void EnsureMosaic(Overlay* o) {
    if (o->mosaicBmp) return;
    int bs = std::max(6, S(10));
    o->mosaicBmp.reset(new Bitmap(o->w, o->h, PixelFormat32bppRGB));
    BitmapData bd; Rect r(0, 0, o->w, o->h);
    if (o->mosaicBmp->LockBits(&r, ImageLockModeWrite, PixelFormat32bppRGB, &bd) != Ok) return;
    const DWORD* src = (const DWORD*)o->baseBits;
    for (int by = 0; by < o->h; by += bs) {
        int ey = std::min(o->h, by + bs);
        for (int bx = 0; bx < o->w; bx += bs) {
            int ex = std::min(o->w, bx + bs);
            unsigned long long sr = 0, sg = 0, sb = 0, n = 0;
            for (int y = by; y < ey; y += 2)
                for (int x = bx; x < ex; x += 2) {
                    DWORD px = src[(size_t)y * o->w + x];
                    sr += (px >> 16) & 255; sg += (px >> 8) & 255; sb += px & 255; ++n;
                }
            DWORD avg = 0xFF000000 | (DWORD)((sr / n) << 16) | (DWORD)((sg / n) << 8) | (DWORD)(sb / n);
            for (int y = by; y < ey; ++y) {
                DWORD* row = (DWORD*)((BYTE*)bd.Scan0 + (size_t)y * bd.Stride);
                for (int x = bx; x < ex; ++x) row[x] = avg;
            }
        }
    }
    o->mosaicBmp->UnlockBits(&bd);
}

static float ToolWidth(int tool, int si) {
    static const float pen[3] = { 2, 4, 7 }, marker[3] = { 12, 18, 26 }, mosaic[3] = { 14, 24, 38 },
                       text[3] = { 16, 22, 32 }, num[3] = { 10, 13, 17 };
    switch (tool) {
    case T_MARKER: return SF(marker[si]);
    case T_MOSAIC: return SF(mosaic[si]);
    case T_TEXT:   return SF(text[si]);
    case T_NUMBER: return SF(num[si]);
    default:       return SF(pen[si]);
    }
}

static void SplitLines(const std::wstring& s, std::vector<std::wstring>& out) {
    out.clear(); size_t st = 0;
    for (;;) { size_t p = s.find(L'\n', st); if (p == std::wstring::npos) { out.push_back(s.substr(st)); break; } out.push_back(s.substr(st, p - st)); st = p + 1; }
}

struct TextMetrics { float lineH, maxW, lastW; int lines; };
static TextMetrics MeasureText(Graphics& g, const Shape& s, Font& f) {
    std::vector<std::wstring> ls; SplitLines(s.text, ls);
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    TextMetrics m; m.lineH = f.GetHeight(&g); m.maxW = 0; m.lastW = 0; m.lines = (int)ls.size();
    for (size_t i = 0; i < ls.size(); ++i) {
        float wv = 0;
        if (!ls[i].empty()) { RectF b; g.MeasureString(ls[i].c_str(), -1, &f, PointF(0, 0), &sf, &b); wv = b.Width; }
        m.maxW = std::max(m.maxW, wv);
        if (i + 1 == ls.size()) m.lastW = wv;
    }
    return m;
}

static void DrawArrowShape(Graphics& g, PointF a, PointF b, float w, const Color& c) {
    float dx = b.X - a.X, dy = b.Y - a.Y, L = sqrtf(dx * dx + dy * dy);
    if (L < 1) return;
    float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
    float head = std::min(L * 0.7f, w * 4.0f + SF(10));
    float hw = head * 0.48f, neckW = std::max(w * 0.55f, SF(1.2f)), tailW = std::max(w * 0.12f, 0.6f);
    PointF wing(b.X - ux * head * 1.05f, b.Y - uy * head * 1.05f);
    PointF neck(b.X - ux * head * 0.8f, b.Y - uy * head * 0.8f);
    PointF poly[7] = {
        PointF(a.X + nx * tailW, a.Y + ny * tailW),
        PointF(neck.X + nx * neckW, neck.Y + ny * neckW),
        PointF(wing.X + nx * hw, wing.Y + ny * hw),
        b,
        PointF(wing.X - nx * hw, wing.Y - ny * hw),
        PointF(neck.X - nx * neckW, neck.Y - ny * neckW),
        PointF(a.X - nx * tailW, a.Y - ny * tailW),
    };
    SolidBrush br(c);
    g.FillPolygon(&br, poly, 7);
}

static void DrawShape(Graphics& g, Overlay* o, const Shape& s, bool editing) {
    if (s.pts.empty()) return;
    switch (s.type) {
    case T_RECT: case T_ELLIPSE: {
        if (s.pts.size() < 2) return;
        PointF a = s.pts[0], b = s.pts[1];
        RectF r(std::min(a.X, b.X), std::min(a.Y, b.Y), fabsf(b.X - a.X), fabsf(b.Y - a.Y));
        Pen p(s.color, s.width); p.SetLineJoin(LineJoinRound);
        if (s.type == T_RECT) g.DrawRectangle(&p, r); else g.DrawEllipse(&p, r);
        break;
    }
    case T_ARROW:
        if (s.pts.size() >= 2) DrawArrowShape(g, s.pts[0], s.pts[1], s.width, s.color);
        break;
    case T_PEN: case T_MARKER: case T_MOSAIC: {
        Color c = s.color;
        if (s.type == T_MARKER) c = Color(110, s.color.GetR(), s.color.GetG(), s.color.GetB());
        std::unique_ptr<Brush> br;
        if (s.type == T_MOSAIC) { EnsureMosaic(o); br.reset(new TextureBrush(o->mosaicBmp.get(), WrapModeTile)); }
        else br.reset(new SolidBrush(c));
        if (s.pts.size() == 1) {
            float r = s.width / 2;
            g.FillEllipse(br.get(), s.pts[0].X - r, s.pts[0].Y - r, r * 2, r * 2);
        } else {
            Pen p(br.get(), s.width);
            p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
            g.DrawLines(&p, s.pts.data(), (INT)s.pts.size());
        }
        break;
    }
    case T_TEXT: {
        Font f(UiFontName(), s.width, FontStyleRegular, UnitPixel);
        TextMetrics m = MeasureText(g, s, f);
        std::vector<std::wstring> ls; SplitLines(s.text, ls);
        SolidBrush br(s.color);
        const StringFormat* tf = StringFormat::GenericTypographic();
        for (size_t i = 0; i < ls.size(); ++i)
            if (!ls[i].empty()) g.DrawString(ls[i].c_str(), -1, &f, PointF(s.pts[0].X, s.pts[0].Y + m.lineH * i), tf, &br);
        if (editing) {
            Pen dash(Color(220, 255, 255, 255), 1.0f); dash.SetDashStyle(DashStyleDash);
            Pen dash2(Color(220, 0, 0, 0), 1.0f); dash2.SetDashStyle(DashStyleDash); dash2.SetDashOffset(2);
            RectF box(s.pts[0].X - SF(4), s.pts[0].Y - SF(3), std::max(m.maxW, SF(12)) + SF(10), m.lineH * m.lines + SF(6));
            g.DrawRectangle(&dash, box); g.DrawRectangle(&dash2, box);
            Pen caret(s.color, std::max(1.0f, SF(1.5f)));
            float cx = s.pts[0].X + m.lastW + SF(1), cy = s.pts[0].Y + m.lineH * (m.lines - 1);
            g.DrawLine(&caret, cx, cy + SF(2), cx, cy + m.lineH - SF(2));
        }
        break;
    }
    case T_NUMBER: {
        float r = s.width; PointF c = s.pts[0];
        SolidBrush fill(s.color);
        g.FillEllipse(&fill, c.X - r, c.Y - r, r * 2, r * 2);
        bool light = (s.color.GetR() * 299 + s.color.GetG() * 587 + s.color.GetB() * 114) / 1000 > 180;
        Pen ring(light ? Color(255, 60, 60, 60) : Color(255, 255, 255, 255), std::max(1.0f, r * 0.13f));
        g.DrawEllipse(&ring, c.X - r, c.Y - r, r * 2, r * 2);
        Font f(UiFontName(), r * (s.number >= 10 ? 0.95f : 1.15f), FontStyleBold, UnitPixel);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush tb(light ? Color(255, 30, 30, 30) : Color(255, 255, 255, 255));
        std::wstring n = std::to_wstring(s.number);
        g.DrawString(n.c_str(), -1, &f, RectF(c.X - r, c.Y - r + r * 0.06f, r * 2, r * 2), &sf, &tb);
        break;
    }
    }
}

static void DrawShapes(Graphics& g, Overlay* o, bool live) {
    for (size_t i = 0; i < o->shapes.size(); ++i) {
        bool editing = live && o->editingText && i + 1 == o->shapes.size();
        DrawShape(g, o, o->shapes[i], editing);
    }
}

// ---- 工具栏布局与绘制 --------------------------------------------------------
static bool ToolbarVisible(Overlay* o) { return o->hasSel && (o->drag == DR_NONE || o->drag == DR_SHAPE); }

static void LayoutToolbar(Overlay* o) {
    int B = S(32), pad = S(5), sepW = S(11);
    int x = pad;
    RECT rel[B_COUNT];
    for (int i = 0; i < B_COUNT; ++i) {
        if (i == B_UNDO || i == B_PIN || i == B_CANCEL) x += sepW;
        rel[i] = { x, pad, x + B, pad + B }; x += B;
    }
    int tw = x + pad, th = B + pad * 2;

    o->propVisible = o->tool != T_NONE;
    int ph = S(34), px = pad;
    RECT prel[11]; o->propCount = 0;
    for (int i = 0; i < 3; ++i) { prel[o->propCount++] = { px, 0, px + S(28), ph }; px += S(28); }
    if (o->tool != T_MOSAIC) {
        px += sepW;
        for (int i = 0; i < 8; ++i) { prel[o->propCount++] = { px, 0, px + S(26), ph }; px += S(26); }
    }
    int pw = px + pad;

    POINT c = { (o->sel.left + o->sel.right) / 2, (o->sel.top + o->sel.bottom) / 2 };
    RECT mon = MonRectAt(o, c);
    int need = th + (o->propVisible ? ph + S(6) : 0);
    int tx = o->sel.right - tw;
    tx = std::max<int>(mon.left + S(4), std::min<int>(tx, mon.right - tw - S(4)));
    int ty; bool below;
    if (o->sel.bottom + S(8) + need <= mon.bottom) { ty = o->sel.bottom + S(8); below = true; }
    else if (o->sel.top - S(8) - need >= mon.top) { ty = o->sel.top - S(8) - th; below = false; }
    else { ty = std::max<int>(mon.top + S(8), o->sel.bottom - S(8) - need); below = true; }
    o->tbBelow = below;
    o->tbRect = { tx, ty, tx + tw, ty + th };
    for (int i = 0; i < B_COUNT; ++i) { o->btn[i] = rel[i]; OffsetRect(&o->btn[i], tx, ty); }
    if (o->propVisible) {
        int py = below ? ty + th + S(6) : ty - S(6) - ph;
        int pxs = std::min<int>(tx, mon.right - pw - S(4));
        o->propRect = { pxs, py, pxs + pw, py + ph };
        for (int i = 0; i < o->propCount; ++i) { o->propItems[i] = prel[i]; OffsetRect(&o->propItems[i], pxs, py); }
    }
}

static int HitBtn(Overlay* o, POINT p) {
    if (!ToolbarVisible(o)) return -1;
    for (int i = 0; i < B_COUNT; ++i) if (PtInRect(&o->btn[i], p)) return i;
    return -1;
}
static int HitProp(Overlay* o, POINT p) {
    if (!ToolbarVisible(o) || !o->propVisible) return -1;
    for (int i = 0; i < o->propCount; ++i) if (PtInRect(&o->propItems[i], p)) return i;
    return -1;
}
static bool InToolUI(Overlay* o, POINT p) {
    if (!ToolbarVisible(o)) return false;
    if (PtInRect(&o->tbRect, p)) return true;
    return o->propVisible && PtInRect(&o->propRect, p);
}

static void DrawGlyph(Graphics& g, int id, const RectF& r, const Color& c) {
    float u = r.Width / 32.0f * 0.95f;
    float ox = r.X + r.Width / 2 - 10 * u, oy = r.Y + r.Height / 2 - 10 * u;
    auto P = [&](float x, float y) { return PointF(ox + x * u, oy + y * u); };
    Pen p(c, 1.7f * u);
    p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    switch (id) {
    case B_RECT:    g.DrawRectangle(&p, RectF(ox + 3 * u, oy + 5 * u, 14 * u, 10 * u)); break;
    case B_ELLIPSE: g.DrawEllipse(&p, RectF(ox + 3 * u, oy + 4 * u, 14 * u, 12 * u)); break;
    case B_ARROW:
        g.DrawLine(&p, P(4, 16), P(15.5f, 4.5f)); g.DrawLine(&p, P(15.5f, 4.5f), P(8.5f, 4.5f));
        g.DrawLine(&p, P(15.5f, 4.5f), P(15.5f, 11.5f)); break;
    case B_PEN:     g.DrawBezier(&p, P(3, 15), P(7, 2), P(11, 20), P(17, 5)); break;
    case B_MARKER: {
        Pen m(Color(140, c.GetR(), c.GetG(), c.GetB()), 5 * u); m.SetStartCap(LineCapRound); m.SetEndCap(LineCapRound);
        g.DrawLine(&m, P(4, 12), P(16, 7));
        g.DrawLine(&p, P(3.5f, 16.5f), P(16.5f, 16.5f)); break;
    }
    case B_MOSAIC: {
        SolidBrush b(c);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if ((i + j) % 2 == 0) g.FillRectangle(&b, RectF(ox + (4 + i * 4) * u, oy + (4 + j * 4) * u, 4 * u, 4 * u));
        Pen thin(c, 1.2f * u); g.DrawRectangle(&thin, RectF(ox + 4 * u, oy + 4 * u, 12 * u, 12 * u)); break;
    }
    case B_TEXT:
        g.DrawLine(&p, P(5, 4.5f), P(15, 4.5f)); g.DrawLine(&p, P(10, 4.5f), P(10, 16));
        g.DrawLine(&p, P(7.5f, 16), P(12.5f, 16)); break;
    case B_NUMBER: {
        g.DrawEllipse(&p, RectF(ox + 3 * u, oy + 3 * u, 14 * u, 14 * u));
        Font f(UiFontName(), 9 * u, FontStyleBold, UnitPixel);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush b(c); g.DrawString(L"1", -1, &f, RectF(ox + 3 * u, oy + 3.6f * u, 14 * u, 14 * u), &sf, &b); break;
    }
    case B_UNDO:
        g.DrawLine(&p, P(7, 4.5f), P(3.5f, 8)); g.DrawLine(&p, P(3.5f, 8), P(7, 11.5f)); g.DrawLine(&p, P(3.5f, 8), P(12, 8));
        g.DrawArc(&p, RectF(ox + 8 * u, oy + 8 * u, 8 * u, 8 * u), 270, 180); g.DrawLine(&p, P(12, 16), P(7, 16)); break;
    case B_TRIM:   // 裁剪：两个交叉的直角
        g.DrawLine(&p, P(6, 2.5f), P(6, 14)); g.DrawLine(&p, P(6, 14), P(17.5f, 14));
        g.DrawLine(&p, P(2.5f, 6), P(14, 6)); g.DrawLine(&p, P(14, 6), P(14, 17.5f)); break;
    case B_PIN: {
        PointF pin[8] = { P(7, 3), P(13, 3), P(12, 4.5f), P(12, 9), P(15, 12.5f), P(5, 12.5f), P(8, 9), P(8, 4.5f) };
        g.DrawPolygon(&p, pin, 8); g.DrawLine(&p, P(10, 12.5f), P(10, 18)); break;
    }
    case B_SAVE:
        g.DrawLine(&p, P(10, 3), P(10, 12)); g.DrawLine(&p, P(6, 8.5f), P(10, 12.5f)); g.DrawLine(&p, P(14, 8.5f), P(10, 12.5f));
        g.DrawLine(&p, P(4, 13), P(4, 16.5f)); g.DrawLine(&p, P(4, 16.5f), P(16, 16.5f)); g.DrawLine(&p, P(16, 16.5f), P(16, 13)); break;
    case B_CANCEL:
        g.DrawLine(&p, P(5, 5), P(15, 15)); g.DrawLine(&p, P(15, 5), P(5, 15)); break;
    case B_DONE: {
        Pen q(c, 2.3f * u); q.SetStartCap(LineCapRound); q.SetEndCap(LineCapRound); q.SetLineJoin(LineJoinRound);
        g.DrawLine(&q, P(3.5f, 10.5f), P(8, 15)); g.DrawLine(&q, P(8, 15), P(16.5f, 5.5f)); break;
    }
    }
}

static RectF ToRectF(const RECT& r) { return RectF((REAL)r.left, (REAL)r.top, (REAL)RW(r), (REAL)RH(r)); }

static void FillRoundGradient(Graphics& g, const RectF& r, float rad, const Color& a, const Color& b) {
    GraphicsPath p; RoundRectPath(p, r, rad);
    LinearGradientBrush br(PointF(r.X - 1, r.Y), PointF(r.X + r.Width + 1, r.Y + r.Height), a, b);
    g.FillPath(&br, &p);
}

static void DrawPanel(Graphics& g, const RectF& r) {
    SoftShadow(g, r, SF(9));
    FillRound(g, r, SF(9), PANEL);
    GraphicsPath p; RoundRectPath(p, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), SF(9));
    Pen edge(Color(70, 255, 255, 255), 1.0f);
    g.DrawPath(&edge, &p);
}

static void DrawToolbar(Graphics& g, Overlay* o) {
    RectF tb = ToRectF(o->tbRect);
    DrawPanel(g, tb);
    Pen sep(SEP_C, 1.0f);
    for (int i = 0; i < B_COUNT; ++i) {
        RectF r = ToRectF(o->btn[i]);
        RectF rin(r.X + SF(1), r.Y + SF(1), r.Width - SF(2), r.Height - SF(2));
        bool active = i <= B_NUMBER && o->tool == i;
        bool hot = o->hoverBtn == i;
        Color c = ICON_FG;
        if (active) { FillRoundGradient(g, rin, SF(6), ACCENT, ACCENT2); c = Color(255, 255, 255, 255); }
        else if (i == B_DONE) { FillRoundGradient(g, rin, SF(6), hot ? Color(255, 52, 211, 110) : OK_GREEN, Color(255, 16, 185, 129)); c = Color(255, 255, 255, 255); }
        else if (hot) FillRound(g, rin, SF(6), PANEL_HOVER);
        if (i == B_CANCEL) c = DANGER;
        if (i == B_UNDO && o->shapes.empty() && o->trimUndo.empty()) c = ICON_DIM;
        DrawGlyph(g, i, r, c);
        if (i == B_UNDO || i == B_PIN || i == B_CANCEL) {
            float sx = r.X - SF(5.5f);
            g.DrawLine(&sep, sx, tb.Y + SF(11), sx, tb.Y + tb.Height - SF(11));
        }
    }
    if (o->propVisible) {
        RectF pr = ToRectF(o->propRect);
        DrawPanel(g, pr);
        static const float dots[3] = { 2.0f, 3.5f, 5.5f };
        for (int i = 0; i < o->propCount; ++i) {
            RectF r = ToRectF(o->propItems[i]);
            float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
            if (i < 3) {
                RectF cell(cx - SF(11), cy - SF(11), SF(22), SF(22));
                if (o->sizeIdx == i) FillRoundGradient(g, cell, SF(6), ACCENT, ACCENT2);
                else if (o->hoverProp == i) FillRound(g, cell, SF(6), PANEL_HOVER);
                SolidBrush b(o->sizeIdx == i ? Color(255, 255, 255, 255) : ICON_FG);
                float d = SF(dots[i]); g.FillEllipse(&b, cx - d, cy - d, d * 2, d * 2);
                if (i == 2 && o->tool != T_MOSAIC) {
                    float sx = r.X + r.Width + SF(5.5f);
                    g.DrawLine(&sep, sx, pr.Y + SF(9), sx, pr.Y + pr.Height - SF(9));
                }
            } else {
                int ci = i - 3; float rad = SF(7.5f);
                if (o->colorIdx == ci) {
                    Pen ring(Color(255, 255, 255, 255), SF(1.8f));
                    g.DrawEllipse(&ring, cx - rad - SF(3.5f), cy - rad - SF(3.5f), (rad + SF(3.5f)) * 2, (rad + SF(3.5f)) * 2);
                } else if (o->hoverProp == i) rad = SF(9);
                SolidBrush b(PALETTE[ci]); g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
                if (ci == 6) { Pen ol(Color(255, 120, 124, 140), 1.0f); g.DrawEllipse(&ol, cx - rad, cy - rad, rad * 2, rad * 2); }
            }
        }
    }
    if (o->hoverBtn >= 0) {
        Font f(UiFontName(), SF(12), FontStyleRegular, UnitPixel);
        RectF m; g.MeasureString(BTN_TIPS[o->hoverBtn], -1, &f, PointF(0, 0), &m);
        RectF br = ToRectF(o->btn[o->hoverBtn]);
        float w = m.Width + SF(18), h = m.Height + SF(10);
        float x = br.X + br.Width / 2 - w / 2;
        float y = o->tbBelow ? tb.Y - h - SF(8) : tb.Y + tb.Height + SF(8);
        x = std::max(0.0f, std::min(x, (REAL)o->w - w));
        RectF box(x, y, w, h);
        DrawPanel(g, box);
        SolidBrush ab(ACCENT2);
        g.FillRectangle(&ab, x + SF(6), y + h - SF(3), w - SF(12), SF(1.5f));
        SolidBrush wb(Color(255, 255, 255, 255));
        g.DrawString(BTN_TIPS[o->hoverBtn], -1, &f, PointF(x + SF(9), y + SF(4)), &wb);
    }
}

static void DrawSizeLabel(Graphics& g, Overlay* o, const RECT& s) {
    wchar_t buf[64]; swprintf(buf, 64, L"%ld × %ld", RW(s), RH(s));
    Font f(UiFontName(), SF(12), FontStyleBold, UnitPixel);
    RectF m; g.MeasureString(buf, -1, &f, PointF(0, 0), &m);
    float w = m.Width + SF(14), h = m.Height + SF(6);
    POINT c = { s.left, s.top };
    RECT mon = MonRectAt(o, c);
    float x = (REAL)s.left, y = s.top - h - SF(6);
    if (y < mon.top) y = s.top + SF(6), x = s.left + SF(6);
    RectF box(x, y, w, h);
    FillRoundGradient(g, box, h / 2, ACCENT, ACCENT2);
    SolidBrush wb(Color(255, 255, 255, 255));
    g.DrawString(buf, -1, &f, PointF(x + SF(7), y + SF(3)), &wb);
}

static void DrawSelBorder(Graphics& g, const RECT& s, float pw, bool hover) {
    RectF r(s.left + pw / 2, s.top + pw / 2, RW(s) - pw, RH(s) - pw);
    if (hover) { SolidBrush tint(Color(28, 108, 92, 231)); g.FillRectangle(&tint, r); }
    LinearGradientBrush br(PointF((REAL)s.left - 1, (REAL)s.top - 1), PointF((REAL)s.right + 1, (REAL)s.bottom + 1), ACCENT, ACCENT2);
    Pen p(&br, pw);
    g.DrawRectangle(&p, r);
}

static void DrawHandles(Graphics& g, const RECT& s) {
    float l = (REAL)s.left, t = (REAL)s.top, r = (REAL)s.right - 1, b = (REAL)s.bottom - 1;
    float cx = (l + r) / 2, cy = (t + b) / 2, rad = SF(4.5f);
    PointF pts[8] = { PointF(l, t), PointF(cx, t), PointF(r, t), PointF(r, cy), PointF(r, b), PointF(cx, b), PointF(l, b), PointF(l, cy) };
    SolidBrush wb(Color(255, 255, 255, 255)); Pen p(ACCENT, SF(2.0f));
    for (auto& pt : pts) { g.FillEllipse(&wb, pt.X - rad, pt.Y - rad, rad * 2, rad * 2); g.DrawEllipse(&p, pt.X - rad, pt.Y - rad, rad * 2, rad * 2); }
}

static bool ShowMagnifier(Overlay* o) {
    return g_cfg.magnifier && (!o->hasSel || o->drag == DR_CREATE || o->drag == DR_RESIZE);
}

static DWORD PixelAt(Overlay* o, POINT p) {
    if (p.x < 0 || p.y < 0 || p.x >= o->w || p.y >= o->h) return 0;
    return ((DWORD*)o->baseBits)[(size_t)p.y * o->w + p.x] & 0xFFFFFF;
}

static void DrawMagnifier(Graphics& g, Overlay* o) {
    const int N = 17; int cell = std::max(5, S(7)); int box = N * cell, infoH = S(62);
    POINT m = o->mouse; RECT mon = MonRectAt(o, m);
    int x = m.x + S(22), y = m.y + S(26);
    if (x + box > mon.right) x = m.x - S(22) - box;
    if (y + box + infoH > mon.bottom) y = m.y - S(26) - box - infoH;
    RectF all((REAL)x, (REAL)y, (REAL)box, (REAL)(box + infoH));
    SoftShadow(g, all, SF(4));
    HDC h = g.GetHDC();
    SetStretchBltMode(h, COLORONCOLOR);
    StretchBlt(h, x, y, box, box, o->baseDC, m.x - N / 2, m.y - N / 2, N, N, SRCCOPY);
    g.ReleaseHDC(h);
    SolidBrush band(Color(75, 108, 92, 231));
    g.FillRectangle(&band, (REAL)x, (REAL)(y + (N / 2) * cell), (REAL)box, (REAL)cell);
    g.FillRectangle(&band, (REAL)(x + (N / 2) * cell), (REAL)y, (REAL)cell, (REAL)box);
    Pen cpen(Color(255, 255, 255, 255), 1.0f);
    g.DrawRectangle(&cpen, (REAL)(x + (N / 2) * cell), (REAL)(y + (N / 2) * cell), (REAL)cell - 1, (REAL)cell - 1);
    SolidBrush infoBg(PANEL);
    g.FillRectangle(&infoBg, (REAL)x, (REAL)(y + box), (REAL)box, (REAL)infoH);
    Pen frame(Color(255, 255, 255, 255), 1.0f);
    { LinearGradientBrush fb(PointF(all.X, all.Y), PointF(all.X + all.Width, all.Y + all.Height), ACCENT, ACCENT2);
      Pen fp(&fb, SF(2)); g.DrawRectangle(&fp, all.X, all.Y, all.Width - 1, all.Height - 1); }

    Font f(UiFontName(), SF(11.5f), FontStyleRegular, UnitPixel);
    Font fs(UiFontName(), SF(10.5f), FontStyleRegular, UnitPixel);
    SolidBrush wb(Color(255, 255, 255, 255)), gb(Color(255, 140, 220, 235));
    wchar_t buf[96];
    if (o->drag == DR_CREATE || o->drag == DR_RESIZE) swprintf(buf, 96, L"选区  %ld × %ld", RW(o->sel), RH(o->sel));
    else swprintf(buf, 96, L"坐标  %d, %d", m.x + o->vx, m.y + o->vy);
    float ly = (REAL)(y + box) + SF(5);
    g.DrawString(buf, -1, &f, PointF((REAL)x + SF(7), ly), &wb);
    DWORD px = PixelAt(o, m);
    int R = (px >> 16) & 255, G = (px >> 8) & 255, B = px & 255;
    SolidBrush sw(Color(255, (BYTE)R, (BYTE)G, (BYTE)B));
    float sy = ly + SF(19);
    g.FillRectangle(&sw, (REAL)x + SF(8), sy + SF(2), SF(12), SF(12));
    g.DrawRectangle(&frame, (REAL)x + SF(8), sy + SF(2), SF(12), SF(12));
    swprintf(buf, 96, L"#%02X%02X%02X  (%d,%d,%d)", R, G, B, R, G, B);
    g.DrawString(buf, -1, &fs, PointF((REAL)x + SF(25), sy), &wb);
    g.DrawString(L"按 C 复制色值", -1, &fs, PointF((REAL)x + SF(7), sy + SF(18)), &gb);
}

static void OvPaint(Overlay* o, HDC hdc) {
    HDC dc = o->backDC;
    BitBlt(dc, 0, 0, o->w, o->h, o->dimDC, 0, 0, SRCCOPY);
    bool hoverMode = !o->hasSel && o->hasHover;
    RECT s = o->hasSel ? o->sel : o->hover;
    if (o->hasSel || hoverMode) BitBlt(dc, s.left, s.top, RW(s), RH(s), o->baseDC, s.left, s.top, SRCCOPY);
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        if (o->hasSel) {
            g.SetClip(Rect(s.left, s.top, RW(s), RH(s)));
            DrawShapes(g, o, true);
            g.ResetClip();
        }
        if (o->hasSel || hoverMode) {
            float pw = hoverMode ? SF(2.5f) : SF(1.5f);
            DrawSelBorder(g, s, pw, hoverMode);
            if (o->hasSel && o->tool == T_NONE && o->drag != DR_MOVE) DrawHandles(g, s);
            DrawSizeLabel(g, o, s);
        }
        if (ToolbarVisible(o)) { LayoutToolbar(o); DrawToolbar(g, o); }
        if (ShowMagnifier(o)) DrawMagnifier(g, o);
    }
    BitBlt(hdc, 0, 0, o->w, o->h, dc, 0, 0, SRCCOPY);
}

// ---- 交互 ------------------------------------------------------------------
static int HitHandle(Overlay* o, POINT p) {
    const RECT& s = o->sel; int tol = S(6);
    bool inX = p.x >= s.left - tol && p.x <= s.right + tol, inY = p.y >= s.top - tol && p.y <= s.bottom + tol;
    if (!inX || !inY) return -1;
    bool L = abs(p.x - s.left) <= tol, R = abs(p.x - (s.right - 1)) <= tol;
    bool T = abs(p.y - s.top) <= tol, Bm = abs(p.y - (s.bottom - 1)) <= tol;
    if (L && T)  return 0;
    if (R && T)  return 2;
    if (R && Bm) return 4;
    if (L && Bm) return 6;
    if (T)  return 1;
    if (R)  return 3;
    if (Bm) return 5;
    if (L)  return 7;
    return -1;
}

static LPCWSTR OvCursor(Overlay* o, POINT p) {
    static const LPCWSTR hc[8] = { IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE, IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE };
    if (InToolUI(o, p)) return IDC_HAND;
    if (o->drag == DR_MOVE) return IDC_SIZEALL;
    if (o->drag == DR_RESIZE && o->handle >= 0) return hc[o->handle];
    if (o->hasSel) {
        if (o->tool == T_NONE) {
            int h = HitHandle(o, p);
            if (h >= 0) return hc[h];
            if (PtInRect(&o->sel, p)) return IDC_SIZEALL;
            return IDC_ARROW;
        }
        if (!PtInRect(&o->sel, p)) return IDC_ARROW;
        return o->tool == T_TEXT ? IDC_IBEAM : IDC_CROSS;
    }
    return IDC_CROSS;
}

static void ClampSel(Overlay* o, RECT& r) {
    r.left = std::max<LONG>(0, r.left); r.top = std::max<LONG>(0, r.top);
    r.right = std::min<LONG>(o->w, r.right); r.bottom = std::min<LONG>(o->h, r.bottom);
    if (r.right <= r.left) r.right = r.left + 1;
    if (r.bottom <= r.top) r.bottom = r.top + 1;
}

static void UpdateIme(Overlay* o) {
    if (!o->editingText || o->shapes.empty()) return;
    HIMC imc = ImmGetContext(g_hOverlay);
    if (!imc) return;
    const Shape& s = o->shapes.back();
    std::vector<std::wstring> ls; SplitLines(s.text, ls);
    COMPOSITIONFORM cf = {}; cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)s.pts[0].X;
    cf.ptCurrentPos.y = (LONG)(s.pts[0].Y + s.width * 1.35f * (ls.size() - 1));
    ImmSetCompositionWindow(imc, &cf);
    ImmReleaseContext(g_hOverlay, imc);
}

static void CommitText(Overlay* o) {
    if (!o->editingText) return;
    o->editingText = false;
    if (!o->shapes.empty() && o->shapes.back().type == T_TEXT) {
        std::wstring& t = o->shapes.back().text;
        bool blank = t.find_first_not_of(L" \n") == std::wstring::npos;
        if (blank) o->shapes.pop_back();
    }
}

static void CloseOverlay() { if (g_hOverlay) DestroyWindow(g_hOverlay); }

// ---- 清晰度：AI 识别增强 ----------------------------------------------------
// 小字号文字在 OCR / 多模态模型里容易认错。放大到 2~3 倍（高质量双三次）后再做一次
// 轻度 USM 锐化，能让笔画边缘更干净；标注在放大后的画布上按矢量重绘，不会糊。
static int OutputScale(int w, int h) {
    switch (g_cfg.upscale) {
    case 0: return 1;
    case 2: return 2;
    case 3: return 3;
    default: return (g_scale < 1.5f && std::max(w, h) <= 1600) ? 2 : 1;   // 自动：普通分辨率下的小图放大 2 倍
    }
}

static void SharpenBitmap(Bitmap* b, float amount) {
    UINT w = b->GetWidth(), h = b->GetHeight();
    if (w < 3 || h < 3) return;
    BitmapData bd; Rect r(0, 0, (INT)w, (INT)h);
    if (b->LockBits(&r, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Ok) return;
    std::vector<DWORD> src((size_t)w * h);
    for (UINT y = 0; y < h; ++y) memcpy(&src[(size_t)y * w], (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, w * 4);
    int k = (int)lroundf(amount * 256);
    for (UINT y = 1; y + 1 < h; ++y) {
        DWORD* out = (DWORD*)((BYTE*)bd.Scan0 + (size_t)y * bd.Stride);
        for (UINT x = 1; x + 1 < w; ++x) {
            int sum[3] = { 0, 0, 0 };
            for (int dy = -1; dy <= 1; ++dy) {
                const DWORD* row = &src[(size_t)(y + dy) * w + x - 1];
                for (int dx = 0; dx < 3; ++dx) {
                    DWORD px = row[dx];
                    sum[0] += px & 255; sum[1] += (px >> 8) & 255; sum[2] += (px >> 16) & 255;
                }
            }
            DWORD c = src[(size_t)y * w + x];
            DWORD res = 0xFF000000;
            for (int ch = 0; ch < 3; ++ch) {
                int v = (int)((c >> (ch * 8)) & 255);
                int blur = sum[ch] / 9;
                int nv = v + (((v - blur) * k) >> 8);
                nv = nv < 0 ? 0 : (nv > 255 ? 255 : nv);
                res |= (DWORD)nv << (ch * 8);
            }
            out[x] = res;
        }
    }
    b->UnlockBits(&bd);
}

static Bitmap* ComposeResult(Overlay* o, int k = 1) {
    RECT s = o->sel; int w = RW(s), h = RH(s);
    int W = w * k, H = h * k;
    Bitmap* bmp = new Bitmap(W, H, PixelFormat32bppARGB);
    {
        Graphics g(bmp);
        g.SetCompositingMode(CompositingModeSourceCopy);
        if (k > 1) {
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        } else {
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);   // 原始像素 1:1，不做任何插值
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
        }
        ImageAttributes ia; ia.SetWrapMode(WrapModeTileFlipXY);       // 防止放大时边缘发灰
        g.DrawImage(o->gdipBase.get(), Rect(0, 0, W, H), s.left, s.top, w, h, UnitPixel, &ia);
    }
    if (k > 1 && g_cfg.sharpen) SharpenBitmap(bmp, 0.6f);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.ScaleTransform((REAL)k, (REAL)k);
    g.TranslateTransform((REAL)-s.left, (REAL)-s.top);
    DrawShapes(g, o, false);
    return bmp;
}

// 返回 true 表示遮罩已关闭（调用方不得再访问 o）
static bool OvFinish(Overlay* o, int how) {
    if (!o->hasSel) return false;
    CommitText(o);
    int k = (how == FIN_PIN) ? 1 : OutputScale(RW(o->sel), RH(o->sel));   // 贴图保持原尺寸
    std::unique_ptr<Bitmap> res(ComposeResult(o, k));
    if (how == FIN_SAVE) {
        std::wstring path;
        if (!SaveAsDialog(g_hOverlay, res.get(), &path)) { InvalidateRect(g_hOverlay, NULL, FALSE); return false; }
        CopyBitmapToClipboard(res.get());
    } else if (how == FIN_PIN) {
        POINT pos = { o->vx + o->sel.left, o->vy + o->sel.top };
        CloseOverlay();
        CreatePin(res.release(), pos);
        return true;
    } else {
        CopyBitmapToClipboard(res.get());
        if (g_cfg.autoSave) AutoSaveBitmap(res.get());
    }
    CloseOverlay();
    return true;
}

// ---- 一键裁边：去掉四周的空白和纯色细边框 ------------------------------------
static inline bool NearColor(DWORD a, DWORD b, int tol) {
    for (int sh = 0; sh <= 16; sh += 8) {
        int d = (int)((a >> sh) & 255) - (int)((b >> sh) & 255);
        if (d > tol || d < -tol) return false;
    }
    return true;
}

// 一整行（horizontal=true）或一整列的像素是否都接近颜色 c
static bool LineIs(Overlay* o, const RECT& r, bool horizontal, int pos, DWORD c, int tol) {
    const DWORD* px = (const DWORD*)o->baseBits;
    if (horizontal) {
        const DWORD* row = px + (size_t)pos * o->w;
        for (int x = r.left; x < r.right; ++x) if (!NearColor(row[x], c, tol)) return false;
    } else {
        for (int y = r.top; y < r.bottom; ++y) if (!NearColor(px[(size_t)y * o->w + pos], c, tol)) return false;
    }
    return true;
}

static DWORD Px(Overlay* o, int x, int y) { return ((const DWORD*)o->baseBits)[(size_t)y * o->w + x] & 0xFFFFFF; }

// 背景色：四个角里，与选区一圈边缘像素吻合最多的那个
static DWORD GuessBackground(Overlay* o, const RECT& r, int tol) {
    DWORD cand[4] = { Px(o, r.left, r.top), Px(o, r.right - 1, r.top), Px(o, r.left, r.bottom - 1), Px(o, r.right - 1, r.bottom - 1) };
    int best = 0, bestScore = -1;
    int stepX = std::max(1, (int)RW(r) / 200), stepY = std::max(1, (int)RH(r) / 200);
    for (int k = 0; k < 4; ++k) {
        int score = 0;
        for (int x = r.left; x < r.right; x += stepX) {
            score += NearColor(Px(o, x, r.top), cand[k], tol) + NearColor(Px(o, x, r.bottom - 1), cand[k], tol);
        }
        for (int y = r.top; y < r.bottom; y += stepY) {
            score += NearColor(Px(o, r.left, y), cand[k], tol) + NearColor(Px(o, r.right - 1, y), cand[k], tol);
        }
        if (score > bestScore) { bestScore = score; best = k; }
    }
    return cand[best];
}

// 从四边向内剥掉与背景色一致的行/列；返回 false 表示整块都是背景
static bool StripBackground(Overlay* o, RECT& r, DWORD bg, int tol) {
    while (r.top < r.bottom && LineIs(o, r, true, r.top, bg, tol)) ++r.top;
    while (r.bottom > r.top && LineIs(o, r, true, r.bottom - 1, bg, tol)) --r.bottom;
    while (r.left < r.right && LineIs(o, r, false, r.left, bg, tol)) ++r.left;
    while (r.right > r.left && LineIs(o, r, false, r.right - 1, bg, tol)) --r.right;
    return RW(r) > 0 && RH(r) > 0;
}

// 剥掉最外侧不超过 maxW 像素宽的纯色细边框（窗口描边、表格外框等）
static void StripThinBorder(Overlay* o, RECT& r, int maxW, int tol) {
    for (int i = 0; i < maxW && RH(r) > 2; ++i) if (LineIs(o, r, true, r.top, Px(o, r.left, r.top), tol)) ++r.top; else break;
    for (int i = 0; i < maxW && RH(r) > 2; ++i) if (LineIs(o, r, true, r.bottom - 1, Px(o, r.left, r.bottom - 1), tol)) --r.bottom; else break;
    for (int i = 0; i < maxW && RW(r) > 2; ++i) if (LineIs(o, r, false, r.left, Px(o, r.left, r.top), tol)) ++r.left; else break;
    for (int i = 0; i < maxW && RW(r) > 2; ++i) if (LineIs(o, r, false, r.right - 1, Px(o, r.right - 1, r.top), tol)) --r.right; else break;
}

static bool TrimSelection(Overlay* o) {
    RECT r = o->sel;
    if (RW(r) < 4 || RH(r) < 4) return false;
    const int tol = 18;
    if (!StripBackground(o, r, GuessBackground(o, r, tol), tol)) return false;   // 外侧空白
    RECT frame = r;
    StripThinBorder(o, frame, 3, tol);                                           // 细边框
    RECT content = frame;
    // 边框里面那一圈空白也去掉；剥完只剩空白则退回只去外侧空白
    if (RW(frame) < 4 || RH(frame) < 4 || !StripBackground(o, content, GuessBackground(o, frame, tol), tol)) { content = r; frame = r; }
    // 给内容留一圈窄边（不超出边框以内的区域），文字贴边时 AI/OCR 更容易认错
    int pad = S(6);
    RECT out = { std::max(frame.left, content.left - pad), std::max(frame.top, content.top - pad),
                 std::min(frame.right, content.right + pad), std::min(frame.bottom, content.bottom + pad) };
    if (EqualRect(&out, &o->sel) || RW(out) < 2 || RH(out) < 2) return false;
    o->trimUndo.push_back(o->sel);
    o->sel = out;
    return true;
}

static bool UndoOne(Overlay* o) {
    if (!o->shapes.empty()) { o->shapes.pop_back(); return true; }
    if (!o->trimUndo.empty()) { o->sel = o->trimUndo.back(); o->trimUndo.pop_back(); return true; }
    return false;
}

static void DoTrim(Overlay* o) {
    CommitText(o);
    if (!TrimSelection(o)) TrayBalloon(APP_NAME, L"选区四周没有可裁掉的空白");
}

static bool OnButton(Overlay* o, int b) {
    if (b <= B_NUMBER) {
        CommitText(o);
        o->tool = (o->tool == b) ? T_NONE : b;
        return false;
    }
    switch (b) {
    case B_UNDO:   CommitText(o); UndoOne(o); return false;
    case B_TRIM:   DoTrim(o); return false;
    case B_PIN:    return OvFinish(o, FIN_PIN);
    case B_SAVE:   return OvFinish(o, FIN_SAVE);
    case B_CANCEL: CloseOverlay(); return true;
    case B_DONE:   return OvFinish(o, FIN_COPY);
    }
    return false;
}

static void OnProp(Overlay* o, int i) {
    if (i < 3) o->sizeIdx = i; else o->colorIdx = i - 3;
    if (o->editingText && !o->shapes.empty()) {
        Shape& s = o->shapes.back();
        s.color = PALETTE[o->colorIdx]; s.width = ToolWidth(T_TEXT, o->sizeIdx);
    }
}

static void StartShape(Overlay* o, POINT p) {
    Shape s; s.type = o->tool; s.color = PALETTE[o->colorIdx]; s.width = ToolWidth(o->tool, o->sizeIdx);
    PointF pt((REAL)p.x, (REAL)p.y);
    switch (o->tool) {
    case T_TEXT:
        s.pts.push_back(PointF(pt.X, pt.Y - s.width * 0.65f));
        o->shapes.push_back(s); o->editingText = true; UpdateIme(o);
        return;
    case T_NUMBER: {
        int n = 0; for (auto& x : o->shapes) if (x.type == T_NUMBER) n = std::max(n, x.number);
        s.number = n + 1; s.pts.push_back(pt); o->shapes.push_back(s);
        return;
    }
    case T_PEN: case T_MARKER: case T_MOSAIC:
        s.pts.push_back(pt); break;
    default:
        s.pts.push_back(pt); s.pts.push_back(pt); break;
    }
    o->shapes.push_back(s);
    o->drag = DR_SHAPE;
}

static void UpdateShape(Overlay* o, POINT p) {
    if (o->shapes.empty()) return;
    Shape& s = o->shapes.back();
    PointF b((REAL)p.x, (REAL)p.y);
    bool shift = GetKeyState(VK_SHIFT) < 0;
    switch (s.type) {
    case T_RECT: case T_ELLIPSE:
        if (shift) {
            float dx = b.X - s.pts[0].X, dy = b.Y - s.pts[0].Y, m = std::max(fabsf(dx), fabsf(dy));
            b.X = s.pts[0].X + (dx < 0 ? -m : m); b.Y = s.pts[0].Y + (dy < 0 ? -m : m);
        }
        s.pts[1] = b; break;
    case T_ARROW:
        if (shift) {
            float dx = b.X - s.pts[0].X, dy = b.Y - s.pts[0].Y, L = sqrtf(dx * dx + dy * dy);
            float a = atan2f(dy, dx), step = 3.14159265f / 4; a = roundf(a / step) * step;
            b.X = s.pts[0].X + cosf(a) * L; b.Y = s.pts[0].Y + sinf(a) * L;
        }
        s.pts[1] = b; break;
    default: {
        PointF& last = s.pts.back();
        if (fabsf(last.X - b.X) + fabsf(last.Y - b.Y) >= 1.0f) s.pts.push_back(b);
    }
    }
}

static void EndShape(Overlay* o) {
    if (o->shapes.empty()) return;
    Shape& s = o->shapes.back();
    if (s.type == T_RECT || s.type == T_ELLIPSE || s.type == T_ARROW) {
        if (fabsf(s.pts[1].X - s.pts[0].X) < 3 && fabsf(s.pts[1].Y - s.pts[0].Y) < 3) o->shapes.pop_back();
    }
}

static void OvMouseDown(HWND hw, Overlay* o, POINT p) {
    o->mouse = p;
    int b = HitBtn(o, p);
    if (b >= 0) { if (OnButton(o, b)) return; InvalidateRect(hw, NULL, FALSE); return; }
    int pi = HitProp(o, p);
    if (pi >= 0) { OnProp(o, pi); InvalidateRect(hw, NULL, FALSE); return; }
    if (InToolUI(o, p)) return;
    SetCapture(hw);
    if (!o->hasSel) { o->drag = DR_PENDING; o->dragStart = p; return; }
    if (o->tool == T_NONE) {
        int h = HitHandle(o, p);
        if (h >= 0) { o->drag = DR_RESIZE; o->handle = h; o->selAtDrag = o->sel; o->trimUndo.clear(); o->dragStart = p; }
        else if (PtInRect(&o->sel, p)) { o->drag = DR_MOVE; o->selAtDrag = o->sel; o->trimUndo.clear(); o->dragStart = p; }
        InvalidateRect(hw, NULL, FALSE);
        return;
    }
    if (!PtInRect(&o->sel, p)) { CommitText(o); InvalidateRect(hw, NULL, FALSE); return; }
    CommitText(o);
    StartShape(o, p);
    InvalidateRect(hw, NULL, FALSE);
}

static void OvMouseMove(HWND hw, Overlay* o, POINT p) {
    o->mouse = p;
    switch (o->drag) {
    case DR_PENDING:
        if (abs(p.x - o->dragStart.x) <= 3 && abs(p.y - o->dragStart.y) <= 3) break;
        o->drag = DR_CREATE;
        // fallthrough
    case DR_CREATE: {
        RECT r = { std::min(o->dragStart.x, p.x), std::min(o->dragStart.y, p.y),
                   std::max(o->dragStart.x, p.x) + 1, std::max(o->dragStart.y, p.y) + 1 };
        ClampSel(o, r); o->sel = r; o->hasSel = true; o->trimUndo.clear();
        break;
    }
    case DR_MOVE: {
        int w = RW(o->selAtDrag), h = RH(o->selAtDrag);
        int nl = o->selAtDrag.left + (p.x - o->dragStart.x), nt = o->selAtDrag.top + (p.y - o->dragStart.y);
        nl = std::max(0, std::min(nl, o->w - w)); nt = std::max(0, std::min(nt, o->h - h));
        o->sel = { nl, nt, nl + w, nt + h };
        break;
    }
    case DR_RESIZE: {
        RECT r = o->selAtDrag; int dx = p.x - o->dragStart.x, dy = p.y - o->dragStart.y;
        switch (o->handle) {
        case 0: r.left += dx; r.top += dy; break;     case 1: r.top += dy; break;
        case 2: r.right += dx; r.top += dy; break;    case 3: r.right += dx; break;
        case 4: r.right += dx; r.bottom += dy; break; case 5: r.bottom += dy; break;
        case 6: r.left += dx; r.bottom += dy; break;  case 7: r.left += dx; break;
        }
        if (r.left > r.right) std::swap(r.left, r.right);
        if (r.top > r.bottom) std::swap(r.top, r.bottom);
        ClampSel(o, r); o->sel = r;
        break;
    }
    case DR_SHAPE:
        UpdateShape(o, p); break;
    default:
        if (!o->hasSel) UpdateHover(o);
        else { o->hoverBtn = HitBtn(o, p); o->hoverProp = HitProp(o, p); }
        break;
    }
    InvalidateRect(hw, NULL, FALSE);
}

static void OvMouseUp(HWND hw, Overlay* o, POINT p) {
    ReleaseCapture();
    switch (o->drag) {
    case DR_PENDING:
        if (o->hasHover) { o->sel = o->hover; o->hasSel = true; o->trimUndo.clear(); }
        break;
    case DR_CREATE:
        if (RW(o->sel) < 3 || RH(o->sel) < 3) o->hasSel = false;
        break;
    case DR_SHAPE:
        UpdateShape(o, p); EndShape(o); break;
    }
    o->drag = DR_NONE; o->handle = -1;
    if (!o->hasSel) UpdateHover(o);
    InvalidateRect(hw, NULL, FALSE);
}

static LRESULT CALLBACK OverlayProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    Overlay* o = g_ov;
    if (!o) return DefWindowProcW(hw, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        OvPaint(o, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(hw, &p);
            SetCursor(LoadCursor(NULL, OvCursor(o, p)));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: OvMouseDown(hw, o, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0;
    case WM_MOUSEMOVE:   OvMouseMove(hw, o, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0;
    case WM_LBUTTONUP:   OvMouseUp(hw, o, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0;
    case WM_LBUTTONDBLCLK: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (o->hasSel && o->tool == T_NONE && PtInRect(&o->sel, p) && !InToolUI(o, p)) { OvFinish(o, FIN_COPY); return 0; }
        OvMouseDown(hw, o, p);
        return 0;
    }
    case WM_RBUTTONUP:
        if (o->editingText) { CommitText(o); InvalidateRect(hw, NULL, FALSE); return 0; }
        if (o->drag != DR_NONE) return 0;
        if (o->hasSel) {
            o->hasSel = false; o->shapes.clear(); o->trimUndo.clear(); o->tool = T_NONE; o->hoverBtn = o->hoverProp = -1;
            UpdateHover(o); InvalidateRect(hw, NULL, FALSE);
        } else CloseOverlay();
        return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (o->editingText) {
            if (wp == VK_ESCAPE) { CommitText(o); InvalidateRect(hw, NULL, FALSE); }
            return 0;
        }
        switch (wp) {
        case VK_ESCAPE: CloseOverlay(); return 0;
        case VK_RETURN: OvFinish(o, FIN_COPY); return 0;
        case 'Z': if (ctrl && UndoOne(o)) InvalidateRect(hw, NULL, FALSE); return 0;
        case 'T': if (!ctrl && o->hasSel && o->drag == DR_NONE) { DoTrim(o); InvalidateRect(hw, NULL, FALSE); } return 0;
        case 'S': if (ctrl) OvFinish(o, FIN_SAVE); return 0;
        case 'C':
            if (ctrl) { OvFinish(o, FIN_COPY); return 0; }
            if (!o->hasSel || o->drag == DR_CREATE) {
                DWORD px = PixelAt(o, o->mouse);
                wchar_t buf[16]; swprintf(buf, 16, L"#%02X%02X%02X", (px >> 16) & 255, (px >> 8) & 255, px & 255);
                std::wstring hex = buf;
                CopyTextToClipboard(hex);
                CloseOverlay();
                TrayBalloon(APP_NAME, L"已复制色值 " + hex);
            }
            return 0;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            int dx = wp == VK_LEFT ? -1 : wp == VK_RIGHT ? 1 : 0, dy = wp == VK_UP ? -1 : wp == VK_DOWN ? 1 : 0;
            if (o->hasSel && o->tool == T_NONE && o->drag == DR_NONE) {
                RECT r = o->sel; int w = RW(r), h = RH(r);
                if (GetKeyState(VK_SHIFT) < 0) { r.right += dx; r.bottom += dy; ClampSel(o, r); }
                else {
                    int nl = std::max(0, std::min((int)r.left + dx, o->w - w)), nt = std::max(0, std::min((int)r.top + dy, o->h - h));
                    r = { nl, nt, nl + w, nt + h };
                }
                o->sel = r; InvalidateRect(hw, NULL, FALSE);
            } else {
                POINT c; GetCursorPos(&c); SetCursorPos(c.x + dx, c.y + dy);
            }
            return 0;
        }
        }
        return 0;
    }
    case WM_CHAR:
        if (o->editingText && !o->shapes.empty()) {
            std::wstring& t = o->shapes.back().text;
            wchar_t ch = (wchar_t)wp;
            if (ch == 8) { if (!t.empty()) { t.pop_back(); if (!t.empty() && IS_HIGH_SURROGATE(t.back())) t.pop_back(); } }
            else if (ch == 13) t.push_back(L'\n');
            else if (ch >= 32 || ch == 9) t.push_back(ch == 9 ? L' ' : ch);
            UpdateIme(o);
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        g_hOverlay = NULL; g_ov = nullptr;
        delete o;
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void StartCapture() {
    if (g_ov || g_hOverlay) return;
    auto* o = new Overlay;
    o->vx = GetSystemMetrics(SM_XVIRTUALSCREEN); o->vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    o->w = GetSystemMetrics(SM_CXVIRTUALSCREEN); o->h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC scr = GetDC(NULL);
    o->baseBmp = MakeDIB(o->w, o->h, &o->baseBits);
    o->dimBmp  = MakeDIB(o->w, o->h, &o->dimBits);
    o->backBmp = CreateCompatibleBitmap(scr, o->w, o->h);
    if (!o->baseBmp || !o->dimBmp || !o->backBmp) {
        ReleaseDC(NULL, scr); delete o;
        MessageBoxW(NULL, L"内存不足，无法截图。", APP_NAME, MB_ICONERROR); return;
    }
    o->baseDC = CreateCompatibleDC(scr); o->oldBase = SelectObject(o->baseDC, o->baseBmp);
    o->dimDC  = CreateCompatibleDC(scr); o->oldDim  = SelectObject(o->dimDC, o->dimBmp);
    o->backDC = CreateCompatibleDC(scr); o->oldBack = SelectObject(o->backDC, o->backBmp);
    BitBlt(o->baseDC, 0, 0, o->w, o->h, scr, o->vx, o->vy, SRCCOPY | CAPTUREBLT);
    ReleaseDC(NULL, scr);
    GdiFlush();

    size_t n = (size_t)o->w * o->h;
    DWORD* src = (DWORD*)o->baseBits; DWORD* dst = (DWORD*)o->dimBits;
    for (size_t i = 0; i < n; ++i) {
        DWORD px = src[i] | 0xFF000000; src[i] = px;
        DWORD r = ((px >> 16) & 255) * 115 >> 8, g = ((px >> 8) & 255) * 115 >> 8, b = (px & 255) * 115 >> 8;
        dst[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    o->gdipBase.reset(new Bitmap(o->w, o->h, o->w * 4, PixelFormat32bppRGB, o->baseBits));
    o->wins.reserve(512);
    EnumWindows(EnumWinProc, (LPARAM)o);

    g_ov = o;
    g_hOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, CLS_OVER, L"老陈截图", WS_POPUP,
                                 o->vx, o->vy, o->w, o->h, NULL, NULL, g_hInst, NULL);
    if (!g_hOverlay) { g_ov = nullptr; delete o; return; }
    GetCursorPos(&o->mouse); o->mouse.x -= o->vx; o->mouse.y -= o->vy;
    UpdateHover(o);
    ShowWindow(g_hOverlay, SW_SHOW);
    SetForegroundWindow(g_hOverlay);
    SetFocus(g_hOverlay);
    UpdateWindow(g_hOverlay);
}

static void CaptureFullScreen() {
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    RECT r = mi.rcMonitor; int w = RW(r), h = RH(r);
    BYTE* bits = nullptr;
    HDC scr = GetDC(NULL);
    HBITMAP hb = MakeDIB(w, h, &bits);
    if (!hb) { ReleaseDC(NULL, scr); return; }
    HDC mdc = CreateCompatibleDC(scr);
    HGDIOBJ old = SelectObject(mdc, hb);
    BitBlt(mdc, 0, 0, w, h, scr, r.left, r.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(NULL, scr); GdiFlush();
    DWORD* p = (DWORD*)bits; for (size_t i = 0, n = (size_t)w * h; i < n; ++i) p[i] |= 0xFF000000;
    std::unique_ptr<Bitmap> res(new Bitmap(w, h, PixelFormat32bppARGB));
    {
        Bitmap tmp(w, h, w * 4, PixelFormat32bppRGB, bits);
        Graphics g(res.get());
        g.DrawImage(&tmp, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel);
    }
    SelectObject(mdc, old); DeleteDC(mdc); DeleteObject(hb);
    CopyBitmapToClipboard(res.get());
    std::wstring msg = L"全屏截图已复制到剪贴板";
    if (g_cfg.autoSave) { std::wstring path = AutoSaveBitmap(res.get()); if (!path.empty()) msg += L"\n已保存：" + path; }
    TrayBalloon(APP_NAME, msg);
}

// ----------------------------------------------------------------------------
//  设置窗口
// ----------------------------------------------------------------------------
enum { IDC_HK0 = 2001, IDC_HK1, IDC_HK2, IDC_DIR, IDC_BROWSE, IDC_AUTOSAVE, IDC_MAGNIFIER, IDC_AUTOSTART, IDC_DEFAULTS, IDC_HINT, IDC_UPSCALE, IDC_SHARPEN, IDC_SECTION };

static WORD ToHkCtl(const Hotkey& h) {
    BYTE f = 0;
    if (h.mod & MOD_SHIFT) f |= HOTKEYF_SHIFT;
    if (h.mod & MOD_CONTROL) f |= HOTKEYF_CONTROL;
    if (h.mod & MOD_ALT) f |= HOTKEYF_ALT;
    if (IsExtendedVk(h.vk)) f |= HOTKEYF_EXT;
    return MAKEWORD((BYTE)h.vk, f);
}
static Hotkey FromHkCtl(WORD w) {
    Hotkey h; h.vk = LOBYTE(w); BYTE f = HIBYTE(w); h.mod = 0;
    if (f & HOTKEYF_SHIFT) h.mod |= MOD_SHIFT;
    if (f & HOTKEYF_CONTROL) h.mod |= MOD_CONTROL;
    if (f & HOTKEYF_ALT) h.mod |= MOD_ALT;
    return h;
}

static HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0, HFONT font = NULL) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S((float)x), S((float)y), S((float)w), S((float)h),
                             parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)(font ? font : g_uiFont), TRUE);
    return c;
}

static void SettingsFill(HWND hw, const Settings& s) {
    for (int i = 0; i < 3; ++i) SendDlgItemMessageW(hw, IDC_HK0 + i, HKM_SETHOTKEY, ToHkCtl(s.hk[i]), 0);
    SetDlgItemTextW(hw, IDC_DIR, s.saveDir.c_str());
    CheckDlgButton(hw, IDC_AUTOSAVE, s.autoSave ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hw, IDC_MAGNIFIER, s.magnifier ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hw, IDC_AUTOSTART, s.autoStart ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hw, IDC_UPSCALE, CB_SETCURSEL, s.upscale, 0);
    CheckDlgButton(hw, IDC_SHARPEN, s.sharpen ? BST_CHECKED : BST_UNCHECKED);
}

static bool PickFolder(HWND owner, std::wstring& path) {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
    DWORD opt = 0; d->GetOptions(&opt);
    d->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"选择截图保存目录");
    bool ok = false;
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); ok = true; }
            it->Release();
        }
    }
    d->Release();
    return ok;
}

static void SettingsApply(HWND hw) {
    Hotkey nh[3];
    for (int i = 0; i < 3; ++i) nh[i] = FromHkCtl((WORD)SendDlgItemMessageW(hw, IDC_HK0 + i, HKM_GETHOTKEY, 0, 0));
    for (int i = 0; i < 3; ++i) {
        bool alnum = (nh[i].vk >= 'A' && nh[i].vk <= 'Z') || (nh[i].vk >= '0' && nh[i].vk <= '9');
        if (nh[i].vk && !nh[i].mod && alnum) {
            MessageBoxW(hw, (std::wstring(HK_NAMES[i]) + L" 的热键需要搭配 Ctrl / Alt / Shift，单独字母或数字会影响正常打字。").c_str(), APP_NAME, MB_ICONWARNING);
            return;
        }
        for (int j = i + 1; j < 3; ++j)
            if (nh[i].vk && nh[i].vk == nh[j].vk && nh[i].mod == nh[j].mod) {
                MessageBoxW(hw, (std::wstring(HK_NAMES[i]) + L" 和 " + HK_NAMES[j] + L" 的热键重复了。").c_str(), APP_NAME, MB_ICONWARNING);
                return;
            }
    }
    wchar_t dir[MAX_PATH * 2] = {}; GetDlgItemTextW(hw, IDC_DIR, dir, MAX_PATH * 2);
    for (int i = 0; i < 3; ++i) g_cfg.hk[i] = nh[i];
    g_cfg.saveDir   = dir[0] ? dir : DefaultSaveDir();
    g_cfg.autoSave  = IsDlgButtonChecked(hw, IDC_AUTOSAVE) == BST_CHECKED;
    g_cfg.magnifier = IsDlgButtonChecked(hw, IDC_MAGNIFIER) == BST_CHECKED;
    g_cfg.autoStart = IsDlgButtonChecked(hw, IDC_AUTOSTART) == BST_CHECKED;
    g_cfg.upscale   = std::max(0, (int)SendDlgItemMessageW(hw, IDC_UPSCALE, CB_GETCURSEL, 0, 0));
    g_cfg.sharpen   = IsDlgButtonChecked(hw, IDC_SHARPEN) == BST_CHECKED;
    SaveSettings();
    SetAutoStart(g_cfg.autoStart);
    std::wstring failed = RegisterAllHotkeys();
    TrayUpdateTip();
    if (!failed.empty()) {
        MessageBoxW(hw, (L"以下热键已被其他程序占用，请换一个组合：\n\n" + failed).c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    DestroyWindow(hw);
    TrayBalloon(L"设置已保存", L"区域截图：" + HotkeyStr(g_cfg.hk[0]) + L"\n全屏截图：" + HotkeyStr(g_cfg.hk[1]) +
                                  L"\n贴图（剪贴板）：" + HotkeyStr(g_cfg.hk[2]));
}

static void SettingsPaintHeader(HWND hw, HDC hdc) {
    RECT rc; GetClientRect(hw, &rc);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    REAL W = (REAL)rc.right, H = SF(88);
    LinearGradientBrush hb(PointF(0, 0), PointF(W, H), ACCENT, ACCENT2);
    g.FillRectangle(&hb, 0.0f, 0.0f, W, H);
    // 装饰光斑
    SolidBrush glow(Color(28, 255, 255, 255));
    g.FillEllipse(&glow, W - SF(120), -SF(60), SF(200), SF(160));
    g.FillEllipse(&glow, W - SF(220), SF(40), SF(110), SF(90));
    // 图标底板
    FillRound(g, RectF(SF(24), SF(20), SF(48), SF(48)), SF(12), Color(60, 255, 255, 255));
    DrawIconEx(hdc, S(28), S(24), g_hIconBig ? g_hIconBig : g_hIconLg, S(40), S(40), 0, NULL, DI_NORMAL);
    Font ft(UiFontName(), SF(22), FontStyleBold, UnitPixel);
    Font fs(UiFontName(), SF(13), FontStyleRegular, UnitPixel);
    SolidBrush wb(Color(255, 255, 255, 255)), wb2(Color(215, 255, 255, 255));
    g.DrawString(APP_NAME, -1, &ft, PointF(SF(86), SF(18)), &wb);
    std::wstring sub = std::wstring(L"v") + APP_VER + L"  ·  纯 Win32，轻巧清晰，为 AI 识别而生";
    g.DrawString(sub.c_str(), -1, &fs, PointF(SF(88), SF(52)), &wb2);
    // 底部分隔
    Pen line(Color(255, 228, 230, 238), 1.0f);
    g.DrawLine(&line, 0.0f, (REAL)rc.bottom - SF(76), W, (REAL)rc.bottom - SF(76));
}

static void SettingsDrawButton(DRAWITEMSTRUCT* d) {
    RECT r = d->rcItem;
    HDC hdc = d->hDC;
    FillRect(hdc, &r, g_bgBrush);
    bool primary = d->CtlID == IDOK, pressed = (d->itemState & ODS_SELECTED) != 0, focus = (d->itemState & ODS_FOCUS) != 0;
    {
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RectF rf((REAL)r.left + 1, (REAL)r.top + 1 + (pressed ? 1 : 0), (REAL)RW(r) - 2, (REAL)RH(r) - 3);
        if (primary) {
            FillRoundGradient(g, rf, SF(7), pressed ? Color(255, 88, 72, 211) : ACCENT, pressed ? Color(255, 0, 160, 190) : ACCENT2);
        } else {
            FillRound(g, rf, SF(7), pressed ? Color(255, 236, 237, 245) : Color(255, 255, 255, 255));
            GraphicsPath p; RoundRectPath(p, rf, SF(7));
            Pen pen(focus ? ACCENT : Color(255, 213, 216, 228), focus ? 1.5f : 1.0f);
            g.DrawPath(&pen, &p);
        }
    }
    wchar_t text[64]; GetWindowTextW(d->hwndItem, text, 64);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, primary ? RGB(255, 255, 255) : RGB(45, 48, 66));
    HGDIOBJ of = SelectObject(hdc, primary ? g_uiFontBold : g_uiFont);
    if (pressed) OffsetRect(&r, 0, 1);
    DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

static LRESULT CALLBACK SettingsProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        const int T = 104;   // 内容起始（头部横幅下方）
        MakeCtl(hw, L"STATIC", L"快捷键", SS_LEFT, 28, T, 300, 24, IDC_SECTION, 0, g_uiFontBold);
        for (int i = 0; i < 3; ++i) {
            int y = T + 32 + i * 38;
            MakeCtl(hw, L"STATIC", HK_NAMES[i], SS_LEFT, 36, y + 4, 120, 22, -1);
            MakeCtl(hw, HOTKEY_CLASSW, L"", WS_TABSTOP, 168, y, 230, 28, IDC_HK0 + i, WS_EX_CLIENTEDGE);
        }
        MakeCtl(hw, L"STATIC", L"点击输入框后直接按下新的组合键，Backspace 清除", SS_LEFT, 168, T + 146, 320, 20, IDC_HINT);

        const int Q = T + 182;
        MakeCtl(hw, L"STATIC", L"清晰度（让 AI 准确识别文字）", SS_LEFT, 28, Q, 360, 24, IDC_SECTION, 0, g_uiFontBold);
        MakeCtl(hw, L"STATIC", L"放大输出", SS_LEFT, 36, Q + 36, 120, 22, -1);
        HWND cb = MakeCtl(hw, WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 168, Q + 32, 230, 200, IDC_UPSCALE);
        const wchar_t* opts[4] = { L"原始像素（1:1 不放大）", L"自动（小图放大 2 倍）推荐", L"始终放大 2 倍", L"始终放大 3 倍" };
        for (auto t : opts) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t);
        MakeCtl(hw, L"BUTTON", L"放大后锐化文字边缘（笔画更干净）", WS_TABSTOP | BS_AUTOCHECKBOX, 36, Q + 70, 420, 24, IDC_SHARPEN);
        MakeCtl(hw, L"STATIC", L"只作用于复制和保存；贴图始终保持原尺寸。标注会按矢量重绘，不会变糊。", SS_LEFT, 36, Q + 98, 440, 20, IDC_HINT);

        const int V = Q + 136;
        MakeCtl(hw, L"STATIC", L"保存与启动", SS_LEFT, 28, V, 300, 24, IDC_SECTION, 0, g_uiFontBold);
        MakeCtl(hw, L"STATIC", L"保存目录", SS_LEFT, 36, V + 36, 120, 22, -1);
        MakeCtl(hw, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 168, V + 32, 230, 28, IDC_DIR, WS_EX_CLIENTEDGE);
        MakeCtl(hw, L"BUTTON", L"浏览…", WS_TABSTOP | BS_OWNERDRAW, 406, V + 31, 76, 30, IDC_BROWSE);
        MakeCtl(hw, L"BUTTON", L"完成截图后同时自动保存到该目录", WS_TABSTOP | BS_AUTOCHECKBOX, 36, V + 70, 420, 24, IDC_AUTOSAVE);
        MakeCtl(hw, L"BUTTON", L"截图时显示放大镜与色值", WS_TABSTOP | BS_AUTOCHECKBOX, 36, V + 98, 420, 24, IDC_MAGNIFIER);
        MakeCtl(hw, L"BUTTON", L"开机自动启动", WS_TABSTOP | BS_AUTOCHECKBOX, 36, V + 126, 420, 24, IDC_AUTOSTART);

        const int F = V + 178;
        MakeCtl(hw, L"BUTTON", L"恢复默认", WS_TABSTOP | BS_OWNERDRAW, 28, F, 104, 34, IDC_DEFAULTS);
        MakeCtl(hw, L"BUTTON", L"取消", WS_TABSTOP | BS_OWNERDRAW, 282, F, 96, 34, IDCANCEL);
        MakeCtl(hw, L"BUTTON", L"保存", WS_TABSTOP | BS_OWNERDRAW, 386, F, 96, 34, IDOK);
        SettingsFill(hw, g_cfg);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
        SettingsPaintHeader(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        SettingsDrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, id == IDC_HINT ? RGB(138, 142, 160) : id == IDC_SECTION ? RGB(108, 92, 231) : RGB(40, 42, 56));
        return (LRESULT)g_bgBrush;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: {
            std::wstring p;
            if (PickFolder(hw, p)) SetDlgItemTextW(hw, IDC_DIR, p.c_str());
            return 0;
        }
        case IDC_DEFAULTS: {
            Settings d; d.saveDir = DefaultSaveDir(); d.autoStart = IsDlgButtonChecked(hw, IDC_AUTOSTART) == BST_CHECKED;
            SettingsFill(hw, d);
            return 0;
        }
        case IDOK: SettingsApply(hw); return 0;
        case IDCANCEL: DestroyWindow(hw); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hw); return 0;
    case WM_DESTROY: g_hSettings = NULL; return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void ShowSettings() {
    if (g_hSettings) { ShowWindow(g_hSettings, SW_RESTORE); SetForegroundWindow(g_hSettings); return; }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, S(510), S(660) };
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = RW(rc), h = RH(rc);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (RW(wa) - w) / 2, y = std::max<int>(wa.top, wa.top + (RH(wa) - h) / 2);
    g_hSettings = CreateWindowExW(0, CLS_SET, L"老陈截图 - 设置", style, x, y, w, h, NULL, NULL, g_hInst, NULL);
    ShowWindow(g_hSettings, SW_SHOW);
    SetForegroundWindow(g_hSettings);
}

// ----------------------------------------------------------------------------
//  主窗口（隐藏，负责托盘与热键）
// ----------------------------------------------------------------------------
static void ShowTrayMenu(HWND hw) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_REGION,  (L"区域截图\t" + HotkeyStr(g_cfg.hk[0])).c_str());
    AppendMenuW(m, MF_STRING, IDM_FULL,    (L"全屏截图\t" + HotkeyStr(g_cfg.hk[1])).c_str());
    AppendMenuW(m, MF_STRING, IDM_PINCLIP, (L"贴图（剪贴板）\t" + HotkeyStr(g_cfg.hk[2])).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"设置与热键…");
    AppendMenuW(m, MF_STRING, IDM_OPENDIR,  L"打开截图文件夹");
    AppendMenuW(m, MF_STRING, IDM_ABOUT,    L"关于 / 使用说明");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT,     L"退出");
    SetMenuDefaultItem(m, IDM_REGION, FALSE);
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hw);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hw, NULL);
    PostMessageW(hw, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) SendMessageW(hw, WM_COMMAND, cmd, 0);
}

static void ShowAbout() {
    std::wstring t = std::wstring(APP_NAME) + L" " + APP_VER + L"\n纯 Win32 + GDI+，无任何第三方依赖\n截图默认按「AI 识别增强」输出，文字更清晰\n\n"
        L"【热键】\n  区域截图：" + HotkeyStr(g_cfg.hk[0]) + L"\n  全屏截图：" + HotkeyStr(g_cfg.hk[1]) +
        L"\n  贴图（剪贴板）：" + HotkeyStr(g_cfg.hk[2]) + L"\n  （右键托盘图标 → 设置与热键 可修改）\n\n"
        L"【截图时】\n  拖动框选，单击选中窗口/控件，双击选区直接复制\n  方向键微调（Shift+方向键调整大小）\n"
        L"  Enter / Ctrl+C 复制并退出，Ctrl+S 保存，Ctrl+Z 撤销\n  C 复制鼠标处色值，右键取消选区，Esc 退出\n\n"
        L"【贴图】\n  拖动移动，滚轮缩放，Ctrl+滚轮调透明度\n  双击或 Esc 关闭，右键更多操作";
    MessageBoxW(NULL, t.c_str(), L"关于老陈截图", MB_ICONINFORMATION);
}

static LRESULT CALLBACK MainProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) { TrayAdd(); return 0; }
    switch (msg) {
    case WM_HOTKEY:
        if (wp == 1) StartCapture();
        else if (wp == 2) CaptureFullScreen();
        else if (wp == 3) PinClipboard();
        return 0;
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK: SetTimer(hw, IDT_REGION, 200, NULL); break;
        case WM_RBUTTONUP: case WM_CONTEXTMENU: ShowTrayMenu(hw); break;
        }
        return 0;
    case WM_TIMER:
        KillTimer(hw, wp);
        if (wp == IDT_REGION) StartCapture();
        else if (wp == IDT_FULL) CaptureFullScreen();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_REGION:   SetTimer(hw, IDT_REGION, 250, NULL); break;   // 等菜单淡出后再截图
        case IDM_FULL:     SetTimer(hw, IDT_FULL, 250, NULL); break;
        case IDM_PINCLIP:  PinClipboard(); break;
        case IDM_SETTINGS: ShowSettings(); break;
        case IDM_OPENDIR:  EnsureSaveDir(); ShellExecuteW(NULL, L"open", g_cfg.saveDir.c_str(), NULL, NULL, SW_SHOWNORMAL); break;
        case IDM_ABOUT:    ShowAbout(); break;
        case IDM_EXIT:     DestroyWindow(hw); break;
        }
        return 0;
    case WM_DESTROY: {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        for (int i = 0; i < 3; ++i) UnregisterHotKey(hw, i + 1);
        CloseOverlay();
        auto all = g_pins; for (HWND h : all) DestroyWindow(h);
        if (g_hSettings) DestroyWindow(g_hSettings);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// ----------------------------------------------------------------------------
//  入口
// ----------------------------------------------------------------------------
static void SetDpiAwareness() {
    typedef BOOL(WINAPI* PFN_CTX)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    auto f = (PFN_CTX)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (f && f((HANDLE)(INT_PTR)-4)) return;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    SetProcessDPIAware();
}

static float SystemScale() {
    typedef UINT(WINAPI* PFN_DPI)();
    auto f = (PFN_DPI)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    UINT dpi = 96;
    if (f) dpi = f();
    else { HDC dc = GetDC(NULL); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); }
    return dpi / 96.0f;
}

static void RegisterClasses() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.hInstance = g_hInst; wc.hIcon = g_hIconLg; wc.hIconSm = g_hIconSm;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    wc.lpfnWndProc = MainProc; wc.lpszClassName = CLS_MAIN;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = OverlayProc; wc.lpszClassName = CLS_OVER; wc.style = CS_DBLCLKS; wc.hCursor = NULL;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = PinProc; wc.lpszClassName = CLS_PIN; wc.style = CS_DBLCLKS | CS_DROPSHADOW;
    wc.hCursor = LoadCursor(NULL, IDC_SIZEALL);
    RegisterClassExW(&wc);

    wc.lpfnWndProc = SettingsProc; wc.lpszClassName = CLS_SET; wc.style = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = g_bgBrush;
    RegisterClassExW(&wc);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;
    SetDpiAwareness();

    HANDLE mtx = CreateMutexW(NULL, TRUE, L"LaoChenShot.SingleInstance.Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(CLS_MAIN, NULL);
        if (w) PostMessageW(w, WM_COMMAND, IDM_SETTINGS, 0);
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdipToken, &gsi, NULL);

    g_scale = SystemScale();
    g_uiFont     = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    g_uiFontBold = CreateFontW(-S(15), 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    g_hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    g_hIconLg = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    if (!g_hIconSm) g_hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!g_hIconLg) g_hIconLg = LoadIcon(NULL, IDI_APPLICATION);
    g_hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(40), S(40), 0);
    g_bgBrush = CreateSolidBrush(BG_RGB);

    LoadSettings();
    RegisterClasses();
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_hMain = CreateWindowExW(WS_EX_TOOLWINDOW, CLS_MAIN, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    TrayAdd();

    std::wstring failed = RegisterAllHotkeys();
    std::wstring ini = ConfigPath();
    if (!GetPrivateProfileIntW(L"General", L"Welcomed", 0, ini.c_str())) {
        WriteIni(L"General", L"Welcomed", L"1", ini);
        SaveSettings();
        TrayBalloon(L"老陈截图已在后台运行", L"按 " + HotkeyStr(g_cfg.hk[0]) + L" 开始截图\n右键托盘图标可修改热键");
    }
    if (!failed.empty())
        MessageBoxW(NULL, (L"以下热键已被其他程序占用，请右键托盘图标 → 设置与热键 修改：\n\n" + failed).c_str(), APP_NAME, MB_ICONWARNING);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_hSettings && IsDialogMessageW(g_hSettings, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_uiFont); DeleteObject(g_uiFontBold);
    GdiplusShutdown(g_gdipToken);
    CoUninitialize();
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}
