// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared layout engine for the B-lite drawn demo.
//
// This header is the embodiment of the docs/11 non-negotiable constraint:
// ONE layout pass is the single source of truth that feeds BOTH
//   (a) the pixel renderer (blite_draw.cc -> a real raster image), and
//   (b) the accessibility geometry (caret bounds / per-character bounds that
//       map to AXNodeData::kCaretBounds and inline-text-box offsets).
// Because both consumers read the exact same Layout, the painted caret and the
// caret the screen reader announces cannot drift.
//
// Deliberately dependency-free (only <string>/<vector>/<cstdint>) so it can be
// compiled BOTH standalone with g++ (the runnable Linux demo) AND inside the
// Chromium tree alongside //ui/accessibility (the real AX bridge). The font and
// metrics here are a lean monospace stand-in; the Windows milestone swaps the
// renderer for Skia + DirectWrite and keeps this contract unchanged.

#ifndef BLITE_DRAW_LAYOUT_ENGINE_H_
#define BLITE_DRAW_LAYOUT_ENGINE_H_

#include <cstdint>
#include <string>
#include <vector>

namespace blite {

// A rectangle in layout pixels (pre-scale). Maps directly to the 4-int
// [x, y, width, height] form Chromium uses for kCaretBounds.
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

// The document model: text + caret + an optional selection range
// [sel_start, sel_end). The "custom surface" model, same role as
// MockCanvasEditor in blite_host.cc, extended with selection.
struct DocModel {
  std::string text;
  int caret = 0;
  int sel_start = 0;
  int sel_end = 0;  // selection is empty when sel_end <= sel_start
};

// Monospace metrics for the lean 5x7 bitmap font. cell = glyph box;
// advance = cell width + 1px inter-glyph gap.
struct Metrics {
  int origin_x = 2;
  int origin_y = 2;
  int cell_w = 5;
  int cell_h = 7;
  int advance = 6;
  int line_h = 10;
  int pad = 2;
};

// The output of the single layout pass. Read by the renderer AND the AX bridge.
struct Layout {
  std::vector<Rect> glyphs;  // bounds of each character cell, in layout px
  Rect caret;                // caret rect, in layout px
  bool has_selection = false;
  Rect selection;            // union rect of the selection, in layout px
  int width = 0;             // overall layout width, in layout px
  int height = 0;            // overall layout height, in layout px
};

// The one layout pass. Pure function of (model, metrics) -> geometry.
inline Layout LayOut(const DocModel& doc, const Metrics& m = Metrics{}) {
  Layout out;
  const int n = static_cast<int>(doc.text.size());
  out.glyphs.reserve(n);
  for (int i = 0; i < n; ++i) {
    out.glyphs.push_back(
        Rect{m.origin_x + i * m.advance, m.origin_y, m.cell_w, m.cell_h});
  }
  // Caret: a 1px bar at the caret index, one px taller than the cell each side.
  out.caret = Rect{m.origin_x + doc.caret * m.advance - 1, m.origin_y - 1, 1,
                   m.cell_h + 2};
  if (doc.sel_end > doc.sel_start) {
    out.has_selection = true;
    const int sx = m.origin_x + doc.sel_start * m.advance;
    const int ex = m.origin_x + doc.sel_end * m.advance;
    out.selection = Rect{sx, m.origin_y - 1, ex - sx, m.cell_h + 2};
  }
  out.width = m.origin_x + n * m.advance + m.pad;
  out.height = m.origin_y + m.line_h + m.pad;
  return out;
}

}  // namespace blite

#endif  // BLITE_DRAW_LAYOUT_ENGINE_H_
