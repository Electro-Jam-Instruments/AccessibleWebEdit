// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The canonical edit sequence the end-to-end demo replays. Defined once and
// consumed by BOTH backends so the visual frames (linux_headless renderer) and
// the real accessibility events (in-tree AXTree e2e, and the Win32/UIA build)
// are driven by the exact same keystrokes. Each Step is a labelled snapshot of
// the document after one editing action.
//
// The sequence exercises the editing operations that matter for accessibility:
// insert, multi-char insert, selection, replace-selection (delete + insert),
// backspace, and caret navigation -- each of which must produce the right
// generated events and caret/selection geometry.

#ifndef DEMO_CORE_DEMO_SCRIPT_H_
#define DEMO_CORE_DEMO_SCRIPT_H_

#include <string>
#include <vector>

#include "text_document.h"

namespace demo {

struct Step {
  std::string label;
  TextDocument doc;
};

inline std::vector<Step> BuildDemoScript() {
  std::vector<Step> steps;
  TextDocument d;
  auto snap = [&](const std::string& label) { steps.push_back({label, d}); };

  snap("start (empty field)");
  d.Insert("Hello");
  snap("type \"Hello\"");
  d.Insert(", World");
  snap("type \", World\"");
  d.Insert("!");
  snap("type \"!\"");
  d.Select(7, 12);
  snap("select \"World\"");
  d.Insert("there");
  snap("replace selection -> \"there\"");
  d.Backspace();
  d.Backspace();
  snap("backspace x2");
  d.CaretTo(0);
  snap("caret to home");
  d.Insert("Hey. ");
  snap("type \"Hey. \" at home");

  return steps;
}

}  // namespace demo

#endif  // DEMO_CORE_DEMO_SCRIPT_H_
