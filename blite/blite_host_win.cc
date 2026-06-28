// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// B-lite host (Windows) -- EXTENDS the Linux spine (blite_host.cc) with the
// platform-node / UIA layer so a real UIA client (NVDA) can attach to a real
// Win32 window and observe editing events end to end.
//
// Pipeline exercised, end to end, as a real running program:
//   MockCanvasEditor (custom surface model)
//     -> Bridge (emits AXTreeData + AXTreeUpdate deltas)        [unchanged from
//        -> AXTree (+ AXEventGenerator)                          blite_host.cc]
//          -> AXPlatformNodeWin / AXPlatformNodeDelegate
//            -> AXFragmentRootWin  (HWND <-> UIA bridge)
//              -> UIA (IRawElementProviderSimple, ITextProvider)
//                -> NVDA (UIA client) -> nvda-run.log "Speaking [...]"
//
// The surface -> bridge -> tree -> events half is identical to blite_host.cc
// (the spine is platform-agnostic by design, docs/09). What this file adds is
// the Windows-only platform layer the Linux host deliberately did NOT link:
//   1. a real Win32 HWND (RegisterClass / CreateWindow + message loop),
//   2. an AXPlatformNodeDelegate backed by the AXTree (one delegate per node),
//   3. AXPlatformNodeWin instances created from those delegates,
//   4. an AXFragmentRootWin so WM_GETOBJECT resolves a UIA provider from the
//      HWND (UiaReturnRawElementProvider path),
//   5. firing the platform events (UIA_Text_TextChangedEventId /
//      UIA_Text_TextSelectionChangedEventId) after the one insert + caret move.
//
// ============================ EVIDENCE LEVEL ============================
// COMPILE/LINK STATUS: this file has now been built against the REAL tag-149
// (149.0.7827.115) headers at C:\src\chromium\src and the TODO(VM) API guesses
// have been resolved against those headers (see blite_host_win.README.md
// "Windows compile results"). It compiles and links as the GN target
// //ui/accessibility/blite:blite_host_win.
//
// It has STILL NOT BEEN RUN (Smart App Control blocks freshly-built exes on the
// build box, and -- separately -- the NVDA end-to-end observation is the actual
// open verification). So everything screen-reader-observable remains
// [VM-UNVERIFIED]: do NOT upgrade any UIA/NVDA claim to PROVEN until the slice
// actually runs and NVDA speaks. This is review backlog item V1
// (results/expert-reviews/REVIEW-BACKLOG.md): the smallest insert+caret -> UIA
// -> NVDA slice.
// =======================================================================
//
// Target: Chromium tag 149.0.7827.115. Carries the Chromium BSD header by
// design (see NOTICE.md).

#include <windows.h>

// NOTE: the Chromium build defines WIN32_LEAN_AND_MEAN, which makes <windows.h>
// skip <objbase.h>. <uiautomation.h> (UIAutomationCore.h) uses the MSVC
// `interface` keyword, which is only defined (as `struct`) by the COM headers
// <objbase.h>/<combaseapi.h>. So those MUST be included BEFORE <uiautomation.h>
// or clang-cl errors with "unknown type name 'interface'". <oleacc.h> supplies
// LresultFromObject / IID_IAccessible. This ordering matches how the real
// ax_platform_node_win.h pulls these headers (objbase -> oleacc -> uiautomation).
#include <objbase.h>

#include <oleacc.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "base/at_exit.h"
#include "base/i18n/icu_util.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_executor.h"
#include "base/win/scoped_com_initializer.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_event_generator.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_node_position.h"
#include "ui/accessibility/ax_selection.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/accessibility/ax_tree_manager.h"
#include "ui/accessibility/ax_tree_update.h"
// Platform layer (Windows-only; leaves the lean cone -- see BUILD note).
#include "ui/accessibility/platform/ax_fragment_root_delegate_win.h"
#include "ui/accessibility/platform/ax_fragment_root_win.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/accessibility/platform/test_ax_node_wrapper.h"
#include "ui/accessibility/platform/ax_platform_for_test.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/accessibility/platform/ax_platform_tree_manager.h"
#include "ui/gfx/geometry/rect.h"

namespace ui {
namespace {

// Node ids for the mock document: root web area, the editable field, the static
// text, and the inline-text-box LEAF that actually carries the characters.
// AXPosition descends to the inline text box as the real text leaf (a StaticText
// with no inline box has no addressable whole-text position) -- without it,
// field->CreateTextPositionAt(0)->AsLeafTextPosition() is null-anchored and the
// UIA DocumentRange (Select / attribute queries) fails validation.
constexpr AXNodeID kRoot = 1;
constexpr AXNodeID kField = 2;
constexpr AXNodeID kText = 3;
constexpr AXNodeID kInline = 4;

// ===========================================================================
// SURFACE + BRIDGE -- copied verbatim from blite_host.cc so the spine is
// provably the same code across platforms. Do not diverge these.
// ===========================================================================

// The "custom surface": a trivial editor model. Real AccessibleWebEdit would
// be a canvas with its own layout/selection; here we only need text + caret.
// Per-character run style. The real editor's formatting unit. Extend with font
// family / weight / color for Phase 3.4; lists/headings live on block nodes.
struct CharStyle {
  bool bold = false;
  bool italic = false;
  bool underline = false;
  std::string font;  // font family ("" = the editor default, Consolas). Phase 3.4.
  bool operator==(const CharStyle& o) const {
    return bold == o.bold && italic == o.italic && underline == o.underline &&
           font == o.font;
  }
  bool operator!=(const CharStyle& o) const { return !(*this == o); }
};

// A maximal span of consecutive characters sharing one CharStyle.
struct StyleRun {
  int start = 0;
  int length = 0;
  CharStyle style;
};

// Block-level structure (Phase 3.2+). Each LINE of the buffer is one block. A
// paragraph/heading is its own block; consecutive list lines of the same kind
// group into one list. A kHeading line also carries a level (1-6) in the editor.
enum class BlockType { kParagraph, kBullet, kNumber, kHeading };

// A maximal run of same-type lines. A paragraph is a 1-line group; a run of
// kBullet/kNumber lines is one list group (ordered? = kNumber).
struct BlockGroup {
  BlockType type = BlockType::kParagraph;
  int first_line = 0;  // 0-based line index of the group's first line
  int line_count = 1;
  bool is_list() const {
    return type == BlockType::kBullet || type == BlockType::kNumber;
  }
};

class MockCanvasEditor {
 public:
  // Demo doc for Phase 3.2/3.3: a level-1 HEADING ("Groceries") + a 3-item bullet
  // list. Exercises block structure (kHeading + kList/kListItem/kListMarker)
  // end-to-end. (Mixed-format runs proven separately, commit 0798d8d.)
  MockCanvasEditor() {
    text_ = "Groceries\nApples\nBananas\nCherries";
    caret_ = 0;  // caret on the HEADING line so NVDA announces "heading level 1"
    anchor_off_ = caret_;  // collapsed selection at startup
    const CharStyle plain_style{false, false, false, ""};
    styles_.assign(text_.size(), plain_style);
    typing_style_ = plain_style;
    line_blocks_ = {BlockType::kHeading, BlockType::kBullet,
                    BlockType::kBullet, BlockType::kBullet};
    line_levels_ = {1, 0, 0, 0};  // line 0 is an <h1>
    // Phase 3.4: the heading "Groceries" (chars 0-8) uses a distinct font family.
    const CharStyle heading_style{false, false, false, "Courier New"};
    for (int i = 0; i < 9 && i < static_cast<int>(styles_.size()); ++i)
      styles_[i] = heading_style;
  }

  // Heading level (1-6) of a line, or 0 if the line is not a heading.
  int heading_level_of_line(int line) const {
    return (line >= 0 && line < static_cast<int>(line_levels_.size()))
               ? line_levels_[line]
               : 0;
  }

  // A simple demo TABLE (Phase 4): rows x columns of cell text, rendered after
  // the text blocks. Not yet part of the caret flow -- 4.1 is structure + cell
  // text reachable by a screen reader; caret-in-cell editing comes later.
  const std::vector<std::vector<std::string>>& table() const { return table_; }

  // The cell the CARET is in (row,col) when in the table, else {-1,-1}. Drives
  // cell focus + the in-cell caret. A collapsed caret in a cell is a TEXT
  // position, NOT a cell selection (doc 18).
  std::pair<int, int> caret_cell() const {
    return in_table_ ? std::make_pair(tr_, tc_) : std::make_pair(-1, -1);
  }
  int cell_off() const { return cell_off_; }
  int anchor_cell_off() const { return anchor_cell_off_; }
  // CELL-BLOCK selection rectangle (inclusive) -- only when caret AND anchor are
  // BOTH in the table and in DIFFERENT cells (cross-cell ⇒ cells, doc 18 / E4).
  // Else {-1,-1,-1,-1} (a same-cell selection is TEXT, not a block).
  std::tuple<int, int, int, int> selected_block() const {
    if (in_table_ && anchor_in_table_) {
      if (anchor_tr_ == tr_ && anchor_tc_ == tc_)
        return {-1, -1, -1, -1};  // same cell -> TEXT selection, not a block
      return {std::min(anchor_tr_, tr_), std::min(anchor_tc_, tc_),
              std::max(anchor_tr_, tr_), std::max(anchor_tc_, tc_)};
    }
    // E7 (D3): exactly ONE endpoint in the table (a body<->table Shift) -> snap to
    // WHOLE cells, never a half-selected cell from outside. Select whole rows from
    // the table top down to the in-table endpoint's row.
    if (in_table_ != anchor_in_table_) {
      const int trow = in_table_ ? tr_ : anchor_tr_;
      return {0, 0, trow, std::max(0, table_cols() - 1)};
    }
    return {-1, -1, -1, -1};
  }
  // A TEXT selection WITHIN one cell (same cell, different offsets) -- E3.
  bool has_cell_text_sel() const {
    return in_table_ && anchor_in_table_ && anchor_tr_ == tr_ &&
           anchor_tc_ == tc_ && anchor_cell_off_ != cell_off_;
  }
  int cell_sel_lo() const { return std::min(anchor_cell_off_, cell_off_); }
  int cell_sel_hi() const { return std::max(anchor_cell_off_, cell_off_); }
  bool cell_in_block(int r, int c) const {
    auto [r0, c0, r1, c1] = selected_block();
    return r0 >= 0 && r >= r0 && r <= r1 && c >= c0 && c <= c1;
  }
  const std::string& cell_text(int r, int c) const {
    static const std::string kEmpty;
    if (r >= 0 && r < static_cast<int>(table_.size()) && c >= 0 &&
        c < static_cast<int>(table_[r].size()))
      return table_[r][c];
    return kEmpty;
  }
  int cur_cell_len() const { return static_cast<int>(cell_text(tr_, tc_).size()); }

  const std::string& text() const { return text_; }

  // The block type of a given 0-based line (default paragraph past the vector).
  BlockType block_of_line(int line) const {
    return (line >= 0 && line < static_cast<int>(line_blocks_.size()))
               ? line_blocks_[line]
               : BlockType::kParagraph;
  }

  // Group lines into blocks: each paragraph is its own group; consecutive list
  // lines of the SAME kind form one list group. Used by the Bridge to build
  // kList/kListItem nodes and by WM_PAINT to draw markers.
  std::vector<BlockGroup> block_groups() const {
    int line_count = 1;
    for (char c : text_)
      if (c == '\n')
        ++line_count;
    std::vector<BlockGroup> out;
    for (int ln = 0; ln < line_count; ++ln) {
      const BlockType bt = block_of_line(ln);
      const bool list = (bt == BlockType::kBullet || bt == BlockType::kNumber);
      if (!out.empty() && list && out.back().type == bt)
        ++out.back().line_count;  // extend the current list
      else
        out.push_back({bt, ln, 1});  // new paragraph or new list
    }
    return out;
  }
  int caret() const { return caret_; }

  // --- selection (body, phase E2) -------------------------------------------
  // A body selection is [anchor_off_, caret_]; collapsed when equal. Shift+move
  // keeps the anchor (the WndProc decides); a plain move calls Collapse().
  int anchor() const { return anchor_off_; }
  bool has_text_sel() const { return !in_table_ && anchor_off_ != caret_; }
  int sel_lo() const { return std::min(anchor_off_, caret_); }
  int sel_hi() const { return std::max(anchor_off_, caret_); }
  // Drop the selection: anchor := caret, in whichever region the caret is.
  void Collapse() {
    anchor_in_table_ = in_table_;
    if (in_table_) {
      anchor_tr_ = tr_;
      anchor_tc_ = tc_;
      anchor_cell_off_ = cell_off_;
    } else {
      anchor_off_ = caret_;
    }
  }
  void CollapseToStart() { caret_ = sel_lo(); anchor_off_ = caret_; }
  void CollapseToEnd() { caret_ = sel_hi(); anchor_off_ = caret_; }
  bool DeleteSelectionIfAny() {
    if (!has_text_sel())
      return false;
    const int lo = sel_lo(), hi = sel_hi();
    text_.erase(lo, hi - lo);
    styles_.erase(styles_.begin() + lo, styles_.begin() + hi);
    caret_ = lo;
    anchor_off_ = lo;
    return true;
  }

  // --- in-cell editing + cell-block clear (phase E5) ------------------------
  // Mutable ref to the caret cell's text (bounds-checked; falls back to a dummy).
  std::string& cell_ref() {
    static std::string dummy;
    if (tr_ >= 0 && tr_ < static_cast<int>(table_.size()) && tc_ >= 0 &&
        tc_ < static_cast<int>(table_[tr_].size()))
      return table_[tr_][tc_];
    return dummy;
  }
  bool DeleteCellSelIfAny() {  // in-cell TEXT selection -> delete the chars
    if (!has_cell_text_sel())
      return false;
    const int lo = cell_sel_lo(), hi = cell_sel_hi();
    cell_ref().erase(lo, hi - lo);
    cell_off_ = lo;
    Collapse();
    return true;
  }
  bool ClearBlockIfAny() {  // cell-BLOCK selection -> clear the cells' contents
    auto [r0, c0, r1, c1] = selected_block();
    if (r0 < 0)
      return false;
    for (int r = r0; r <= r1; ++r)
      for (int c = c0; c <= c1; ++c)
        if (r < static_cast<int>(table_.size()) &&
            c < static_cast<int>(table_[r].size()))
          table_[r][c].clear();
    tr_ = anchor_tr_;  // caret to the anchor (block origin) cell, collapsed
    tc_ = anchor_tc_;
    cell_off_ = 0;
    Collapse();
    return true;
  }

  void InsertText(const std::string& s) {
    if (in_table_) {
      ClearBlockIfAny();      // typing replaces a cell-block selection (D4)...
      DeleteCellSelIfAny();   // ...or an in-cell text selection
      cell_ref().insert(cell_off_, s);
      cell_off_ += static_cast<int>(s.size());
      Collapse();
      return;
    }
    DeleteSelectionIfAny();  // typing replaces a selection
    text_.insert(caret_, s);
    styles_.insert(styles_.begin() + caret_, s.size(), typing_style_);
    caret_ += static_cast<int>(s.size());
    anchor_off_ = caret_;
  }

  // Replace the whole buffer -- used when a UIA client writes via
  // IValueProvider::SetValue. Caret goes to the end, like committing a value.
  void SetText(const std::string& s) {
    text_ = s;
    styles_.assign(s.size(), typing_style_);
    caret_ = static_cast<int>(text_.size());
  }

  // Keyboard editing ops (for the interactive --viewer mode). Each returns true
  // if the TEXT changed (vs. a pure caret move), so the host fires the right
  // UIA events. Per-char styles stay in lockstep with the text.
  bool Backspace() {
    if (in_table_) {
      if (ClearBlockIfAny() || DeleteCellSelIfAny())
        return true;
      if (cell_off_ <= 0)
        return false;  // NO-OP at cell start: never merge across cell boundaries
      cell_ref().erase(cell_off_ - 1, 1);
      --cell_off_;
      Collapse();
      return true;
    }
    if (DeleteSelectionIfAny())
      return true;  // delete the selection, not a char
    if (caret_ <= 0)
      return false;
    text_.erase(caret_ - 1, 1);
    styles_.erase(styles_.begin() + (caret_ - 1));
    --caret_;
    anchor_off_ = caret_;
    return true;
  }
  bool DeleteForward() {
    if (in_table_) {
      if (ClearBlockIfAny() || DeleteCellSelIfAny())
        return true;
      if (cell_off_ >= cur_cell_len())
        return false;  // NO-OP at cell end: never merge across cell boundaries
      cell_ref().erase(cell_off_, 1);
      Collapse();
      return true;
    }
    if (DeleteSelectionIfAny())
      return true;
    if (caret_ >= static_cast<int>(text_.size()))
      return false;
    text_.erase(caret_, 1);
    styles_.erase(styles_.begin() + caret_);
    anchor_off_ = caret_;
    return true;
  }
  void MoveCaret(int delta) {
    caret_ += delta;
    if (caret_ < 0)
      caret_ = 0;
    if (caret_ > static_cast<int>(text_.size()))
      caret_ = static_cast<int>(text_.size());
  }
  // Tab in a table (Word model, D5): move to the adjacent cell and SELECT its
  // whole text; Tab in the last cell appends a row. SelectWholeCell makes an
  // in-cell text selection [0, len] (collapsed if the cell is empty).
  void SelectWholeCell() {
    anchor_in_table_ = true;
    anchor_tr_ = tr_;
    anchor_tc_ = tc_;
    anchor_cell_off_ = 0;
    cell_off_ = cur_cell_len();
  }
  void TabNext() {
    if (!in_table_)
      return;  // body Tab: no-op for now (table-focused)
    if (tc_ < table_cols() - 1) {
      ++tc_;
    } else if (tr_ < table_rows() - 1) {
      ++tr_;
      tc_ = 0;
    } else {
      table_.push_back(std::vector<std::string>(table_cols(), ""));  // new row
      tr_ = table_rows() - 1;
      tc_ = 0;
    }
    SelectWholeCell();
  }
  void TabPrev() {
    if (!in_table_)
      return;
    if (tc_ > 0)
      --tc_;
    else if (tr_ > 0) {
      --tr_;
      tc_ = table_cols() - 1;
    } else
      return;  // no-op at the first cell
    SelectWholeCell();
  }

  void CaretHome() {
    if (in_table_)
      cell_off_ = 0;
    else
      caret_ = 0;
  }
  void CaretEnd() {
    if (in_table_)
      cell_off_ = cur_cell_len();
    else
      caret_ = static_cast<int>(text_.size());
  }

  // Move the caret one line up (dir=-1) or down (dir=+1), preserving the column.
  // Returns false if there is no line in that direction (caller may then enter the
  // table on a downward move past the last body line).
  bool CaretVertical(int dir) {
    int line = 0, col = 0;
    for (int i = 0; i < caret_ && i < static_cast<int>(text_.size()); ++i) {
      if (text_[i] == '\n') {
        ++line;
        col = 0;
      } else {
        ++col;
      }
    }
    const int target = line + dir;
    if (target < 0)
      return false;
    int ln = 0, s = 0;
    const int n = static_cast<int>(text_.size());
    for (int i = 0; i <= n; ++i) {
      if (i == n || text_[i] == '\n') {
        if (ln == target) {
          const int len = i - s;
          caret_ = s + (col < len ? col : len);
          return true;
        }
        ++ln;
        s = i + 1;
      }
    }
    return false;  // no line in that direction
  }

  // --- caret navigation incl. INTO the table (Phase 4 caret-in-cell) --------
  // The caret is either in the body text (in_table_=false, offset caret_) or on a
  // table CELL cursor (in_table_=true, cell (tr_,tc_)). Arrows move within the
  // current region and transition at the boundaries (Down past the last body line
  // enters the table; Up from the top table row returns to the body).
  bool in_table() const { return in_table_; }
  int table_rows() const { return static_cast<int>(table_.size()); }
  int table_cols() const {
    return table_.empty() ? 0 : static_cast<int>(table_[0].size());
  }

  void CaretLeft() {
    if (!in_table_) {
      MoveCaret(-1);
      return;
    }
    if (cell_off_ > 0) {
      --cell_off_;  // within the cell text
    } else if (tc_ > 0) {
      --tc_;
      cell_off_ = cur_cell_len();  // end of the previous cell
    } else if (tr_ > 0) {
      --tr_;
      tc_ = table_cols() - 1;
      cell_off_ = cur_cell_len();
    }  // at (0,0,0): stay
  }
  void CaretRight() {
    if (!in_table_) {
      MoveCaret(1);
      return;
    }
    if (cell_off_ < cur_cell_len()) {
      ++cell_off_;  // within the cell text
    } else if (tc_ < table_cols() - 1) {
      ++tc_;
      cell_off_ = 0;  // start of the next cell
    } else if (tr_ < table_rows() - 1) {
      ++tr_;
      tc_ = 0;
      cell_off_ = 0;
    }  // at the last cell end: stay
  }
  void CaretUp() {
    if (!in_table_) {
      CaretVertical(-1);
    } else if (tr_ > 0) {
      --tr_;  // up a row, keep column best-effort (D1)
      cell_off_ = std::min(cell_off_, cur_cell_len());
    } else {
      in_table_ = false;  // leave the table back into the body (at its end)
      caret_ = static_cast<int>(text_.size());
    }
  }
  void CaretDown() {
    if (in_table_) {
      if (tr_ < table_rows() - 1) {
        ++tr_;
        cell_off_ = std::min(cell_off_, cur_cell_len());
      }
    } else if (!CaretVertical(1) && !table_.empty()) {
      in_table_ = true;  // past the last body line -> enter the table top-left
      tr_ = 0;
      tc_ = 0;
      cell_off_ = 0;
    }
  }

  // --- formatting (per-character) ------------------------------------------
  CharStyle style_at(int i) const {
    return (i >= 0 && i < static_cast<int>(styles_.size())) ? styles_[i]
                                                            : typing_style_;
  }
  void SetRangeStyle(int start, int len, const CharStyle& s) {
    for (int i = start; i < start + len && i < static_cast<int>(styles_.size());
         ++i) {
      if (i >= 0)
        styles_[i] = s;
    }
  }
  // Maximal same-style spans, in order. The bridge emits one text run per entry;
  // the painter draws one styled run per entry. Empty text -> no runs.
  std::vector<StyleRun> runs() const {
    std::vector<StyleRun> out;
    for (int i = 0; i < static_cast<int>(text_.size()); ++i) {
      if (out.empty() || styles_[i] != out.back().style)
        out.push_back(StyleRun{i, 1, styles_[i]});
      else
        out.back().length += 1;
    }
    return out;
  }

  // Backward-compat uniform accessors (used by the single-run paint + a11y paths
  // until those go per-run): the first character's style, or the typing style.
  bool bold() const { return style_at(0).bold; }
  bool italic() const { return style_at(0).italic; }
  bool underline() const { return style_at(0).underline; }
  void set_bold(bool b) {
    typing_style_.bold = b;
    for (auto& s : styles_)
      s.bold = b;
  }

 private:
  std::string text_ = "hello";
  std::vector<CharStyle> styles_ =
      std::vector<CharStyle>(5, CharStyle{true, true, true});
  int caret_ = 5;
  int anchor_off_ = 5;  // body selection anchor; == caret_ when collapsed (E2)
  CharStyle typing_style_ = CharStyle{true, true, true};
  // Per-line block type (parallel to the lines of text_). Empty => all paragraph.
  std::vector<BlockType> line_blocks_;
  // Per-line heading level (1-6); 0 / out-of-range => not a heading.
  std::vector<int> line_levels_;
  // Demo table (Phase 4): table_[row][col] = cell text. Header row + 2 data rows.
  std::vector<std::vector<std::string>> table_ = {
      {"Qty", "Item"}, {"2", "Apples"}, {"6", "Bananas"}};
  // Caret-in-table state: when in_table_, the caret is a TEXT position inside
  // cell (tr_,tc_) at character offset cell_off_ (a real caret in the cell text).
  bool in_table_ = false;
  int tr_ = 0, tc_ = 0, cell_off_ = 0;
  // Table selection anchor (mirrors the caret's table state); set by Collapse().
  bool anchor_in_table_ = false;
  int anchor_tr_ = 0, anchor_tc_ = 0, anchor_cell_off_ = 0;
};

// ---------------------------------------------------------------------------
// THE SINGLE LAYOUT PASS (docs/11 + docs/14 thesis): pure function of the
// editor -> per-glyph rects + caret rect, in CLIENT pixels. The window paints
// from this (WM_PAINT) AND the a11y bridge feeds the SAME caret rect to UIA as
// kCaretBounds -- so the painted caret and the caret a screen reader announces
// cannot drift. Monospace metrics here; a Skia/DirectWrite swap keeps the
// contract. (Mirrors demo/core/layout_engine.h, adapted to MockCanvasEditor.)
// ---------------------------------------------------------------------------
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

struct Metrics {
  int origin_x = 14;
  int origin_y = 16;
  int advance = 13;  // per-glyph x step; the painter forces the font cell to
                     // this exact width so painted glyphs == layout positions.
  int cell_w = 13;
  int cell_h = 24;
  int line_h = 28;   // vertical step per line (multi-line). cell_h + leading.
};

struct Layout {
  std::vector<Rect> glyphs;  // bounds of each character cell (client px)
  Rect caret;                // caret rect (client px)
  int width = 0;
  int height = 0;
};

// Line+column of a character offset, honoring '\n' line breaks. (Explicit
// newlines only for now; soft word-wrap is a later refinement.)
inline void LineColOf(const std::string& t, int offset, int* line, int* col) {
  int ln = 0, cl = 0;
  for (int i = 0; i < offset && i < static_cast<int>(t.size()); ++i) {
    if (t[i] == '\n') { ++ln; cl = 0; } else { ++cl; }
  }
  *line = ln;
  *col = cl;
}

// A list-item line is indented by this many glyph cells, reserving space for the
// marker ("•"/"N.") drawn to its left. The indent lives in the ONE layout pass so
// painted text, caret, and a11y glyph bounds all shift together (stay coupled).
constexpr int kListIndentCells = 2;

inline int LayoutLineIndentPx(const MockCanvasEditor& ed, int line, const Metrics& m) {
  const BlockType bt = ed.block_of_line(line);
  return (bt == BlockType::kBullet || bt == BlockType::kNumber)
             ? kListIndentCells * m.advance
             : 0;
}

inline Layout LayOut(const MockCanvasEditor& ed, const Metrics& m = Metrics{}) {
  Layout out;
  const std::string& t = ed.text();
  out.glyphs.reserve(t.size());
  int line = 0, col = 0, max_col = 0;
  for (size_t i = 0; i < t.size(); ++i) {
    // Each char (including '\n') gets a rect at its current line/col so per-char
    // bounds stay addressable; the newline's rect sits at end-of-line.
    out.glyphs.push_back(Rect{m.origin_x + LayoutLineIndentPx(ed, line, m) +
                                  col * m.advance,
                              m.origin_y + line * m.line_h, m.cell_w, m.cell_h});
    if (t[i] == '\n') { ++line; col = 0; }
    else { ++col; if (col > max_col) max_col = col; }
  }
  int cl = 0, cc = 0;
  LineColOf(t, ed.caret(), &cl, &cc);
  out.caret = Rect{m.origin_x + LayoutLineIndentPx(ed, cl, m) + cc * m.advance - 1,
                   m.origin_y + cl * m.line_h - 2, 2, m.cell_h + 4};
  out.width =
      m.origin_x + (max_col + kListIndentCells) * m.advance + m.origin_x;
  out.height = m.origin_y + (line + 1) * m.line_h + m.origin_y;
  return out;
}

// Fill an inline-text-box leaf with the run's characters. The character offsets
// (cumulative end-x per glyph) make it a real, position-addressable text leaf so
// AXPosition can anchor a whole-text range on it. Rich-text run attributes
// (bold/italic/underline) attach here too -- AXPlatformNodeWin maps them to the
// UIA TextRange attributes (ax_platform_node_win.cc GetAttributeValue):
//   bold      -> FloatAttribute::kFontWeight (700)  -> UIA_FontWeightAttributeId
//   italic    -> TextStyle::kItalic bit in kTextStyle -> UIA_IsItalicAttributeId
//   underline -> kTextUnderlineStyle = kSolid -> UIA_UnderlineStyleAttributeId
// Apply the run's formatting. UIA's ITextRangeProvider::GetAttributeValue reads
// from each leaf's GetLowestPlatformAncestor (it excludes descendants of leaves),
// which for our text is the StaticText -- so the attributes must live there, not
// only on the inline box. We set them on BOTH so either resolution works.
// Per-RUN attribute writer (the real one): maps a CharStyle to the UIA TextRange
// attributes. The per-run Bridge calls this with each run's style.
void FillRunAttributes(AXNodeData& node, const CharStyle& s) {
  // Always set a weight (700 bold / 400 normal) so a bold->not-bold change is a
  // clean value change UIA_FontWeightAttributeId reports (not 700->absent).
  node.AddFloatAttribute(ax::mojom::FloatAttribute::kFontWeight,
                         s.bold ? 700.0f : 400.0f);
  if (s.italic) {
    node.AddIntAttribute(
        ax::mojom::IntAttribute::kTextStyle,
        1 << static_cast<int>(ax::mojom::TextStyle::kItalic));
  }
  if (s.underline) {
    node.AddIntAttribute(
        ax::mojom::IntAttribute::kTextUnderlineStyle,
        static_cast<int>(ax::mojom::TextDecorationStyle::kSolid));
  }
  // Per-run FONT FAMILY (Phase 3.4) -> UIA_FontNameAttributeId. Only set when the
  // run names a font, so default text keeps the system/inherited font.
  if (!s.font.empty())
    node.AddStringAttribute(ax::mojom::StringAttribute::kFontFamily, s.font);
}

// Back-compat overload for the current single-run callers: the first char's style.
void FillRunAttributes(AXNodeData& node, const MockCanvasEditor& editor) {
  FillRunAttributes(node, editor.style_at(0));
}

// (FillInlineTextBox removed -- the per-run Bridge::BuildTree builds each run's
// inline text box inline, with that run's char offsets + style.)

// The editable field node. Factored so BuildInitialTree and BuildEditDelta stay
// identical -- the edit delta rebuilds it fresh so run-attribute CHANGES (e.g.
// bold -> not bold) actually take effect (copying old data would keep stale
// attributes). docs/03 §1.1: the kTextField ROLE gates editing-event targeting;
// §2.2: it advertises the UIA Text pattern so TextSelectionChanged isn't dropped.
void FillFieldNode(AXNodeData& node, const MockCanvasEditor& editor) {
  node.id = kField;
  node.role = ax::mojom::Role::kTextField;
  node.AddState(ax::mojom::State::kEditable);
  node.AddState(ax::mojom::State::kRichlyEditable);
  node.AddState(ax::mojom::State::kFocusable);
  node.SetValue(editor.text());  // pair Text with Value (MSDN)
  // NON-atomic editable root: this moves UIA attribute resolution from the field
  // DOWN to the inner per-run text leaves (GetLowestPlatformAncestor stops at the
  // StaticText, not the field) -- the prerequisite for per-run (mixed) attributes.
  // IsAtomicTextField() = IsTextField && !kNonAtomicTextFieldRoot (ax_node_data.cc).
  node.AddBoolAttribute(ax::mojom::BoolAttribute::kNonAtomicTextFieldRoot, true);
  FillRunAttributes(node, editor);
  // Caret bounds from the SAME layout the window paints (docs/11/14 coupling):
  // the painted caret and the caret a screen reader announces share one source.
  const Layout lay = LayOut(editor);
  node.AddIntListAttribute(
      ax::mojom::IntListAttribute::kCaretBounds,
      {lay.caret.x, lay.caret.y, lay.caret.w, lay.caret.h});
  node.child_ids = {kText};
}

// The bridge: turns the editor model into accessibility tree updates. Caret /
// selection live in AXTreeData (tree level). docs/03 §1.1 (role-only editable
// root), §2.1/§2.2 (selection in AXTreeData).
class Bridge {
 public:
  // Build the WHOLE tree from the editor, block by block (docs/03): the field
  // contains paragraph text runs directly, and each list group becomes a
  // kList -> kListItem(kListMarker + text) subtree. Ids are allocated
  // sequentially (kRoot=1, kField=2 fixed; structure from 3 up) and the tree is
  // emitted pre-order (parents before children). Used for the initial tree and
  // every edit delta (full rebuild -- node COUNT changes; the delegate reconcile
  // + our manual UIA event firing handle that).
  AXTreeUpdate BuildTree(const MockCanvasEditor& editor, AXTreeID tree_id) {
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data.focus_id = kField;
    update.tree_data.tree_id = tree_id;
    update.root_id = kRoot;

    const std::string& text = editor.text();
    struct Line {
      int start;
      int len;
    };
    std::vector<Line> lines;
    {
      int s = 0;
      const int n = static_cast<int>(text.size());
      for (int i = 0; i <= n; ++i)
        if (i == n || text[i] == '\n') {
          lines.push_back({s, i - s});
          s = i + 1;
        }
    }

    int next_id = 3;
    auto NewId = [&]() { return next_id++; };
    std::map<int, AXNodeData> nodes;
    std::vector<int> line_inline(lines.size(), 0);  // inline-box id per line

    AXNodeData root;
    root.id = kRoot;
    root.role = ax::mojom::Role::kRootWebArea;
    root.child_ids = {kField};
    nodes[kRoot] = root;

    AXNodeData field;
    FillFieldNode(field, editor);  // id/role/states/value/non-atomic/caret bounds
    field.child_ids.clear();

    // Emit a StaticText+InlineTextBox pair for a RAW string; append the StaticText
    // to `parent` and return the inline-box id. One run per call (per-run mixed
    // lives WITHIN a line as a later merge; mixed-format proven, commit 0798d8d).
    auto EmitRawText = [&](const std::string& s, const CharStyle& style,
                           std::vector<int32_t>* parent) -> int {
      const int stid = NewId(), inid = NewId();
      AXNodeData stn;
      stn.id = stid;
      stn.role = ax::mojom::Role::kStaticText;
      stn.SetName(s);
      FillRunAttributes(stn, style);
      stn.child_ids = {inid};
      AXNodeData inn;
      inn.id = inid;
      inn.role = ax::mojom::Role::kInlineTextBox;
      inn.SetName(s);
      std::vector<int32_t> offs;
      for (int k = 0; k < static_cast<int>(s.size()); ++k)
        offs.push_back(static_cast<int32_t>((k + 1) * 7));
      inn.AddIntListAttribute(ax::mojom::IntListAttribute::kCharacterOffsets,
                              offs);
      FillRunAttributes(inn, style);
      nodes[stid] = stn;
      nodes[inid] = inn;
      parent->push_back(stid);
      return inid;
    };
    // Line-based wrapper: text + style come from the line, and the inline box is
    // recorded for the caret-selection mapping.
    auto EmitText = [&](int li, std::vector<int32_t>* parent) {
      line_inline[li] = EmitRawText(text.substr(lines[li].start, lines[li].len),
                                    editor.style_at(lines[li].start), parent);
    };

    for (const BlockGroup& g : editor.block_groups()) {
      if (g.type == BlockType::kHeading) {
        const int h_id = NewId();
        AXNodeData h;
        h.id = h_id;
        h.role = ax::mojom::Role::kHeading;
        h.AddIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel,
                          editor.heading_level_of_line(g.first_line));
        field.child_ids.push_back(h_id);
        EmitText(g.first_line, &h.child_ids);  // heading text under the heading
        nodes[h_id] = h;
        continue;
      }
      if (!g.is_list()) {
        EmitText(g.first_line, &field.child_ids);  // paragraph -> text in field
        continue;
      }
      const int list_id = NewId();
      AXNodeData list;
      list.id = list_id;
      list.role = ax::mojom::Role::kList;
      list.AddIntAttribute(ax::mojom::IntAttribute::kSetSize, g.line_count);
      field.child_ids.push_back(list_id);
      for (int k = 0; k < g.line_count; ++k) {
        const int li = g.first_line + k;
        const int item_id = NewId();
        AXNodeData item;
        item.id = item_id;
        item.role = ax::mojom::Role::kListItem;
        item.AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, k + 1);
        item.AddIntAttribute(ax::mojom::IntAttribute::kSetSize, g.line_count);
        const int marker_id = NewId();
        AXNodeData marker;
        marker.id = marker_id;
        marker.role = ax::mojom::Role::kListMarker;
        marker.SetName(g.type == BlockType::kNumber
                           ? (std::to_string(k + 1) + ".")
                           : std::string("\xE2\x80\xA2"));  // U+2022 bullet
        item.child_ids.push_back(marker_id);
        EmitText(li, &item.child_ids);  // item text after the marker
        nodes[marker_id] = marker;
        nodes[item_id] = item;
        list.child_ids.push_back(item_id);
      }
      nodes[list_id] = list;
    }

    // TABLE (Phase 4): kTable -> kRow -> kCell(text). Row/col counts on the table,
    // row/col index + 1x1 span on each cell -> UIA Grid/Table + GridItem. The
    // active cell (when the caret is in the table) becomes focused so NVDA reads
    // it on entry/navigation.
    int caret_cell_id = 0, caret_cell_inline = 0;
    const auto& tbl = editor.table();
    if (!tbl.empty()) {
      const int n_rows = static_cast<int>(tbl.size());
      const int n_cols = static_cast<int>(tbl[0].size());
      const int table_id = NewId();
      AXNodeData table;
      table.id = table_id;
      // kGrid (not kTable): a Grid IS a selection container (UIA Selection
      // pattern), so cells can be selected -- Phase 4.2. kMultiselectable allows
      // selecting more than one cell.
      table.role = ax::mojom::Role::kGrid;
      table.AddState(ax::mojom::State::kMultiselectable);
      table.AddIntAttribute(ax::mojom::IntAttribute::kTableRowCount, n_rows);
      table.AddIntAttribute(ax::mojom::IntAttribute::kTableColumnCount, n_cols);
      field.child_ids.push_back(table_id);
      for (int r = 0; r < n_rows; ++r) {
        const int row_id = NewId();
        AXNodeData row;
        row.id = row_id;
        row.role = ax::mojom::Role::kRow;
        row.AddIntAttribute(ax::mojom::IntAttribute::kTableRowIndex, r);
        table.child_ids.push_back(row_id);
        for (int c = 0; c < static_cast<int>(tbl[r].size()); ++c) {
          const int cell_id = NewId();
          AXNodeData cell;
          cell.id = cell_id;
          // Row 0 = column headers; data rows = kGridCell (always selectable, the
          // SelectionItem pattern -> Phase 4.2 cell selection).
          cell.role = (r == 0) ? ax::mojom::Role::kColumnHeader
                               : ax::mojom::Role::kGridCell;
          cell.AddState(ax::mojom::State::kFocusable);  // cell can hold the caret
          cell.AddIntAttribute(ax::mojom::IntAttribute::kTableCellRowIndex, r);
          cell.AddIntAttribute(ax::mojom::IntAttribute::kTableCellColumnIndex, c);
          cell.AddIntAttribute(ax::mojom::IntAttribute::kTableCellRowSpan, 1);
          cell.AddIntAttribute(ax::mojom::IntAttribute::kTableCellColumnSpan, 1);
          cell.SetName(tbl[r][c]);  // cell name = its text (name-from-contents),
                                    // so the cell self-describes on table nav
          // Phase 4.2: pre-select one data cell to exercise the Selection
          // pattern (GetSelection returns it; ISelectionItemProvider reports
          // IsSelected; the visual highlights the same cell).
          // CELL-BLOCK selection (Shift, phase E4) -> kSelected. A bare caret in
          // a cell does NOT select the cell (it's a text position).
          if (editor.cell_in_block(r, c))
            cell.AddBoolAttribute(ax::mojom::BoolAttribute::kSelected, true);
          const int cell_inline =
              EmitRawText(tbl[r][c], CharStyle{}, &cell.child_ids);
          // The cell holding the caret -> focused, with the caret inside its text.
          if (r == editor.caret_cell().first &&
              c == editor.caret_cell().second) {
            caret_cell_id = cell_id;
            caret_cell_inline = cell_inline;
          }
          nodes[cell_id] = cell;
          row.child_ids.push_back(cell_id);
        }
        nodes[row_id] = row;
      }
      nodes[table_id] = table;
    }

    nodes[kField] = field;

    // Focus + selection. When the caret is IN the table, focus the active cell
    // (so NVDA announces "<cell>, selected" on entry/nav) with a degenerate caret
    // in the cell's text. Otherwise the field is focused and the caret maps to the
    // owning body line's inline box at its local offset.
    if (editor.in_table() && caret_cell_id) {
      update.tree_data.focus_id = caret_cell_id;
      const int n = caret_cell_inline ? caret_cell_inline : caret_cell_id;
      if (editor.has_cell_text_sel()) {
        // E3: TEXT selection WITHIN the cell -> a real range on the cell's box.
        update.tree_data.sel_anchor_object_id = n;
        update.tree_data.sel_anchor_offset = editor.anchor_cell_off();
        update.tree_data.sel_focus_object_id = n;
        update.tree_data.sel_focus_offset = editor.cell_off();
      } else {
        // E1 caret OR E4 cell-block: a degenerate text caret at the focus cell.
        // (Block cells carry kSelected via cell_in_block; the text range stays
        // collapsed so NVDA reads the cell selection, not partial text.)
        update.tree_data.sel_anchor_object_id = n;
        update.tree_data.sel_anchor_offset = editor.cell_off();
        update.tree_data.sel_focus_object_id = n;
        update.tree_data.sel_focus_offset = editor.cell_off();
      }
    } else {
      // Body: map BOTH anchor and focus to (inline box, local offset) so a
      // Shift-selection is a real range (E2), not degenerate.
      auto map_body = [&](int off, int* node, int* local) {
        *node = kField;
        *local = 0;
        for (int li = 0; li < static_cast<int>(lines.size()); ++li)
          if (off >= lines[li].start && off <= lines[li].start + lines[li].len) {
            *node = line_inline[li] ? line_inline[li] : kField;
            *local = off - lines[li].start;
            return;
          }
      };
      int an = kField, al = 0, fn = kField, fl = 0;
      map_body(editor.anchor(), &an, &al);
      map_body(editor.caret(), &fn, &fl);
      update.tree_data.sel_anchor_object_id = an;
      update.tree_data.sel_anchor_offset = al;
      update.tree_data.sel_focus_object_id = fn;
      update.tree_data.sel_focus_offset = fl;
    }

    // Emit pre-order so parents precede children in update.nodes.
    std::function<void(int)> emit = [&](int id) {
      auto it = nodes.find(id);
      if (it == nodes.end())
        return;
      update.nodes.push_back(it->second);
      for (int c : it->second.child_ids)
        emit(c);
    };
    emit(kRoot);
    return update;
  }

  AXTreeUpdate BuildInitialTree(const MockCanvasEditor& editor) {
    return BuildTree(editor, AXTreeID::CreateNewAXTreeID());
  }
  AXTreeUpdate BuildEditDelta(const MockCanvasEditor& editor, AXTree* tree) {
    return BuildTree(editor, tree->data().tree_id);
  }
};

// ===========================================================================
// PLATFORM LAYER (Windows) -- the part blite_host.cc deliberately omitted.
// ===========================================================================

class BliteAXHost;  // forward decl for the delegate back-pointer.

// One AXPlatformNodeDelegate per AXNode. AXPlatformNodeWin talks to the tree
// exclusively through this delegate, so this is the minimum surface that makes
// a node UIA-resolvable.
//
// RESOLVED(VM): at tag 149 `AXPlatformNodeDelegate` is a CONCRETE class (no
// longer abstract, and there is no separate ...DelegateBase). It carries the
// backing `AXNode*` itself (protected ctor `AXPlatformNodeDelegate(AXNode*)`,
// plus `SetNode`/`node()`), and supplies default impls for nearly everything
// (GetData, GetRole, GetTreeData, GetUniqueId, attribute accessors, ...). The
// only methods whose defaults are no-ops -- and which therefore must be
// overridden to wire the AXNode tree to the per-node platform nodes -- are the
// navigation/native-accessible quartet below. So this subclass is small.
class BliteNodeDelegate : public AXPlatformNodeDelegate {
 public:
  BliteNodeDelegate(BliteAXHost* host, AXNode* node)
      // RESOLVED(VM): the protected `AXPlatformNodeDelegate(AXNode*)` ctor sets
      // the backing node; the base then serves GetData()/GetRole()/etc. from
      // it. No GetData override needed (the base returns node_->data()).
      : AXPlatformNodeDelegate(node), host_(host) {
    // RESOLVED(VM): the tag-149 factory is
    //   static AXPlatformNode::Pointer Create(AXPlatformNodeDelegate&);
    // It takes the delegate BY REFERENCE (not pointer) and returns a
    // std::unique_ptr<AXPlatformNode, Deleter> whose Deleter calls Destroy().
    // So lifetime is RAII via `platform_node_` -- there is NO manual Destroy()
    // in the dtor (that was the old shape). On Windows the concrete instance is
    // AXPlatformNodeWin.
    platform_node_ = AXPlatformNode::Create(*this);
  }

  ~BliteNodeDelegate() override = default;  // Pointer's Deleter calls Destroy().

  AXPlatformNode* platform_node() const { return platform_node_.get(); }

  // --- AXPlatformNodeDelegate overrides (the load-bearing few) -------------
  //
  // RESOLVED(VM): exact tag-149 signatures, confirmed against
  // ax_platform_node_delegate.h:
  //   gfx::NativeViewAccessible GetNativeViewAccessible();          // non-const
  //   gfx::NativeViewAccessible GetParent() const;
  //   size_t                    GetChildCount() const;
  //   gfx::NativeViewAccessible ChildAtIndex(size_t index) const;
  // On Windows gfx::NativeViewAccessible == IAccessible* (ui/gfx/native_ui_-
  // types.h); the platform node hands back its own IAccessible. UIA is layered
  // on top via QueryInterface on the fragment root (see OnGetObject).

  gfx::NativeViewAccessible GetNativeViewAccessible() override {
    return platform_node_ ? platform_node_->GetNativeViewAccessible() : nullptr;
  }

  // Tree navigation. UIA walks parent/children to build its tree; we resolve
  // ids back through the host's id->delegate map. The base-class defaults for
  // these are no-ops (return null / 0), so we MUST override them.
  gfx::NativeViewAccessible GetParent() const override;       // see below
  gfx::NativeViewAccessible ChildAtIndex(size_t index) const override;
  size_t GetChildCount() const override;
  // The owning HWND for this node -- how UIA associates the content with this
  // window's fragment root. This was the missing link that left the window
  // childless: AXFragmentRootWin returns the real HWND but our content delegates
  // returned null, so UIA could not tie the subtree to the window.
  gfx::AcceleratedWidget GetTargetForNativeAccessibilityEvent() override;
  // The tree's focused node (tree_data.focus_id) -- so AXPlatformNodeWin reports
  // HasKeyboardFocus on the field and UIA's GetFocusedElement lands on it (and the
  // caret/selection, which is gated on focus, gets exposed).
  gfx::NativeViewAccessible GetFocus() const override;
  // Resolve node ids to platform nodes (base returns nullptr) -- the Text pattern
  // GetSelection resolves the selection's anchor/focus objects through this, so
  // without it the caret/insertion point never surfaces.
  AXPlatformNode* GetFromNodeID(int32_t id) override;
  // Same resolution but keyed by (tree id, node id) -- ITextRangeProvider::Select
  // resolves the selection's delegate through this; base returns null -> DCHECK.
  AXPlatformNode* GetFromTreeIDAndNodeID(const AXTreeID& tree_id,
                                         int32_t id) override;
  // The tree's text selection -- the source GetSelection turns into the degenerate
  // caret range (the insertion point a screen reader tracks).
  const AXSelection GetUnignoredSelection() const override;
  // Surface the AXNode's COMPUTED pos-in-set / set-size (from the kList/kListItem
  // structure) to the platform layer. Base returns nullopt -> UIA
  // PositionInSet/SizeOfSet came back 0 even though the AXNode computes 1/2/3.
  std::optional<int> GetPosInSet() const override { return node()->GetPosInSet(); }
  std::optional<int> GetSetSize() const override { return node()->GetSetSize(); }
  // The node's on-screen bounds, taken from the SAME layout the window paints
  // (base returns an empty rect) -- so a UIA client reads element/caret bounds at
  // the painted location. This is the pixels<->a11y geometry coupling.
  gfx::Rect GetBoundsRect(AXCoordinateSystem coordinate_system,
                          AXClippingBehavior clipping_behavior,
                          AXOffscreenResult* offscreen_result) const override;
  // Screen rect for a character sub-range [start,end) -- the per-character path
  // UIA's ITextRangeProvider::GetBoundingRectangles uses (and the degenerate
  // caret rect). From the same layout metrics the window paints. Base = empty.
  gfx::Rect GetInnerTextRangeBoundsRect(
      int start_offset,
      int end_offset,
      AXCoordinateSystem coordinate_system,
      AXClippingBehavior clipping_behavior,
      AXOffscreenResult* offscreen_result) const override;
  // Apply a client-initiated action (client->provider WRITE direction). Handles
  // kSetValue (IValueProvider::SetValue) by mutating the editor+tree; base returns
  // false (E_FAIL), so without this every client write is rejected.
  bool AccessibilityPerformAction(const AXActionData& data) override;

  // RESOLVED(VM): no GetUniqueId override needed. The base
  // AXPlatformNodeDelegate::GetUniqueId() returns a stable per-instance
  // AXPlatformNodeId (lazily created AXUniqueId), which is exactly what UIA
  // runtime-id generation needs. AXNode::data().id is NOT the UIA unique id.

 private:
  raw_ptr<BliteAXHost> host_;
  AXPlatformNode::Pointer platform_node_;
};

// AXFragmentRootDelegateWin lets AXFragmentRootWin find the child of the HWND
// (our document root) and ask whether the host control "is" the fragment root.
// This is the documented standalone-host bridge (the same interface views uses
// for a desktop-widget HWND).
//
// RESOLVED(VM): the AXFragmentRootDelegateWin interface lives in its OWN header
// (ax_fragment_root_delegate_win.h, not ax_fragment_root_win.h) at tag 149, and
// the three pure-virtuals are exactly:
//   gfx::NativeViewAccessible GetChildOfAXFragmentRoot();
//   gfx::NativeViewAccessible GetParentOfAXFragmentRoot();
//   bool                      IsAXFragmentRootAControlElement();
// Defined below; declared here so BliteAXHost::ApplyClientSetValue can fire the
// platform events after applying a client write.
void FirePlatformEditEvents(BliteAXHost* host);

class BliteAXHost : public AXFragmentRootDelegateWin {
 public:
  BliteAXHost(HWND hwnd, AXTree* tree) : hwnd_(hwnd), tree_(tree) {
    // Build a delegate (+ AXPlatformNodeWin) for every node. BliteNodeDelegate
    // overrides GetTargetForNativeAccessibilityEvent() to return OUR HWND --
    // which is how UIA ties the content to THIS window's fragment. TestAXNodeWrapper
    // can't do this (it returns a mock/null HWND, no setter), and that mismatch
    // is exactly why the content never appeared under the window.
    MaterializeDelegates();  // a delegate per tree node (dynamic: per-run nodes)
    // Create the fragment root bound to the HWND. This is what makes
    // WM_GETOBJECT resolve a UIA provider: AXFragmentRootWin installs the
    // bridge so the UIA root object id resolves to our root.
    //
    // RESOLVED(VM): tag 149 has NO `AXFragmentRootWin::Create(...)` factory.
    // The constructor is PUBLIC:
    //   AXFragmentRootWin(gfx::AcceleratedWidget widget,
    //                     AXFragmentRootDelegateWin* delegate);
    // gfx::AcceleratedWidget == HWND on Windows. Own it with a unique_ptr.
    fragment_root_ =
        std::make_unique<AXFragmentRootWin>(hwnd_, this);
    // Diagnostic: is the fragment-root -> child link actually wired?
    std::cout << "[host] fragment child non-null="
              << (GetChildOfAXFragmentRoot() != nullptr)
              << " fragmentChildDelegate non-null="
              << (fragment_root_->GetChildNodeDelegate() != nullptr)
              << " fragmentNVA non-null="
              << (fragment_root_->GetNativeViewAccessible() != nullptr) << "\n";
  }

  ~BliteAXHost() override = default;

  AXTree* tree() { return tree_; }
  HWND hwnd() const { return hwnd_; }

  // Wire the editor+bridge so a client-initiated action can mutate them.
  void SetEditContext(MockCanvasEditor* editor, Bridge* bridge) {
    editor_ = editor;
    bridge_ = bridge;
  }
  MockCanvasEditor* editor() const { return editor_; }  // for WM_PAINT

  // A node's on-screen bounds (physical px) from the SAME layout the window
  // paints: the text-content nodes get the text's bounding box; the root gets
  // the client area. Client px -> screen via ClientToScreen.
  gfx::Rect NodeScreenBounds(AXNodeID id) {
    if (!editor_)
      return gfx::Rect();
    Metrics m;
    if (id == kRoot) {
      RECT cl;
      ::GetClientRect(hwnd_, &cl);
      POINT o = {0, 0};
      ::ClientToScreen(hwnd_, &o);
      return gfx::Rect(o.x, o.y, cl.right - cl.left, cl.bottom - cl.top);
    }
    // Multi-line text bounding box: widest column x line count.
    const std::string& t = editor_->text();
    int lines = 1, col = 0, max_col = 0;
    for (char c : t) {
      if (c == '\n') { ++lines; col = 0; }
      else { ++col; if (col > max_col) max_col = col; }
    }
    int cw = (max_col > 0 ? max_col : 1) * m.advance;
    int ch = lines * m.line_h;
    POINT tl = {m.origin_x, m.origin_y};
    ::ClientToScreen(hwnd_, &tl);
    return gfx::Rect(tl.x, tl.y, cw, ch);
  }

  // Screen rect spanning characters [start,end), line-aware. A degenerate range
  // (start==end) returns width 0 on its line -- AXRange widens it to a 1px caret
  // bar (and DCHECKs width==0 first). A same-line span is its width; a cross-line
  // span is approximated on the start line (per-line rects need AX line structure).
  gfx::Rect InnerTextRangeScreenBounds(int start_offset, int end_offset) {
    if (!editor_)
      return gfx::Rect();
    Metrics m;
    const std::string& t = editor_->text();
    int sl, sc, el, ec;
    LineColOf(t, start_offset, &sl, &sc);
    LineColOf(t, end_offset, &el, &ec);
    int x = m.origin_x + sc * m.advance;
    int y = m.origin_y + sl * m.line_h;
    int w = (el == sl) ? (ec - sc) * m.advance : m.cell_w;
    POINT tl = {x, y};
    ::ClientToScreen(hwnd_, &tl);
    return gfx::Rect(tl.x, tl.y, w, m.cell_h);
  }

  // Apply a client write (IValueProvider::SetValue): replace the editor buffer,
  // re-serialize the field value + static-text + caret as one atomic delta, then
  // fire the platform events so the client observes the change. This is the
  // client->provider WRITE half of the bidirectional flow.
  bool ApplyClientSetValue(const std::string& value) {
    if (!editor_ || !bridge_ || !tree_)
      return false;
    editor_->SetText(value);
    AXTreeUpdate delta = bridge_->BuildEditDelta(*editor_, tree_);
    if (!tree_->Unserialize(delta)) {
      std::cerr << "[host] client SetValue Unserialize failed: " << tree_->error()
                << "\n";
      return false;
    }
    MaterializeDelegates();  // any new run nodes get delegates
    std::cout << "[host] client SetValue applied -> \"" << editor_->text()
              << "\"\n";
    FirePlatformEditEvents(this);
    ::InvalidateRect(hwnd_, nullptr, TRUE);  // repaint the visual surface
    return true;
  }

  // Apply a client selection/caret move (ITextRangeProvider::Select ->
  // kSetSelection): update the tree-data selection to the client's anchor/focus
  // and fire TextSelectionChanged. This is cursor-routing (backlog V4): a screen
  // reader / braille display moving the insertion point from the client side.
  bool ApplyClientSetSelection(int32_t anchor_id, int anchor_offset,
                               int32_t focus_id, int focus_offset) {
    if (!tree_)
      return false;
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data = tree_->data();
    update.tree_data.sel_anchor_object_id = anchor_id;
    update.tree_data.sel_anchor_offset = anchor_offset;
    update.tree_data.sel_focus_object_id = focus_id;
    update.tree_data.sel_focus_offset = focus_offset;
    if (!tree_->Unserialize(update)) {
      std::cerr << "[host] client SetSelection Unserialize failed: "
                << tree_->error() << "\n";
      return false;
    }
    std::cout << "[host] client SetSelection applied -> anchor(" << anchor_id
              << "," << anchor_offset << ") focus(" << focus_id << ","
              << focus_offset << ")\n";
    if (AXPlatformNode* field = PlatformNodeFor(kField))
      field->NotifyAccessibilityEvent(ax::mojom::Event::kTextSelectionChanged);
    ::InvalidateRect(hwnd_, nullptr, TRUE);  // repaint the visual surface
    return true;
  }

  // Apply a LOCAL keyboard edit (the editor was already mutated by the WndProc):
  // re-serialize, fire UIA events (text+value only when the text changed,
  // selection always), and repaint. Same pixels<->a11y pipeline as the scripted
  // edit, driven live by typing.
  void ApplyLocalEdit(bool text_changed) {
    if (!editor_ || !bridge_ || !tree_)
      return;
    AXTreeUpdate delta = bridge_->BuildEditDelta(*editor_, tree_);
    if (!tree_->Unserialize(delta))
      return;
    MaterializeDelegates();  // any new run nodes get delegates
    if (AXPlatformNode* field = PlatformNodeFor(kField)) {
      if (text_changed) {
        field->NotifyAccessibilityEvent(ax::mojom::Event::kTextChanged);
        field->NotifyAccessibilityEvent(ax::mojom::Event::kValueChanged);
      }
      field->NotifyAccessibilityEvent(ax::mojom::Event::kTextSelectionChanged);
    }
    // Caret-in-table: fire FOCUS on the active cell so NVDA announces it on entry
    // and on each cell move; fire FOCUS back on the field when leaving the table.
    const bool now_in_table = editor_->in_table();
    if (now_in_table) {
      if (AXPlatformNode* cell = PlatformNodeFor(tree_->data().focus_id))
        cell->NotifyAccessibilityEvent(ax::mojom::Event::kFocus);
    } else if (was_in_table_) {
      if (AXPlatformNode* field = PlatformNodeFor(kField))
        field->NotifyAccessibilityEvent(ax::mojom::Event::kFocus);
    }
    was_in_table_ = now_in_table;
    ::InvalidateRect(hwnd_, nullptr, TRUE);
  }

  // Keyboard handlers call RequestSync (cheap: just sets a flag) instead of
  // ApplyLocalEdit directly. Firing UIA events from INSIDE a WM_CHAR/WM_KEYDOWN
  // dispatch can stall the STA message loop (a UIA event raise re-enters the
  // pump). FlushPendingSync runs the tree update + event firing at the message-
  // loop level (after DispatchMessage returns), exactly like the scripted path.
  void RequestSync(bool text_changed) {
    pending_sync_ = true;
    pending_text_changed_ = pending_text_changed_ || text_changed;
  }
  void FlushPendingSync() {
    if (!pending_sync_)
      return;
    const bool text_changed = pending_text_changed_;
    pending_sync_ = false;
    pending_text_changed_ = false;
    ApplyLocalEdit(text_changed);
  }

  // Reconcile delegates with the tree: add a delegate (+AXPlatformNodeWin) for
  // every current node, and DROP delegates whose node was removed (e.g. a run
  // deleted when formatting merges runs). Called after each Unserialize -- i.e.
  // AFTER the tree update finishes (no GetTreeUpdateInProgressState) and BEFORE
  // any UIA event is fired -- so NVDA's reentrant get_accChild can never walk a
  // dangling delegate (the crash 3.1-ROBUST fixes).
  void MaterializeDelegates() {
    std::set<AXNodeID> live;
    MaterializeSubtree(tree_->root(), &live);
    for (auto it = delegates_.begin(); it != delegates_.end();) {
      if (live.count(it->first))
        ++it;
      else
        it = delegates_.erase(it);  // node gone -> destroy its platform node
    }
  }
  void MaterializeSubtree(AXNode* n, std::set<AXNodeID>* live) {
    if (!n)
      return;
    live->insert(n->id());
    if (!delegates_.count(n->id()))
      delegates_[n->id()] = std::make_unique<BliteNodeDelegate>(this, n);
    for (size_t i = 0; i < n->GetChildCount(); ++i)
      MaterializeSubtree(n->GetChildAtIndex(i), live);
  }

  BliteNodeDelegate* DelegateFor(AXNodeID id) {
    auto it = delegates_.find(id);
    return it == delegates_.end() ? nullptr : it->second.get();
  }

  AXPlatformNode* PlatformNodeFor(AXNodeID id) {
    BliteNodeDelegate* d = DelegateFor(id);
    return d ? d->platform_node() : nullptr;
  }

  // Handle WM_GETOBJECT: when UIA (or MSAA) queries the HWND, hand back the
  // fragment root's provider.
  //
  // RESOLVED(VM): modeled on the authoritative handler in
  // ui/views/win/hwnd_message_handler.cc (HWNDMessageHandler::OnGetObject).
  // The lparam selects the API:
  //   * UiaRootObjectId -> UIA. Get the fragment root's IAccessible via
  //     GetNativeViewAccessible() (which performs the required bookkeeping),
  //     QueryInterface it to IRawElementProviderSimple, then return it with
  //     ::UiaReturnRawElementProvider(hwnd, wparam, lparam, provider).
  //     (NOT UiaReturnRawElementProviderForHwnd -- that was the wrong guess.)
  //   * OBJID_CLIENT -> MSAA. Return the IAccessible with
  //     ::LresultFromObject(IID_IAccessible, wparam, accessible).
  // gfx::NativeViewAccessible == IAccessible* on Windows, so the UIA provider
  // is obtained by QueryInterface, not by a direct assignment.
  LRESULT OnGetObject(WPARAM wparam, LPARAM lparam) {
    std::cout << "[OnGetObject] lparam=" << static_cast<LONG>(lparam)
              << " fragment_root=" << (fragment_root_ ? "yes" : "no") << "\n";
    if (!fragment_root_)
      return 0;

    switch (static_cast<LONG>(lparam)) {
      case UiaRootObjectId: {
        bool enabled = AXPlatform::GetInstance().IsUiaProviderEnabled();
        std::cout << "[OnGetObject] UiaRootObjectId uia_enabled=" << enabled
                  << "\n";
        if (!enabled)
          break;
        gfx::NativeViewAccessible root_accessible =
            fragment_root_->GetNativeViewAccessible();
        std::cout << "[OnGetObject] root_accessible="
                  << (root_accessible ? "yes" : "NULL") << "\n";
        if (!root_accessible)
          break;
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider;
        HRESULT qi = root_accessible->QueryInterface(IID_PPV_ARGS(&provider));
        std::cout << "[OnGetObject] QI IRawElementProviderSimple hr=0x"
                  << std::hex << qi << std::dec << "\n";
        if (FAILED(qi))
          break;
        std::cout << "[OnGetObject] returning NATIVE UIA provider\n";
        return ::UiaReturnRawElementProvider(hwnd_, wparam, lparam,
                                             provider.Get());
      }
      case OBJID_CLIENT: {
        if (gfx::NativeViewAccessible root_accessible =
                fragment_root_->GetNativeViewAccessible()) {
          return ::LresultFromObject(IID_IAccessible, wparam, root_accessible);
        }
        break;
      }
      default:
        break;
    }
    return 0;
  }

  // --- AXFragmentRootDelegateWin -----------------------------------------
  // RESOLVED(VM): names/return-types confirmed against
  // ax_fragment_root_delegate_win.h. The child-of-fragment-root is our
  // document root node.
  gfx::NativeViewAccessible GetChildOfAXFragmentRoot() override {
    BliteNodeDelegate* root = DelegateFor(kRoot);
    return root ? root->GetNativeViewAccessible() : nullptr;
  }
  gfx::NativeViewAccessible GetParentOfAXFragmentRoot() override {
    return nullptr;  // top-level host: no parent above the fragment root.
  }
  bool IsAXFragmentRootAControlElement() override {
    return true;  // the host window is itself a UIA control element.
  }

 private:
  HWND hwnd_;
  raw_ptr<AXTree> tree_;
  raw_ptr<MockCanvasEditor> editor_ = nullptr;  // for client-write actions
  raw_ptr<Bridge> bridge_ = nullptr;            // for client-write actions
  bool pending_sync_ = false;          // a keystroke edit awaits loop-level sync
  bool pending_text_changed_ = false;  // ...and whether the text (not just caret) changed
  bool was_in_table_ = false;          // caret was in the table on the last sync
  std::unique_ptr<AXFragmentRootWin> fragment_root_;
  std::map<AXNodeID, std::unique_ptr<BliteNodeDelegate>> delegates_;
};

// A real AXPlatformTreeManager (not a plain AXTreeManager) is REQUIRED for UIA
// text ranges: AXPlatformNodeTextRangeProviderWin::GetOwner() returns null unless
// the position's manager is_platform_tree_manager() AND resolves nodes via
// GetPlatformNodeFromTree -- and a null owner makes every range method
// (GetText/Select/attributes) fail UIA_E_ELEMENTNOTAVAILABLE. We route
// GetPlatformNodeFromTree back to the host's existing platform nodes so it does
// NOT create a second, conflicting set (which collapsed navigation before).
class BliteTreeManager : public AXPlatformTreeManager {
 public:
  explicit BliteTreeManager(std::unique_ptr<AXTree> tree)
      : AXPlatformTreeManager(std::move(tree)) {}
  void SetHost(BliteAXHost* host) { host_ = host; }

  AXPlatformNode* GetPlatformNodeFromTree(AXNodeID node_id) const override {
    return host_ ? host_->PlatformNodeFor(node_id) : nullptr;
  }
  AXPlatformNode* GetPlatformNodeFromTree(const AXNode& node) const override {
    return host_ ? host_->PlatformNodeFor(node.id()) : nullptr;
  }
  AXPlatformNodeDelegate* RootDelegate() const override {
    return host_ ? host_->DelegateFor(kRoot) : nullptr;
  }

 private:
  raw_ptr<BliteAXHost> host_ = nullptr;
};

// BliteNodeDelegate tree-navigation overrides resolve ids through the host.
// RESOLVED(VM): AXNode navigation names confirmed at tag 149 (ax_node.h):
// AXNode::GetParent(), AXNode::GetChildCount(), AXNode::GetChildAtIndex(size_t).
gfx::NativeViewAccessible BliteNodeDelegate::GetParent() const {
  AXNode* parent = node()->GetParent();
  if (!parent) {
    // Root node: its UIA parent is the HWND's fragment root, so a UIA client can
    // navigate from the window element into our internal tree. Returning nullptr
    // here (the old behavior) left the tree unreachable from the HWND -- the bug
    // the first UIA-client probe surfaced (client saw only the bare Window).
    AXFragmentRootWin* fragment_root =
        AXFragmentRootWin::GetForAcceleratedWidget(host_->hwnd());
    return fragment_root ? fragment_root->GetNativeViewAccessible() : nullptr;
  }
  BliteNodeDelegate* d = host_->DelegateFor(parent->id());
  return d ? d->GetNativeViewAccessible() : nullptr;
}

// A text field / static text is a UIA LEAF: its inner text structure (StaticText,
// InlineTextBox) exists only for AXPosition, never as navigable UIA elements --
// exposing the inline box made NVDA's web text navigation error on focus
// (_moveToEdgeOfReplacedContent). AXPosition still walks the AXNode tree directly,
// and GetFromNodeID still resolves the (still-present) inner platform nodes.
static bool HidesChildrenFromUIA(const AXNode* node) {
  const ax::mojom::Role role = node->GetRole();
  // A StaticText is always a leaf -- its inline box is for AXPosition only.
  if (role == ax::mojom::Role::kStaticText)
    return true;
  // A text field is a FLAT leaf (NVDA text nav) UNLESS it contains block
  // structure (a list or a heading): then expose that structure so NVDA can
  // navigate it. Hide only when EVERY child is plain text.
  if (role == ax::mojom::Role::kTextField) {
    for (size_t i = 0; i < node->GetChildCount(); ++i) {
      const ax::mojom::Role cr = node->GetChildAtIndex(i)->GetRole();
      if (cr != ax::mojom::Role::kStaticText &&
          cr != ax::mojom::Role::kInlineTextBox)
        return false;  // structured field (list/heading) -> children navigable
    }
    return true;  // plain field -> flat leaf
  }
  return false;  // list / listitem / listmarker / inline box: navigable as built
}

size_t BliteNodeDelegate::GetChildCount() const {
  if (HidesChildrenFromUIA(node()))
    return 0;
  return node()->GetChildCount();
}

gfx::NativeViewAccessible BliteNodeDelegate::ChildAtIndex(size_t index) const {
  if (HidesChildrenFromUIA(node()))
    return nullptr;
  AXNode* child = node()->GetChildAtIndex(index);
  if (!child)
    return nullptr;
  BliteNodeDelegate* d = host_->DelegateFor(child->id());
  return d ? d->GetNativeViewAccessible() : nullptr;
}

gfx::AcceleratedWidget BliteNodeDelegate::GetTargetForNativeAccessibilityEvent() {
  // Return OUR HWND so UIA associates this content node with the host window's
  // fragment root (AXFragmentRootWin is built on the same HWND). Without this the
  // content is orphaned from the window and never surfaces as its children.
  return host_->hwnd();
}

gfx::NativeViewAccessible BliteNodeDelegate::GetFocus() const {
  // Report the tree's focused node (tree_data.focus_id == kField). With this,
  // AXPlatformNodeWin computes HasKeyboardFocus = (GetFocus() == self), so the
  // field reads as focused and UIA exposes its caret/selection.
  AXNodeID focus_id = host_->tree()->data().focus_id;
  BliteNodeDelegate* d = host_->DelegateFor(focus_id);
  return d ? d->GetNativeViewAccessible() : nullptr;
}

AXPlatformNode* BliteNodeDelegate::GetFromNodeID(int32_t id) {
  // Resolve a node id to its platform node so the Text provider can map the
  // selection's anchor/focus objects (base returns nullptr -> empty selection).
  return host_->PlatformNodeFor(id);
}

AXPlatformNode* BliteNodeDelegate::GetFromTreeIDAndNodeID(const AXTreeID&,
                                                          int32_t id) {
  // Single-tree host: the tree id is always ours, so resolve by node id.
  return host_->PlatformNodeFor(id);
}

const AXSelection BliteNodeDelegate::GetUnignoredSelection() const {
  // The text selection lives in AXTreeData (anchor/focus object+offset). Hand it
  // to the Text provider so GetSelection yields the degenerate caret range.
  return host_->tree()->GetUnignoredSelection();
}

gfx::Rect BliteNodeDelegate::GetBoundsRect(
    AXCoordinateSystem,
    AXClippingBehavior,
    AXOffscreenResult* offscreen_result) const {
  // Physical screen px from the single layout pass (correct at 100% DPI; the
  // coordinate-system arg is ignored for now). This makes a UIA client's
  // get_BoundingRectangle land on the painted text.
  if (offscreen_result)
    *offscreen_result = AXOffscreenResult::kOnscreen;
  return host_->NodeScreenBounds(node()->id());
}

gfx::Rect BliteNodeDelegate::GetInnerTextRangeBoundsRect(
    int start_offset,
    int end_offset,
    AXCoordinateSystem,
    AXClippingBehavior,
    AXOffscreenResult* offscreen_result) const {
  // Per-character span rect (and degenerate caret rect) from the layout metrics.
  // Must report kOnscreen or AXRange::GetRects discards the rect.
  if (offscreen_result)
    *offscreen_result = AXOffscreenResult::kOnscreen;
  return host_->InnerTextRangeScreenBounds(start_offset, end_offset);
}

bool BliteNodeDelegate::AccessibilityPerformAction(const AXActionData& data) {
  switch (data.action) {
    case ax::mojom::Action::kSetValue:
      // IValueProvider::SetValue -> mutate the editor + tree, fire events.
      return host_->ApplyClientSetValue(data.value);
    case ax::mojom::Action::kSetSelection:
      // ITextRangeProvider::Select -> move the caret/selection (cursor routing).
      return host_->ApplyClientSetSelection(data.anchor_node_id,
                                            data.anchor_offset,
                                            data.focus_node_id,
                                            data.focus_offset);
    case ax::mojom::Action::kFocus:
      // Single-field demo: the field is already the focused node; accept it so
      // a client focus request succeeds rather than reporting failure.
      return true;
    default:
      return false;
  }
}

// ===========================================================================
// Win32 window: a real top-level HWND with a message loop so UIA can attach.
// ===========================================================================

BliteAXHost* g_host = nullptr;  // window proc needs to reach the host.

LRESULT CALLBACK BliteWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                              LPARAM lparam) {
  switch (msg) {
    case WM_GETOBJECT:
      // The UIA / MSAA entry point: a client (NVDA) is asking the HWND for its
      // accessible object. Route to the fragment root.
      if (g_host) {
        LRESULT result = g_host->OnGetObject(wparam, lparam);
        if (result)
          return result;
      }
      break;
    case WM_PAINT: {
      // The VISUAL half: paint the editor text + caret from the SAME single
      // layout pass that feeds UIA kCaretBounds. Each glyph is drawn AT its
      // layout x, so painted positions equal the layout (and thus the a11y)
      // positions regardless of the font's own advance. The run's bold/italic/
      // underline (also exposed via UIA attributes) drive the font here too.
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT client;
      GetClientRect(hwnd, &client);
      FillRect(hdc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      if (g_host && g_host->editor()) {
        const MockCanvasEditor& ed = *g_host->editor();
        const Layout lay = LayOut(ed);
        const Metrics m;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        HFONT old_font = static_cast<HFONT>(GetCurrentObject(hdc, OBJ_FONT));
        // BODY selection highlight (E2): fill each selected glyph cell BEFORE the
        // text so the text draws on top. Same per-glyph rects as the a11y range.
        if (ed.has_text_sel()) {
          HBRUSH selb = CreateSolidBrush(RGB(173, 214, 255));  // light blue
          for (int i = ed.sel_lo();
               i < ed.sel_hi() && i < static_cast<int>(lay.glyphs.size()); ++i) {
            const Rect& g = lay.glyphs[i];
            RECT gr = {g.x, g.y, g.x + m.advance, g.y + m.cell_h};
            FillRect(hdc, &gr, selb);
          }
          DeleteObject(selb);
        }
        // One font PER STYLE RUN (bold/italic/underline) -> mixed-format renders.
        // Within a run, draw line-segments (split on '\n') at the layout glyph
        // positions, so per-run fonts AND multi-line both work and match the a11y.
        // The font's cell is forced to the layout advance so painted == layout.
        const std::string& t = ed.text();
        for (const StyleRun& r : ed.runs()) {
          int i = 0;
          while (i < r.length) {
            const int seg_g = r.start + i;
            std::string seg;
            while (i < r.length && t[r.start + i] != '\n') {
              seg += t[r.start + i];
              ++i;
            }
            if (!seg.empty() && seg_g < static_cast<int>(lay.glyphs.size())) {
              // A HEADING line renders bold + a little taller (3.3 visual); the
              // font width stays m.advance so x positions match the layout.
              int sline = 0, scol = 0;
              LineColOf(t, seg_g, &sline, &scol);
              const bool heading =
                  ed.block_of_line(sline) == BlockType::kHeading;
              // Per-run FONT FAMILY (3.4): paint with the run's font, else the
              // editor default Consolas. (Demo fonts are monospace so the forced
              // advance still lines up with the layout / a11y geometry.)
              const std::wstring face =
                  r.style.font.empty()
                      ? L"Consolas"
                      : std::wstring(r.style.font.begin(), r.style.font.end());
              HFONT font = CreateFontW(
                  heading ? m.cell_h + 6 : m.cell_h, m.advance, 0, 0,
                  (r.style.bold || heading) ? FW_BOLD : FW_NORMAL,
                  r.style.italic ? TRUE : FALSE,
                  r.style.underline ? TRUE : FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  FIXED_PITCH | FF_MODERN, face.c_str());
              SelectObject(hdc, font);
              std::wstring w(seg.begin(), seg.end());
              TextOutW(hdc, lay.glyphs[seg_g].x, lay.glyphs[seg_g].y, w.c_str(),
                       static_cast<int>(w.size()));
              SelectObject(hdc, old_font);
              DeleteObject(font);
            }
            if (i < r.length && t[r.start + i] == '\n')
              ++i;  // skip the newline
          }
        }
        // List MARKERS: draw "•" / "N." in the indent reserved by LayOut, just
        // left of each list-item line's (indented) text. Same single layout pass.
        {
          HFONT mfont = CreateFontW(
              m.cell_h, m.advance, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
              CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
          SelectObject(hdc, mfont);
          for (const BlockGroup& g : ed.block_groups()) {
            if (!g.is_list())
              continue;
            for (int k = 0; k < g.line_count; ++k) {
              const int ln = g.first_line + k;
              const std::wstring mk =
                  (g.type == BlockType::kNumber)
                      ? (std::to_wstring(k + 1) + L".")
                      : std::wstring(1, static_cast<wchar_t>(0x2022));  // bullet
              TextOutW(hdc, m.origin_x, m.origin_y + ln * m.line_h, mk.c_str(),
                       static_cast<int>(mk.size()));
            }
          }
          SelectObject(hdc, old_font);
          DeleteObject(mfont);
        }
        // Caret: a filled vertical bar at the layout caret rect -- only when the
        // caret is in the BODY text; in the table the highlighted cell is the
        // cursor (drawn below).
        if (!ed.in_table()) {
          RECT cr = {lay.caret.x, lay.caret.y, lay.caret.x + lay.caret.w,
                     lay.caret.y + lay.caret.h};
          HBRUSH caret_brush = CreateSolidBrush(RGB(0, 0, 0));
          FillRect(hdc, &cr, caret_brush);
          DeleteObject(caret_brush);
        }
        // TABLE visual (4.1): a simple grid with cell text, below the text blocks.
        // The header row (row 0) is bold. Geometry is separate from the text
        // LayOut for now (the table is not yet in the caret flow).
        {
          const auto& tbl = ed.table();
          if (!tbl.empty()) {
            int n_text_lines = 1;
            for (char ch : ed.text())
              if (ch == '\n')
                ++n_text_lines;
            const int tx = m.origin_x;
            const int ty = m.origin_y + (n_text_lines + 1) * m.line_h;  // gap row
            const int col_w = 96, row_h = m.line_h;
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
            HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
            HBRUSH old_brush = static_cast<HBRUSH>(
                SelectObject(hdc, GetStockObject(NULL_BRUSH)));  // cells unfilled
            for (int r = 0; r < static_cast<int>(tbl.size()); ++r) {
              HFONT tfont = CreateFontW(
                  m.cell_h, m.advance, 0, 0, (r == 0) ? FW_BOLD : FW_NORMAL,
                  FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                  L"Consolas");
              SelectObject(hdc, tfont);
              for (int c = 0; c < static_cast<int>(tbl[r].size()); ++c) {
                const int x = tx + c * col_w, y = ty + r * row_h;
                // Block-SELECTED cells (Shift, E4) get the highlight -- same cells
                // the a11y marks kSelected (painted == announced selection).
                if (ed.cell_in_block(r, c)) {
                  RECT hl = {x + 1, y + 1, x + col_w, y + row_h};
                  HBRUSH sb = CreateSolidBrush(RGB(173, 214, 255));  // light blue
                  FillRect(hdc, &hl, sb);
                  DeleteObject(sb);
                }
                // In-cell TEXT selection (E3): partial highlight in the caret cell.
                if (ed.in_table() && r == ed.caret_cell().first &&
                    c == ed.caret_cell().second && ed.has_cell_text_sel()) {
                  const int hx = x + 4 + ed.cell_sel_lo() * m.advance;
                  const int hw =
                      (ed.cell_sel_hi() - ed.cell_sel_lo()) * m.advance;
                  RECT hl = {hx, y + 2, hx + hw, y + 2 + m.cell_h};
                  HBRUSH sb = CreateSolidBrush(RGB(173, 214, 255));
                  FillRect(hdc, &hl, sb);
                  DeleteObject(sb);
                }
                Rectangle(hdc, x, y, x + col_w, y + row_h);
                std::wstring w(tbl[r][c].begin(), tbl[r][c].end());
                TextOutW(hdc, x + 4, y + 2, w.c_str(),
                         static_cast<int>(w.size()));
                // Caret bar INSIDE the cell holding the caret, at cell_off (E1).
                if (ed.in_table() && r == ed.caret_cell().first &&
                    c == ed.caret_cell().second) {
                  const int cx = x + 4 + ed.cell_off() * m.advance;
                  RECT cb = {cx, y + 2, cx + 2, y + 2 + m.cell_h};
                  HBRUSH cbsh = CreateSolidBrush(RGB(0, 0, 0));
                  FillRect(hdc, &cb, cbsh);
                  DeleteObject(cbsh);
                }
              }
              SelectObject(hdc, old_font);
              DeleteObject(tfont);
            }
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
            SelectObject(hdc, old_brush);
          }
        }
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN:
      ::SetFocus(hwnd);  // clicking the window grabs keyboard focus (fallback)
      return 0;
    // PATTERN (load-bearing): keyboard handlers do the CHEAP, input-thread-safe
    // work inline -- mutate the editor model and InvalidateRect (repaint) -- but
    // DEFER the accessibility work (tree re-serialize + UIA event firing) via
    // RequestSync. Firing a UIA event from inside a WM_CHAR/WM_KEYDOWN dispatch
    // re-enters the STA message pump and HANGS the window. The deferred work runs
    // at the message-loop level (FlushPendingSync, after DispatchMessage), which
    // is the only safe place for it -- Chromium's AXTree/UIA are UI-thread-affine
    // (sequence-checked), so this stays on the UI thread but OFF the input
    // dispatch's critical path. Do not call ApplyLocalEdit directly from a
    // WndProc message handler.
    case WM_CHAR:
      if (g_host && g_host->editor() && wparam >= 0x20 && wparam < 0x7f) {
        g_host->editor()->InsertText(std::string(1, static_cast<char>(wparam)));
        g_host->RequestSync(/*text_changed=*/true);  // a11y work -> loop level
        ::InvalidateRect(hwnd, nullptr, TRUE);        // visual now (cheap)
        return 0;
      }
      break;
    case WM_KEYDOWN:
      // Backspace/Delete change text; arrows/Home/End are pure caret moves.
      if (g_host && g_host->editor()) {
        MockCanvasEditor* ed = g_host->editor();
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool handled = true, text_changed = false, is_move = false;
        switch (wparam) {
          case VK_BACK: text_changed = ed->Backspace(); break;
          case VK_DELETE: text_changed = ed->DeleteForward(); break;
          case VK_RETURN: ed->InsertText("\n"); text_changed = true; break;
          case VK_LEFT: ed->CaretLeft(); is_move = true; break;
          case VK_RIGHT: ed->CaretRight(); is_move = true; break;
          case VK_UP: ed->CaretUp(); is_move = true; break;
          case VK_DOWN: ed->CaretDown(); is_move = true; break;
          case VK_HOME: ed->CaretHome(); is_move = true; break;
          case VK_END: ed->CaretEnd(); is_move = true; break;
          case VK_TAB: {
            const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl) {
              ed->InsertText("\t");  // Ctrl+Tab -> literal tab in the cell
              text_changed = true;
            } else if (shift) {
              ed->TabPrev();  // Shift+Tab -> previous cell + select
            } else {
              ed->TabNext();  // Tab -> next cell (+ new row at the end) + select
            }
            break;
          }
          default: handled = false; break;
        }
        // Shift extends the selection (anchor pinned); a plain move collapses it.
        if (is_move && !shift)
          ed->Collapse();
        if (handled) {
          g_host->RequestSync(text_changed);     // a11y work -> loop level
          ::InvalidateRect(hwnd, nullptr, TRUE);  // visual now (cheap)
          return 0;
        }
      }
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}

HWND CreateHostWindow() {
  const wchar_t kClassName[] = L"BliteHostWindow";
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = BliteWndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  return CreateWindowExW(
      0, kClassName, L"AccessibleWebEdit B-lite host",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
      /*parent=*/nullptr, /*menu=*/nullptr, GetModuleHandle(nullptr),
      /*param=*/nullptr);
}

// ===========================================================================
// Diagnostics (same shape as blite_host.cc).
// ===========================================================================

void DumpEvents(const char* label, const AXEventGenerator& generator) {
  std::cout << "\n--- generated events [" << label << "] ---\n";
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    std::cout << "  " << ToString(event.event_params->event) << " on node "
              << event.node_id << "\n";
  }
}

// Fire the UIA platform events for the edit. On Windows the
// browser_accessibility_manager_win finalize path normally translates the
// generated AXEventGenerator events into UIA events; in this standalone host we
// fire them directly on the platform nodes so a UIA client sees TextChanged +
// TextSelectionChanged without pulling the full manager.
//
// RESOLVED(VM): AXPlatformNodeWin::NotifyAccessibilityEvent(ax::mojom::Event)
// is the right entry point. Its tag-149 UIA mapping (MojoEventToUIAEvent in
// ax_platform_node_win.cc) is:
//     ax::mojom::Event::kTextChanged          -> UIA_Text_TextChangedEventId
//     ax::mojom::Event::kTextSelectionChanged -> UIA_Text_TextSelectionChangedEventId
// CORRECTION: the earlier draft fired kValueInTextFieldChanged and
// kDocumentSelectionChanged -- NEITHER maps to a UIA event id at 149
// (MojoEventToUIAEvent returns nullopt for them), so they would raise nothing.
// Use kTextChanged + kTextSelectionChanged. Note the platform node only raises
// the UIA event when AXPlatform::IsUiaProviderEnabled() AND a UIA client has
// registered a listener for that event id (HasEventListenerForEvent) -- i.e.
// when NVDA is actually attached.
void FirePlatformEditEvents(BliteAXHost* host) {
  std::cout << "\n--- firing UIA platform events ---\n";

  AXPlatformNode* field = host->PlatformNodeFor(kField);

  // (1) Text changed on the field (UIA_Text_TextChangedEventId). docs/03 §3.1.
  if (field) {
    field->NotifyAccessibilityEvent(ax::mojom::Event::kTextChanged);
    std::cout << "  UIA_Text_TextChangedEventId on field (node " << kField
              << ")\n";

    // (2) Selection changed on the field
    // (UIA_Text_TextSelectionChangedEventId). docs/03 §2.2: the field must
    // advertise the UIA Text pattern (kTextField does) or this is dropped.
    field->NotifyAccessibilityEvent(ax::mojom::Event::kTextSelectionChanged);
    std::cout << "  UIA_Text_TextSelectionChangedEventId on field (node "
              << kField << ")\n";

    // (3) Value changed on the field -> UIA Value property change. kValueChanged
    // (NOT kValueInTextFieldChanged) maps to UIA_ValueValuePropertyId via
    // MojoEventToUIAProperty (ax_platform_node_win.cc:8259), pairing Text with
    // Value as the UIA Text-pattern guidance recommends.
    field->NotifyAccessibilityEvent(ax::mojom::Event::kValueChanged);
    std::cout << "  UIA_ValueValuePropertyId change on field (node " << kField
              << ")\n";
  }
}

int Run() {
  MockCanvasEditor editor;
  Bridge bridge;

  std::cout << "=== B-lite host (Windows / UIA) ===\n";
  std::cout << "pipeline: MockCanvasEditor -> Bridge -> AXTree -> "
               "AXEventGenerator -> AXPlatformNodeWin -> UIA (HWND)\n";
  std::cout << "[compiled against tag 149; VM-UNVERIFIED at runtime -- NVDA "
               "end-to-end is the open verification]\n";

  // 1. Build and serialize the initial tree from the custom surface.
  // One-shot host: build the tree and hand it to a platform AXTreeManager so the
  // Text provider's position/range machinery (CreatePositionAt, selection -> caret
  // range) has the tree context it needs. LEAK the manager (+tree): destroying the
  // tree would trip AXTree's ~AXTree() observers_.empty() CHECK; a single-run
  // process can leak, the OS reclaims it.
  auto tree_owned = std::make_unique<AXTree>(bridge.BuildInitialTree(editor));
  AXTree* tree_ptr = tree_owned.get();
  // A platform tree manager that routes GetPlatformNodeFromTree to the host's own
  // nodes -- needed for UIA text-range GetOwner() (see BliteTreeManager). Created
  // before the host so the tree is registered; host wired in via SetHost below.
  // Leaked (one-shot host) to avoid the ~AXTree observers_.empty() CHECK at exit.
  auto* tree_manager = new BliteTreeManager(std::move(tree_owned));
  std::cout << "\ninitial surface text: \"" << editor.text()
            << "\" caret=" << editor.caret() << "\n";

  // 2. Stand up the real Win32 window + the platform/UIA layer on top of the
  //    tree, so a UIA client (NVDA) can attach via WM_GETOBJECT.
  HWND hwnd = CreateHostWindow();
  if (!hwnd) {
    std::cerr << "CreateWindow failed: " << GetLastError() << "\n";
    return 1;
  }
  BliteAXHost host(hwnd, tree_ptr);
  tree_manager->SetHost(&host);  // now GetPlatformNodeFromTree -> our nodes
  host.SetEditContext(&editor, &bridge);  // enable client-write actions

  g_host = &host;
  ::SetForegroundWindow(hwnd);
  ::SetFocus(hwnd);
  std::cout << "host window up; UIA provider resolvable from HWND.\n";

  // Interactive viewer: focus the field and let the user TYPE. The WndProc
  // (WM_CHAR / WM_KEYDOWN) drives the editor -> repaint + UIA events, so the
  // painted caret and the UIA caret move together as you type. This skips the
  // scripted auto-edit that the probe / NVDA capture runs depend on.
  const bool viewer = ::wcsstr(::GetCommandLineW(), L"--viewer") != nullptr;
  if (viewer) {
    // GENTLE activation only. Do NOT use the AttachThreadInput foreground-steal
    // trick: it can DEADLOCK against a running screen reader's input hooks (NVDA)
    // and locks the app up at launch, and stealing input is disruptive. A
    // freshly-launched window is usually allowed to take foreground; if not, a
    // click activates it (WM_LBUTTONDOWN -> SetFocus).
    ::ShowWindow(hwnd, SW_SHOW);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);

    // Fire focus only AFTER we are about to pump messages, so an attached AT's
    // synchronous queries (which marshal onto this STA) get serviced promptly.
    if (AXPlatformNode* f = host.PlatformNodeFor(kField))
      f->NotifyAccessibilityEvent(ax::mojom::Event::kFocus);
    ::InvalidateRect(hwnd, nullptr, TRUE);
    std::cout << "VIEWER mode: type into the window (Backspace/Delete/arrows/"
                 "Home/End work); close the window to exit.\n";
    // Use a NON-BLOCKING PeekMessage pump (the same shape as the scripted
    // pump_for that keeps the UIA provider responsive), NOT a blocking
    // GetMessage. A blocking GetMessage interleaved with Chromium's UI message
    // pump + real keyboard input wedged the STA. Apply the deferred a11y work
    // once per drained batch, at loop level (off the WndProc dispatch).
    bool running = true;
    while (running) {
      MSG msg;
      while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          running = false;
          break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
      }
      host.FlushPendingSync();  // see PATTERN note in BliteWndProc
      ::Sleep(10);
    }
    g_host = nullptr;
    std::cout << "\n=== B-lite Windows host exited (viewer) ===\n";
    return 0;
  }

  // A helper that pumps the Win32 message queue for `ms` ms so the provider's
  // STA stays responsive while NVDA attaches/queries.
  auto pump_for = [](DWORD ms) {
    ULONGLONG deadline = ::GetTickCount64() + ms;
    MSG m;
    while (::GetTickCount64() < deadline) {
      while (::PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&m);
        ::DispatchMessage(&m);
      }
      ::Sleep(20);
    }
  };

  // 2b. CRITICAL: let NVDA attach + register UIA event listeners BEFORE firing
  //     the edit. FirePlatformEditEvents only raises a UIA event when a client
  //     has registered a listener for it (HasEventListenerForEvent) -- i.e. when
  //     NVDA is actually attached. Firing immediately (the old behavior) raised
  //     nothing because no client was listening yet. Settle ~8s.
  std::cout << "waiting ~8s for NVDA to attach to the window ...\n";
  pump_for(8000);

  // 2c. Signal FOCUS on the editable field FIRST. NVDA filters focus changes and
  //     will not navigate into our UIA subtree (nor register listeners on the
  //     field) until it sees a legitimate focus land on a real control. Without
  //     this, the subsequent TextChanged/TextSelectionChanged have no listener
  //     and NVDA stays silent. kFocus -> AXPlatformNodeWin fires the UIA focus +
  //     EVENT_OBJECT_FOCUS WinEvent that NVDA follows.
  if (AXPlatformNode* focus_field = host.PlatformNodeFor(kField)) {
    focus_field->NotifyAccessibilityEvent(ax::mojom::Event::kFocus);
    std::cout << "fired kFocus on the field; pumping ~4s for NVDA to follow "
                 "focus into it ...\n";
  }
  pump_for(4000);

  // 3. Attach the event generator and apply one keystroke from the surface --
  //    ONE atomic AXTreeUpdate (text delta + tree-data caret move), exactly as
  //    blite_host.cc does (docs/03 §3.2).
  AXEventGenerator generator(tree_ptr);
  editor.InsertText("!");
  // COLLAPSE the runs (bold->normal everywhere) -> 2 runs become 1, REMOVING the
  // run-2 nodes. This is the dynamic-node stress case: MaterializeDelegates (now a
  // reconcile) drops the removed run's delegate after Unserialize and before any
  // event fires, so NVDA's reentrant get_accChild never walks a dangling delegate
  // (the 3.1-ROBUST crash). Also exercises the bold->not-bold attribute change.
  editor.set_bold(false);
  AXTreeUpdate delta = bridge.BuildEditDelta(editor, tree_ptr);
  if (!tree_ptr->Unserialize(delta)) {
    std::cerr << "Unserialize failed: " << tree_ptr->error() << "\n";
    return 1;
  }
  host.MaterializeDelegates();  // any new run nodes get delegates
  std::cout << "\nafter InsertText(\"!\"): \"" << editor.text()
            << "\" caret=" << editor.caret() << "\n";
  DumpEvents("keystroke", generator);

  // 4. Fire the platform/UIA events so a UIA client surfaces TextChanged +
  //    TextSelectionChanged. This is the half blite_host.cc could not reach.
  FirePlatformEditEvents(&host);
  ::InvalidateRect(hwnd, nullptr, TRUE);  // repaint with the edited text/caret

  // 5. Keep pumping so NVDA can process + announce the events, then self-exit
  //    (the scripted/non-viewer path; --viewer returned earlier into its own
  //    interactive loop). ~12s is ample for NVDA to coalesce + speak.
  std::cout << "\nedit fired; pumping ~12s for NVDA to announce, then exit.\n";
  pump_for(12000);

  g_host = nullptr;
  std::cout << "\n=== B-lite Windows host exited ===\n";
  return 0;
}

}  // namespace
}  // namespace ui

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  // Load ICU data. AXPosition's grapheme/word break iteration (which a screen
  // reader triggers via ITextRangeProvider::ExpandToEnclosingUnit when it
  // navigates by character/word) calls into ICU; without InitializeICU the
  // break iterator's ubrk_open fails and the process FATALs. (Reads icudtl.dat
  // from the executable directory -- present in out/host.)
  if (!base::i18n::InitializeICU())
    std::cerr << "WARNING: InitializeICU failed -- text navigation may crash\n";
  // COM apartment for UIA. STA is the conventional choice for a UI message
  // pump + UIA provider.
  // RESOLVED(VM): ScopedCOMInitializer default ctor initializes an STA, which
  // matches a Win32 message-loop UI thread serving a UIA provider. Kept STA.
  base::win::ScopedCOMInitializer com_initializer;
  // A SingleThreadTaskExecutor gives base/ a current message loop, which some
  // platform-layer code paths expect (e.g. posted tasks during finalize).
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::UI);

  // A process-wide AXPlatform instance MUST exist before any AXPlatformNode is
  // created (AXPlatformNode/AXPlatformNodeWin call AXPlatform::GetInstance()).
  // RESOLVED(VM): at 149 AXPlatform is a non-singleton that takes a
  // Delegate&; the test harness wraps one in AXPlatformForTest
  // (ax_platform_for_test.h), which owns an AXPlatform and supplies the
  // Windows ProductStrings. Instantiate it here so GetInstance() resolves.
  //
  // Note on UIA enablement: AXPlatform::IsUiaProviderEnabled() defaults true
  // but is additionally gated at runtime on base::Feature `features::kUiaProvider`
  // (ax_platform.cc). There is no public setter; for the live NVDA run enable
  // it via the standard --enable-features=UiaProvider feature mechanism /
  // ScopedFeatureList. That is a RUNTIME concern only and does not affect this
  // compile; the NotifyAccessibilityEvent -> UiaRaiseAutomationEvent code path
  // is still linked either way.
  ui::AXPlatformForTest ax_platform_for_test;

  return ui::Run();
}
