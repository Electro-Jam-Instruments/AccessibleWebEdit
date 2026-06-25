// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A tiny, self-contained 5x7 bitmap font for the B-lite drawn demo. Just enough
// printable glyphs to render the sample string legibly. Each glyph is 7 rows
// top->bottom; in each row the 5 low bits are columns left->right (bit 4 =
// leftmost). This is intentionally lean -- the real cross-platform build uses
// HarfBuzz shaping + DirectWrite/CoreText/fontconfig glyphs (docs/11); the font
// is NOT the thesis, the layout->pixels+a11y coupling is.

#ifndef BLITE_DRAW_FONT5X7_H_
#define BLITE_DRAW_FONT5X7_H_

#include <cstdint>

namespace blite {

struct Glyph {
  uint8_t rows[7];
};

inline Glyph GlyphFor(char c) {
  switch (c) {
    case ' ': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    case 'H': return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'e': return {{0x00, 0x00, 0x0E, 0x11, 0x1E, 0x10, 0x0E}};
    case 'l': return {{0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    case 'o': return {{0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}};
    case ',': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}};
    case 'W': return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}};
    case 'r': return {{0x00, 0x00, 0x16, 0x18, 0x10, 0x10, 0x10}};
    case 'd': return {{0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}};
    case '!': return {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}};
    // Fallback: an open box, so any unmapped char is visibly "tofu".
    default:  return {{0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F}};
  }
}

}  // namespace blite

#endif  // BLITE_DRAW_FONT5X7_H_
