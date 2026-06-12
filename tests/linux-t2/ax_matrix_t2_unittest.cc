// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated-event verification for docs/10 scenario-matrix rows that are
// testable off-Windows, including the generated-event half of T2-7
// (cell-versus-text selection handoff). Companion to
// ax_event_generator_t2_unittest.cc; same logging contract (T2-LOG lines).

#include <iostream>
#include <set>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_event_generator.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ui {
namespace {

using testing::Contains;
using testing::Not;

MATCHER_P2(IsEventAtNode, expected_event, expected_node_id, "") {
  return arg.event_params->event == expected_event &&
         arg.node_id == expected_node_id;
}

void LogEvents(const std::string& label, const AXEventGenerator& generator) {
  std::multiset<std::string> lines;
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    lines.insert(std::string(ToString(event.event_params->event)) +
                 " on node " + base::NumberToString(event.node_id));
  }
  std::cout << "T2-LOG [" << label << "] " << lines.size() << " event(s)\n";
  for (const std::string& line : lines)
    std::cout << "T2-LOG   " << line << "\n";
}

bool HasEvent(const AXEventGenerator& generator,
              AXEventGenerator::Event event) {
  for (const AXEventGenerator::TargetedEvent& e : generator) {
    if (e.event_params->event == event)
      return true;
  }
  return false;
}

// root(1) -> grid(2) -> row(3) -> cell(4), cell(5).
// Each cell contains a text field (6 in cell 4) -> static text (7).
AXTreeUpdate BuildGridTree() {
  AXTreeUpdate update;
  update.has_tree_data = true;
  update.root_id = 1;
  update.nodes.resize(7);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kGrid;
  update.nodes[1].AddState(ax::mojom::State::kMultiselectable);
  update.nodes[1].child_ids = {3};
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kRow;
  update.nodes[2].child_ids = {4, 5};
  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kCell;
  update.nodes[3].child_ids = {6};
  update.nodes[4].id = 5;
  update.nodes[4].role = ax::mojom::Role::kCell;
  update.nodes[5].id = 6;
  update.nodes[5].role = ax::mojom::Role::kTextField;
  update.nodes[5].AddState(ax::mojom::State::kEditable);
  update.nodes[5].child_ids = {7};
  update.nodes[6].id = 7;
  update.nodes[6].role = ax::mojom::Role::kStaticText;
  update.nodes[6].SetName("abc");
  return update;
}

// T2-7 (generated-event half), part 1: single then discontiguous cell
// selection via the kSelected attribute. Expects SELECTED_CHANGED on the cell
// and SELECTED_CHILDREN_CHANGED on the selection container (the grid).
TEST(AXMatrixT2Test, T27_CellSelection_SingleAndDiscontiguous) {
  AXTree tree(BuildGridTree());
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(4)->data();
  update.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kSelected, true);
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("T2-7 single cell selected", generator);
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::SELECTED_CHANGED, 4)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::SELECTED_CHILDREN_CHANGED, 2)));

  generator.ClearEvents();
  AXTreeUpdate update2;
  update2.nodes.resize(1);
  update2.nodes[0] = tree.GetFromId(5)->data();
  update2.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kSelected, true);
  ASSERT_TRUE(tree.Unserialize(update2)) << tree.error();
  LogEvents("T2-7 second cell selected (discontiguous)", generator);
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::SELECTED_CHANGED, 5)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::SELECTED_CHILDREN_CHANGED, 2)));
}

// T2-7, part 2: the handoff. Start with a text selection inside the cell's
// text field; in ONE atomic update, move the document selection to the cell
// node and mark the cell selected. Both mechanisms must produce their events
// in the single event set without suppressing each other.
TEST(AXMatrixT2Test, T27_TextToCellSelectionHandoff_Atomic) {
  AXTreeUpdate initial = BuildGridTree();
  initial.tree_data.sel_anchor_object_id = 7;
  initial.tree_data.sel_anchor_offset = 0;
  initial.tree_data.sel_focus_object_id = 7;
  initial.tree_data.sel_focus_offset = 2;
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.has_tree_data = true;
  update.tree_data = tree.data();
  update.tree_data.sel_anchor_object_id = 4;
  update.tree_data.sel_anchor_offset = 0;
  update.tree_data.sel_focus_object_id = 4;
  update.tree_data.sel_focus_offset = 0;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(4)->data();
  update.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kSelected, true);
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-7 atomic text-to-cell selection handoff", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::DOCUMENT_SELECTION_CHANGED, 1)));
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::SELECTED_CHANGED, 4)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::SELECTED_CHILDREN_CHANGED, 2)));
  // The selection focus is now the (non-editable) cell, so no text field
  // ancestor exists for it: TEXT_SELECTION_CHANGED must NOT fire.
  EXPECT_FALSE(
      HasEvent(generator, AXEventGenerator::Event::TEXT_SELECTION_CHANGED));
}

// docs/10 "Spellcheck markers": adding a spelling marker to text inside a
// text field fires TEXT_ATTRIBUTE_CHANGED and SPELLING_MARKER_CHANGED on the
// text-field ancestor (GetTextAttributeTarget walks to it).
TEST(AXMatrixT2Test, Markers_SpellingAdded) {
  AXTree tree(BuildGridTree());
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(7)->data();
  update.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kMarkerTypes,
      {static_cast<int32_t>(ax::mojom::MarkerType::kSpelling)});
  update.nodes[0].AddIntListAttribute(ax::mojom::IntListAttribute::kMarkerStarts,
                                      {0});
  update.nodes[0].AddIntListAttribute(ax::mojom::IntListAttribute::kMarkerEnds,
                                      {3});
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("matrix spelling marker added", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::SPELLING_MARKER_CHANGED, 6)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::TEXT_ATTRIBUTE_CHANGED, 6)));
}

// docs/10 "rich attribute editing": formatting-change events REQUIRE
// State::kRichlyEditable on the changed node. With it, kTextStyle changes
// fire TEXT_ATTRIBUTE_CHANGED; with only kEditable in the ancestry, nothing
// fires. This pins a bridge-contract requirement docs/09/10 did not state.
TEST(AXMatrixT2Test, FormatBold_RequiresRichlyEditable) {
  AXTreeUpdate initial = BuildGridTree();
  initial.nodes[6].AddState(ax::mojom::State::kRichlyEditable);
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(7)->data();
  update.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kTextStyle,
      static_cast<int32_t>(ax::mojom::TextStyle::kBold));
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix bold applied (richly editable)", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::TEXT_ATTRIBUTE_CHANGED, 7)));

  // Negative: same delta without kRichlyEditable on the text node.
  AXTree tree2(BuildGridTree());
  AXEventGenerator generator2(&tree2);
  AXTreeUpdate update2;
  update2.nodes.resize(1);
  update2.nodes[0] = tree2.GetFromId(7)->data();
  update2.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kTextStyle,
      static_cast<int32_t>(ax::mojom::TextStyle::kBold));
  ASSERT_TRUE(tree2.Unserialize(update2)) << tree2.error();
  LogEvents("matrix bold applied (NOT richly editable)", generator2);
  EXPECT_FALSE(
      HasEvent(generator2, AXEventGenerator::Event::TEXT_ATTRIBUTE_CHANGED));
}

// root(1) -> table(2) -> row(3) -> columnHeader(4), cell(5).
AXTreeUpdate BuildTableTree() {
  AXTreeUpdate update;
  update.has_tree_data = true;
  update.root_id = 1;
  update.nodes.resize(5);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[1].id = 2;
  update.nodes[1].role = ax::mojom::Role::kTable;
  update.nodes[1].child_ids = {3};
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kRow;
  update.nodes[2].child_ids = {4, 5};
  update.nodes[3].id = 4;
  update.nodes[3].role = ax::mojom::Role::kColumnHeader;
  update.nodes[4].id = 5;
  update.nodes[4].role = ax::mojom::Role::kCell;
  return update;
}

// docs/10 "sort": kSortDirection change fires SORT_CHANGED, but ONLY on
// table/grid header roles (source-gated by IsTableHeader).
TEST(AXMatrixT2Test, Sort_HeaderOnly) {
  AXTree tree(BuildTableTree());
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(4)->data();
  update.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kSortDirection,
      static_cast<int32_t>(ax::mojom::SortDirection::kAscending));
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix sort direction on column header", generator);
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::SORT_CHANGED, 4)));

  // Negative: same attribute on a plain cell produces no SORT_CHANGED.
  generator.ClearEvents();
  AXTreeUpdate update2;
  update2.nodes.resize(1);
  update2.nodes[0] = tree.GetFromId(5)->data();
  update2.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kSortDirection,
      static_cast<int32_t>(ax::mojom::SortDirection::kAscending));
  ASSERT_TRUE(tree.Unserialize(update2)) << tree.error();
  LogEvents("matrix sort direction on plain cell (negative)", generator);
  EXPECT_FALSE(HasEvent(generator, AXEventGenerator::Event::SORT_CHANGED));
}

// docs/10 "add/delete rows": structural row addition announces via
// CHILDREN_CHANGED + SUBTREE_CREATED. Pins the finding that there is no
// kTableRowCount-driven event in the generator (no case exists for it).
TEST(AXMatrixT2Test, Table_RowAddition_StructuralEventsOnly) {
  AXTree tree(BuildTableTree());
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(2);
  update.nodes[0] = tree.GetFromId(2)->data();
  update.nodes[0].child_ids = {3, 6};
  update.nodes[1].id = 6;
  update.nodes[1].role = ax::mojom::Role::kRow;
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix row added", generator);
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::CHILDREN_CHANGED, 2)));
  EXPECT_THAT(generator, Contains(IsEventAtNode(
                             AXEventGenerator::Event::SUBTREE_CREATED, 6)));
}

// docs/10 "checkable task items": kCheckedState change fires
// CHECKED_STATE_CHANGED on the checkbox node.
TEST(AXMatrixT2Test, Checkbox_Toggle) {
  AXTreeUpdate initial = BuildTableTree();
  initial.nodes.resize(6);
  initial.nodes[4].child_ids = {7};
  initial.nodes[5].id = 7;
  initial.nodes[5].role = ax::mojom::Role::kCheckBox;
  initial.nodes[5].SetCheckedState(ax::mojom::CheckedState::kFalse);
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(7)->data();
  update.nodes[0].SetCheckedState(ax::mojom::CheckedState::kTrue);
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix checkbox toggled", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::CHECKED_STATE_CHANGED, 7)));
}

// docs/10 "progress trackers": kValueForRange change fires
// RANGE_VALUE_CHANGED on the progress indicator.
TEST(AXMatrixT2Test, Progress_RangeValue) {
  AXTreeUpdate initial = BuildTableTree();
  initial.nodes.resize(6);
  initial.nodes[4].child_ids = {7};
  initial.nodes[5].id = 7;
  initial.nodes[5].role = ax::mojom::Role::kProgressIndicator;
  initial.nodes[5].AddFloatAttribute(ax::mojom::FloatAttribute::kValueForRange,
                                     0.3f);
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(7)->data();
  // AddFloatAttribute would DCHECK on duplicates; rebuild the attribute.
  update.nodes[0].RemoveFloatAttribute(
      ax::mojom::FloatAttribute::kValueForRange);
  update.nodes[0].AddFloatAttribute(ax::mojom::FloatAttribute::kValueForRange,
                                    0.6f);
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix progress value changed", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::RANGE_VALUE_CHANGED, 7)));
}

// docs/10 "comment threads" (expand/collapse) and "kanban reorder"
// (pos-in-set): kExpanded state toggles fire EXPANDED/COLLAPSED; kPosInSet
// changes fire POSITION_IN_SET_CHANGED.
TEST(AXMatrixT2Test, ExpandCollapse_And_PosInSet) {
  AXTreeUpdate initial = BuildTableTree();
  initial.nodes.resize(6);
  initial.nodes[4].child_ids = {7};
  initial.nodes[5].id = 7;
  initial.nodes[5].role = ax::mojom::Role::kComment;
  initial.nodes[5].AddState(ax::mojom::State::kExpanded);
  initial.nodes[5].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 1);
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(7)->data();
  update.nodes[0].RemoveState(ax::mojom::State::kExpanded);
  update.nodes[0].AddState(ax::mojom::State::kCollapsed);
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();
  LogEvents("matrix comment thread collapsed", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(AXEventGenerator::Event::COLLAPSED, 7)));

  generator.ClearEvents();
  AXTreeUpdate update2;
  update2.nodes.resize(1);
  update2.nodes[0] = tree.GetFromId(7)->data();
  update2.nodes[0].RemoveIntAttribute(ax::mojom::IntAttribute::kPosInSet);
  update2.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 3);
  ASSERT_TRUE(tree.Unserialize(update2)) << tree.error();
  LogEvents("matrix kanban pos-in-set changed", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::POSITION_IN_SET_CHANGED, 7)));
}

}  // namespace
}  // namespace ui
