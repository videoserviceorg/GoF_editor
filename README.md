# Lexi — Trình soạn thảo văn bản Windows theo mẫu thiết kế GoF

Đây là trình soạn thảo văn bản có giao diện đồ hoạ Win32, viết bằng C++17. Thiết kế đi theo case study **Lexi** ở Chương 2 của cuốn
*Design Patterns: Elements of Reusable Object-Oriented Software* (Gamma, Helm, Johnson, Vlissides).

## Cấu trúc

| Tệp | Nội dung |
|---|---|
| `lexi_core.h` | Toàn bộ mô hình tài liệu và các mẫu thiết kế. Phần này không phụ thuộc Windows. |
| `lexi_win32.cpp` | Cửa sổ Win32: `Win32WindowImp` (Bridge vẽ bằng GDI), menu, thanh công cụ, chuột, bàn phím, clipboard, mở/lưu tệp |
| `tests/test_core.cpp` | Kiểm thử phần lõi, chạy được trên mọi hệ điều hành |
| `CMakeLists.txt`, `build.bat` | Tệp dựng chương trình |

## Biên dịch

Có ba cách:

**1. Visual Studio 2019 hoặc 2022 (cần cài workload "Desktop development with C++")**
- Chọn *File → Open → Folder…*, mở thư mục này. Visual Studio tự nhận `CMakeLists.txt`.
- Chọn target `Lexi.exe` rồi nhấn F5.

**2. Dòng lệnh**
- Mở *x64 Native Tools Command Prompt for VS*, `cd` vào thư mục này rồi chạy `build.bat`.

**3. MSYS2 / MinGW-w64**
```
g++ -std=c++17 -O2 -mwindows -static -o Lexi.exe lexi_win32.cpp -luser32 -lgdi32 -lcomdlg32
```

Chạy kiểm thử phần lõi (trên bất kỳ hệ điều hành nào):
```
g++ -std=c++17 tests/test_core.cpp -o test_core && ./test_core
```

## Sử dụng

- **Gõ phím:** gõ tiếng Việt bằng Unikey hoặc EVKey (chế độ Unicode).
- **Hoàn tác / Làm lại:** Ctrl+Z / Ctrl+Y. Các phím gõ liên tiếp trong cùng một từ được gộp thành một bước.
- **Di chuyển và chọn:** dùng mũi tên, Home/End, PgUp/PgDn. Giữ Shift để chọn, giữ Ctrl để nhảy theo từ. Kéo chuột để chọn, nhấp đúp để chọn một từ.
- **Clipboard:** Ctrl+X / C / V, chọn tất cả bằng Ctrl+A.
- **Thanh công cụ:** đổi bố cục **Simple / TeX / Array**, đổi giao diện **Cổ điển / Tối**, bật/tắt kiểm tra chính tả, xuất SVG.
- **Chính tả:**
  - Từ lạ bị gạch đỏ lượn sóng.
  - Một từ được coi là đúng nếu có trong từ điển tiếng Anh hoặc là âm tiết tiếng Việt hợp cấu trúc.
  - Nhấp chuột phải vào từ bị gạch để thêm nó vào từ điển. F7 hiện danh sách lỗi.
- **Tệp:**
  - Mở và lưu tệp `.txt` (UTF-8).
  - *Tệp → Xuất ảnh SVG* vẽ cùng cây Glyph đó qua một `WindowImp` khác.

## Các mẫu thiết kế

| Bài toán trong sách | Mẫu | Lớp |
|---|---|---|
| Cấu trúc tài liệu | Composite | `Glyph`, `Character`, `Row`, `Column`, `Composition` |
| Mỗi ký tự là một đối tượng | Flyweight | `Character`, `CharacterFactory` |
| Định dạng (ngắt dòng) | Strategy | `Compositor`, `SimpleCompositor`, `TeXCompositor`, `ArrayCompositor` |
| Tô điểm giao diện | Decorator | `MonoGlyph`, `Border` (`SunkenBorder`, `FlatBorder`), `Scroller` |
| Nhiều look-and-feel | Abstract Factory | `GUIFactory`, `ClassicFactory`, `DarkFactory` → `Button`, `ScrollBar`, `Border`, `Palette` |
| Nhiều hệ thống cửa sổ | Bridge | `Window` + `WindowImp` (`Win32WindowImp`, `SvgWindowImp`) |
| Thao tác người dùng, Undo/Redo | Command | `InsertCommand`, `DeleteCommand`, `ChangeCompositorCommand`, `MacroCommand`, `SimpleCommand`, `CommandHistory` |
| Duyệt cấu trúc | Iterator | `PreorderIterator`, `ListIterator`, `NullIterator` |
| Phân tích tài liệu | Visitor | `SpellingChecker`, `WordCounter` |
| Đối tượng duy nhất | Singleton | `Dictionary`, `CharacterFactory` |
| Cập nhật tự động | Observer | `Composition` (Subject) → `StatusBar`, `LiveSpellChecker` |
| Khung thuật toán cố định | Template Method | `ScrollBar::Draw`, `Border::Draw` |

Một số điểm đáng chú ý:

- **Flyweight:** mỗi `Character` chỉ giữ mã ký tự, tức trạng thái nội tại. Vị trí, màu, việc có đang được bôi chọn hay bị gạch lỗi hay không là trạng thái ngoại lai. `Composition` tính các thông tin này khi vẽ. Vì vậy cả tài liệu chỉ cần vài chục đối tượng `Character` (thanh trạng thái có hiển thị con số này).
- **Command:** mỗi mục menu, nút trên thanh công cụ và phím tắt đều gắn với một `Command`, giống `MenuItem` trong sách. Các lệnh sửa tài liệu có thể hoàn tác. Lệnh đổi bố cục cũng hoàn tác được: nó hoán đổi `Compositor` cũ và mới qua lại.
- **Bridge:** `Glyph` chỉ biết lớp `Window`. Khi vẽ lên màn hình, `Window` dùng `Win32WindowImp` (GDI). Khi xuất SVG, nó dùng `SvgWindowImp`. Không có dòng nào trong cây Glyph phải thay đổi giữa hai trường hợp.
