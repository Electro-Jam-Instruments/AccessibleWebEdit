// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// In-tree end-to-end host: the SAME demo core (text model + the single layout
// pass + the shared edit script) driving the REAL Chromium accessibility
// engine -- a live AXTree + AXEventGenerator -- and dumping the actual
// generated events per editing step. This is the accessibility half of the
// demo made real (not mirrored): every event printed here is produced by
// Chromium's own ui/accessibility code, and the per-step kCaretBounds is taken
// from the very layout the renderer paints (demo/core/layout_engine.h).
//
// Pipeline:
//   demo::BuildDemoScript() -> per-step TextDocument
//     -> LayOut() (the one layout pass)            [shared with the renderer]
//       -> AXTreeUpdate delta (name + selection + kCaretBounds)
//         -> AXTree::Unserialize -> AXEventGenerator (REAL events)
//
// Build (in a Chromium 149.0.7827.115 checkout): flatten the demo core headers
// next to this file -- copy demo/core/*.h and this .cc into
// //ui/accessibility/demo_e2e/, add the BUILD.gn here, then:
//   ninja -C out/rel demo_e2e && ./out/rel/demo_e2e
// Same lean cone as blite_host: //ui/accessibility:accessibility_internal +
// //base. No platform node layer, no v8.

#include <iostream>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "demo_script.h"
#include "layout_engine.h"
#include "text_document.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_event_generator.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"

namespace ui {
namespace {

// Node ids for the mock document, matching the T2 tests / blite_host.
constexpr AXNodeID kRoot = 1;
constexpr AXNodeID kField = 2;
constexpr AXNodeID kText = 3;

// Build the initial tree from the first script step: root web area -> editable
// text field -> static text leaf carrying the value.
AXTreeUpdate BuildInitialTree(const demo::TextDocument& doc) {
  AXTreeUpdate update;
  update.has_tree_data = true;
  update.tree_data.sel_anchor_object_id = kText;
  update.tree_data.sel_anchor_offset = doc.caret();
  update.tree_data.sel_focus_object_id = kText;
  update.tree_data.sel_focus_offset = doc.caret();
  update.root_id = kRoot;
  update.nodes.resize(3);
  update.nodes[0].id = kRoot;
  update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  update.nodes[0].child_ids = {kField};
  update.nodes[1].id = kField;
  update.nodes[1].role = ax::mojom::Role::kTextField;
  update.nodes[1].AddState(ax::mojom::State::kEditable);
  update.nodes[1].AddState(ax::mojom::State::kRichlyEditable);
  update.nodes[1].child_ids = {kText};
  update.nodes[2].id = kText;
  update.nodes[2].role = ax::mojom::Role::kStaticText;
  update.nodes[2].SetName(doc.text());
  return update;
}

// Build a delta for one editing step the way a real producer would: include
// ONLY the nodes that actually changed. The text leaf is re-sent only when its
// value changed; the field node is re-sent only when the caret moved (new
// kCaretBounds). Selection always lives on the tree data. Sending unchanged
// nodes would make the generator over-fire (e.g. a pure selection emitting a
// spurious text-changed) -- so the per-step event set here matches the
// semantics, not just "something changed."
AXTreeUpdate BuildDelta(const demo::TextDocument& doc,
                        const demo::Layout& layout,
                        AXTree* tree) {
  AXTreeUpdate update;
  update.has_tree_data = true;
  update.tree_data = tree->data();
  update.tree_data.sel_anchor_object_id = kText;
  update.tree_data.sel_focus_object_id = kText;
  if (doc.has_selection()) {
    update.tree_data.sel_anchor_offset = doc.sel_start();
    update.tree_data.sel_focus_offset = doc.sel_end();
  } else {
    update.tree_data.sel_anchor_offset = doc.caret();
    update.tree_data.sel_focus_offset = doc.caret();
  }

  // Field node, re-sent only if the caret bounds changed.
  const std::vector<int> caret_bounds = {layout.caret.x, layout.caret.y,
                                         layout.caret.w, layout.caret.h};
  AXNode* field_node = tree->GetFromId(kField);
  const bool caret_moved =
      !field_node->HasIntListAttribute(
          ax::mojom::IntListAttribute::kCaretBounds) ||
      field_node->GetIntListAttribute(
          ax::mojom::IntListAttribute::kCaretBounds) != caret_bounds;
  if (caret_moved) {
    AXNodeData field = field_node->data();
    field.RemoveIntListAttribute(ax::mojom::IntListAttribute::kCaretBounds);
    field.AddIntListAttribute(ax::mojom::IntListAttribute::kCaretBounds,
                              caret_bounds);
    update.nodes.push_back(field);
  }

  // Text leaf, re-sent only if the value changed. Compare against the stored
  // kName attribute (not GetNameUTF8(), which returns a computed name).
  AXNode* text_node = tree->GetFromId(kText);
  const std::string current_value =
      text_node->data().GetStringAttribute(ax::mojom::StringAttribute::kName);
  if (current_value != doc.text()) {
    AXNodeData text = text_node->data();
    text.SetName(doc.text());
    update.nodes.push_back(text);
  }
  return update;
}

void DumpEvents(const std::string& label, const AXEventGenerator& generator) {
  std::cout << "  REAL generated events:";
  bool any = false;
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    std::cout << "\n    " << ToString(event.event_params->event) << " on node "
              << event.node_id;
    any = true;
  }
  if (!any)
    std::cout << " (none)";
  std::cout << "\n";
}

int Run() {
  const std::vector<demo::Step> steps = demo::BuildDemoScript();
  const demo::Metrics metrics;

  std::cout << "=== AccessibleWebEdit e2e: demo core -> REAL AXTree + "
               "AXEventGenerator ===\n";
  std::cout << "pipeline: script -> layout -> AXTreeUpdate -> Unserialize -> "
               "generated events\n\n";

  AXTree tree(BuildInitialTree(steps.front().doc));
  AXEventGenerator generator(&tree);
  std::cout << "[0] " << steps.front().label << "  value=\""
            << steps.front().doc.text() << "\"\n  (initial tree built)\n\n";

  for (size_t i = 1; i < steps.size(); ++i) {
    const demo::Step& step = steps[i];
    const demo::Layout layout = demo::LayOut(step.doc, metrics);
    AXTreeUpdate delta = BuildDelta(step.doc, layout, &tree);
    if (!tree.Unserialize(delta)) {
      std::cerr << "Unserialize failed at step " << i << ": " << tree.error()
                << "\n";
      return 1;
    }
    std::cout << "[" << i << "] " << step.label << "  value=\""
              << step.doc.text() << "\"  caret=" << step.doc.caret()
              << "  kCaretBounds=[" << layout.caret.x << "," << layout.caret.y
              << "," << layout.caret.w << "," << layout.caret.h << "]\n";
    DumpEvents(step.label, generator);
    // The generator accumulates events across Unserialize calls; clear after
    // each step so the printed set is exactly that step's events.
    generator.ClearEvents();
    std::cout << "\n";
  }

  std::cout << "=== e2e OK: every event above came from Chromium's real "
               "AXEventGenerator ===\n";
  return 0;
}

}  // namespace
}  // namespace ui

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  return ui::Run();
}
