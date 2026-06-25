// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The editable text document model for the AccessibleWebEdit end-to-end demo.
// This is the "custom surface" state: the text plus a caret and an optional
// selection range [sel_start, sel_end). It is the producer-side model that a
// real canvas editor would own; nothing here knows about pixels or about the
// accessibility tree -- both of those are derived from this model + the layout.
//
// Dependency-free (std only) so it compiles identically standalone (g++) and
// inside the Chromium tree next to //ui/accessibility.

#ifndef DEMO_CORE_TEXT_DOCUMENT_H_
#define DEMO_CORE_TEXT_DOCUMENT_H_

#include <algorithm>
#include <string>

namespace demo {

class TextDocument {
 public:
  const std::string& text() const { return text_; }
  int caret() const { return caret_; }
  int sel_start() const { return sel_start_; }
  int sel_end() const { return sel_end_; }
  bool has_selection() const { return sel_end_ > sel_start_; }
  int length() const { return static_cast<int>(text_.size()); }

  // Insert text at the caret, replacing any selection first (standard editor
  // semantics). Caret lands after the inserted text; selection collapses.
  void Insert(const std::string& s) {
    if (has_selection())
      DeleteSelection();
    text_.insert(static_cast<size_t>(caret_), s);
    caret_ += static_cast<int>(s.size());
    CollapseSelection();
  }

  // Backspace: delete the selection if any, else the char before the caret.
  void Backspace() {
    if (has_selection()) {
      DeleteSelection();
      return;
    }
    if (caret_ > 0) {
      text_.erase(static_cast<size_t>(caret_ - 1), 1);
      --caret_;
    }
    CollapseSelection();
  }

  void DeleteSelection() {
    if (!has_selection())
      return;
    text_.erase(static_cast<size_t>(sel_start_),
                static_cast<size_t>(sel_end_ - sel_start_));
    caret_ = sel_start_;
    CollapseSelection();
  }

  void MoveCaret(int delta) { CaretTo(caret_ + delta); }

  void CaretTo(int pos) {
    caret_ = Clamp(pos);
    CollapseSelection();
  }

  // Select [a, b); caret (focus) goes to b.
  void Select(int a, int b) {
    a = Clamp(a);
    b = Clamp(b);
    sel_start_ = std::min(a, b);
    sel_end_ = std::max(a, b);
    caret_ = b;
  }

 private:
  int Clamp(int p) const { return std::max(0, std::min(p, length())); }
  void CollapseSelection() {
    sel_start_ = sel_end_ = caret_;
  }

  std::string text_;
  int caret_ = 0;
  int sel_start_ = 0;
  int sel_end_ = 0;
};

}  // namespace demo

#endif  // DEMO_CORE_TEXT_DOCUMENT_H_
