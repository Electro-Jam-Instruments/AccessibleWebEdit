// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the AccessibleWebEdit bridge contract (docs/09, Tier 2 tasks
// T2-1, T2-2, T2-3, T2-5): verifies that a non-Blink tree producer obtains
// editing-related generated events purely from AXTreeUpdate deltas and
// AXTreeData selection changes, with no event intents supplied.

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

// Logs every generated event so the test output doubles as the raw event
// record for results/T2-linux-generated-events.md.
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

// Builds: root(1, kRootWebArea) -> field(2) -> staticText(3, |text|).
// When |editable| is true, node 2 is an atomic text field (role kTextField +
// State::kEditable), matching the docs/09 section 03 constraint for the
// canvas document subtree root. When false, node 2 is a plain generic
// container, which must suppress all editable-text events (T2-2).
AXTreeUpdate BuildTextFieldTree(bool editable, const std::string& text) {
  AXTreeUpdate update;
  update.has_tree_data = true;
  update.root_id = 1;
  update.nodes.resize(3);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {2};
  update.nodes[1].id = 2;
  if (editable) {
    update.nodes[1].role = ax::mojom::Role::kTextField;
    update.nodes[1].AddState(ax::mojom::State::kEditable);
  } else {
    update.nodes[1].role = ax::mojom::Role::kGenericContainer;
  }
  update.nodes[1].child_ids = {3};
  update.nodes[2].id = 3;
  update.nodes[2].role = ax::mojom::Role::kStaticText;
  update.nodes[2].SetName(text);
  return update;
}

// T2-1, shape 1: single character insert expressed as a name-attribute delta
// on the text node. Expects EDITABLE_TEXT_CHANGED and
// VALUE_IN_TEXT_FIELD_CHANGED on the text-field ancestor with no intents.
TEST(AXEventGeneratorT2Test, T2_1_CharacterInsert) {
  AXTree tree(BuildTextFieldTree(/*editable=*/true, "hello"));
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(3)->data();
  update.nodes[0].SetName("hellox");
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-1 character insert", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::EDITABLE_TEXT_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::VALUE_IN_TEXT_FIELD_CHANGED, 2)));
}

// T2-1, shape 2: word deletion expressed as removal of a static text child.
// OnSubtreeWillBeDeleted must fire the editable-text pair on the ancestor.
TEST(AXEventGeneratorT2Test, T2_1_WordDelete) {
  AXTreeUpdate initial = BuildTextFieldTree(/*editable=*/true, "hello ");
  initial.nodes.resize(4);
  initial.nodes[1].child_ids = {3, 4};
  initial.nodes[3].id = 4;
  initial.nodes[3].role = ax::mojom::Role::kStaticText;
  initial.nodes[3].SetName("world");
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(2)->data();
  update.nodes[0].child_ids = {3};
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-1 word delete", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::EDITABLE_TEXT_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::VALUE_IN_TEXT_FIELD_CHANGED, 2)));
}

// T2-1, shape 3 (autocorrect): one atomic update carrying both the word
// replacement (name delta) and the caret move (tree-data delta). This is the
// announcement-coherence shape from docs/09: the producer posts exactly one
// AXTreeUpdate and the generator must emit text and selection changes
// together in a single event set.
TEST(AXEventGeneratorT2Test, T2_1_AutocorrectReplacement) {
  AXTreeUpdate initial = BuildTextFieldTree(/*editable=*/true, "teh");
  initial.tree_data.sel_anchor_object_id = 3;
  initial.tree_data.sel_anchor_offset = 3;
  initial.tree_data.sel_focus_object_id = 3;
  initial.tree_data.sel_focus_offset = 3;
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.has_tree_data = true;
  update.tree_data = tree.data();
  update.tree_data.sel_anchor_offset = 4;
  update.tree_data.sel_focus_offset = 4;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(3)->data();
  update.nodes[0].SetName("the ");
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-1 autocorrect replacement (atomic)", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::EDITABLE_TEXT_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::VALUE_IN_TEXT_FIELD_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::DOCUMENT_SELECTION_CHANGED, 1)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::TEXT_SELECTION_CHANGED, 2)));
}

// T2-2: identical text delta, but the container is not a text field. The
// editable-text event pair must NOT fire anywhere. Validates the section 03
// role constraint: event targeting walks GetTextFieldAncestor.
TEST(AXEventGeneratorT2Test, T2_2_NoTextFieldAncestor) {
  AXTree tree(BuildTextFieldTree(/*editable=*/false, "hello"));
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(3)->data();
  update.nodes[0].SetName("hellox");
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-2 no text-field ancestor", generator);
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    EXPECT_NE(event.event_params->event,
              AXEventGenerator::Event::EDITABLE_TEXT_CHANGED);
    EXPECT_NE(event.event_params->event,
              AXEventGenerator::Event::VALUE_IN_TEXT_FIELD_CHANGED);
    EXPECT_NE(event.event_params->event,
              AXEventGenerator::Event::TEXT_SELECTION_CHANGED);
  }
}

// T2-3: caret and selection driven purely through AXTreeData sel_ fields.
// Each delta shape must produce DOCUMENT_SELECTION_CHANGED at the root and
// TEXT_SELECTION_CHANGED on the text field containing the selection focus.
TEST(AXEventGeneratorT2Test, T2_3_SelectionViaTreeData) {
  AXTreeUpdate initial = BuildTextFieldTree(/*editable=*/true, "hello world");
  initial.tree_data.sel_anchor_object_id = 3;
  initial.tree_data.sel_anchor_offset = 0;
  initial.tree_data.sel_focus_object_id = 3;
  initial.tree_data.sel_focus_offset = 0;
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  struct Step {
    const char* label;
    int32_t anchor_offset;
    int32_t focus_offset;
    bool is_backward;
    ax::mojom::TextAffinity focus_affinity;
  };
  const Step kSteps[] = {
      {"T2-3 caret move (collapsed)", 5, 5, false,
       ax::mojom::TextAffinity::kDownstream},
      {"T2-3 forward range selection", 5, 11, false,
       ax::mojom::TextAffinity::kDownstream},
      {"T2-3 backward range selection", 11, 5, true,
       ax::mojom::TextAffinity::kDownstream},
      // Affinity-only change: the wrapped-line caret case. Offsets identical
      // to the previous step; only sel_focus_affinity differs.
      {"T2-3 affinity-only change", 11, 5, true,
       ax::mojom::TextAffinity::kUpstream},
  };

  for (const Step& step : kSteps) {
    generator.ClearEvents();
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data = tree.data();
    update.tree_data.sel_anchor_offset = step.anchor_offset;
    update.tree_data.sel_focus_offset = step.focus_offset;
    update.tree_data.sel_is_backward = step.is_backward;
    update.tree_data.sel_focus_affinity = step.focus_affinity;
    ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

    LogEvents(step.label, generator);
    EXPECT_THAT(generator,
                Contains(IsEventAtNode(
                    AXEventGenerator::Event::DOCUMENT_SELECTION_CHANGED, 1)))
        << step.label;
    EXPECT_THAT(generator,
                Contains(IsEventAtNode(
                    AXEventGenerator::Event::TEXT_SELECTION_CHANGED, 2)))
        << step.label;
  }
}

// T2-3 addendum: caret bounds. docs/09 attributed CARET_BOUNDS_CHANGED to
// tree-data selection changes; at source it is driven by the kCaretBounds
// int-list attribute (OnIntListAttributeChanged), not by sel_ fields. This
// test pins the actual contract a producer must follow.
TEST(AXEventGeneratorT2Test, T2_3_CaretBoundsAttribute) {
  AXTree tree(BuildTextFieldTree(/*editable=*/true, "hello"));
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0] = tree.GetFromId(2)->data();
  update.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kCaretBounds, {10, 4, 1, 12});
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-3 caret bounds via kCaretBounds attribute", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::CARET_BOUNDS_CHANGED, 2)));
}

// T2-5: atomic multi-part update in the structural form a real autocorrect
// takes: delete the old word node, insert a new one with different text, and
// move the caret -- all in one AXTreeUpdate. All resulting events must appear
// in the single event set produced by the one Unserialize pass, and the
// editable-text pair must not be duplicated despite both the deletion and the
// creation paths calling FireValueInTextFieldChangedEventIfNecessary.
TEST(AXEventGeneratorT2Test, T2_5_AtomicNodeReplacementWithSelection) {
  AXTreeUpdate initial = BuildTextFieldTree(/*editable=*/true, "hello ");
  initial.nodes.resize(4);
  initial.nodes[1].child_ids = {3, 4};
  initial.nodes[3].id = 4;
  initial.nodes[3].role = ax::mojom::Role::kStaticText;
  initial.nodes[3].SetName("teh");
  initial.tree_data.sel_anchor_object_id = 4;
  initial.tree_data.sel_anchor_offset = 3;
  initial.tree_data.sel_focus_object_id = 4;
  initial.tree_data.sel_focus_offset = 3;
  AXTree tree(initial);
  AXEventGenerator generator(&tree);

  AXTreeUpdate update;
  update.has_tree_data = true;
  update.tree_data = tree.data();
  update.tree_data.sel_anchor_object_id = 5;
  update.tree_data.sel_anchor_offset = 3;
  update.tree_data.sel_focus_object_id = 5;
  update.tree_data.sel_focus_offset = 3;
  update.nodes.resize(2);
  update.nodes[0] = tree.GetFromId(2)->data();
  update.nodes[0].child_ids = {3, 5};
  update.nodes[1].id = 5;
  update.nodes[1].role = ax::mojom::Role::kStaticText;
  update.nodes[1].SetName("the");
  ASSERT_TRUE(tree.Unserialize(update)) << tree.error();

  LogEvents("T2-5 atomic node replacement + selection", generator);
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::EDITABLE_TEXT_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::VALUE_IN_TEXT_FIELD_CHANGED, 2)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::DOCUMENT_SELECTION_CHANGED, 1)));
  EXPECT_THAT(generator,
              Contains(IsEventAtNode(
                  AXEventGenerator::Event::TEXT_SELECTION_CHANGED, 2)));

  // Atomicity: events are deduplicated per node within the single pass.
  int editable_text_changed_count = 0;
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    if (event.event_params->event ==
        AXEventGenerator::Event::EDITABLE_TEXT_CHANGED) {
      ++editable_text_changed_count;
    }
  }
  EXPECT_EQ(editable_text_changed_count, 1);
}

}  // namespace
}  // namespace ui
