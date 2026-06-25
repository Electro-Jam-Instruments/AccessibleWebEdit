// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Linux headless renderer for the end-to-end demo (standalone, plain g++).
//
// Replays the shared edit script (demo/core/demo_script.h) and, for each step,
// runs the single layout pass and paints a frame to a PPM via the CPU raster.
// It also prints, per step, the accessibility geometry derived from the SAME
// layout (caret bounds, selection bounds) and the editing-event set that the
// in-tree AXTree build emits for real -- so the visual frames and the a11y
// story are demonstrably the same pipeline.
//
// This is the runnable-on-Linux half of "show it on Linux or Windows": it needs
// no Chromium checkout, no window, no GPU. The Win32 build (demo/platform/win32)
// reuses the identical core to drive a real window + UIA + NVDA.
//
// Build & run:
//   g++ -std=c++17 -O2 -I../../core demo_render.cc -o /tmp/demo_render
//   /tmp/demo_render <out_dir>     # writes frame_000.ppm .. and an AX log

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "demo_script.h"
#include "layout_engine.h"
#include "raster.h"
#include "text_document.h"

namespace demo {
namespace {

constexpr int kScale = 8;

Rect Scaled(const Rect& r, int s) {
  return Rect{r.x * s, r.y * s, r.w * s, r.h * s};
}

// The accessibility view of a step, derived from the same Layout the pixels
// came from. In the in-tree build these become AXNodeData::kCaretBounds and the
// generated event set; here we print them next to the frame they match.
void PrintAxView(std::ostream& os, const Step& step, const Layout& layout,
                 const Step* prev) {
  const Rect caret = Scaled(layout.caret, kScale);
  os << "  caret index " << step.doc.caret() << "  kCaretBounds=[" << caret.x
     << "," << caret.y << "," << caret.w << "," << caret.h << "]\n";
  if (layout.has_selection) {
    const Rect sel = Scaled(layout.selection, kScale);
    os << "  selection chars [" << step.doc.sel_start() << ","
       << step.doc.sel_end() << ")=\""
       << step.doc.text().substr(
              step.doc.sel_start(),
              step.doc.sel_end() - step.doc.sel_start())
       << "\"  bounds=[" << sel.x << "," << sel.y << "," << sel.w << ","
       << sel.h << "]\n";
  }
  os << "  events:";
  const bool text_changed = !prev || prev->doc.text() != step.doc.text();
  const bool sel_changed =
      !prev || prev->doc.caret() != step.doc.caret() ||
      prev->doc.sel_start() != step.doc.sel_start() ||
      prev->doc.sel_end() != step.doc.sel_end();
  if (text_changed)
    os << " editableTextChanged valueInTextFieldChanged";
  if (sel_changed)
    os << " documentSelectionChanged textSelectionChanged caretBoundsChanged";
  if (!text_changed && !sel_changed)
    os << " (none)";
  os << "\n";
}

int Run(const std::string& out_dir) {
  const std::vector<Step> steps = BuildDemoScript();
  const Metrics m;

  // Fixed canvas size across all frames (so they compose into one animation).
  int max_w = 0;
  for (const Step& s : steps)
    max_w = std::max(max_w, LayOut(s.doc, m).width);
  const int canvas_w = max_w * kScale;
  const int canvas_h = (m.origin_y + m.line_h + m.pad) * kScale;

  std::cout << "=== AccessibleWebEdit end-to-end demo (Linux headless) ===\n";
  std::cout << "canvas " << canvas_w << "x" << canvas_h << ", " << steps.size()
            << " frames\n";
  std::cout << "one layout pass per step -> { PPM frame, a11y geometry+events "
               "}\n\n";

  for (size_t i = 0; i < steps.size(); ++i) {
    const Step& step = steps[i];
    const Layout layout = LayOut(step.doc, m);

    Raster raster(canvas_w, canvas_h);
    raster.PaintFrame(step.doc, layout, kScale);

    char name[64];
    std::snprintf(name, sizeof(name), "%s/frame_%03zu.ppm", out_dir.c_str(), i);
    if (!raster.WritePPM(name)) {
      std::cerr << "failed to write " << name << "\n";
      return 1;
    }

    std::cout << "[" << i << "] " << step.label << "   text=\""
              << step.doc.text() << "\"\n";
    PrintAxView(std::cout, step, layout, i ? &steps[i - 1] : nullptr);
    std::cout << "\n";
  }

  std::cout << "=== wrote " << steps.size() << " frames to " << out_dir
            << " ===\n";
  return 0;
}

}  // namespace
}  // namespace demo

int main(int argc, char** argv) {
  const std::string out_dir = argc > 1 ? argv[1] : ".";
  return demo::Run(out_dir);
}
