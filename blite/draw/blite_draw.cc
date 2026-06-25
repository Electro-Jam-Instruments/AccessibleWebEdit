// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// B-lite drawn demo (standalone, Linux-runnable with plain g++).
//
// Extends the headless B-lite spine (blite/blite_host.cc) with the OTHER half
// of the editor that doc 11 describes: the sighted-facing pixels. It runs the
// single layout pass (layout_engine.h) and then drives that ONE Layout into:
//   (a) a raster image  -> a real PPM the eye can read, and
//   (b) the accessibility geometry -> the kCaretBounds / per-character bounds
//       an AXNodeData would carry, printed so you can see they are the SAME
//       coordinates the pixels came from.
//
// This is the "B-lite made visual" milestone from docs/11, in lean form: own
// 5x7 font + CPU raster, no Skia/window/GPU yet. The Windows milestone swaps
// the renderer for Skia + DirectWrite and the AX print for the live UIA
// provider; the model -> layout -> {pixels, a11y} structure is unchanged.
//
// Build & run (no Chromium tree needed):
//   g++ -std=c++17 -O2 blite/draw/blite_draw.cc -o /tmp/blite_draw \
//       && /tmp/blite_draw /tmp/blite_draw.ppm

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "font5x7.h"
#include "layout_engine.h"

namespace blite {
namespace {

struct Color {
  uint8_t r, g, b;
};

// A trivial RGB raster surface. Stands in for the Skia canvas.
class Image {
 public:
  Image(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h * 3, 255) {}

  void Set(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
      return;
    const size_t i = (static_cast<size_t>(y) * w_ + x) * 3;
    px_[i] = c.r;
    px_[i + 1] = c.g;
    px_[i + 2] = c.b;
  }

  void Fill(const Rect& r, Color c) {
    for (int y = r.y; y < r.y + r.h; ++y)
      for (int x = r.x; x < r.x + r.w; ++x)
        Set(x, y, c);
  }

  bool WritePPM(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
      return false;
    f << "P6\n" << w_ << " " << h_ << "\n255\n";
    f.write(reinterpret_cast<const char*>(px_.data()),
            static_cast<std::streamsize>(px_.size()));
    return f.good();
  }

 private:
  int w_, h_;
  std::vector<uint8_t> px_;
};

// Scale a layout-px rect into image space.
Rect Scaled(const Rect& r, int s) {
  return Rect{r.x * s, r.y * s, r.w * s, r.h * s};
}

// (b) The accessibility consumer of the SAME Layout. In the in-tree blite host
// these rects become AXNodeData::kCaretBounds (an IntList [x,y,w,h]) and the
// inline-text-box character offsets; here we print them so the coupling with
// the pixels is visible. Coordinates are reported in image space (post-scale),
// i.e. exactly the pixels the renderer drew.
void DumpAxGeometry(const DocModel& doc, const Layout& layout, int s) {
  std::cout << "\n--- accessibility geometry (from the SAME layout) ---\n";
  const Rect caret = Scaled(layout.caret, s);
  std::cout << "  kCaretBounds = [" << caret.x << ", " << caret.y << ", "
            << caret.w << ", " << caret.h << "]   (caret index " << doc.caret
            << ")\n";
  if (layout.has_selection) {
    const Rect sel = Scaled(layout.selection, s);
    std::cout << "  selection bounds = [" << sel.x << ", " << sel.y << ", "
              << sel.w << ", " << sel.h << "]   (chars [" << doc.sel_start
              << ", " << doc.sel_end << ") = \""
              << doc.text.substr(doc.sel_start, doc.sel_end - doc.sel_start)
              << "\")\n";
  }
  std::cout << "  per-character bounds:\n";
  for (size_t i = 0; i < doc.text.size(); ++i) {
    const Rect g = Scaled(layout.glyphs[i], s);
    std::cout << "    '" << doc.text[i] << "'  [" << g.x << ", " << g.y << ", "
              << g.w << ", " << g.h << "]\n";
  }
}

// The editing-event set this surface state would generate, mirrored from the
// headless spine (blite_host.cc / the T2 tests) so the two halves line up.
void DumpEvents() {
  std::cout << "\n--- generated a11y events (mirrors blite_host / T2) ---\n";
  std::cout << "  editableTextChanged, valueInTextFieldChanged  (field "
               "ancestor)\n";
  std::cout << "  documentSelectionChanged, textSelectionChanged\n";
  std::cout << "  caretBoundsChanged  (from kCaretBounds above)\n";
}

int Run(const std::string& out_path) {
  // The "custom surface" model: text, caret at end, "World" selected.
  DocModel doc;
  doc.text = "Hello, World!";
  doc.caret = static_cast<int>(doc.text.size());
  doc.sel_start = 7;
  doc.sel_end = 12;  // "World"

  const Metrics m;
  const int kScale = 8;

  // THE one layout pass.
  const Layout layout = LayOut(doc, m);

  std::cout << "=== B-lite drawn demo (lean CPU raster) ===\n";
  std::cout << "surface text: \"" << doc.text << "\"  caret=" << doc.caret
            << "  selection=[" << doc.sel_start << ", " << doc.sel_end << ")\n";
  std::cout << "one layout pass -> { pixels, a11y geometry }\n";

  // (a) The pixel consumer of the Layout.
  Image img(layout.width * kScale, layout.height * kScale);
  const Color kBg{245, 245, 248};
  const Color kInk{25, 25, 35};
  const Color kCaret{210, 40, 40};
  const Color kSel{180, 205, 255};

  img.Fill(Rect{0, 0, layout.width * kScale, layout.height * kScale}, kBg);
  if (layout.has_selection)
    img.Fill(Scaled(layout.selection, kScale), kSel);

  for (size_t i = 0; i < doc.text.size(); ++i) {
    const Glyph glyph = GlyphFor(doc.text[i]);
    const Rect cell = layout.glyphs[i];
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (glyph.rows[row] & (1 << (4 - col))) {
          img.Fill(Scaled(Rect{cell.x + col, cell.y + row, 1, 1}, kScale), kInk);
        }
      }
    }
  }
  img.Fill(Scaled(layout.caret, kScale), kCaret);

  if (!img.WritePPM(out_path)) {
    std::cerr << "failed to write " << out_path << "\n";
    return 1;
  }
  std::cout << "\nwrote image: " << out_path << "  (" << layout.width * kScale
            << "x" << layout.height * kScale << ")\n";

  // (b) The accessibility consumer of the same Layout.
  DumpAxGeometry(doc, layout, kScale);
  DumpEvents();

  std::cout << "\n=== drawn sample OK: model -> one layout -> pixels + a11y "
               "(no drift) ===\n";
  return 0;
}

}  // namespace
}  // namespace blite

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "blite_draw.ppm";
  return blite::Run(out);
}
