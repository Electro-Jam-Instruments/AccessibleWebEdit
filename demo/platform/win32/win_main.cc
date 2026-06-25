// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Win32 interactive frontend for the AccessibleWebEdit demo (Windows 11).
//
// This is the "show it on Windows" half: a real top-level window with live
// keyboard input, driving the SAME demo core (text model + the single layout
// pass + the raster) as the Linux headless renderer. Type, backspace, and move
// the caret with the arrow/Home/End keys; the window repaints from the layout.
//
// It is standalone: compiles with MSVC (cl.exe) against the demo core headers
// only -- no Chromium checkout, no Skia, no GPU. That makes it the fast path to
// see the editor running on a Win11 desktop. The accessibility half (UIA ->
// NVDA) is the in-tree build documented in uia_bridge.md; the SAME core feeds
// both, so the pixels you see here and the events a screen reader reads are one
// pipeline.
//
// Build:  see build_win.ps1   (cl /std:c++17 /EHsc /I..\..\core win_main.cc
//                              /link user32.lib gdi32.lib)

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "font5x7.h"
#include "layout_engine.h"
#include "raster.h"
#include "text_document.h"

namespace {

constexpr int kScale = 8;
demo::TextDocument g_doc;

// Render the current document into a 32-bit top-down BGRA buffer and blit it to
// the window with StretchDIBits. The RGB bytes come straight from the shared
// raster (the same one that writes PPMs on Linux).
void Paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  const int cw = rc.right - rc.left;
  const int ch = rc.bottom - rc.top;
  if (cw <= 0 || ch <= 0)
    return;

  const demo::Layout layout = demo::LayOut(g_doc);
  demo::Raster raster(cw, ch);
  raster.PaintFrame(g_doc, layout, kScale);

  // RGB -> BGRA top-down.
  const std::vector<uint8_t>& rgb = raster.rgb();
  std::vector<uint8_t> bgra(static_cast<size_t>(cw) * ch * 4);
  for (int i = 0; i < cw * ch; ++i) {
    bgra[i * 4 + 0] = rgb[i * 3 + 2];  // B
    bgra[i * 4 + 1] = rgb[i * 3 + 1];  // G
    bgra[i * 4 + 2] = rgb[i * 3 + 0];  // R
    bgra[i * 4 + 3] = 255;             // A
  }

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = cw;
  bmi.bmiHeader.biHeight = -ch;  // negative => top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, cw, ch, bgra.data(), &bmi,
                DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_CHAR: {
      // Printable ASCII -> insert; backspace handled here too.
      const wchar_t c = static_cast<wchar_t>(wparam);
      if (c == L'\b') {
        g_doc.Backspace();
      } else if (c >= 0x20 && c < 0x7f) {
        g_doc.Insert(std::string(1, static_cast<char>(c)));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_KEYDOWN: {
      switch (wparam) {
        case VK_LEFT: g_doc.MoveCaret(-1); break;
        case VK_RIGHT: g_doc.MoveCaret(1); break;
        case VK_HOME: g_doc.CaretTo(0); break;
        case VK_END: g_doc.CaretTo(g_doc.length()); break;
        default: return 0;
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      Paint(hwnd, hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  g_doc.Insert("Type here. ");

  const wchar_t kClass[] = L"AccessibleWebEditDemo";
  WNDCLASS wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
  RegisterClass(&wc);

  HWND hwnd = CreateWindowEx(
      0, kClass, L"AccessibleWebEdit — custom canvas editor (demo)",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 200, nullptr,
      nullptr, hInstance, nullptr);
  if (!hwnd)
    return 1;

  ShowWindow(hwnd, nCmdShow);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
