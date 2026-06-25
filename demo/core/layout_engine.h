// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The single layout pass for the end-to-end demo. Pure function of
// (TextDocument, Metrics) -> Layout. This is the doc-11 "single source of
// truth": the resulting Layout is consumed by BOTH the pixel renderer
// (raster.h) AND the accessibility bridge (kCaretBounds / per-character bounds
// fed to a real AXTree). Because both read the same Layout, the painted caret
// and the caret a screen reader announces cannot drift.
//
// Lean monospace metrics + a 5x7 bitmap font stand in for real shaping; the
// Windows milestone swaps the renderer for Skia + DirectWrite and keeps this
// contract. Dependency-free (std only).

#ifndef DEMO_CORE_LAYOUT_ENGINE_H_
#define DEMO_CORE_LAYOUT_ENGINE_H_

#include <vector>

#include "text_document.h"

namespace demo {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

struct Metrics {
  int origin_x = 4;
  int origin_y = 4;
  int cell_w = 5;
  int cell_h = 7;
  int advance = 6;   // cell_w + 1px inter-glyph gap (monospace)
  int line_h = 11;
  int pad = 4;
};

struct Layout {
  std::vector<Rect> glyphs;  // bounds of each character cell, layout px
  Rect caret;                // caret rect, layout px
  bool has_selection = false;
  Rect selection;            // union rect of the selection, layout px
  int width = 0;
  int height = 0;
};

inline Layout LayOut(const TextDocument& doc, const Metrics& m = Metrics{}) {
  Layout out;
  const int n = doc.length();
  out.glyphs.reserve(n);
  for (int i = 0; i < n; ++i) {
    out.glyphs.push_back(
        Rect{m.origin_x + i * m.advance, m.origin_y, m.cell_w, m.cell_h});
  }
  out.caret = Rect{m.origin_x + doc.caret() * m.advance - 1, m.origin_y - 1, 1,
                   m.cell_h + 2};
  if (doc.has_selection()) {
    out.has_selection = true;
    const int sx = m.origin_x + doc.sel_start() * m.advance;
    const int ex = m.origin_x + doc.sel_end() * m.advance;
    out.selection = Rect{sx, m.origin_y - 1, ex - sx, m.cell_h + 2};
  }
  out.width = m.origin_x + n * m.advance + m.pad;
  if (out.width < 220)
    out.width = 220;  // keep a stable canvas width across frames
  out.height = m.origin_y + m.line_h + m.pad;
  return out;
}

}  // namespace demo

#endif  // DEMO_CORE_LAYOUT_ENGINE_H_
