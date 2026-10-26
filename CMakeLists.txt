cmake_minimum_required(VERSION 3.15)
project(Lexi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Kiểm thử phần lõi (chạy được trên mọi hệ điều hành)
add_executable(test_core tests/test_core.cpp)

# Ứng dụng giao diện đồ hoạ Windows
if(WIN32)
  add_executable(Lexi WIN32 lexi_win32.cpp lexi_core.h)
  target_link_libraries(Lexi PRIVATE user32 gdi32 comdlg32)
  if(MINGW)
    target_link_options(Lexi PRIVATE -static -mwindows)
  endif()
endif()
