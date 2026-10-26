// ============================================================================
//  lexi_win32.cpp — Ứng dụng Lexi chạy giao diện đồ hoạ Windows (Win32 API)
//
//  Phần này chỉ chứa những gì phụ thuộc Windows:
//   - Win32WindowImp : hiện thực Bridge bằng GDI (vẽ lên HDC)
//   - LexiApp        : cửa sổ, menu, thanh công cụ, chuột, bàn phím, clipboard,
//                      hộp thoại mở/lưu. Mỗi mục menu, nút và phím tắt đều gắn
//                      với một đối tượng Command (đúng như MenuItem trong sách).
//  Toàn bộ mô hình tài liệu và các mẫu thiết kế nằm trong lexi_core.h.
// ============================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "lexi_core.h"

using namespace lexi;

namespace {

enum CommandId : unsigned {
    ID_NEW = 100, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_EXPORT, ID_EXIT,
    ID_UNDO, ID_REDO, ID_CUT, ID_COPY, ID_PASTE, ID_SELECTALL, ID_HISTORY,
    ID_SIMPLE, ID_TEX, ID_ARRAY,
    ID_THEME_CLASSIC, ID_THEME_DARK, ID_THEME_TOGGLE,
    ID_SPELL_LIVE, ID_SPELL_LIST, ID_STATS, ID_ABOUT, ID_ADDWORD
};

constexpr UINT_PTR kBlinkTimer = 1;

// ============================================================================
//  BRIDGE — hiện thực Win32/GDI của WindowImp
// ============================================================================
class Win32WindowImp : public WindowImp {
public:
    Win32WindowImp(HDC dc, int charWidth, int lineHeight) : dc_(dc), cw_(charWidth), lh_(lineHeight) {
        SetBkMode(dc_, TRANSPARENT);
    }
    void DeviceFillRect(const Rect& r, Color c) override {
        RECT rc{r.left, r.top, r.right, r.bottom};
        SetDCBrushColor(dc_, ToColorRef(c));
        ::FillRect(dc_, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    void DeviceLine(Point a, Point b, Color c) override {
        HGDIOBJ old = SelectObject(dc_, GetStockObject(DC_PEN));
        SetDCPenColor(dc_, ToColorRef(c));
        MoveToEx(dc_, a.x, a.y, nullptr);
        LineTo(dc_, b.x, b.y);
        SelectObject(dc_, old);
    }
    void DeviceText(int x, int y, const std::wstring& s, Color c) override {
        SetTextColor(dc_, ToColorRef(c));
        TextOutW(dc_, x, y, s.c_str(), static_cast<int>(s.size()));
    }
    void DevicePushClip(const Rect& r) override {
        SaveDC(dc_);
        IntersectClipRect(dc_, r.left, r.top, r.right, r.bottom);
    }
    void DevicePopClip() override { RestoreDC(dc_, -1); }
    int DeviceCharWidth() const override { return cw_; }
    int DeviceLineHeight() const override { return lh_; }

private:
    static COLORREF ToColorRef(Color c) { return RGB(c.r, c.g, c.b); }
    HDC dc_;
    int cw_, lh_;
};

// ---------------------------------------------------------------------------
//  Tệp tin
// ---------------------------------------------------------------------------
bool ReadAllBytes(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > 32LL * 1024 * 1024) {
        CloseHandle(f);
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(f);
    return ok && read == out.size();
}

bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = data.empty() ? TRUE : WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(f);
    return ok && written == data.size();
}

// UTF-8 (có/không BOM); nếu không hợp lệ thì coi là mã ANSI của hệ thống
std::wstring DecodeText(const std::string& bytes) {
    std::string b = bytes;
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF && static_cast<unsigned char>(b[1]) == 0xBB &&
        static_cast<unsigned char>(b[2]) == 0xBF)
        b.erase(0, 3);
    if (b.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b.data(), static_cast<int>(b.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (n == 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, b.data(), static_cast<int>(b.size()), nullptr, 0);
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (n > 0) MultiByteToWideChar(cp, flags, b.data(), static_cast<int>(b.size()), &w[0], n);
    return w;
}

std::string EncodeText(const std::wstring& t) {
    std::wstring crlf;
    for (wchar_t c : t) {
        if (c == L'\n') crlf += L"\r\n";
        else crlf += c;
    }
    return text::ToUtf8(crlf);
}

// Chuẩn hoá văn bản đưa vào: bỏ \r, tab -> 4 khoảng trắng, bỏ ký tự điều khiển
std::wstring Normalize(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\t') o += L"    ";
        else if (c == L'\n' || c >= 32) o += c;
    }
    return o;
}

ACCEL Acc(int flags, int key, unsigned id) {
    ACCEL a;
    a.fVirt = static_cast<BYTE>(flags);
    a.key = static_cast<WORD>(key);
    a.cmd = static_cast<WORD>(id);
    return a;
}

const wchar_t kWelcome[] =
    L"Chào mừng đến với Lexi!\n"
    L"\n"
    L"Đây là trình soạn thảo văn bản minh hoạ các mẫu thiết kế của Gang of Four, dựa theo case study Lexi "
    L"trong cuốn Design Patterns. Mỗi ký tự là một Glyph (Composite + Flyweight), việc ngắt dòng do Compositor "
    L"đảm nhiệm (Strategy), khung viền và thanh cuộn là Decorator, giao diện Cổ điển và Tối do Abstract Factory "
    L"tạo ra, còn việc vẽ lên Win32 hay xuất ảnh SVG đều đi qua Bridge.\n"
    L"\n"
    L"Hãy thử: gõ chữ rồi nhấn Ctrl+Z để hoàn tác, đổi bố cục Simple, TeX hoặc Array trên thanh công cụ, "
    L"đổi giao diện, hoặc nhấp chuột phải vào từ bị gạch đỏ.\n"
    L"\n"
    L"The quick brown fox jumps over teh lazy dog. Thiss sentence has a few speling mistakes.";

// ============================================================================
//  Ứng dụng
// ============================================================================
class LexiApp {
public:
    LexiApp()
        : comp_(std::make_shared<Composition>(std::make_unique<SimpleCompositor>(), 80)),
          status_(*comp_),
          spell_(*comp_),
          factory_(std::make_unique<ClassicFactory>()) {
        comp_->Attach(&status_);  // Observer
        comp_->Attach(&spell_);
        pal_ = factory_->CreatePalette();
        RebuildView();
        RebuildToolbar();
        RegisterActions();
        LoadText(kWelcome, L"");
    }

    ~LexiApp() {
        comp_->Detach(&spell_);
        comp_->Detach(&status_);
        if (font_) DeleteObject(font_);
        if (accel_) DestroyAcceleratorTable(accel_);
    }

    bool Create(HINSTANCE inst, int show) {
        inst_ = inst;
        SetProcessDPIAware();

        HDC sdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
        font_ = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                            L"Consolas");
        HGDIOBJ old = SelectObject(sdc, font_);
        SIZE sz;
        GetTextExtentPoint32W(sdc, L"M", 1, &sz);
        TEXTMETRICW tm;
        GetTextMetricsW(sdc, &tm);
        cw_ = sz.cx;
        lh_ = tm.tmHeight + tm.tmExternalLeading + 2;
        SelectObject(sdc, old);
        ReleaseDC(nullptr, sdc);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &LexiApp::WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.lpszClassName = L"LexiGoFWindow";
        if (!RegisterClassExW(&wc)) return false;

        BuildMenu();
        accel_ = BuildAccelerators();

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Lexi", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                MulDiv(1000, dpi, 96), MulDiv(700, dpi, 96), nullptr, menu_, inst, this);
        if (!hwnd_) return false;
        ShowWindow(hwnd_, show);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run() {
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (!TranslateAcceleratorW(hwnd_, accel_, &m)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        return static_cast<int>(m.wParam);
    }

private:
    // ------------------------------------------------------------------
    //  Thủ tục cửa sổ
    // ------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        LexiApp* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<LexiApp*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<LexiApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self) return self->Handle(msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE:
                ResetBlink();
                UpdateTitle();
                return 0;
            case WM_SIZE:
                Layout();
                Invalidate();
                return 0;
            case WM_GETMINMAXINFO: {
                auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
                mm->ptMinTrackSize.x = 560;
                mm->ptMinTrackSize.y = 360;
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;  // vẽ kép (double buffer) nên không cần xoá nền
            case WM_PAINT:
                Paint();
                return 0;
            case WM_SETFOCUS:
                focused_ = true;
                ResetBlink();
                return 0;
            case WM_KILLFOCUS:
                focused_ = false;
                Invalidate();
                return 0;
            case WM_TIMER:
                if (wp == kBlinkTimer) {
                    blinkOn_ = !blinkOn_;
                    RECT rc{docRect_.left, docRect_.top, docRect_.right, docRect_.bottom};
                    InvalidateRect(hwnd_, &rc, FALSE);
                }
                return 0;
            case WM_KEYDOWN:
                if (OnKeyDown(wp)) return 0;
                break;
            case WM_CHAR:
                OnChar(static_cast<wchar_t>(wp));
                return 0;
            case WM_LBUTTONDOWN:
                OnLButtonDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
                return 0;
            case WM_LBUTTONDBLCLK:
                OnDoubleClick({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp);
                return 0;
            case WM_MOUSEMOVE:
                OnMouseMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
                return 0;
            case WM_LBUTTONUP:
                OnLButtonUp({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
                return 0;
            case WM_CAPTURECHANGED:
                dragging_ = draggingBar_ = false;
                if (pressed_ >= 0) { pressed_ = -1; Invalidate(); }
                return 0;
            case WM_MOUSELEAVE:
                trackingMouse_ = false;
                if (hot_ != -1) { hot_ = -1; Invalidate(); }
                return 0;
            case WM_MOUSEWHEEL:
                scroller_->ScrollBy(-GET_WHEEL_DELTA_WPARAM(wp) * 3 * lh_ / WHEEL_DELTA);
                Invalidate();
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    POINT p;
                    GetCursorPos(&p);
                    ScreenToClient(hwnd_, &p);
                    Point q{static_cast<int>(p.x), static_cast<int>(p.y)};
                    bool overText = docRect_.Contains(q) && !scroller_->BarRect().Contains(q);
                    SetCursor(LoadCursorW(nullptr, overText ? IDC_IBEAM : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_CONTEXTMENU:
                ShowContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                return 0;
            case WM_COMMAND: {
                unsigned id = LOWORD(wp);
                if (RunCommand(id)) return 0;
                break;
            }
            case WM_INITMENUPOPUP:
                UpdateMenuState();
                return 0;
            case WM_CLOSE:
                if (ConfirmDiscard()) DestroyWindow(hwnd_);
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd_, kBlinkTimer);
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    // ------------------------------------------------------------------
    //  COMMAND: mỗi mục menu / nút / phím tắt -> một đối tượng Command
    // ------------------------------------------------------------------
    void RegisterActions() {
        auto reg = [this](unsigned id, const wchar_t* name, std::function<void()> f) {
            actions_[id] = std::make_unique<SimpleCommand>(name, std::move(f));
        };
        reg(ID_NEW, L"Mới", [this] { NewDoc(); });
        reg(ID_OPEN, L"Mở", [this] { OpenDoc(); });
        reg(ID_SAVE, L"Lưu", [this] { SaveDoc(false); });
        reg(ID_SAVEAS, L"Lưu thành", [this] { SaveDoc(true); });
        reg(ID_EXPORT, L"Xuất SVG", [this] { ExportSvg(); });
        reg(ID_EXIT, L"Thoát", [this] { PostMessageW(hwnd_, WM_CLOSE, 0, 0); });
        reg(ID_UNDO, L"Hoàn tác", [this] { Undo(); });
        reg(ID_REDO, L"Làm lại", [this] { Redo(); });
        reg(ID_CUT, L"Cắt", [this] { Copy(); DeleteSelection(); });
        reg(ID_COPY, L"Sao chép", [this] { Copy(); });
        reg(ID_PASTE, L"Dán", [this] { Paste(); });
        reg(ID_SELECTALL, L"Chọn tất cả", [this] { anchor_ = 0; caret_ = comp_->Length(); Invalidate(); });
        reg(ID_HISTORY, L"Lịch sử lệnh", [this] { ShowHistory(); });
        reg(ID_SIMPLE, L"Bố cục Simple", [this] { SetCompositor(std::make_unique<SimpleCompositor>()); });
        reg(ID_TEX, L"Bố cục TeX", [this] { SetCompositor(std::make_unique<TeXCompositor>()); });
        reg(ID_ARRAY, L"Bố cục Array", [this] { SetCompositor(std::make_unique<ArrayCompositor>(5)); });
        reg(ID_THEME_CLASSIC, L"Giao diện Cổ điển", [this] { SetTheme(std::make_unique<ClassicFactory>()); });
        reg(ID_THEME_DARK, L"Giao diện Tối", [this] { SetTheme(std::make_unique<DarkFactory>()); });
        reg(ID_THEME_TOGGLE, L"Đổi giao diện", [this] {
            if (dynamic_cast<DarkFactory*>(factory_.get())) SetTheme(std::make_unique<ClassicFactory>());
            else SetTheme(std::make_unique<DarkFactory>());
        });
        reg(ID_SPELL_LIVE, L"Chính tả khi gõ", [this] { spell_.SetEnabled(!spell_.Enabled()); Invalidate(); });
        reg(ID_SPELL_LIST, L"Danh sách lỗi", [this] { ShowSpellList(); });
        reg(ID_STATS, L"Thống kê", [this] { ShowStats(); });
        reg(ID_ABOUT, L"Giới thiệu", [this] { ShowAbout(); });
        reg(ID_ADDWORD, L"Thêm vào từ điển", [this] {
            if (pendingWord_.empty()) return;
            Dictionary::Instance().Add(pendingWord_);
            spell_.Update(*comp_);
            Invalidate();
        });
    }

    bool RunCommand(unsigned id) {
        auto it = actions_.find(id);
        if (it == actions_.end()) return false;
        it->second->Execute();
        return true;
    }

    void BuildMenu() {
        HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, ID_NEW, L"&Mới\tCtrl+N");
        AppendMenuW(file, MF_STRING, ID_OPEN, L"M&ở...\tCtrl+O");
        AppendMenuW(file, MF_STRING, ID_SAVE, L"&Lưu\tCtrl+S");
        AppendMenuW(file, MF_STRING, ID_SAVEAS, L"Lưu &thành...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_EXPORT, L"&Xuất ảnh SVG... (Bridge)");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_EXIT, L"T&hoát");

        HMENU edit = CreatePopupMenu();
        AppendMenuW(edit, MF_STRING, ID_UNDO, L"&Hoàn tác\tCtrl+Z");
        AppendMenuW(edit, MF_STRING, ID_REDO, L"&Làm lại\tCtrl+Y");
        AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(edit, MF_STRING, ID_CUT, L"&Cắt\tCtrl+X");
        AppendMenuW(edit, MF_STRING, ID_COPY, L"&Sao chép\tCtrl+C");
        AppendMenuW(edit, MF_STRING, ID_PASTE, L"&Dán\tCtrl+V");
        AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(edit, MF_STRING, ID_SELECTALL, L"Chọn &tất cả\tCtrl+A");
        AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(edit, MF_STRING, ID_HISTORY, L"Lịch sử l&ệnh... (Command)");

        HMENU format = CreatePopupMenu();
        AppendMenuW(format, MF_STRING, ID_SIMPLE, L"&Simple — ngắt dòng tham lam, canh trái");
        AppendMenuW(format, MF_STRING, ID_TEX, L"&TeX — tối ưu cả đoạn, canh đều hai bên");
        AppendMenuW(format, MF_STRING, ID_ARRAY, L"&Array — cố định 5 từ mỗi dòng");

        HMENU view = CreatePopupMenu();
        AppendMenuW(view, MF_STRING, ID_THEME_CLASSIC, L"&Cổ điển (ClassicFactory)");
        AppendMenuW(view, MF_STRING, ID_THEME_DARK, L"&Tối (DarkFactory)");

        HMENU tools = CreatePopupMenu();
        AppendMenuW(tools, MF_STRING, ID_SPELL_LIVE, L"Kiểm tra &chính tả khi gõ");
        AppendMenuW(tools, MF_STRING, ID_SPELL_LIST, L"&Danh sách lỗi chính tả...\tF7");
        AppendMenuW(tools, MF_STRING, ID_STATS, L"&Thống kê... (Visitor)");

        HMENU help = CreatePopupMenu();
        AppendMenuW(help, MF_STRING, ID_ABOUT, L"&Về Lexi và các mẫu thiết kế...");

        menu_ = CreateMenu();
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Tệp");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Sửa");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(format), L"Định &dạng");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Giao diện");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"&Công cụ");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Trợ g&iúp");
    }

    HACCEL BuildAccelerators() {
        const int ctrl = FVIRTKEY | FCONTROL;
        ACCEL a[] = {
            Acc(ctrl, 'N', ID_NEW),     Acc(ctrl, 'O', ID_OPEN),  Acc(ctrl, 'S', ID_SAVE),
            Acc(ctrl, 'Z', ID_UNDO),    Acc(ctrl, 'Y', ID_REDO),  Acc(ctrl | FSHIFT, 'Z', ID_REDO),
            Acc(ctrl, 'X', ID_CUT),     Acc(ctrl, 'C', ID_COPY),  Acc(ctrl, 'V', ID_PASTE),
            Acc(ctrl, 'A', ID_SELECTALL), Acc(FVIRTKEY, VK_F7, ID_SPELL_LIST)};
        return CreateAcceleratorTableW(a, static_cast<int>(sizeof(a) / sizeof(a[0])));
    }

    void UpdateMenuState() {
        auto enable = [this](unsigned id, bool on) {
            EnableMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
        };
        auto check = [this](unsigned id, bool on) {
            CheckMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
        };
        enable(ID_UNDO, history_.CanUndo());
        enable(ID_REDO, history_.CanRedo());
        enable(ID_CUT, HasSelection());
        enable(ID_COPY, HasSelection());
        enable(ID_PASTE, IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE);
        for (unsigned id : {ID_SIMPLE, ID_TEX, ID_ARRAY, ID_SPELL_LIVE}) check(id, IsChecked(id));
        bool dark = dynamic_cast<DarkFactory*>(factory_.get()) != nullptr;
        check(ID_THEME_CLASSIC, !dark);
        check(ID_THEME_DARK, dark);
    }

    bool IsChecked(unsigned id) const {
        const std::wstring name = comp_->GetCompositor().Name();
        switch (id) {
            case ID_SIMPLE: return name == L"Simple";
            case ID_TEX: return name == L"TeX";
            case ID_ARRAY: return name == L"Array";
            case ID_SPELL_LIVE: return spell_.Enabled();
            default: return false;
        }
    }

    // ------------------------------------------------------------------
    //  ABSTRACT FACTORY: đổi giao diện = đổi nhà máy, dựng lại widget
    // ------------------------------------------------------------------
    void SetTheme(std::unique_ptr<GUIFactory> f) {
        factory_ = std::move(f);
        pal_ = factory_->CreatePalette();
        RebuildView();
        RebuildToolbar();
        Layout();
        Invalidate();
    }

    // DECORATOR: Border( Scroller( Composition ) )
    void RebuildView() {
        int offset = scroller_ ? scroller_->Offset() : 0;
        scroller_ = std::make_shared<Scroller>(comp_, factory_->CreateScrollBar());
        scroller_->ScrollTo(offset);
        border_ = factory_->CreateBorder(scroller_);
    }

    void RebuildToolbar() {
        struct Spec { const wchar_t* label; unsigned id; bool gap; };
        static const Spec specs[] = {
            {L"Mới", ID_NEW, false},       {L"Mở", ID_OPEN, false},     {L"Lưu", ID_SAVE, false},
            {L"Hoàn tác", ID_UNDO, true},  {L"Làm lại", ID_REDO, false},
            {L"Simple", ID_SIMPLE, true},  {L"TeX", ID_TEX, false},     {L"Array", ID_ARRAY, false},
            {L"Chính tả", ID_SPELL_LIVE, true}, {L"Giao diện", ID_THEME_TOGGLE, false},
            {L"Xuất SVG", ID_EXPORT, true}};
        tools_.clear();
        for (const auto& s : specs) tools_.push_back({factory_->CreateButton(s.label, s.id), Rect{}, s.gap});
        hot_ = pressed_ = -1;
    }

    void Layout() {
        if (!hwnd_) return;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int W = rc.right, H = rc.bottom;
        int tbH = lh_ + 16, sbH = lh_ + 10;
        toolbarRect_ = {0, 0, W, tbH};
        statusRect_ = {0, H - sbH, W, H};
        docRect_ = {8, tbH + 8, std::max(9, W - 8), std::max(tbH + 9, H - sbH - 8)};

        int x = 8;
        for (auto& t : tools_) {
            if (t.gapBefore) x += 12;
            int w = t.button->PreferredWidth(cw_);
            t.rect = {x, 6, x + w, tbH - 6};
            x += w + 4;
        }

        int in = border_->Inset();
        scroller_->SetViewport({std::max(1, docRect_.Width() - 2 * in), std::max(1, docRect_.Height() - 2 * in)});
        size_t cols = static_cast<size_t>(std::max(8, scroller_->ContentWidth() / std::max(1, cw_)));
        comp_->SetWidth(cols);  // cửa sổ đổi cỡ -> Compositor dựng lại các dòng
    }

    // ------------------------------------------------------------------
    //  Vẽ (vẽ kép vào bitmap rồi BitBlt để không nhấp nháy)
    // ------------------------------------------------------------------
    void Paint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int W = std::max<int>(1, rc.right), H = std::max<int>(1, rc.bottom);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HGDIOBJ oldFont = SelectObject(mem, font_);
        {
            Win32WindowImp imp(mem, cw_, lh_);
            window_.SetImp(&imp);  // Bridge: Window dùng hiện thực GDI
            window_.SetPalette(&pal_);

            window_.FillRect({0, 0, W, H}, pal_.chrome);
            window_.FillRect(toolbarRect_, pal_.toolbar);
            window_.FillRect({0, toolbarRect_.bottom - 1, W, toolbarRect_.bottom}, pal_.toolbarLine);
            for (size_t i = 0; i < tools_.size(); ++i) {
                Button::State st;
                st.hot = static_cast<int>(i) == hot_;
                st.pressed = static_cast<int>(i) == pressed_ && st.hot;
                st.checked = IsChecked(tools_[i].button->CommandId());
                tools_[i].button->Draw(window_, tools_[i].rect, st);
            }

            hints_.selBegin = SelBegin();
            hints_.selEnd = SelEnd();
            hints_.caret = caret_;
            hints_.caretVisible = focused_ && blinkOn_;
            hints_.misspelled = &spell_.Marks();
            comp_->SetHints(&hints_);
            border_->Draw(window_, {docRect_.left, docRect_.top});
            comp_->SetHints(nullptr);

            window_.FillRect(statusRect_, pal_.statusBg);
            window_.DrawString({10, statusRect_.top + (statusRect_.Height() - lh_) / 2 + 1}, StatusLine(),
                               pal_.statusText);
            window_.SetImp(nullptr);
        }
        BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldFont);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd_, &ps);
    }

    std::wstring StatusLine() const {
        size_t r, c;
        comp_->IndexToRowCol(caret_, r, c);
        std::wstring s = L"Dòng " + std::to_wstring(r + 1) + L", Cột " + std::to_wstring(c + 1) + L"   |   " +
                         status_.Text() + L"   |   Giao diện: " + factory_->Name() + L"   |   Chính tả: ";
        s += spell_.Enabled() ? std::to_wstring(spell_.Errors().size()) + L" từ lạ" : std::wstring(L"tắt");
        return s;
    }

    void UpdateTitle() {
        if (!hwnd_) return;
        std::wstring name = filePath_.empty() ? L"Chưa đặt tên" : filePath_.substr(filePath_.find_last_of(L"\\/") + 1);
        std::wstring title = name + (IsModified() ? L" *" : L"") + L" — Lexi (GoF Design Patterns)";
        SetWindowTextW(hwnd_, title.c_str());
    }

    void Invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void ResetBlink() {
        blinkOn_ = true;
        if (hwnd_) {
            UINT t = GetCaretBlinkTime();
            if (t == 0 || t == INFINITE) t = 530;
            SetTimer(hwnd_, kBlinkTimer, t, nullptr);
        }
        Invalidate();
    }

    // ------------------------------------------------------------------
    //  Soạn thảo
    // ------------------------------------------------------------------
    bool HasSelection() const { return caret_ != anchor_; }
    size_t SelBegin() const { return std::min(caret_, anchor_); }
    size_t SelEnd() const { return std::max(caret_, anchor_); }
    bool IsModified() const { return comp_->Text() != savedText_; }

    void SetCaret(size_t c) { caret_ = anchor_ = std::min(c, comp_->Length()); }

    void AfterEdit() {
        caret_ = std::min(caret_, comp_->Length());
        anchor_ = std::min(anchor_, comp_->Length());
        desiredCol_ = -1;
        UpdateTitle();
        EnsureCaretVisible();
        ResetBlink();
    }

    void Exec(std::unique_ptr<Command> cmd) {
        size_t c = history_.Execute(std::move(cmd));
        if (c != kPad) SetCaret(c);
        AfterEdit();
    }

    void Undo() {
        auto c = history_.Undo();
        if (!c) return;
        if (*c != kPad) SetCaret(*c);
        AfterEdit();
    }

    void Redo() {
        auto c = history_.Redo();
        if (!c) return;
        if (*c != kPad) SetCaret(*c);
        AfterEdit();
    }

    void SetCompositor(std::unique_ptr<Compositor> c) {
        if (c->Name() == comp_->GetCompositor().Name()) return;
        history_.Seal();
        Exec(std::make_unique<ChangeCompositorCommand>(*comp_, std::move(c)));
    }

    void InsertTyped(const std::wstring& s) {
        if (HasSelection()) {  // gõ đè vùng chọn = lệnh gộp Xoá + Chèn
            history_.Seal();
            auto m = std::make_unique<MacroCommand>(L"Gõ đè");
            m->Add(std::make_unique<DeleteCommand>(*comp_, SelBegin(), SelEnd() - SelBegin()));
            m->Add(std::make_unique<InsertCommand>(*comp_, SelBegin(), s));
            Exec(std::move(m));
            history_.Seal();
        } else {
            Exec(std::make_unique<InsertCommand>(*comp_, caret_, s));
        }
    }

    void DeleteSelection() {
        if (!HasSelection()) return;
        history_.Seal();
        Exec(std::make_unique<DeleteCommand>(*comp_, SelBegin(), SelEnd() - SelBegin()));
        history_.Seal();
    }

    void Backspace() {
        if (HasSelection()) DeleteSelection();
        else if (caret_ > 0) Exec(std::make_unique<DeleteCommand>(*comp_, caret_ - 1, 1));
    }

    void DeleteWordBack() {
        if (HasSelection()) { DeleteSelection(); return; }
        size_t p = PrevWord(caret_);
        if (p == caret_) return;
        history_.Seal();
        Exec(std::make_unique<DeleteCommand>(*comp_, p, caret_ - p));
        history_.Seal();
    }

    void DeleteForward(bool word) {
        if (HasSelection()) { DeleteSelection(); return; }
        if (caret_ >= comp_->Length()) return;
        size_t n = word ? std::max<size_t>(1, NextWord(caret_) - caret_) : 1;
        if (n > 1) history_.Seal();
        Exec(std::make_unique<DeleteCommand>(*comp_, caret_, n));
        if (n > 1) history_.Seal();
    }

    void MoveCaret(size_t pos, bool extend) {
        caret_ = std::min(pos, comp_->Length());
        if (!extend) anchor_ = caret_;
        desiredCol_ = -1;
        history_.Seal();
        EnsureCaretVisible();
        ResetBlink();
    }

    void MoveVertical(long long delta, bool extend) {
        size_t r, c;
        comp_->IndexToRowCol(caret_, r, c);
        if (desiredCol_ < 0) desiredCol_ = static_cast<long long>(c);
        long long nr = static_cast<long long>(r) + delta;
        size_t pos;
        if (nr < 0) pos = 0;
        else if (nr >= static_cast<long long>(comp_->RowCount())) pos = comp_->Length();
        else pos = comp_->RowColToIndex(static_cast<size_t>(nr), static_cast<size_t>(desiredCol_));
        caret_ = pos;
        if (!extend) anchor_ = caret_;
        history_.Seal();
        EnsureCaretVisible();
        ResetBlink();
    }

    long long PageRows() const { return std::max(1, scroller_->Viewport().h / std::max(1, lh_) - 1); }

    void EnsureCaretVisible() {
        size_t r, c;
        comp_->IndexToRowCol(caret_, r, c);
        scroller_->ScrollToShow(static_cast<int>(r) * lh_, lh_);
    }

    size_t HitTest(Point p) const {
        Point o = comp_->LastOrigin();
        int ry = p.y - o.y;
        size_t row = ry < 0 ? 0 : static_cast<size_t>(ry / std::max(1, lh_));
        row = std::min(row, comp_->RowCount() - 1);
        int cx = p.x - o.x;
        size_t col = cx < 0 ? 0 : static_cast<size_t>((cx + cw_ / 2) / std::max(1, cw_));
        return comp_->RowColToIndex(row, col);
    }

    size_t WordStart(size_t i) const {
        const auto& t = comp_->Text();
        while (i > 0 && text::IsWordChar(t[i - 1])) --i;
        return i;
    }
    size_t WordEnd(size_t i) const {
        const auto& t = comp_->Text();
        while (i < t.size() && text::IsWordChar(t[i])) ++i;
        return i;
    }
    size_t PrevWord(size_t i) const {
        const auto& t = comp_->Text();
        while (i > 0 && !text::IsWordChar(t[i - 1])) --i;
        return WordStart(i);
    }
    size_t NextWord(size_t i) const {
        const auto& t = comp_->Text();
        i = WordEnd(i);
        while (i < t.size() && !text::IsWordChar(t[i])) ++i;
        return i;
    }

    // ------------------------------------------------------------------
    //  Bàn phím
    // ------------------------------------------------------------------
    bool OnKeyDown(WPARAM vk) {
        bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        size_t r, c;
        switch (vk) {
            case VK_LEFT:
                if (HasSelection() && !shift) MoveCaret(SelBegin(), false);
                else MoveCaret(ctrl ? PrevWord(caret_) : (caret_ > 0 ? caret_ - 1 : 0), shift);
                return true;
            case VK_RIGHT:
                if (HasSelection() && !shift) MoveCaret(SelEnd(), false);
                else MoveCaret(ctrl ? NextWord(caret_) : caret_ + 1, shift);
                return true;
            case VK_UP: MoveVertical(-1, shift); return true;
            case VK_DOWN: MoveVertical(1, shift); return true;
            case VK_PRIOR: MoveVertical(-PageRows(), shift); return true;
            case VK_NEXT: MoveVertical(PageRows(), shift); return true;
            case VK_HOME:
                comp_->IndexToRowCol(caret_, r, c);
                MoveCaret(ctrl ? 0 : comp_->RowStart(r), shift);
                return true;
            case VK_END:
                comp_->IndexToRowCol(caret_, r, c);
                MoveCaret(ctrl ? comp_->Length() : comp_->RowEnd(r), shift);
                return true;
            case VK_DELETE:
                DeleteForward(ctrl);
                return true;
            default:
                return false;
        }
    }

    void OnChar(wchar_t ch) {
        if (ch == 8) Backspace();
        else if (ch == 0x7F) DeleteWordBack();  // Ctrl+Backspace
        else if (ch == L'\r') InsertTyped(L"\n");
        else if (ch == L'\t') InsertTyped(L"    ");
        else if (ch >= 32) InsertTyped(std::wstring(1, ch));
    }

    // ------------------------------------------------------------------
    //  Chuột
    // ------------------------------------------------------------------
    void OnLButtonDown(Point p, WPARAM wp) {
        SetFocus(hwnd_);
        for (size_t i = 0; i < tools_.size(); ++i) {
            if (tools_[i].rect.Contains(p)) {
                pressed_ = hot_ = static_cast<int>(i);
                SetCapture(hwnd_);
                Invalidate();
                return;
            }
        }
        if (scroller_->BarRect().Contains(p)) {
            scroller_->JumpToBarPosition(p.y);
            draggingBar_ = true;
            SetCapture(hwnd_);
            Invalidate();
            return;
        }
        if (docRect_.Contains(p)) {
            MoveCaret(HitTest(p), (wp & MK_SHIFT) != 0);
            dragging_ = true;
            SetCapture(hwnd_);
        }
    }

    void OnDoubleClick(Point p, WPARAM wp) {
        if (!docRect_.Contains(p) || scroller_->BarRect().Contains(p)) {
            OnLButtonDown(p, wp);
            return;
        }
        size_t idx = HitTest(p);
        size_t b = WordStart(idx), e = WordEnd(idx);
        if (b == e && e < comp_->Length()) ++e;
        anchor_ = b;
        caret_ = e;
        history_.Seal();
        ResetBlink();
    }

    void OnMouseMove(Point p) {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT t{};
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = hwnd_;
            TrackMouseEvent(&t);
            trackingMouse_ = true;
        }
        if (draggingBar_) {
            scroller_->JumpToBarPosition(p.y);
            Invalidate();
            return;
        }
        if (dragging_) {
            if (p.y < docRect_.top) scroller_->ScrollBy(-lh_);
            else if (p.y > docRect_.bottom) scroller_->ScrollBy(lh_);
            caret_ = HitTest(p);  // anchor_ giữ nguyên -> mở rộng vùng chọn
            ResetBlink();
            return;
        }
        int h = -1;
        for (size_t i = 0; i < tools_.size(); ++i)
            if (tools_[i].rect.Contains(p)) h = static_cast<int>(i);
        if (h != hot_) {
            hot_ = h;
            Invalidate();
        }
    }

    void OnLButtonUp(Point p) {
        if (dragging_ || draggingBar_) {
            dragging_ = draggingBar_ = false;
            ReleaseCapture();
            return;
        }
        if (pressed_ >= 0) {
            size_t i = static_cast<size_t>(pressed_);
            bool inside = tools_[i].rect.Contains(p);
            unsigned id = tools_[i].button->CommandId();
            pressed_ = -1;
            ReleaseCapture();
            Invalidate();
            if (inside) RunCommand(id);  // nút bấm -> Command
        }
    }

    void ShowContextMenu(int sx, int sy) {
        POINT pt{sx, sy};
        bool fromKeyboard = (sx == -1 && sy == -1);
        if (fromKeyboard) {
            size_t r, c;
            comp_->IndexToRowCol(caret_, r, c);
            Point o = comp_->LastOrigin();
            pt = {o.x + static_cast<int>(c) * cw_, o.y + static_cast<int>(r + 1) * lh_};
            ClientToScreen(hwnd_, &pt);
        } else {
            POINT cp = pt;
            ScreenToClient(hwnd_, &cp);
            Point p{static_cast<int>(cp.x), static_cast<int>(cp.y)};
            if (!docRect_.Contains(p) || scroller_->BarRect().Contains(p)) return;
            size_t idx = HitTest(p);
            if (!(HasSelection() && idx >= SelBegin() && idx < SelEnd())) MoveCaret(idx, false);
        }

        HMENU m = CreatePopupMenu();
        pendingWord_.clear();
        size_t at = kPad;
        if (spell_.IsMarked(caret_)) at = caret_;
        else if (caret_ > 0 && spell_.IsMarked(caret_ - 1)) at = caret_ - 1;
        if (at != kPad) {
            size_t b = WordStart(at), e = WordEnd(at);
            const auto& t = comp_->Text();
            while (b < e && t[b] == L'\'') ++b;
            while (e > b && t[e - 1] == L'\'') --e;
            pendingWord_ = t.substr(b, e - b);
            std::wstring label = L"Thêm \u201C" + pendingWord_ + L"\u201D vào từ điển";
            AppendMenuW(m, MF_STRING, ID_ADDWORD, label.c_str());
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        }
        UINT selFlag = HasSelection() ? MF_STRING : (MF_STRING | MF_GRAYED);
        AppendMenuW(m, selFlag, ID_CUT, L"Cắt\tCtrl+X");
        AppendMenuW(m, selFlag, ID_COPY, L"Sao chép\tCtrl+C");
        AppendMenuW(m, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : (MF_STRING | MF_GRAYED), ID_PASTE,
                    L"Dán\tCtrl+V");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_SELECTALL, L"Chọn tất cả\tCtrl+A");
        TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(m);
    }

    // ------------------------------------------------------------------
    //  Clipboard
    // ------------------------------------------------------------------
    void Copy() {
        if (!HasSelection()) return;
        std::wstring s = comp_->Text().substr(SelBegin(), SelEnd() - SelBegin()), out;
        for (wchar_t ch : s) {
            if (ch == L'\n') out += L"\r\n";
            else out += ch;
        }
        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        size_t bytes = (out.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                std::memcpy(p, out.c_str(), bytes);
                GlobalUnlock(h);
                if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
        CloseClipboard();
    }

    void Paste() {
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) return;
        std::wstring s;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
            if (p) {
                s = p;
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        s = Normalize(s);
        if (s.empty()) return;
        history_.Seal();
        InsertTyped(s);
        history_.Seal();
    }

    // ------------------------------------------------------------------
    //  Tệp
    // ------------------------------------------------------------------
    std::wstring FileDialog(bool save, const wchar_t* filter, const wchar_t* defExt, const std::wstring& initial) {
        std::vector<wchar_t> buf(4096, L'\0');
        if (!initial.empty() && initial.size() < buf.size()) std::copy(initial.begin(), initial.end(), buf.begin());
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = buf.data();
        ofn.nMaxFile = static_cast<DWORD>(buf.size());
        ofn.lpstrDefExt = defExt;
        ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
        BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        return ok ? std::wstring(buf.data()) : std::wstring();
    }

    void LoadText(const std::wstring& t, const std::wstring& path) {
        comp_->SetText(t);
        history_.Clear();
        filePath_ = path;
        savedText_ = comp_->Text();
        caret_ = anchor_ = 0;
        if (scroller_) scroller_->ScrollTo(0);
        AfterEdit();
    }

    void NewDoc() {
        if (!ConfirmDiscard()) return;
        LoadText(L"", L"");
    }

    void OpenDoc() {
        if (!ConfirmDiscard()) return;
        static const wchar_t filter[] = L"Văn bản (*.txt)\0*.txt\0Tất cả tệp (*.*)\0*.*\0";
        std::wstring path = FileDialog(false, filter, L"txt", L"");
        if (path.empty()) return;
        std::string bytes;
        if (!ReadAllBytes(path, bytes)) {
            MessageBoxW(hwnd_, (L"Không đọc được tệp:\n" + path).c_str(), L"Lexi", MB_ICONERROR);
            return;
        }
        LoadText(Normalize(DecodeText(bytes)), path);
    }

    bool SaveDoc(bool saveAs) {
        std::wstring path = filePath_;
        if (saveAs || path.empty()) {
            static const wchar_t filter[] = L"Văn bản UTF-8 (*.txt)\0*.txt\0Tất cả tệp (*.*)\0*.*\0";
            path = FileDialog(true, filter, L"txt", filePath_);
            if (path.empty()) return false;
        }
        if (!WriteAllBytes(path, EncodeText(comp_->Text()))) {
            MessageBoxW(hwnd_, (L"Không ghi được tệp:\n" + path).c_str(), L"Lexi", MB_ICONERROR);
            return false;
        }
        filePath_ = path;
        savedText_ = comp_->Text();
        UpdateTitle();
        return true;
    }

    bool ConfirmDiscard() {
        if (!IsModified()) return true;
        int r = MessageBoxW(hwnd_, L"Tài liệu đã thay đổi. Bạn có muốn lưu lại không?", L"Lexi",
                            MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDYES) return SaveDoc(false);
        return r == IDNO;
    }

    // BRIDGE: cùng cây Glyph, vẽ qua SvgWindowImp thay vì Win32WindowImp
    void ExportSvg() {
        static const wchar_t filter[] = L"Ảnh SVG (*.svg)\0*.svg\0";
        std::wstring path = FileDialog(true, filter, L"svg", L"");
        if (path.empty()) return;
        std::ostringstream os;
        ClassicFactory printFactory;
        Palette pp = printFactory.CreatePalette();
        {
            int W = static_cast<int>(comp_->Width()) * SvgWindowImp::kCharW + 40;
            int H = static_cast<int>(comp_->RowCount()) * SvgWindowImp::kLineH + 40;
            SvgWindowImp imp(os, {W, H}, pp.background);
            Window w;
            w.SetImp(&imp);
            w.SetPalette(&pp);
            comp_->Draw(w, {20, 20});
            imp.Finish();
        }
        if (!WriteAllBytes(path, os.str())) {
            MessageBoxW(hwnd_, (L"Không ghi được tệp:\n" + path).c_str(), L"Lexi", MB_ICONERROR);
            return;
        }
        MessageBoxW(hwnd_,
                    (L"Đã xuất tài liệu ra ảnh SVG:\n" + path +
                     L"\n\nCùng một cây Glyph được vẽ qua SvgWindowImp thay cho Win32WindowImp (mẫu Bridge).")
                        .c_str(),
                    L"Lexi", MB_ICONINFORMATION);
    }

    // ------------------------------------------------------------------
    //  Hộp thoại thông tin
    // ------------------------------------------------------------------
    void ShowSpellList() {
        SpellingChecker sc;  // Visitor duyệt cây Glyph bằng PreorderIterator
        Traverse(*comp_, sc);
        const auto& bad = sc.Misspelled();
        std::wstring msg = L"SpellingChecker (Visitor) đã kiểm tra " + std::to_wstring(sc.Checked()) + L" từ.\n\n";
        if (bad.empty()) {
            msg += L"Không phát hiện từ lạ.";
        } else {
            msg += L"Các từ không có trong từ điển và không phải âm tiết tiếng Việt hợp lệ (" +
                   std::to_wstring(bad.size()) + L"):\n\n";
            size_t i = 0;
            for (const auto& w : bad) {
                msg += (i++ ? L", " : L"") + w;
                if (i >= 60) { msg += L", …"; break; }
            }
            msg += L"\n\nNhấp chuột phải vào một từ bị gạch đỏ để thêm nó vào từ điển.";
        }
        MessageBoxW(hwnd_, msg.c_str(), L"Kiểm tra chính tả", MB_ICONINFORMATION);
    }

    void ShowStats() {
        WordCounter wc;
        Traverse(*comp_, wc);
        std::wstring msg = L"WordCounter (Visitor):\n\n";
        msg += L"Số dòng hiển thị:\t" + std::to_wstring(wc.rows_) + L"\n";
        msg += L"Số từ:\t\t" + std::to_wstring(wc.words_) + L"\n";
        msg += L"Ký tự (trừ khoảng trắng):\t" + std::to_wstring(wc.chars_) + L"\n";
        msg += L"Tổng ký tự:\t\t" + std::to_wstring(comp_->Length()) + L"\n\n";
        msg += L"Flyweight: toàn bộ tài liệu chỉ dùng " +
               std::to_wstring(CharacterFactory::Instance().PoolSize()) +
               L" đối tượng Character, được chia sẻ giữa mọi vị trí.";
        MessageBoxW(hwnd_, msg.c_str(), L"Thống kê", MB_ICONINFORMATION);
    }

    void ShowHistory() {
        auto lines = history_.Listing();
        std::wstring msg = L"CommandHistory (Command):\n\n";
        if (lines.empty()) msg += L"(trống)";
        size_t start = lines.size() > 30 ? lines.size() - 30 : 0;
        if (start) msg += L"…\n";
        for (size_t i = start; i < lines.size(); ++i) msg += lines[i] + L"\n";
        MessageBoxW(hwnd_, msg.c_str(), L"Lịch sử lệnh", MB_ICONINFORMATION);
    }

    void ShowAbout() {
        const wchar_t* msg =
            L"Lexi — trình soạn thảo minh hoạ mẫu thiết kế GoF\n"
            L"(case study Chương 2, \"Design Patterns\" — Gamma, Helm, Johnson, Vlissides)\n\n"
            L"Composite\tGlyph, Character, Row, Column, Composition\n"
            L"Flyweight\tCharacterFactory chia sẻ Character\n"
            L"Strategy\tCompositor: Simple / TeX / Array\n"
            L"Decorator\tBorder, Scroller bọc Composition\n"
            L"Abstract Factory\tClassicFactory / DarkFactory\n"
            L"Bridge\t\tWindow + Win32WindowImp / SvgWindowImp\n"
            L"Command\tInsert, Delete, Macro, SimpleCommand, Undo/Redo\n"
            L"Iterator\tPreorderIterator, ListIterator, NullIterator\n"
            L"Visitor\t\tSpellingChecker, WordCounter\n"
            L"Singleton\tDictionary, CharacterFactory\n"
            L"Observer\tStatusBar, LiveSpellChecker\n"
            L"Template Method\tScrollBar::Draw, Border::Draw";
        MessageBoxW(hwnd_, msg, L"Về Lexi", MB_ICONINFORMATION);
    }

    // ------------------------------------------------------------------
    //  Dữ liệu
    // ------------------------------------------------------------------
    struct ToolButton {
        std::unique_ptr<Button> button;  // sản phẩm của Abstract Factory
        Rect rect;
        bool gapBefore;
    };

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HMENU menu_ = nullptr;
    HACCEL accel_ = nullptr;
    HFONT font_ = nullptr;
    int cw_ = 8, lh_ = 18;

    std::shared_ptr<Composition> comp_;
    StatusBar status_;
    LiveSpellChecker spell_;
    CommandHistory history_;
    std::unique_ptr<GUIFactory> factory_;
    Palette pal_{};
    std::shared_ptr<Scroller> scroller_;
    std::shared_ptr<Border> border_;
    Window window_;
    Composition::RenderHints hints_;

    std::vector<ToolButton> tools_;
    int hot_ = -1, pressed_ = -1;
    bool trackingMouse_ = false;
    Rect toolbarRect_, docRect_, statusRect_;

    size_t caret_ = 0, anchor_ = 0;
    long long desiredCol_ = -1;
    bool focused_ = false, blinkOn_ = true, dragging_ = false, draggingBar_ = false;

    std::map<unsigned, std::unique_ptr<Command>> actions_;
    std::wstring filePath_, savedText_, pendingWord_;
};

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    LexiApp app;
    if (!app.Create(inst, show)) {
        MessageBoxW(nullptr, L"Không tạo được cửa sổ.", L"Lexi", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
