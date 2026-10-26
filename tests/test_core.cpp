// Kiểm thử phần lõi (chạy được trên mọi nền tảng):
//   g++ -std=c++17 -I.. test_core.cpp -o test_core && ./test_core
#include <cassert>
#include <iostream>
#include <sstream>

#include "../lexi_core.h"

using namespace lexi;

static int failures = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } \
    } while (0)

// WindowImp giả: vẽ lên lưới ký tự để kiểm tra kết quả (cũng là một hiện thực Bridge)
class GridImp : public WindowImp {
public:
    GridImp(int cols, int rows) : grid_(rows, std::wstring(cols, L'.')) {}
    void DeviceFillRect(const Rect&, Color) override {}
    void DeviceLine(Point, Point, Color) override {}
    void DeviceText(int x, int y, const std::wstring& s, Color) override {
        int r = y, c = x;
        if (r < 0 || r >= static_cast<int>(grid_.size())) return;
        for (wchar_t ch : s) { if (c >= 0 && c < static_cast<int>(grid_[r].size())) grid_[r][c] = ch; ++c; }
    }
    void DevicePushClip(const Rect&) override {}
    void DevicePopClip() override {}
    int DeviceCharWidth() const override { return 1; }
    int DeviceLineHeight() const override { return 1; }
    std::wstring RowStr(int r) const { return grid_[r]; }
private:
    std::vector<std::wstring> grid_;
};

static std::wstring RowText(const Composition& c, size_t r) {
    std::wstring s;
    const Glyph& row = *c.Child(r);
    for (size_t k = 0; k < row.ChildCount(); ++k) s += static_cast<const Character&>(*row.Child(k)).Value();
    return s;
}

int main() {
    // ---- Composite + Strategy: Simple -------------------------------------
    Composition comp(std::make_unique<SimpleCompositor>(), 10);
    comp.SetText(L"hello world foo bar\n\nend");
    CHECK(comp.RowCount() == 5);
    CHECK(RowText(comp, 0) == L"hello ");
    CHECK(RowText(comp, 1) == L"world foo ");
    CHECK(RowText(comp, 2) == L"bar");
    CHECK(RowText(comp, 3) == L"");
    CHECK(RowText(comp, 4) == L"end");

    // ---- ánh xạ con trỏ ------------------------------------------------------
    size_t r, c;
    comp.IndexToRowCol(0, r, c);   CHECK(r == 0 && c == 0);
    comp.IndexToRowCol(6, r, c);   CHECK(r == 1 && c == 0);   // 'w'
    comp.IndexToRowCol(19, r, c);  CHECK(r == 2 && c == 3);   // ký tự xuống dòng sau "bar"
    comp.IndexToRowCol(20, r, c);  CHECK(r == 3 && c == 0);   // đoạn rỗng
    comp.IndexToRowCol(24, r, c);  CHECK(r == 4 && c == 3);
    CHECK(comp.RowColToIndex(1, 2) == 8);
    CHECK(comp.RowColToIndex(4, 99) == 24);
    CHECK(comp.RowColToIndex(3, 5) == 20);
    CHECK(comp.RowColToIndex(0, 99) == 5);  // cuối dòng 0 trước khoảng trắng ngắt dòng

    // ---- Strategy: TeX canh đều ---------------------------------------------
    Composition tex(std::make_unique<TeXCompositor>(), 12);
    tex.SetText(L"aa bb cc dd ee ff gg");
    CHECK(RowText(tex, 0).size() >= 12);  // dòng không cuối phải canh đủ 12 cột
    std::wstring first = RowText(tex, 0);
    CHECK(first.substr(0, 2) == L"aa");
    size_t rr, cc;
    tex.IndexToRowCol(3, rr, cc);  // 'b' đầu tiên phải map đúng cột hiển thị
    CHECK(RowText(tex, rr)[cc] == L'b');

    // ---- Strategy: Array ----------------------------------------------------
    Composition arr(std::make_unique<ArrayCompositor>(2), 80);
    arr.SetText(L"one two three four five");
    CHECK(arr.RowCount() == 3);

    // ---- Flyweight ----------------------------------------------------------
    size_t pool = CharacterFactory::Instance().PoolSize();
    comp.SetText(L"aaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(CharacterFactory::Instance().PoolSize() == pool);  // không tạo đối tượng mới
    CHECK(comp.Child(0)->Child(0) == comp.Child(0)->Child(1));

    // ---- Command + Undo/Redo + gộp lệnh -------------------------------------
    comp.SetText(L"");
    CommandHistory h;
    h.Execute(std::make_unique<InsertCommand>(comp, 0, L"a"));
    h.Execute(std::make_unique<InsertCommand>(comp, 1, L"b"));
    h.Execute(std::make_unique<InsertCommand>(comp, 2, L"c"));
    CHECK(comp.Text() == L"abc");
    auto u = h.Undo();
    CHECK(comp.Text() == L"" && u && *u == 0);  // ba phím gộp thành một bước
    h.Redo();
    CHECK(comp.Text() == L"abc");
    h.Seal();
    h.Execute(std::make_unique<DeleteCommand>(comp, 2, 1));  // backspace
    h.Execute(std::make_unique<DeleteCommand>(comp, 1, 1));
    CHECK(comp.Text() == L"a");
    h.Undo();
    CHECK(comp.Text() == L"abc");
    auto macro = std::make_unique<MacroCommand>(L"Thay thế");
    macro->Add(std::make_unique<DeleteCommand>(comp, 0, 3));
    macro->Add(std::make_unique<InsertCommand>(comp, 0, L"xyz"));
    h.Seal();
    h.Execute(std::move(macro));
    CHECK(comp.Text() == L"xyz");
    h.Undo();
    CHECK(comp.Text() == L"abc");
    h.Execute(std::make_unique<ChangeCompositorCommand>(comp, std::make_unique<TeXCompositor>()));
    CHECK(comp.GetCompositor().Name() == L"TeX");
    h.Undo();
    CHECK(comp.GetCompositor().Name() == L"Simple");
    h.Redo();
    CHECK(comp.GetCompositor().Name() == L"TeX");

    // ---- Tiếng Việt & chính tả (Visitor + Iterator + Singleton) -------------
    CHECK(text::IsVietnameseSyllable(L"việt"));             // việt
    CHECK(text::IsVietnameseSyllable(L"nghĩa"));            // nghĩa
    CHECK(text::IsVietnameseSyllable(L"gì"));               // gì
    CHECK(text::IsVietnameseSyllable(L"quốc"));             // quốc
    CHECK(text::IsVietnameseSyllable(L"khuỷnh"));           // khuỷnh
    CHECK(!text::IsVietnameseSyllable(L"teh"));
    CHECK(!text::IsVietnameseSyllable(L"việệt"));      // hai dấu thanh
    CHECK(text::ToLower(L'Ạ') == L'ạ');                 // Ạ -> ạ
    CHECK(text::ToLower(L'Đ') == L'đ');                 // Đ -> đ

    Composition doc(std::make_unique<SimpleCompositor>(), 40);
    LiveSpellChecker spell(doc);
    doc.Attach(&spell);
    doc.SetText(L"Xin chào, teh quick brown fox. Tiếng Việt rất hay.");
    CHECK(spell.Errors().size() == 1 && spell.Errors().count(L"teh"));
    CHECK(spell.IsMarked(11) && !spell.IsMarked(0));
    Dictionary::Instance().Add(L"teh");
    doc.Notify();
    CHECK(spell.Errors().empty());

    // ---- Iterator ------------------------------------------------------------
    size_t count = 0;
    PreorderIterator it(doc);
    for (it.First(); !it.IsDone(); it.Next()) ++count;
    size_t expected = doc.RowCount();
    for (size_t i = 0; i < doc.RowCount(); ++i) expected += doc.Child(i)->ChildCount();
    CHECK(count == expected);

    // ---- Observer -----------------------------------------------------------
    StatusBar status(doc);
    doc.Attach(&status);
    doc.SetText(L"một hai ba");
    CHECK(status.Text().find(L"Từ: 3") != std::wstring::npos);

    // ---- Bridge: vẽ lên GridImp, có Decorator Border + Scroller --------------
    ClassicFactory factory;
    Palette pal = factory.CreatePalette();
    auto scroller = std::make_shared<Scroller>(std::shared_ptr<Glyph>(std::shared_ptr<Glyph>{}, &doc),
                                               factory.CreateScrollBar());
    scroller->SetViewport({60, 3});
    auto border = factory.CreateBorder(scroller);
    GridImp grid(120, 40);
    Window w;
    w.SetImp(&grid);
    w.SetPalette(&pal);
    Composition::RenderHints hints;
    doc.SetHints(&hints);
    border->Draw(w, {0, 0});
    doc.SetHints(nullptr);
    int in = border->Inset();
    CHECK(grid.RowStr(in).substr(in, 10) == L"một.hai.ba");  // ô khoảng trắng không vẽ
    CHECK(doc.LastOrigin().x == in && doc.LastOrigin().y == in);

    // ---- Bridge: xuất SVG ----------------------------------------------------
    std::ostringstream svg;
    {
        SvgWindowImp imp(svg, {400, 100}, pal.background);
        Window sw;
        sw.SetImp(&imp);
        sw.SetPalette(&pal);
        doc.Draw(sw, {10, 10});
        imp.Finish();
    }
    std::string s = svg.str();
    CHECK(s.find("<svg") != std::string::npos && s.find("</svg>") != std::string::npos);
    CHECK(s.find(text::ToUtf8(L"một")) != std::string::npos);

    // ---- UTF-8 khứ hồi --------------------------------------------------------
    std::wstring vn = L"Tiếng Việt đẹp";
    CHECK(text::FromUtf8(text::ToUtf8(vn)) == vn);

    if (failures == 0) std::cout << "Tất cả kiểm thử đều đạt.\n";
    return failures == 0 ? 0 : 1;
}
