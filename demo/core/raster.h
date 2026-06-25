// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A tiny CPU raster surface for the demo: stands in for the Skia canvas. Given
// a Layout (from the single layout pass) it paints the selection highlight,
// the glyph runs, and the caret into an RGB buffer, which it can write as a
// PPM. The Win32 build blits the same RGB buffer to a GDI bitmap; the eventual
// production build replaces this file with Skia. Dependency-free (STL only).

#ifndef DEMO_CORE_RASTER_H_
#define DEMO_CORE_RASTER_H_

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "font5x7.h"
#include "layout_engine.h"
#include "text_document.h"

namespace demo {

struct Color {
  uint8_t r, g, b;
};

namespace palette {
constexpr Color kBg{245, 245, 248};
constexpr Color kInk{25, 25, 35};
constexpr Color kCaret{210, 40, 40};
constexpr Color kSelection{180, 205, 255};
}  // namespace palette

class Raster {
 public:
  Raster(int w, int h)
      : w_(w), h_(h), px_(static_cast<size_t>(w) * h * 3, 255) {}

  int width() const { return w_; }
  int height() const { return h_; }
  const std::vector<uint8_t>& rgb() const { return px_; }

  void Set(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
      return;
    const size_t i = (static_cast<size_t>(y) * w_ + x) * 3;
    px_[i] = c.r;
    px_[i + 1] = c.g;
    px_[i + 2] = c.b;
  }

  void Fill(const Rect& r, Color c, int scale) {
    for (int y = 0; y < r.h * scale; ++y)
      for (int x = 0; x < r.w * scale; ++x)
        Set(r.x * scale + x, r.y * scale + y, c);
  }

  // Paint the whole document frame from the layout. `scale` magnifies each
  // layout pixel into a scale x scale block so the bitmap font is legible.
  void PaintFrame(const TextDocument& doc, const Layout& layout, int scale) {
    Fill(Rect{0, 0, w_ / scale + 1, h_ / scale + 1}, palette::kBg, scale);
    if (layout.has_selection)
      Fill(layout.selection, palette::kSelection, scale);
    for (int i = 0; i < doc.length(); ++i) {
      const GlyphRows& glyph = GlyphFor(doc.text()[i]);
      const Rect& cell = layout.glyphs[i];
      for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
          if (GlyphPixel(glyph, row, col))
            Fill(Rect{cell.x + col, cell.y + row, 1, 1}, palette::kInk, scale);
    }
    Fill(layout.caret, palette::kCaret, scale);
  }

  bool WritePPM(const std::string& path) const {
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

}  // namespace demo

#endif  // DEMO_CORE_RASTER_H_
