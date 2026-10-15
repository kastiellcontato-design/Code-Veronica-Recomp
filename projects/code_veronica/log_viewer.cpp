/* log_viewer.cpp -- see log_viewer.h.
 *
 * The viewer is a read-only edit control under a small toolbar, dark like the
 * launcher. Every 400 ms it flushes the game's output and appends what the
 * file has grown by; "Follow the end" keeps the newest line in view, and off
 * it leaves the view where the reader scrolled. The control holds the last few
 * megabytes; copies and saves always take the whole file.
 */
#include "log_viewer.h"
#include <shellapi.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const UINT_PTR kTimer = 1;
const int kBar = 46;                        /* toolbar height */
const size_t kInitialTail = 1u << 20;       /* bytes shown when the viewer opens */
const int kMaxChars = 4 << 20;              /* the control is trimmed past this */
const COLORREF kBg = RGB(18, 15, 15), kText = RGB(222, 214, 210), kDim = RGB(150, 140, 136),
               kBtn = RGB(42, 34, 34), kBtnHot = RGB(62, 48, 48), kRed = RGB(200, 30, 36), kBorder = RGB(80, 66, 66);

struct Viewer {
    HWND hwnd = nullptr, edit = nullptr;
    HFONT mono = nullptr, ui = nullptr;
    HBRUSH bg = nullptr;
    LogViewerText text{};
    std::wstring (*save_as)(HWND) = nullptr;
    std::wstring path;
    unsigned long long read_to = 0;   /* bytes of the file already shown */
    std::string pending;              /* an incomplete UTF-8 tail */
    bool follow = true;
    int hot = -1;
    std::wstring status;
    ULONGLONG status_until = 0;
} v;

enum { B_COPY, B_SAVE, B_FOLDER, B_FOLLOW, B_COUNT };

std::wstring utf8_to_wide(const std::string& s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

/* "\n" -> "\r\n" for the edit control; other control characters dropped. */
std::wstring for_edit(const std::wstring& w)
{
    std::wstring out;
    out.reserve(w.size() + w.size() / 32);
    for (wchar_t c : w) {
        if (c == L'\n') out += L"\r\n";
        else if (c == L'\r') continue;
        else if (c < 32 && c != L'\t') continue;
        else out += c;
    }
    return out;
}

HANDLE open_log(const std::wstring& p)
{
    return CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

/* bytes [from, size) of the log */
std::string read_from(unsigned long long from, unsigned long long* size_out)
{
    std::string out;
    HANDLE f = open_log(v.path);
    if (f == INVALID_HANDLE_VALUE) { *size_out = 0; return out; }
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    *size_out = (unsigned long long)sz.QuadPart;
    if ((unsigned long long)sz.QuadPart > from) {
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)from;
        SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
        out.resize((size_t)((unsigned long long)sz.QuadPart - from));
        DWORD got = 0;
        ReadFile(f, &out[0], (DWORD)out.size(), &got, nullptr);
        out.resize(got);
    }
    CloseHandle(f);
    return out;
}

/* complete UTF-8 characters of `bytes` (after what was pending); the rest waits */
std::wstring take_text(const std::string& bytes)
{
    std::string s = v.pending + bytes;
    size_t end = s.size();
    for (size_t k = 0; k < 4 && end > 0; k++) {   /* back off a cut multi-byte character */
        const unsigned char c = (unsigned char)s[end - 1 - k];
        if (c < 0x80) break;
        if (c >= 0xC0) {
            const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            if (k + 1 < need) end = end - 1 - k;
            break;
        }
    }
    v.pending = s.substr(end);
    return for_edit(utf8_to_wide(s.substr(0, end)));
}

void reload()
{
    unsigned long long size = 0;
    v.pending.clear();
    HANDLE f = open_log(v.path);
    if (f != INVALID_HANDLE_VALUE) { LARGE_INTEGER sz; GetFileSizeEx(f, &sz); size = (unsigned long long)sz.QuadPart; CloseHandle(f); }
    const unsigned long long from = size > kInitialTail ? size - kInitialTail : 0;
    std::string bytes = read_from(from, &size);
    if (from) {   /* start on a whole line */
        const size_t nl = bytes.find('\n');
        bytes = nl == std::string::npos ? std::string() : bytes.substr(nl + 1);
    }
    std::wstring t = from ? std::wstring(v.text.cut) + L"\r\n\r\n" : L"";
    t += !v.path.empty() ? take_text(bytes) : std::wstring(log_to_console() ? v.text.console : v.text.none);
    SetWindowTextW(v.edit, t.c_str());
    v.read_to = size;
    if (v.follow) { const int n = GetWindowTextLengthW(v.edit); SendMessageW(v.edit, EM_SETSEL, n, n); SendMessageW(v.edit, EM_SCROLLCARET, 0, 0); }
}

void append_new()
{
    if (v.path.empty()) return;
    log_flush();
    unsigned long long size = 0;
    const std::string bytes = read_from(v.read_to, &size);
    if (size < v.read_to) { reload(); return; }   /* a new run started the file over */
    if (bytes.empty()) return;
    v.read_to += bytes.size();
    const std::wstring add = take_text(bytes);
    if (add.empty()) return;
    if (GetWindowTextLengthW(v.edit) + (int)add.size() > kMaxChars) { reload(); return; }
    DWORD s0 = 0, s1 = 0;
    SendMessageW(v.edit, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
    const int first = (int)SendMessageW(v.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(v.edit, WM_SETREDRAW, FALSE, 0);
    const int n = GetWindowTextLengthW(v.edit);
    SendMessageW(v.edit, EM_SETSEL, n, n);
    SendMessageW(v.edit, EM_REPLACESEL, FALSE, (LPARAM)add.c_str());
    if (v.follow) {
        const int m = GetWindowTextLengthW(v.edit);
        SendMessageW(v.edit, EM_SETSEL, m, m);
        SendMessageW(v.edit, WM_SETREDRAW, TRUE, 0);
        SendMessageW(v.edit, EM_SCROLLCARET, 0, 0);
    } else {   /* where the reader was */
        SendMessageW(v.edit, EM_SETSEL, s0, s1);
        const int now = (int)SendMessageW(v.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
        SendMessageW(v.edit, EM_LINESCROLL, 0, first - now);
        SendMessageW(v.edit, WM_SETREDRAW, TRUE, 0);
    }
    InvalidateRect(v.edit, nullptr, TRUE);
}

RECT button_rect(int b)
{
    static const int x[B_COUNT] = { 12, 142, 292, 0 };
    static const int w[B_COUNT] = { 120, 140, 120, 150 };
    RECT r = { x[b], 8, x[b] + w[b], 8 + 30 };
    if (b == B_FOLLOW) { RECT c; GetClientRect(v.hwnd, &c); r.right = c.right - 12; r.left = r.right - w[b]; }
    return r;
}

const wchar_t* button_label(int b)
{
    return b == B_COPY ? v.text.copy_all : b == B_SAVE ? v.text.save : b == B_FOLDER ? v.text.folder : v.text.follow;
}

void paint(HDC dc)
{
    RECT c; GetClientRect(v.hwnd, &c);
    RECT bar = { 0, 0, c.right, kBar };
    HBRUSH bb = CreateSolidBrush(RGB(26, 21, 21)); FillRect(dc, &bar, bb); DeleteObject(bb);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old = SelectObject(dc, v.ui);
    for (int b = 0; b < B_COUNT; b++) {
        RECT r = button_rect(b);
        if (b == B_FOLLOW) {   /* a tick box */
            RECT box = { r.left, r.top + 7, r.left + 16, r.top + 23 };
            HBRUSH fb = CreateSolidBrush(v.follow ? kRed : kBtn); FillRect(dc, &box, fb); DeleteObject(fb);
            if (v.follow) {
                HPEN pen = CreatePen(PS_SOLID, 2, kText); HGDIOBJ op = SelectObject(dc, pen);
                MoveToEx(dc, box.left + 3, box.top + 8, nullptr); LineTo(dc, box.left + 7, box.top + 12); LineTo(dc, box.left + 13, box.top + 4);
                SelectObject(dc, op); DeleteObject(pen);
            }
            RECT t = { box.right + 8, r.top, r.right, r.bottom };
            SetTextColor(dc, kText);
            DrawTextW(dc, button_label(b), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        HBRUSH fb = CreateSolidBrush(v.hot == b ? kBtnHot : kBtn); FillRect(dc, &r, fb); DeleteObject(fb);
        HBRUSH frame = CreateSolidBrush(kBorder); FrameRect(dc, &r, frame); DeleteObject(frame);
        SetTextColor(dc, kText);
        DrawTextW(dc, button_label(b), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    /* a note from the last action, or where the file is */
    RECT st = { button_rect(B_FOLDER).right + 16, 8, button_rect(B_FOLLOW).left - 12, 38 };
    const std::wstring s = GetTickCount64() < v.status_until ? v.status : v.path;
    SetTextColor(dc, GetTickCount64() < v.status_until ? RGB(110, 200, 120) : kDim);
    DrawTextW(dc, s.c_str(), -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    SelectObject(dc, old);
}

void set_status(const std::wstring& s) { v.status = s; v.status_until = GetTickCount64() + 4000; InvalidateRect(v.hwnd, nullptr, FALSE); }

void click(int b)
{
    if (b == B_COPY) {
        const unsigned long long n = log_copy_to_clipboard(v.hwnd);
        if (n) { wchar_t m[128]; swprintf(m, 128, v.text.copied_fmt, n / 1048576.0); set_status(m); }
    } else if (b == B_SAVE && v.save_as) {
        const std::wstring dest = v.save_as(v.hwnd);
        if (!dest.empty() && log_save_copy(dest)) set_status(v.text.saved);
    } else if (b == B_FOLDER && !v.path.empty()) {
        const std::wstring a = L"/select,\"" + v.path + L"\"";
        ShellExecuteW(v.hwnd, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
    } else if (b == B_FOLLOW) {
        v.follow = !v.follow;
        if (v.follow) { const int n = GetWindowTextLengthW(v.edit); SendMessageW(v.edit, EM_SETSEL, n, n); SendMessageW(v.edit, EM_SCROLLCARET, 0, 0); }
        InvalidateRect(v.hwnd, nullptr, FALSE);
    }
}

int button_at(int x, int y)
{
    POINT p = { x, y };
    for (int b = 0; b < B_COUNT; b++) { RECT r = button_rect(b); if (PtInRect(&r, p)) return b; }
    return -1;
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE:
        if (v.edit) MoveWindow(v.edit, 0, kBar, LOWORD(lp), HIWORD(lp) - kBar, TRUE);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, kText); SetBkColor((HDC)wp, kBg); return (LRESULT)v.bg;
    case WM_MOUSEMOVE: {
        const int b = button_at((short)LOWORD(lp), (short)HIWORD(lp));
        if (b != v.hot) { v.hot = b; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: v.hot = -1; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: { const int b = button_at((short)LOWORD(lp), (short)HIWORD(lp)); if (b >= 0) click(b); return 0; }
    case WM_TIMER:
        if (wp == kTimer) {
            append_new();
            if (v.status_until && GetTickCount64() >= v.status_until) { v.status_until = 0; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, kTimer);
        v.hwnd = v.edit = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}

std::wstring log_file_path()
{
    const intptr_t os = _get_osfhandle(_fileno(stdout));
    if (os == -1 || os == -2) return L"";
    HANDLE f = (HANDLE)os;
    if (GetFileType(f) != FILE_TYPE_DISK) return L"";
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetFinalPathNameByHandleW(f, buf, (DWORD)(sizeof buf / sizeof buf[0]), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= sizeof buf / sizeof buf[0]) return L"";
    std::wstring p(buf, n);
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    return p;
}

bool log_to_console()
{
    const intptr_t os = _get_osfhandle(_fileno(stdout));
    return os != -1 && os != -2 && GetFileType((HANDLE)os) == FILE_TYPE_CHAR;
}

void log_flush() { fflush(stdout); fflush(stderr); }

unsigned long long log_size()
{
    const std::wstring p = log_file_path();
    if (p.empty()) return 0;
    log_flush();
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

unsigned long long log_copy_to_clipboard(HWND owner)
{
    v.path = log_file_path();
    if (v.path.empty()) return 0;
    log_flush();
    unsigned long long size = 0;
    const std::string bytes = read_from(0, &size);
    if (bytes.empty()) return 0;
    std::wstring w = utf8_to_wide(bytes), t;
    t.reserve(w.size() + w.size() / 32);
    for (wchar_t c : w) { if (c == L'\n') t += L"\r\n"; else if (c != L'\r' && c != 0) t += c; }
    if (!OpenClipboard(owner)) return 0;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (mem) {
        wchar_t* d = (wchar_t*)GlobalLock(mem);
        memcpy(d, t.c_str(), (t.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok ? bytes.size() : 0;
}

bool log_save_copy(const std::wstring& dest)
{
    const std::wstring p = log_file_path();
    if (p.empty()) return false;
    log_flush();
    return CopyFileW(p.c_str(), dest.c_str(), FALSE) != 0;
}

void log_viewer_open(HWND owner, const LogViewerText& text, std::wstring (*save_as)(HWND))
{
    v.text = text; v.save_as = save_as;
    if (v.hwnd) {
        SetWindowTextW(v.hwnd, text.title);
        ShowWindow(v.hwnd, SW_RESTORE); SetForegroundWindow(v.hwnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof wc };
        wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"cv_log_viewer";
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0);
        wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        registered = true;
    }
    if (!v.bg) v.bg = CreateSolidBrush(kBg);
    if (!v.mono) v.mono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    if (!v.ui) v.ui = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    v.path = log_file_path();
    v.hwnd = CreateWindowExW(0, L"cv_log_viewer", text.title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1100, 680, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!v.hwnd) return;
    v.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                             ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                             0, kBar, 100, 100, v.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(v.edit, WM_SETFONT, (WPARAM)v.mono, TRUE);
    SendMessageW(v.edit, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    RECT c; GetClientRect(v.hwnd, &c);
    MoveWindow(v.edit, 0, kBar, c.right, c.bottom - kBar, TRUE);
    log_flush();
    reload();
    SetTimer(v.hwnd, kTimer, 400, nullptr);
    ShowWindow(v.hwnd, SW_SHOW);
    SetForegroundWindow(v.hwnd);
}
