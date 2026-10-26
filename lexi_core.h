// ============================================================================
//  lexi_core.h — Lõi trình soạn thảo Lexi, độc lập nền tảng
//  Theo case study "Lexi" (Chương 2) trong "Design Patterns" — Gang of Four.
//
//  Bài toán thiết kế của Lexi          -> Mẫu
//  ------------------------------------------------------------------------
//  Cấu trúc tài liệu                   -> Composite  (Glyph/Row/Column/Composition)
//  Mỗi ký tự là một đối tượng          -> Flyweight  (Character + CharacterFactory)
//  Định dạng / ngắt dòng               -> Strategy   (Compositor)
//  Tô điểm giao diện                   -> Decorator  (Border, Scroller)
//  Nhiều chuẩn look-and-feel           -> Abstract Factory (GUIFactory)
//  Nhiều hệ thống cửa sổ               -> Bridge     (Window / WindowImp)
//  Thao tác người dùng, Undo/Redo      -> Command    (+ MacroCommand, SimpleCommand)
//  Duyệt cấu trúc                      -> Iterator   (PreorderIterator)
//  Phân tích (chính tả, đếm từ)        -> Visitor    (SpellingChecker, WordCounter)
//  Đối tượng duy nhất                  -> Singleton  (Dictionary, CharacterFactory)
//  Cập nhật tự động                    -> Observer   (StatusBar, LiveSpellChecker)
//  Khung thuật toán cố định            -> Template Method (ScrollBar::Draw, Border::Draw)
//
//  Phần phụ thuộc Windows nằm ở lexi_win32.cpp (Win32WindowImp + ứng dụng).
// ============================================================================
#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lexi {

constexpr size_t kPad = static_cast<size_t>(-1);  // khoảng trắng do canh lề thêm vào

// ----------------------------------------------------------------------------
//  Kiểu hình học & màu
// ----------------------------------------------------------------------------
struct Point { int x = 0, y = 0; };
struct Size { int w = 0, h = 0; };

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool Contains(Point p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
    Rect Intersect(const Rect& o) const {
        return {std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom)};
    }
};

struct Color { std::uint8_t r = 0, g = 0, b = 0; };
constexpr Color Rgb(int r, int g, int b) {
    return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
}

// ----------------------------------------------------------------------------
//  Tiện ích văn bản (Unicode, tiếng Việt)
// ----------------------------------------------------------------------------
namespace text {

inline wchar_t ToLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return static_cast<wchar_t>(c + 32);   // À..Þ
    if (c == 0x102 || c == 0x110 || c == 0x128 || c == 0x168) return static_cast<wchar_t>(c + 1);  // Ă Đ Ĩ Ũ
    if (c == 0x1A0) return 0x1A1;                                                  // Ơ
    if (c == 0x1AF) return 0x1B0;                                                  // Ư
    if (c >= 0x1EA0 && c <= 0x1EF9 && (c % 2 == 0)) return static_cast<wchar_t>(c + 1);  // Ạ..Ỹ
    return c;
}

inline std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = ToLower(c);
    return s;
}

inline bool IsLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x2000);
}
inline bool IsWordChar(wchar_t c) { return IsLetter(c) || c == L'\''; }

// Tìm các từ [b, e) trong chuỗi, bỏ dấu nháy ở hai đầu.
inline std::vector<std::pair<size_t, size_t>> FindWords(const std::wstring& s) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && !IsWordChar(s[i])) ++i;
        size_t b = i;
        while (i < s.size() && IsWordChar(s[i])) ++i;
        size_t e = i;
        while (b < e && s[b] == L'\'') ++b;
        while (e > b && s[e - 1] == L'\'') --e;
        if (e > b) out.emplace_back(b, e);
    }
    return out;
}

inline std::string ToUtf8(const std::wstring& w) {
    std::string o;
    for (size_t i = 0; i < w.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            std::uint32_t lo = static_cast<std::uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
        }
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return o;
}

inline std::wstring FromUtf8(const std::string& s) {
    std::wstring o;
    size_t i = 0;
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
        i = 3;  // bỏ BOM
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { o += static_cast<wchar_t>(0xFFFD); ++i; continue; }
        if (i + len > s.size()) { o += static_cast<wchar_t>(0xFFFD); break; }
        for (size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            o += static_cast<wchar_t>(0xD800 + (cp >> 10));
            o += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            o += static_cast<wchar_t>(cp);
        }
    }
    return o;
}

// ---- Kiểm tra một âm tiết tiếng Việt có hợp cấu trúc hay không -------------
//  âm đầu + vần (nguyên âm) + âm cuối, tối đa một dấu thanh.
inline bool IsVietnameseSyllable(const std::wstring& lowerWord) {
    struct ToneRow { const wchar_t* toned; wchar_t base; };
    static const ToneRow kTones[] = {
        {L"àáảãạ", L'a'},      {L"ằắẳẵặ", L'ă'},
        {L"ầấẩẫậ", L'â'}, {L"èéẻẽẹ", L'e'},
        {L"ềếểễệ", L'ê'}, {L"ìíỉĩị", L'i'},
        {L"òóỏõọ", L'o'},      {L"ồốổỗộ", L'ô'},
        {L"ờớởỡợ", L'ơ'}, {L"ùúủũụ", L'u'},
        {L"ừứửữự", L'ư'}, {L"ỳýỷỹỵ", L'y'},
    };
    static const std::wstring kAllowed = L"abcdeghiklmnopqrstuvxyăâêôơưđ";
    static const std::vector<std::wstring> kInitials = {
        L"ngh", L"ng", L"nh", L"ch", L"gh", L"gi", L"kh", L"ph", L"qu", L"th", L"tr", L"b", L"c", L"d",
        L"đ", L"g", L"h", L"k", L"l", L"m", L"n", L"p", L"r", L"s", L"t", L"v", L"x", L""};
    static const std::vector<std::wstring> kFinals = {L"ch", L"ng", L"nh", L"c", L"m", L"n", L"p", L"t", L""};
    static const std::set<std::wstring> kNuclei = {
        L"a", L"ă", L"â", L"e", L"ê", L"i", L"o", L"ô", L"ơ", L"u", L"ư", L"y",
        L"ai", L"ao", L"au", L"ay", L"âu", L"ây", L"eo", L"êu", L"ia", L"iê", L"iu",
        L"oa", L"oă", L"oe", L"oi", L"ôi", L"ơi", L"oo", L"ua", L"uâ", L"uê", L"ui",
        L"uô", L"uơ", L"uy", L"ưa", L"ưi", L"ươ", L"ưu", L"yê",
        L"iêu", L"yêu", L"oai", L"oay", L"oeo", L"uây", L"uôi", L"ươi",
        L"ươu", L"uya", L"uyê", L"uyu"};

    std::wstring s;
    int tones = 0;
    for (wchar_t c : lowerWord) {
        wchar_t base = c;
        for (const auto& row : kTones) {
            for (const wchar_t* t = row.toned; *t; ++t)
                if (*t == c) { base = row.base; ++tones; }
        }
        if (kAllowed.find(base) == std::wstring::npos) return false;
        s += base;
    }
    if (s.empty() || tones > 1) return false;
    for (const auto& ini : kInitials) {
        if (s.compare(0, ini.size(), ini) != 0) continue;
        for (const auto& fin : kFinals) {
            if (ini.size() + fin.size() >= s.size()) continue;
            if (s.compare(s.size() - fin.size(), fin.size(), fin) != 0) continue;
            if (kNuclei.count(s.substr(ini.size(), s.size() - ini.size() - fin.size()))) return true;
        }
    }
    return false;
}

}  // namespace text

// ============================================================================
//  BRIDGE — phía hiện thực: WindowImp (mỗi nền tảng một lớp con)
// ============================================================================
class WindowImp {
public:
    virtual ~WindowImp() = default;
    virtual void DeviceFillRect(const Rect& r, Color c) = 0;
    virtual void DeviceLine(Point a, Point b, Color c) = 0;
    virtual void DeviceText(int x, int y, const std::wstring& s, Color c) = 0;
    virtual void DevicePushClip(const Rect& r) = 0;
    virtual void DevicePopClip() = 0;
    virtual int DeviceCharWidth() const = 0;
    virtual int DeviceLineHeight() const = 0;
};

// Bảng màu — một "sản phẩm" của Abstract Factory
struct Palette {
    Color background, text, selectionBg, selectionText, error, caret;
    Color chrome, toolbar, toolbarLine, statusBg, statusText;
    Color frameDark, frameLight, buttonFace, buttonHot, buttonChecked, buttonText, accent;
    Color scrollTrack, scrollThumb, scrollThumbEdge;
};

// ============================================================================
//  BRIDGE — phía trừu tượng: Window. Glyph chỉ biết Window, không biết Win32.
// ============================================================================
class Window {
public:
    struct TextStyle { bool selected = false; bool misspelled = false; };

    void SetImp(WindowImp* imp) { imp_ = imp; clips_.clear(); }
    void SetPalette(const Palette* p) { palette_ = p; }
    const Palette& Pal() const { return *palette_; }
    int CharWidth() const { return imp_->DeviceCharWidth(); }
    int LineHeight() const { return imp_->DeviceLineHeight(); }

    void FillRect(const Rect& r, Color c) { imp_->DeviceFillRect(r, c); }
    void FrameRect(const Rect& r, Color c) { Draw3DFrame(r, c, c); }
    void Draw3DFrame(const Rect& r, Color topLeft, Color bottomRight) {
        FillRect({r.left, r.top, r.right, r.top + 1}, topLeft);
        FillRect({r.left, r.top, r.left + 1, r.bottom}, topLeft);
        FillRect({r.left, r.bottom - 1, r.right, r.bottom}, bottomRight);
        FillRect({r.right - 1, r.top, r.right, r.bottom}, bottomRight);
    }
    void DrawLine(Point a, Point b, Color c) { imp_->DeviceLine(a, b, c); }
    void DrawString(Point p, const std::wstring& s, Color c) { imp_->DeviceText(p.x, p.y, s, c); }
    void DrawCenteredString(const Rect& r, const std::wstring& s, Color c) {
        int w = static_cast<int>(s.size()) * CharWidth();
        DrawString({r.left + (r.Width() - w) / 2, r.top + (r.Height() - LineHeight()) / 2}, s, c);
    }

    void PushClip(const Rect& r) {
        clips_.push_back(clips_.empty() ? r : clips_.back().Intersect(r));
        imp_->DevicePushClip(r);
    }
    void PopClip() {
        if (clips_.empty()) return;
        clips_.pop_back();
        imp_->DevicePopClip();
    }
    Rect ClipRect() const { return clips_.empty() ? Rect{INT_MIN / 2, INT_MIN / 2, INT_MAX / 2, INT_MAX / 2} : clips_.back(); }

    void SetTextStyle(TextStyle s) { style_ = s; }

    // Vẽ một ô ký tự theo kiểu hiện hành (Character::Draw gọi hàm này)
    void DrawStyledChar(wchar_t ch, Point p) {
        const Palette& P = Pal();
        int cw = CharWidth(), lh = LineHeight();
        if (style_.selected) FillRect({p.x, p.y, p.x + cw, p.y + lh}, P.selectionBg);
        if (ch != L' ') imp_->DeviceText(p.x, p.y, std::wstring(1, ch), style_.selected ? P.selectionText : P.text);
        if (style_.misspelled) {  // gạch chân lượn sóng
            int y = p.y + lh - 3;
            for (int x = p.x; x < p.x + cw; x += 2) {
                int up = ((x / 2) % 2) ? 0 : 2;
                DrawLine({x, y + up}, {x + 2, y + 2 - up}, P.error);
            }
        }
    }

private:
    WindowImp* imp_ = nullptr;
    const Palette* palette_ = nullptr;
    std::vector<Rect> clips_;
    TextStyle style_;
};

// ============================================================================
//  VISITOR & ITERATOR — khai báo chung
// ============================================================================
class Glyph;
class Character;
class Row;
class Column;
using GlyphPtr = std::shared_ptr<Glyph>;

class GlyphVisitor {
public:
    virtual ~GlyphVisitor() = default;
    virtual void VisitCharacter(Character&) {}
    virtual void VisitRow(Row&) {}
    virtual void VisitColumn(Column&) {}
};

template <class Item>
class Iterator {
public:
    virtual ~Iterator() = default;
    virtual void First() = 0;
    virtual void Next() = 0;
    virtual bool IsDone() const = 0;
    virtual Item CurrentItem() const = 0;
};

template <class Item>
class NullIterator : public Iterator<Item> {
public:
    void First() override {}
    void Next() override {}
    bool IsDone() const override { return true; }
    Item CurrentItem() const override { throw std::out_of_range("NullIterator"); }
};

class ListIterator : public Iterator<GlyphPtr> {
public:
    explicit ListIterator(const std::vector<GlyphPtr>& list) : list_(list) {}
    void First() override { current_ = 0; }
    void Next() override { ++current_; }
    bool IsDone() const override { return current_ >= list_.size(); }
    GlyphPtr CurrentItem() const override {
        if (IsDone()) throw std::out_of_range("ListIterator");
        return list_[current_];
    }
private:
    const std::vector<GlyphPtr>& list_;
    size_t current_ = 0;
};

// ============================================================================
//  COMPOSITE — Glyph
// ============================================================================
class Glyph {
public:
    virtual ~Glyph() = default;
    virtual void Draw(Window& w, Point p) const = 0;
    virtual Size Bounds(const Window& w) const = 0;
    virtual void Accept(GlyphVisitor& v) = 0;

    virtual void Insert(GlyphPtr, size_t) { throw std::logic_error("Glyph lá không có con"); }
    virtual void Remove(size_t) { throw std::logic_error("Glyph lá không có con"); }
    virtual GlyphPtr Child(size_t) const { return nullptr; }
    virtual size_t ChildCount() const { return 0; }
    virtual std::unique_ptr<Iterator<GlyphPtr>> CreateIterator() const {
        return std::make_unique<NullIterator<GlyphPtr>>();
    }
};

// ---- FLYWEIGHT: Character chỉ giữ trạng thái nội tại (mã ký tự).
//      Vị trí, màu, bôi chọn... là trạng thái ngoại lai do Composition cung cấp.
class Character : public Glyph {
public:
    explicit Character(wchar_t c) : ch_(c) {}
    wchar_t Value() const { return ch_; }
    void Draw(Window& w, Point p) const override { w.DrawStyledChar(ch_, p); }
    Size Bounds(const Window& w) const override { return {w.CharWidth(), w.LineHeight()}; }
    void Accept(GlyphVisitor& v) override { v.VisitCharacter(*this); }
private:
    wchar_t ch_;
};

// Flyweight factory + SINGLETON
class CharacterFactory {
public:
    static CharacterFactory& Instance() {
        static CharacterFactory instance;
        return instance;
    }
    std::shared_ptr<Character> Get(wchar_t c) {
        auto it = pool_.find(c);
        if (it != pool_.end()) return it->second;
        auto ch = std::make_shared<Character>(c);
        pool_.emplace(c, ch);
        return ch;
    }
    size_t PoolSize() const { return pool_.size(); }
    CharacterFactory(const CharacterFactory&) = delete;
    CharacterFactory& operator=(const CharacterFactory&) = delete;
private:
    CharacterFactory() = default;
    std::unordered_map<wchar_t, std::shared_ptr<Character>> pool_;
};

class CompositeGlyph : public Glyph {
public:
    void Insert(GlyphPtr g, size_t i) override {
        i = std::min(i, children_.size());
        children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(i), std::move(g));
    }
    void Remove(size_t i) override {
        if (i < children_.size()) children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    GlyphPtr Child(size_t i) const override { return i < children_.size() ? children_[i] : nullptr; }
    size_t ChildCount() const override { return children_.size(); }
    std::unique_ptr<Iterator<GlyphPtr>> CreateIterator() const override {
        return std::make_unique<ListIterator>(children_);
    }
protected:
    std::vector<GlyphPtr> children_;
};

class Row : public CompositeGlyph {
public:
    void Draw(Window& w, Point p) const override {
        for (auto& g : children_) {
            g->Draw(w, p);
            p.x += g->Bounds(w).w;
        }
    }
    Size Bounds(const Window& w) const override {
        Size s{0, w.LineHeight()};
        for (auto& g : children_) s.w += g->Bounds(w).w;
        return s;
    }
    void Accept(GlyphVisitor& v) override { v.VisitRow(*this); }
};

class Column : public CompositeGlyph {
public:
    void Draw(Window& w, Point p) const override {
        Rect clip = w.ClipRect();
        for (auto& g : children_) {
            Size s = g->Bounds(w);
            if (p.y + s.h >= clip.top && p.y <= clip.bottom) g->Draw(w, p);
            p.y += s.h;
        }
    }
    Size Bounds(const Window& w) const override {
        Size s;
        for (auto& g : children_) {
            Size c = g->Bounds(w);
            s.w = std::max(s.w, c.w);
            s.h += c.h;
        }
        return s;
    }
    void Accept(GlyphVisitor& v) override { v.VisitColumn(*this); }
};

// ---- ITERATOR duyệt tiền thứ tự (ngăn xếp iterator, như trong sách) ---------
class PreorderIterator : public Iterator<GlyphPtr> {
public:
    explicit PreorderIterator(const Glyph& root) : root_(root) {}
    void First() override {
        stack_.clear();
        auto it = root_.CreateIterator();
        it->First();
        stack_.push_back(std::move(it));
        PopFinished();
    }
    void Next() override {
        if (IsDone()) return;
        auto it = stack_.back()->CurrentItem()->CreateIterator();
        it->First();
        stack_.push_back(std::move(it));
        PopFinished();
    }
    bool IsDone() const override { return stack_.empty(); }
    GlyphPtr CurrentItem() const override {
        if (IsDone()) throw std::out_of_range("PreorderIterator");
        return stack_.back()->CurrentItem();
    }
private:
    void PopFinished() {
        while (!stack_.empty() && stack_.back()->IsDone()) {
            stack_.pop_back();
            if (!stack_.empty()) stack_.back()->Next();
        }
    }
    const Glyph& root_;
    std::vector<std::unique_ptr<Iterator<GlyphPtr>>> stack_;
};

inline void Traverse(const Glyph& root, GlyphVisitor& v) {
    PreorderIterator it(root);
    for (it.First(); !it.IsDone(); it.Next()) it.CurrentItem()->Accept(v);
}

// ============================================================================
//  STRATEGY — Compositor: thuật toán ngắt dòng
//  Mỗi dòng kết quả ghi lại chỉ số ký tự nguồn (hoặc kPad), nhờ đó con trỏ,
//  vùng chọn và gạch lỗi chính tả ánh xạ được về văn bản gốc.
// ============================================================================
struct ComposedLine {
    size_t start = 0;            // chỉ số văn bản nơi dòng bắt đầu
    std::vector<size_t> src;     // mỗi ô: chỉ số ký tự nguồn hoặc kPad
};

class Compositor {
public:
    virtual ~Compositor() = default;
    virtual std::wstring Name() const = 0;
    virtual std::vector<ComposedLine> Compose(const std::wstring& t, size_t width) const = 0;

protected:
    struct Segment { size_t b, we, se; };  // từ [b, we) + khoảng trắng [we, se)
    struct Paragraph { size_t start; std::vector<Segment> segs; };

    static std::vector<Paragraph> Split(const std::wstring& t, size_t width) {
        std::vector<Paragraph> paras;
        size_t ps = 0;
        for (;;) {
            size_t pe = t.find(L'\n', ps);
            if (pe == std::wstring::npos) pe = t.size();
            Paragraph p{ps, {}};
            size_t i = ps;
            while (i < pe) {
                size_t b = i;
                while (i < pe && t[i] != L' ') ++i;
                size_t we = i;
                while (i < pe && t[i] == L' ') ++i;
                size_t se = i;
                if (we - b > width) {  // từ quá dài: cắt nhỏ
                    for (size_t c = b; c < we; c += width) {
                        size_t ce = std::min(c + width, we);
                        p.segs.push_back({c, ce, ce == we ? se : ce});
                    }
                } else {
                    p.segs.push_back({b, we, se});
                }
            }
            paras.push_back(std::move(p));
            if (pe == t.size()) break;
            ps = pe + 1;
        }
        return paras;
    }

    static size_t VisibleLength(const std::vector<Segment>& segs, size_t i, size_t j) {
        size_t len = 0;
        for (size_t k = i; k < j; ++k) len += (k + 1 == j) ? segs[k].we - segs[k].b : segs[k].se - segs[k].b;
        return len;
    }

    // pads[k] = số khoảng trắng chèn thêm sau đoạn k
    static ComposedLine MakeLine(const std::vector<Segment>& segs, size_t i, size_t j,
                                 const std::vector<size_t>& pads = {}) {
        ComposedLine l;
        l.start = segs[i].b;
        for (size_t k = i; k < j; ++k) {
            for (size_t c = segs[k].b; c < segs[k].se; ++c) l.src.push_back(c);
            if (!pads.empty()) l.src.insert(l.src.end(), pads[k - i], kPad);
        }
        return l;
    }
};

// Tham lam, canh trái (giống Notepad bật "Word Wrap")
class SimpleCompositor : public Compositor {
public:
    std::wstring Name() const override { return L"Simple"; }
    std::vector<ComposedLine> Compose(const std::wstring& t, size_t width) const override {
        std::vector<ComposedLine> out;
        for (auto& p : Split(t, width)) {
            if (p.segs.empty()) { out.push_back({p.start, {}}); continue; }
            size_t i = 0, len = 0;
            for (size_t k = 0; k < p.segs.size(); ++k) {
                size_t word = p.segs[k].we - p.segs[k].b;
                if (k > i && len + word > width) {
                    out.push_back(MakeLine(p.segs, i, k));
                    i = k;
                    len = 0;
                }
                len += p.segs[k].se - p.segs[k].b;
            }
            out.push_back(MakeLine(p.segs, i, p.segs.size()));
        }
        return out;
    }
};

// Kiểu TeX: tối ưu cả đoạn bằng quy hoạch động (cực tiểu tổng bình phương
// khoảng thừa), rồi canh đều hai bên.
class TeXCompositor : public Compositor {
public:
    std::wstring Name() const override { return L"TeX"; }
    std::vector<ComposedLine> Compose(const std::wstring& t, size_t width) const override {
        std::vector<ComposedLine> out;
        for (auto& p : Split(t, width)) {
            const auto& s = p.segs;
            size_t n = s.size();
            if (n == 0) { out.push_back({p.start, {}}); continue; }
            std::vector<double> cost(n + 1, 1e300);
            std::vector<size_t> next(n + 1, n);
            cost[n] = 0;
            for (size_t i = n; i-- > 0;) {
                for (size_t j = i + 1; j <= n; ++j) {
                    size_t len = VisibleLength(s, i, j);
                    if (len > width && j > i + 1) break;
                    double slack = (j == n) ? 0.0 : static_cast<double>(width > len ? width - len : 0);
                    double c = slack * slack + cost[j];
                    if (c < cost[i]) { cost[i] = c; next[i] = j; }
                }
            }
            for (size_t i = 0; i < n;) {
                size_t j = next[i];
                if (j == n || j == i + 1) {
                    out.push_back(MakeLine(s, i, j));
                } else {  // canh đều: rải khoảng trắng vào các khe
                    size_t len = VisibleLength(s, i, j);
                    size_t extra = width > len ? width - len : 0, gaps = j - i - 1;
                    std::vector<size_t> pads(j - i, 0);
                    for (size_t g = 0; g < gaps; ++g) pads[g] = extra / gaps + (g < extra % gaps ? 1 : 0);
                    out.push_back(MakeLine(s, i, j, pads));
                }
                i = j;
            }
        }
        return out;
    }
};

// Mỗi dòng đúng N từ (ArrayCompositor trong sách: số phần tử cố định mỗi hàng)
class ArrayCompositor : public Compositor {
public:
    explicit ArrayCompositor(size_t perRow = 5) : perRow_(perRow) {}
    std::wstring Name() const override { return L"Array"; }
    std::vector<ComposedLine> Compose(const std::wstring& t, size_t width) const override {
        std::vector<ComposedLine> out;
        for (auto& p : Split(t, width)) {
            if (p.segs.empty()) { out.push_back({p.start, {}}); continue; }
            for (size_t i = 0; i < p.segs.size(); i += perRow_)
                out.push_back(MakeLine(p.segs, i, std::min(p.segs.size(), i + perRow_)));
        }
        return out;
    }
private:
    size_t perRow_;
};

// ============================================================================
//  OBSERVER
// ============================================================================
class Subject;
class Observer {
public:
    virtual ~Observer() = default;
    virtual void Update(Subject& changed) = 0;
};

class Subject {
public:
    virtual ~Subject() = default;
    void Attach(Observer* o) { observers_.push_back(o); }
    void Detach(Observer* o) { observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end()); }
    void Notify() { for (auto* o : observers_) o->Update(*this); }
private:
    std::vector<Observer*> observers_;
};

// ============================================================================
//  Composition — Column gồm các Row do Compositor dựng (Composite + Strategy),
//  đồng thời là Subject.
// ============================================================================
class Composition : public Column, public Subject {
public:
    struct RenderHints {
        size_t selBegin = 0, selEnd = 0, caret = 0;
        bool caretVisible = false;
        const std::vector<bool>* misspelled = nullptr;
    };

    Composition(std::unique_ptr<Compositor> compositor, size_t width)
        : compositor_(std::move(compositor)), width_(std::max<size_t>(1, width)) { Repair(); }

    const std::wstring& Text() const { return text_; }
    size_t Length() const { return text_.size(); }

    void SetText(std::wstring t) { text_ = std::move(t); Repair(); }
    void InsertText(size_t pos, const std::wstring& s) {
        pos = std::min(pos, text_.size());
        text_.insert(pos, s);
        Repair();
    }
    std::wstring EraseText(size_t pos, size_t n) {
        pos = std::min(pos, text_.size());
        n = std::min(n, text_.size() - pos);
        std::wstring removed = text_.substr(pos, n);
        text_.erase(pos, n);
        Repair();
        return removed;
    }
    std::unique_ptr<Compositor> SetCompositor(std::unique_ptr<Compositor> c) {
        std::swap(compositor_, c);
        Repair();
        return c;
    }
    const Compositor& GetCompositor() const { return *compositor_; }
    void SetWidth(size_t w) {
        w = std::max<size_t>(1, w);
        if (w == width_) return;
        width_ = w;
        Repair();
    }
    size_t Width() const { return width_; }
    size_t RowCount() const { return lines_.size(); }

    size_t RowStart(size_t r) const { return lines_[std::min(r, lines_.size() - 1)].start; }
    size_t RowEnd(size_t r) const {
        r = std::min(r, lines_.size() - 1);
        const auto& L = lines_[r];
        size_t e = L.start;
        for (size_t k = L.src.size(); k-- > 0;)
            if (L.src[k] != kPad) { e = L.src[k] + 1; break; }
        if (r + 1 < lines_.size() && lines_[r + 1].start == e && e > L.start) --e;
        return e;
    }

    // Chỉ số ký tự -> (dòng, cột) trên màn hình
    void IndexToRowCol(size_t idx, size_t& row, size_t& col) const {
        row = 0;
        for (size_t r = 0; r < lines_.size(); ++r) {
            if (lines_[r].start <= idx) row = r;
            else break;
        }
        const auto& L = lines_[row];
        col = 0;
        for (size_t k = 0; k < L.src.size(); ++k) {
            if (L.src[k] == kPad) continue;
            if (L.src[k] >= idx) { col = k; return; }
            col = k + 1;
        }
    }

    // (dòng, cột) -> chỉ số ký tự
    size_t RowColToIndex(size_t row, size_t col) const {
        row = std::min(row, lines_.size() - 1);
        const auto& L = lines_[row];
        for (size_t k = col; k < L.src.size(); ++k)
            if (L.src[k] != kPad) return std::min(L.src[k], RowEnd(row));
        return RowEnd(row);
    }

    void SetHints(const RenderHints* h) { hints_ = h; }
    Point LastOrigin() const { return lastOrigin_; }

    void Draw(Window& w, Point p) const override {
        if (hints_) lastOrigin_ = p;  // chỉ ghi khi vẽ lên màn hình (phục vụ hit-test)
        int cw = w.CharWidth(), lh = w.LineHeight();
        Rect clip = w.ClipRect();
        size_t selB = hints_ ? hints_->selBegin : 0, selE = hints_ ? hints_->selEnd : 0;
        const std::vector<bool>* mis = hints_ ? hints_->misspelled : nullptr;
        for (size_t r = 0; r < lines_.size(); ++r) {
            int y = p.y + static_cast<int>(r) * lh;
            if (y + lh < clip.top) continue;
            if (y > clip.bottom) break;
            const auto& L = lines_[r];
            const Glyph& row = *children_[r];
            for (size_t k = 0; k < L.src.size(); ++k) {
                size_t idx = L.src[k];
                Window::TextStyle st;
                if (idx != kPad) {
                    st.selected = idx >= selB && idx < selE;
                    st.misspelled = mis && idx < mis->size() && (*mis)[idx];
                } else {  // khoảng canh lề: được chọn nếu nằm giữa vùng chọn
                    size_t a = kPad, b = kPad;
                    for (size_t q = k; q-- > 0;) if (L.src[q] != kPad) { a = L.src[q]; break; }
                    for (size_t q = k + 1; q < L.src.size(); ++q) if (L.src[q] != kPad) { b = L.src[q]; break; }
                    st.selected = a != kPad && b != kPad && a >= selB && b < selE;
                }
                w.SetTextStyle(st);
                row.Child(k)->Draw(w, {p.x + static_cast<int>(k) * cw, y});
            }
            // vùng chọn chứa ký tự xuống dòng: tô thêm một ô ở cuối dòng
            size_t e = RowEnd(r);
            if (selE > selB && e < text_.size() && text_[e] == L'\n' && e >= selB && e < selE) {
                size_t rr, cc;
                IndexToRowCol(e, rr, cc);
                w.FillRect({p.x + static_cast<int>(cc) * cw, y, p.x + static_cast<int>(cc + 1) * cw, y + lh},
                           w.Pal().selectionBg);
            }
        }
        w.SetTextStyle({});
        if (hints_ && hints_->caretVisible) {
            size_t r, c;
            IndexToRowCol(hints_->caret, r, c);
            int x = p.x + static_cast<int>(c) * cw, y = p.y + static_cast<int>(r) * lh;
            w.FillRect({x, y, x + 2, y + lh}, w.Pal().caret);
        }
    }

    Size Bounds(const Window& w) const override {
        Size s = Column::Bounds(w);
        s.w = std::max(s.w, static_cast<int>(width_) * w.CharWidth());
        s.h = std::max(s.h, w.LineHeight());
        return s;
    }

private:
    void Repair() {
        lines_ = compositor_->Compose(text_, width_);
        if (lines_.empty()) lines_.push_back({0, {}});
        children_.clear();
        auto& factory = CharacterFactory::Instance();
        for (auto& L : lines_) {
            auto row = std::make_shared<Row>();
            for (size_t idx : L.src) row->Insert(factory.Get(idx == kPad ? L' ' : text_[idx]), row->ChildCount());
            children_.push_back(row);
        }
        Notify();
    }

    std::wstring text_;
    std::unique_ptr<Compositor> compositor_;
    size_t width_;
    std::vector<ComposedLine> lines_;
    const RenderHints* hints_ = nullptr;
    mutable Point lastOrigin_;
};

// ============================================================================
//  ABSTRACT FACTORY — các sản phẩm giao diện
// ============================================================================

// ScrollBar: TEMPLATE METHOD — Draw() cố định, lớp con vẽ rãnh và con trượt
class ScrollBar {
public:
    virtual ~ScrollBar() = default;
    virtual int Width() const = 0;
    void Draw(Window& w, const Rect& r, int total, int offset, int visible) const {
        DrawTrack(w, r);
        DrawThumb(w, ThumbRect(r, total, offset, visible));
    }
    static Rect ThumbRect(const Rect& r, int total, int offset, int visible) {
        int h = r.Height();
        if (total <= visible || total <= 0) return {r.left, r.top, r.right, r.bottom};
        int th = std::max(20, static_cast<int>(static_cast<long long>(h) * visible / total));
        th = std::min(th, h);
        int pos = static_cast<int>(static_cast<long long>(h - th) * offset / std::max(1, total - visible));
        return {r.left, r.top + pos, r.right, r.top + pos + th};
    }
protected:
    virtual void DrawTrack(Window& w, const Rect& r) const = 0;
    virtual void DrawThumb(Window& w, const Rect& r) const = 0;
};

class ClassicScrollBar : public ScrollBar {
public:
    int Width() const override { return 17; }
protected:
    void DrawTrack(Window& w, const Rect& r) const override { w.FillRect(r, w.Pal().scrollTrack); }
    void DrawThumb(Window& w, const Rect& r) const override {
        Rect t{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        w.FillRect(t, w.Pal().scrollThumb);
        w.Draw3DFrame(t, w.Pal().frameLight, w.Pal().scrollThumbEdge);
    }
};

class FlatScrollBar : public ScrollBar {
public:
    int Width() const override { return 12; }
protected:
    void DrawTrack(Window& w, const Rect& r) const override { w.FillRect(r, w.Pal().scrollTrack); }
    void DrawThumb(Window& w, const Rect& r) const override {
        w.FillRect({r.left + 3, r.top + 2, r.right - 3, r.bottom - 2}, w.Pal().scrollThumb);
    }
};

class Button {
public:
    struct State { bool hot = false, pressed = false, checked = false; };
    Button(std::wstring label, unsigned commandId) : label_(std::move(label)), id_(commandId) {}
    virtual ~Button() = default;
    virtual void Draw(Window& w, const Rect& r, State s) const = 0;
    const std::wstring& Label() const { return label_; }
    unsigned CommandId() const { return id_; }
    int PreferredWidth(int charWidth) const { return static_cast<int>(label_.size()) * charWidth + 20; }
protected:
    std::wstring label_;
    unsigned id_;  // lệnh (Command) mà nút kích hoạt
};

class ClassicButton : public Button {
public:
    using Button::Button;
    void Draw(Window& w, const Rect& r, State s) const override {
        const Palette& P = w.Pal();
        w.FillRect(r, s.checked ? P.buttonChecked : (s.hot ? P.buttonHot : P.buttonFace));
        bool sunken = s.pressed || s.checked;
        w.Draw3DFrame(r, sunken ? P.frameDark : P.frameLight, sunken ? P.frameLight : P.frameDark);
        Rect t = r;
        if (s.pressed) { t.left += 1; t.top += 1; t.right += 1; t.bottom += 1; }
        w.DrawCenteredString(t, label_, P.buttonText);
    }
};

class FlatButton : public Button {
public:
    using Button::Button;
    void Draw(Window& w, const Rect& r, State s) const override {
        const Palette& P = w.Pal();
        Color fill = s.pressed ? P.buttonChecked : (s.hot ? P.buttonHot : (s.checked ? P.buttonChecked : P.toolbar));
        w.FillRect(r, fill);
        if (s.checked) w.FillRect({r.left, r.bottom - 2, r.right, r.bottom}, P.accent);
        w.DrawCenteredString(r, label_, P.buttonText);
    }
};

// ============================================================================
//  DECORATOR — MonoGlyph bọc một Glyph để thêm trách nhiệm
// ============================================================================
class MonoGlyph : public Glyph {
public:
    explicit MonoGlyph(GlyphPtr component) { component_.push_back(std::move(component)); }
    void Draw(Window& w, Point p) const override { component_[0]->Draw(w, p); }
    Size Bounds(const Window& w) const override { return component_[0]->Bounds(w); }
    void Accept(GlyphVisitor& v) override { component_[0]->Accept(v); }
    GlyphPtr Child(size_t i) const override { return i == 0 ? component_[0] : nullptr; }
    size_t ChildCount() const override { return 1; }
    std::unique_ptr<Iterator<GlyphPtr>> CreateIterator() const override {
        return std::make_unique<ListIterator>(component_);
    }
protected:
    std::vector<GlyphPtr> component_;  // luôn đúng 1 phần tử
};

// Border: TEMPLATE METHOD — lớp con chỉ quyết định cách vẽ khung
class Border : public MonoGlyph {
public:
    Border(GlyphPtr component, int thickness, int padding)
        : MonoGlyph(std::move(component)), thickness_(thickness), padding_(padding) {}
    int Inset() const { return thickness_ + padding_; }
    Size Bounds(const Window& w) const override {
        Size s = MonoGlyph::Bounds(w);
        return {s.w + 2 * Inset(), s.h + 2 * Inset()};
    }
    void Draw(Window& w, Point p) const override {
        Size s = Bounds(w);
        Rect r{p.x, p.y, p.x + s.w, p.y + s.h};
        w.FillRect(r, w.Pal().background);
        MonoGlyph::Draw(w, {p.x + Inset(), p.y + Inset()});
        DrawFrame(w, r);
    }
protected:
    virtual void DrawFrame(Window& w, const Rect& r) const = 0;
    int thickness_, padding_;
};

class SunkenBorder : public Border {
public:
    explicit SunkenBorder(GlyphPtr c) : Border(std::move(c), 2, 6) {}
protected:
    void DrawFrame(Window& w, const Rect& r) const override {
        w.Draw3DFrame(r, w.Pal().frameDark, w.Pal().frameLight);
        w.Draw3DFrame({r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, Rgb(105, 105, 105), w.Pal().buttonFace);
    }
};

class FlatBorder : public Border {
public:
    explicit FlatBorder(GlyphPtr c) : Border(std::move(c), 1, 10) {}
protected:
    void DrawFrame(Window& w, const Rect& r) const override { w.FrameRect(r, w.Pal().frameDark); }
};

class Scroller : public MonoGlyph {
public:
    Scroller(GlyphPtr component, std::shared_ptr<ScrollBar> bar) : MonoGlyph(std::move(component)), bar_(std::move(bar)) {}
    void SetViewport(Size s) { viewport_ = s; }
    Size Viewport() const { return viewport_; }
    int ContentWidth() const { return viewport_.w - bar_->Width() - 4; }
    Size Bounds(const Window&) const override { return viewport_; }

    void Draw(Window& w, Point p) const override {
        Size content = component_[0]->Bounds(w);
        lastContentH_ = content.h;
        offset_ = std::clamp(offset_, 0, std::max(0, content.h - viewport_.h));
        Rect view{p.x, p.y, p.x + viewport_.w - bar_->Width(), p.y + viewport_.h};
        w.PushClip(view);
        component_[0]->Draw(w, {p.x, p.y - offset_});
        w.PopClip();
        lastBar_ = {view.right, p.y, p.x + viewport_.w, p.y + viewport_.h};
        bar_->Draw(w, lastBar_, content.h, offset_, viewport_.h);
    }

    int Offset() const { return offset_; }
    void ScrollTo(int y) { offset_ = std::max(0, y); }
    void ScrollBy(int dy) { ScrollTo(offset_ + dy); }
    void ScrollToShow(int top, int height) {
        if (top < offset_) ScrollTo(top);
        else if (top + height > offset_ + viewport_.h) ScrollTo(top + height - viewport_.h);
    }
    Rect BarRect() const { return lastBar_; }
    void JumpToBarPosition(int y) {
        int h = std::max(1, lastBar_.Height());
        long long rel = static_cast<long long>(y - lastBar_.top) * lastContentH_ / h;
        ScrollTo(static_cast<int>(rel) - viewport_.h / 2);
    }

private:
    std::shared_ptr<ScrollBar> bar_;
    Size viewport_{100, 100};
    mutable int offset_ = 0;
    mutable int lastContentH_ = 0;
    mutable Rect lastBar_;
};

// ============================================================================
//  ABSTRACT FACTORY — các nhà máy cụ thể
// ============================================================================
class GUIFactory {
public:
    virtual ~GUIFactory() = default;
    virtual std::wstring Name() const = 0;
    virtual Palette CreatePalette() const = 0;
    virtual std::shared_ptr<ScrollBar> CreateScrollBar() const = 0;
    virtual std::unique_ptr<Button> CreateButton(const std::wstring& label, unsigned id) const = 0;
    virtual std::shared_ptr<Border> CreateBorder(GlyphPtr content) const = 0;
};

class ClassicFactory : public GUIFactory {  // tương tự MotifWidgetFactory
public:
    std::wstring Name() const override { return L"Cổ điển"; }
    Palette CreatePalette() const override {
        Palette p;
        p.background = Rgb(255, 255, 255); p.text = Rgb(0, 0, 0);
        p.selectionBg = Rgb(51, 153, 255); p.selectionText = Rgb(255, 255, 255);
        p.error = Rgb(220, 0, 0); p.caret = Rgb(0, 0, 0);
        p.chrome = Rgb(240, 240, 240); p.toolbar = Rgb(240, 240, 240); p.toolbarLine = Rgb(200, 200, 200);
        p.statusBg = Rgb(225, 225, 225); p.statusText = Rgb(30, 30, 30);
        p.frameDark = Rgb(160, 160, 160); p.frameLight = Rgb(255, 255, 255);
        p.buttonFace = Rgb(228, 228, 228); p.buttonHot = Rgb(229, 241, 251); p.buttonChecked = Rgb(204, 228, 247);
        p.buttonText = Rgb(0, 0, 0); p.accent = Rgb(0, 120, 215);
        p.scrollTrack = Rgb(240, 240, 240); p.scrollThumb = Rgb(205, 205, 205); p.scrollThumbEdge = Rgb(150, 150, 150);
        return p;
    }
    std::shared_ptr<ScrollBar> CreateScrollBar() const override { return std::make_shared<ClassicScrollBar>(); }
    std::unique_ptr<Button> CreateButton(const std::wstring& l, unsigned id) const override {
        return std::make_unique<ClassicButton>(l, id);
    }
    std::shared_ptr<Border> CreateBorder(GlyphPtr c) const override { return std::make_shared<SunkenBorder>(std::move(c)); }
};

class DarkFactory : public GUIFactory {  // tương tự PMWidgetFactory
public:
    std::wstring Name() const override { return L"Tối"; }
    Palette CreatePalette() const override {
        Palette p;
        p.background = Rgb(30, 30, 30); p.text = Rgb(212, 212, 212);
        p.selectionBg = Rgb(38, 79, 120); p.selectionText = Rgb(255, 255, 255);
        p.error = Rgb(244, 71, 71); p.caret = Rgb(230, 230, 230);
        p.chrome = Rgb(37, 37, 38); p.toolbar = Rgb(45, 45, 48); p.toolbarLine = Rgb(63, 63, 70);
        p.statusBg = Rgb(0, 122, 204); p.statusText = Rgb(255, 255, 255);
        p.frameDark = Rgb(63, 63, 70); p.frameLight = Rgb(63, 63, 70);
        p.buttonFace = Rgb(45, 45, 48); p.buttonHot = Rgb(62, 62, 64); p.buttonChecked = Rgb(9, 71, 113);
        p.buttonText = Rgb(230, 230, 230); p.accent = Rgb(0, 122, 204);
        p.scrollTrack = Rgb(30, 30, 30); p.scrollThumb = Rgb(79, 79, 79); p.scrollThumbEdge = Rgb(79, 79, 79);
        return p;
    }
    std::shared_ptr<ScrollBar> CreateScrollBar() const override { return std::make_shared<FlatScrollBar>(); }
    std::unique_ptr<Button> CreateButton(const std::wstring& l, unsigned id) const override {
        return std::make_unique<FlatButton>(l, id);
    }
    std::shared_ptr<Border> CreateBorder(GlyphPtr c) const override { return std::make_shared<FlatBorder>(std::move(c)); }
};

// ============================================================================
//  BRIDGE — một WindowImp thứ hai, độc lập nền tảng: xuất ảnh SVG.
//  Cùng một cây Glyph, cùng lớp Window, chỉ đổi phần hiện thực.
// ============================================================================
class SvgWindowImp : public WindowImp {
public:
    SvgWindowImp(std::ostream& out, Size size, Color bg) : out_(out) {
        out_ << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size.w << "\" height=\"" << size.h
             << "\" viewBox=\"0 0 " << size.w << ' ' << size.h << "\">\n"
             << "<rect width=\"100%\" height=\"100%\" fill=\"" << Hex(bg) << "\"/>\n"
             << "<g font-family=\"Consolas, 'DejaVu Sans Mono', monospace\" font-size=\"14\">\n";
    }
    ~SvgWindowImp() override { Finish(); }
    void Finish() {
        if (finished_) return;
        FlushRun();
        out_ << "</g>\n</svg>\n";
        finished_ = true;
    }
    void DeviceFillRect(const Rect& r, Color c) override {
        FlushRun();
        out_ << "<rect x=\"" << r.left << "\" y=\"" << r.top << "\" width=\"" << r.Width() << "\" height=\""
             << r.Height() << "\" fill=\"" << Hex(c) << "\"/>\n";
    }
    void DeviceLine(Point a, Point b, Color c) override {
        FlushRun();
        out_ << "<line x1=\"" << a.x << "\" y1=\"" << a.y << "\" x2=\"" << b.x << "\" y2=\"" << b.y
             << "\" stroke=\"" << Hex(c) << "\"/>\n";
    }
    void DeviceText(int x, int y, const std::wstring& s, Color c) override {
        bool sameColor = run_.c.r == c.r && run_.c.g == c.g && run_.c.b == c.b;
        if (hasRun_ && y == run_.y && sameColor && x == run_.x + static_cast<int>(run_.s.size()) * kCharW) {
            run_.s += s;  // gộp ký tự liền nhau thành một thẻ <text>
            return;
        }
        FlushRun();
        run_ = {x, y, c, s};
        hasRun_ = true;
    }
    void DevicePushClip(const Rect&) override {}
    void DevicePopClip() override {}
    int DeviceCharWidth() const override { return kCharW; }
    int DeviceLineHeight() const override { return kLineH; }

    static constexpr int kCharW = 8, kLineH = 18;

private:
    struct Run { int x = 0, y = 0; Color c; std::wstring s; };
    static std::string Hex(Color c) {
        const char* d = "0123456789abcdef";
        std::string s = "#";
        for (int v : {c.r, c.g, c.b}) { s += d[v >> 4]; s += d[v & 15]; }
        return s;
    }
    void FlushRun() {
        if (!hasRun_) return;
        std::string esc;
        for (char ch : text::ToUtf8(run_.s)) {
            if (ch == '<') esc += "&lt;";
            else if (ch == '>') esc += "&gt;";
            else if (ch == '&') esc += "&amp;";
            else esc += ch;
        }
        out_ << "<text x=\"" << run_.x << "\" y=\"" << run_.y + 14 << "\" fill=\"" << Hex(run_.c)
             << "\" textLength=\"" << run_.s.size() * kCharW << "\">" << esc << "</text>\n";
        hasRun_ = false;
    }
    std::ostream& out_;
    Run run_;
    bool hasRun_ = false, finished_ = false;
};

// ============================================================================
//  COMMAND
// ============================================================================
class Command {
public:
    virtual ~Command() = default;
    virtual void Execute() = 0;
    virtual void Unexecute() {}
    virtual bool Reversible() const { return true; }
    virtual std::wstring Describe() const = 0;
    virtual bool MergeWith(const Command&) { return false; }  // gộp lệnh gõ phím liên tiếp
    virtual size_t CaretAfterExecute() const { return kPad; }
    virtual size_t CaretAfterUnexecute() const { return kPad; }
};

// Lệnh không hoàn tác được, gói một hàm — dùng cho menu và nút (SimpleCommand trong sách)
class SimpleCommand : public Command {
public:
    SimpleCommand(std::wstring name, std::function<void()> action) : name_(std::move(name)), action_(std::move(action)) {}
    void Execute() override { action_(); }
    bool Reversible() const override { return false; }
    std::wstring Describe() const override { return name_; }
private:
    std::wstring name_;
    std::function<void()> action_;
};

inline std::wstring Preview(const std::wstring& s, size_t max = 24) {
    std::wstring o;
    for (size_t i = 0; i < s.size() && i < max; ++i) o += (s[i] == L'\n') ? std::wstring(L"↵") : std::wstring(1, s[i]);
    if (s.size() > max) o += L"…";
    return o;
}

class InsertCommand : public Command {
public:
    InsertCommand(Composition& c, size_t pos, std::wstring text) : comp_(c), pos_(pos), text_(std::move(text)) {}
    void Execute() override {
        pos_ = std::min(pos_, comp_.Length());
        comp_.InsertText(pos_, text_);
    }
    void Unexecute() override { comp_.EraseText(pos_, text_.size()); }
    std::wstring Describe() const override { return L"Chèn \"" + Preview(text_) + L"\""; }
    size_t CaretAfterExecute() const override { return pos_ + text_.size(); }
    size_t CaretAfterUnexecute() const override { return pos_; }
    bool MergeWith(const Command& next) override {
        auto* n = dynamic_cast<const InsertCommand*>(&next);
        if (!n || &n->comp_ != &comp_ || n->text_.size() != 1 || text_.empty()) return false;
        if (n->pos_ != pos_ + text_.size() || n->text_[0] == L'\n' || text_.back() == L'\n') return false;
        if (text_.back() == L' ' && n->text_[0] != L' ') return false;  // từ mới -> bước undo mới
        text_ += n->text_;
        return true;
    }
private:
    Composition& comp_;
    size_t pos_;
    std::wstring text_;
};

class DeleteCommand : public Command {
public:
    DeleteCommand(Composition& c, size_t pos, size_t n) : comp_(c), pos_(pos), n_(n) {}
    void Execute() override {
        pos_ = std::min(pos_, comp_.Length());
        removed_ = comp_.EraseText(pos_, n_);
    }
    void Unexecute() override { comp_.InsertText(pos_, removed_); }
    std::wstring Describe() const override { return L"Xoá \"" + Preview(removed_) + L"\""; }
    size_t CaretAfterExecute() const override { return pos_; }
    size_t CaretAfterUnexecute() const override { return pos_ + removed_.size(); }
    bool MergeWith(const Command& next) override {
        auto* d = dynamic_cast<const DeleteCommand*>(&next);
        if (!d || &d->comp_ != &comp_ || d->removed_.size() != 1 || removed_.empty() || removed_.find(L'\n') != std::wstring::npos)
            return false;
        if (d->pos_ + 1 == pos_) { removed_ = d->removed_ + removed_; pos_ = d->pos_; return true; }  // Backspace
        if (d->pos_ == pos_) { removed_ += d->removed_; return true; }                               // Delete
        return false;
    }
private:
    Composition& comp_;
    size_t pos_, n_;
    std::wstring removed_;
};

class ChangeCompositorCommand : public Command {
public:
    ChangeCompositorCommand(Composition& c, std::unique_ptr<Compositor> next)
        : comp_(c), other_(std::move(next)), name_(other_->Name()) {}
    void Execute() override { other_ = comp_.SetCompositor(std::move(other_)); }
    void Unexecute() override { other_ = comp_.SetCompositor(std::move(other_)); }  // hoán đổi lại
    std::wstring Describe() const override { return L"Đổi bố cục → " + name_; }
private:
    Composition& comp_;
    std::unique_ptr<Compositor> other_;
    std::wstring name_;
};

class MacroCommand : public Command {
public:
    explicit MacroCommand(std::wstring name) : name_(std::move(name)) {}
    void Add(std::unique_ptr<Command> c) { cmds_.push_back(std::move(c)); }
    void Execute() override { for (auto& c : cmds_) c->Execute(); }
    void Unexecute() override { for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->Unexecute(); }
    std::wstring Describe() const override {
        std::wstring s = name_ + L" {";
        for (size_t i = 0; i < cmds_.size(); ++i) s += (i ? L"; " : L" ") + cmds_[i]->Describe();
        return s + L" }";
    }
    size_t CaretAfterExecute() const override { return cmds_.empty() ? kPad : cmds_.back()->CaretAfterExecute(); }
    size_t CaretAfterUnexecute() const override { return cmds_.empty() ? kPad : cmds_.front()->CaretAfterUnexecute(); }
private:
    std::wstring name_;
    std::vector<std::unique_ptr<Command>> cmds_;
};

class CommandHistory {
public:
    // Trả về vị trí con trỏ gợi ý (kPad nếu không có)
    size_t Execute(std::unique_ptr<Command> cmd) {
        cmd->Execute();
        size_t caret = cmd->CaretAfterExecute();
        if (!cmd->Reversible()) return caret;
        undone_.clear();
        if (mergeable_ && !done_.empty() && done_.back()->MergeWith(*cmd)) return caret;
        done_.push_back(std::move(cmd));
        mergeable_ = true;
        return caret;
    }
    std::optional<size_t> Undo() {
        if (done_.empty()) return std::nullopt;
        done_.back()->Unexecute();
        size_t c = done_.back()->CaretAfterUnexecute();
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        mergeable_ = false;
        return c;
    }
    std::optional<size_t> Redo() {
        if (undone_.empty()) return std::nullopt;
        undone_.back()->Execute();
        size_t c = undone_.back()->CaretAfterExecute();
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
        mergeable_ = false;
        return c;
    }
    void Seal() { mergeable_ = false; }  // ngắt việc gộp (khi di chuyển con trỏ)
    void Clear() { done_.clear(); undone_.clear(); mergeable_ = false; }
    bool CanUndo() const { return !done_.empty(); }
    bool CanRedo() const { return !undone_.empty(); }
    std::wstring NextUndoName() const { return done_.empty() ? L"" : done_.back()->Describe(); }
    std::vector<std::wstring> Listing() const {
        std::vector<std::wstring> out;
        for (size_t i = 0; i < done_.size(); ++i) out.push_back(std::to_wstring(i + 1) + L". " + done_[i]->Describe());
        for (auto it = undone_.rbegin(); it != undone_.rend(); ++it) out.push_back(L"(đã hoàn tác) " + (*it)->Describe());
        return out;
    }
private:
    std::vector<std::unique_ptr<Command>> done_, undone_;
    bool mergeable_ = false;
};

// ============================================================================
//  SINGLETON — từ điển chính tả
// ============================================================================
class Dictionary {
public:
    static Dictionary& Instance() {
        static Dictionary instance;
        return instance;
    }
    bool Contains(const std::wstring& w) const { return words_.count(text::ToLower(w)) > 0; }
    void Add(const std::wstring& w) { words_.insert(text::ToLower(w)); }
    size_t Size() const { return words_.size(); }
    // Đúng chính tả nếu có trong từ điển tiếng Anh hoặc là âm tiết tiếng Việt hợp lệ
    bool IsCorrect(const std::wstring& w) const {
        std::wstring lw = text::ToLower(w);
        return lw.size() == 1 || words_.count(lw) > 0 || text::IsVietnameseSyllable(lw);
    }
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

private:
    Dictionary() {
        static const wchar_t* const kWords[] = {
            L"a", L"about", L"above", L"after", L"again", L"against", L"all", L"also", L"am", L"an", L"and", L"any",
            L"are", L"as", L"at", L"be", L"because", L"been", L"before", L"being", L"below", L"between", L"both",
            L"but", L"by", L"can", L"could", L"did", L"do", L"does", L"doing", L"down", L"during", L"each", L"few",
            L"for", L"from", L"further", L"had", L"has", L"have", L"having", L"he", L"her", L"here", L"hers",
            L"him", L"his", L"how", L"i", L"if", L"in", L"into", L"is", L"it", L"its", L"itself", L"just", L"me",
            L"more", L"most", L"my", L"no", L"nor", L"not", L"now", L"of", L"off", L"on", L"once", L"only", L"or",
            L"other", L"our", L"out", L"over", L"own", L"same", L"she", L"should", L"so", L"some", L"such", L"than",
            L"that", L"the", L"their", L"them", L"then", L"there", L"these", L"they", L"this", L"those", L"through",
            L"to", L"too", L"under", L"until", L"up", L"very", L"was", L"we", L"were", L"what", L"when", L"where",
            L"which", L"while", L"who", L"whom", L"why", L"will", L"with", L"would", L"you", L"your", L"yours",
            L"don't", L"can't", L"won't", L"isn't", L"it's", L"i'm", L"you're", L"we're", L"they're", L"doesn't",
            L"one", L"two", L"three", L"four", L"five", L"first", L"second", L"new", L"old", L"good", L"great",
            L"small", L"large", L"big", L"long", L"little", L"high", L"low", L"right", L"left", L"next", L"last",
            L"make", L"makes", L"made", L"use", L"uses", L"used", L"using", L"like", L"get", L"gets", L"go",
            L"see", L"know", L"take", L"come", L"think", L"look", L"want", L"give", L"find", L"tell", L"work",
            L"works", L"call", L"try", L"need", L"feel", L"become", L"leave", L"put", L"mean", L"keep", L"let",
            L"begin", L"seem", L"help", L"show", L"shows", L"hear", L"play", L"run", L"move", L"live", L"believe",
            L"write", L"writes", L"read", L"reads", L"type", L"types", L"change", L"changes", L"open", L"save",
            L"close", L"edit", L"print", L"click", L"press", L"select", L"copy", L"cut", L"paste", L"delete",
            L"insert", L"add", L"remove", L"create", L"creates", L"draw", L"draws", L"build", L"choose",
            L"time", L"year", L"people", L"way", L"ways", L"day", L"man", L"thing", L"things", L"world", L"life",
            L"hand", L"part", L"place", L"case", L"week", L"point", L"number", L"group", L"problem", L"problems",
            L"fact", L"idea", L"example", L"examples", L"name", L"names", L"book", L"books", L"chapter", L"study",
            L"design", L"designs", L"designed", L"pattern", L"patterns", L"object", L"objects", L"oriented",
            L"software", L"reusable", L"element", L"elements", L"text", L"editor", L"document", L"documents",
            L"structure", L"structures", L"format", L"formatting", L"user", L"users", L"interface", L"interfaces",
            L"window", L"windows", L"system", L"systems", L"command", L"commands", L"undo", L"redo", L"spelling",
            L"checking", L"check", L"hyphenation", L"gang", L"class", L"classes", L"instance", L"instances",
            L"program", L"programs", L"programming", L"code", L"composite", L"strategy", L"decorator", L"factory",
            L"abstract", L"bridge", L"iterator", L"visitor", L"flyweight", L"singleton", L"observer", L"method",
            L"template", L"glyph", L"glyphs", L"line", L"lines", L"word", L"words", L"character", L"characters",
            L"page", L"pages", L"feel", L"simple", L"solution", L"solutions", L"common", L"described", L"describes",
            L"demo", L"hello", L"quick", L"brown", L"fox", L"jumps", L"lazy", L"dog", L"lexi", L"gamma", L"helm",
            L"johnson", L"vlissides", L"windows", L"menu", L"button", L"buttons", L"scroll", L"scrollbar", L"border",
            L"theme", L"dark", L"classic", L"light", L"color", L"colors", L"font", L"fonts", L"file", L"files",
            L"tex", L"array", L"layout", L"align", L"justify", L"justified", L"paragraph", L"paragraphs",
            L"tree", L"node", L"leaf", L"parent", L"child", L"children", L"operation", L"operations", L"request",
            L"requests", L"client", L"clients", L"state", L"behavior", L"behaviour", L"structural", L"creational",
            L"behavioral", L"family", L"families", L"product", L"products", L"concrete", L"implementation",
            L"implementations", L"platform", L"platforms", L"independent", L"support", L"supports", L"feature",
            L"features", L"many", L"much", L"every", L"another", L"different", L"each", L"well", L"even", L"back",
            L"still", L"never", L"always", L"often", L"here", L"yes", L"ok", L"okay", L"please", L"thanks", L"thank",
            L"today", L"tomorrow", L"yesterday", L"english", L"vietnamese", L"language", L"languages", L"test",
            L"tests", L"testing", L"error", L"errors", L"correct", L"wrong", L"try", L"again", L"enjoy",
            L"c", L"cpp", L"api", L"gui", L"svg", L"html", L"win", L"microsoft", L"keyboard", L"mouse", L"caret",
            L"selection", L"clipboard", L"status", L"bar", L"toolbar", L"recursive", L"composition", L"compositor",
            L"ctrl", L"alt", L"shift", L"enter", L"sentence", L"sentences", L"mistake", L"mistakes", L"has"};
        for (auto w : kWords) words_.insert(w);
    }
    std::set<std::wstring> words_;
};

// ============================================================================
//  VISITOR cụ thể
// ============================================================================
class SpellingChecker : public GlyphVisitor {
public:
    void VisitCharacter(Character& c) override {
        if (text::IsWordChar(c.Value())) word_ += c.Value();
        else Flush();
    }
    void VisitRow(Row&) override { Flush(); }
    const std::set<std::wstring>& Misspelled() { Flush(); return misspelled_; }
    size_t Checked() const { return checked_; }
private:
    void Flush() {
        size_t b = 0, e = word_.size();
        while (b < e && word_[b] == L'\'') ++b;
        while (e > b && word_[e - 1] == L'\'') --e;
        if (e > b) {
            std::wstring w = text::ToLower(word_.substr(b, e - b));
            ++checked_;
            if (!Dictionary::Instance().IsCorrect(w)) misspelled_.insert(w);
        }
        word_.clear();
    }
    std::wstring word_;
    std::set<std::wstring> misspelled_;
    size_t checked_ = 0;
};

class WordCounter : public GlyphVisitor {
public:
    void VisitCharacter(Character& c) override {
        if (c.Value() == L' ') { inWord_ = false; return; }
        ++chars_;
        if (!inWord_) { ++words_; inWord_ = true; }
    }
    void VisitRow(Row&) override { ++rows_; inWord_ = false; }
    size_t chars_ = 0, words_ = 0, rows_ = 0;
private:
    bool inWord_ = false;
};

// ============================================================================
//  OBSERVER cụ thể
// ============================================================================
class StatusBar : public Observer {
public:
    explicit StatusBar(Composition& c) : comp_(c) {}
    void Update(Subject&) override {
        WordCounter wc;
        Traverse(comp_, wc);
        text_ = L"Từ: " + std::to_wstring(wc.words_) + L"   Ký tự: " + std::to_wstring(comp_.Length()) +
                L"   |   Bố cục: " + comp_.GetCompositor().Name() + L"   |   Flyweight: " +
                std::to_wstring(CharacterFactory::Instance().PoolSize()) + L" glyph dùng chung";
    }
    const std::wstring& Text() const { return text_; }
private:
    Composition& comp_;
    std::wstring text_;
};

class LiveSpellChecker : public Observer {
public:
    explicit LiveSpellChecker(Composition& c) : comp_(c) {}
    void SetEnabled(bool on) { enabled_ = on; Update(comp_); }
    bool Enabled() const { return enabled_; }
    void Update(Subject&) override {
        marks_.assign(comp_.Length(), false);
        errors_.clear();
        if (!enabled_) return;
        SpellingChecker sc;
        Traverse(comp_, sc);  // Visitor + Iterator trên cây Glyph
        errors_ = sc.Misspelled();
        if (errors_.empty()) return;
        const std::wstring& t = comp_.Text();
        for (auto [b, e] : text::FindWords(t))
            if (errors_.count(text::ToLower(t.substr(b, e - b))))
                for (size_t i = b; i < e; ++i) marks_[i] = true;
    }
    const std::vector<bool>& Marks() const { return marks_; }
    const std::set<std::wstring>& Errors() const { return errors_; }
    bool IsMarked(size_t i) const { return i < marks_.size() && marks_[i]; }
private:
    Composition& comp_;
    bool enabled_ = true;
    std::vector<bool> marks_;
    std::set<std::wstring> errors_;
};

}  // namespace lexi
