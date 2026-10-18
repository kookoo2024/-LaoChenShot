cmake_minimum_required(VERSION 3.15)
project(LaoChenShot LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(LaoChenShot WIN32 src/main.cpp res/app.rc)
target_compile_definitions(LaoChenShot PRIVATE UNICODE _UNICODE)

if(MSVC)
  # 静态 CRT：生成的 exe 不需要安装 VC++ 运行库
  set_property(TARGET LaoChenShot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(LaoChenShot PRIVATE /utf-8 /W3)
  target_link_options(LaoChenShot PRIVATE /MANIFEST:NO)   # 清单由 res/app.rc 提供
else()
  target_link_options(LaoChenShot PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(LaoChenShot PRIVATE
  gdiplus comctl32 shell32 ole32 uuid comdlg32 dwmapi imm32 advapi32 gdi32 user32)
