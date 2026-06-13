// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// B-lite host (spine): a standalone application that stands in for the
// AccessibleWebEdit canvas editor and drives Chromium's ui/accessibility
// layer directly -- the non-Blink tree-producer architecture from docs/09.
//
// Pipeline exercised, end to end, as a real running program:
//   MockCanvasEditor (custom surface model)
//     -> Bridge (emits AXTreeData + AXTreeUpdate deltas)
//       -> AXTree (+ AXEventGenerator)
//         -> AXPlatformNode / AXPlatformNodeDelegate  (the platform layer)
//
// Scope (Linux, lean cone): this host exercises the platform-agnostic spine --
// surface -> bridge -> AXTree -> AXEventGenerator -- as a real running program.
// The platform-node layer (AXPlatformNode: AXPlatformNodeWin/UIA on Windows,
// AXPlatformNodeAuraLinux/AT-SPI with use_atk) is deliberately NOT linked here:
// it leaves the lean build cone and is being done on the Windows VM where UIA
// is the real target. See results/ENVIRONMENT.md and docs/TASK-00.

#include <iostream>
#include <string>

#include "base/at_exit.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_event_generator.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"

namespace ui {
namespace {

// Node ids for the mock document: root web area, the editable field, and the
// static text leaf carrying the field's value.
constexpr AXNodeID kRoot = 1;
constexpr AXNodeID kField = 2;
constexpr AXNodeID kText = 3;

// The "custom surface": a trivial editor model. Real AccessibleWebEdit would
// be a canvas with its own layout/selection; here we only need text + caret.
class MockCanvasEditor {
 public:
  const std::string& text() const { return text_; }
  int caret() const { return caret_; }

  void InsertText(const std::string& s) {
    text_.insert(caret_, s);
    caret_ += static_cast<int>(s.size());
  }

 private:
  std::string text_ = "hello";
  int caret_ = 5;
};

// The bridge: turns the editor model into accessibility tree updates. This is
// the contract docs/09 is about -- a non-Blink producer posting AXTreeUpdate
// deltas plus AXTreeData. Caret/selection live in AXTreeData (tree level).
class Bridge {
 public:
  AXTreeUpdate BuildInitialTree(const MockCanvasEditor& editor) {
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data.sel_anchor_object_id = kText;
    update.tree_data.sel_anchor_offset = editor.caret();
    update.tree_data.sel_focus_object_id = kText;
    update.tree_data.sel_focus_offset = editor.caret();
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
    update.nodes[2].SetName(editor.text());
    return update;
  }

  // A single-node text delta plus the moved caret, the way the editor would
  // post one keystroke.
  AXTreeUpdate BuildEditDelta(const MockCanvasEditor& editor, AXTree* tree) {
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data = tree->data();
    update.tree_data.sel_anchor_offset = editor.caret();
    update.tree_data.sel_focus_offset = editor.caret();
    update.nodes.resize(1);
    update.nodes[0] = tree->GetFromId(kText)->data();
    update.nodes[0].SetName(editor.text());
    return update;
  }
};

void DumpTree(AXTree* tree) {
  std::cout << "\n--- accessibility tree (role / name / selection) ---\n";
  const AXTreeData& data = tree->data();
  for (AXNodeID id : {kRoot, kField, kText}) {
    AXNode* node = tree->GetFromId(id);
    std::cout << "  node " << id << "  role=" << ui::ToString(node->GetRole())
              << "  name=\"" << node->GetNameUTF8() << "\"";
    if (node->GetRole() == ax::mojom::Role::kTextField) {
      std::cout << "  [editable]";
    }
    std::cout << "\n";
  }
  std::cout << "  tree-data selection: focus_object=" << data.sel_focus_object_id
            << " offset=" << data.sel_focus_offset << "\n";
}

void DumpEvents(const char* label, const AXEventGenerator& generator) {
  std::cout << "\n--- generated events [" << label << "] ---\n";
  for (const AXEventGenerator::TargetedEvent& event : generator) {
    std::cout << "  " << ToString(event.event_params->event) << " on node "
              << event.node_id << "\n";
  }
}

int Run() {
  MockCanvasEditor editor;
  Bridge bridge;

  std::cout << "=== B-lite host (Linux lean spine) ===\n";
  std::cout << "pipeline: MockCanvasEditor -> Bridge -> AXTree -> "
               "AXEventGenerator\n";
  std::cout << "(platform-node/UIA layer intentionally not linked here; "
               "VM work)\n";

  // 1. Build and serialize the initial tree from the custom surface.
  AXTree tree(bridge.BuildInitialTree(editor));
  std::cout << "\ninitial surface text: \"" << editor.text() << "\" caret="
            << editor.caret() << "\n";
  DumpTree(&tree);

  // 2. Attach the event generator and apply one keystroke from the surface.
  AXEventGenerator generator(&tree);
  editor.InsertText("!");
  AXTreeUpdate delta = bridge.BuildEditDelta(editor, &tree);
  if (!tree.Unserialize(delta)) {
    std::cerr << "Unserialize failed: " << tree.error() << "\n";
    return 1;
  }
  std::cout << "\nafter InsertText(\"!\"): \"" << editor.text() << "\" caret="
            << editor.caret() << "\n";
  DumpEvents("keystroke", generator);
  DumpTree(&tree);

  std::cout << "\n=== B-lite lean spine ran: surface -> bridge -> AXTree -> "
               "AXEventGenerator OK ===\n";
  return 0;
}

}  // namespace
}  // namespace ui

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  return ui::Run();
}
