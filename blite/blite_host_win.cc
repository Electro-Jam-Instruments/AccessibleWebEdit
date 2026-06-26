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

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/at_exit.h"
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
class MockCanvasEditor {
 public:
  const std::string& text() const { return text_; }
  int caret() const { return caret_; }

  void InsertText(const std::string& s) {
    text_.insert(caret_, s);
    caret_ += static_cast<int>(s.size());
  }

  // Replace the whole buffer -- used when a UIA client writes via
  // IValueProvider::SetValue. Caret goes to the end, like committing a value.
  void SetText(const std::string& s) {
    text_ = s;
    caret_ = static_cast<int>(text_.size());
  }

  // Run formatting (mock: applies to the whole text). A real editor would carry
  // per-run styles; this is enough to exercise UIA text attributes + changes.
  bool bold() const { return bold_; }
  bool italic() const { return italic_; }
  bool underline() const { return underline_; }
  void set_bold(bool b) { bold_ = b; }

 private:
  std::string text_ = "hello";
  int caret_ = 5;
  bool bold_ = true;
  bool italic_ = true;
  bool underline_ = true;
};

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
void FillRunAttributes(AXNodeData& node, const MockCanvasEditor& editor) {
  // Always set a weight (700 bold / 400 normal) so a bold->not-bold change is a
  // clean value change UIA_FontWeightAttributeId reports (not 700->absent).
  node.AddFloatAttribute(ax::mojom::FloatAttribute::kFontWeight,
                         editor.bold() ? 700.0f : 400.0f);
  if (editor.italic()) {
    node.AddIntAttribute(
        ax::mojom::IntAttribute::kTextStyle,
        1 << static_cast<int>(ax::mojom::TextStyle::kItalic));
  }
  if (editor.underline()) {
    node.AddIntAttribute(
        ax::mojom::IntAttribute::kTextUnderlineStyle,
        static_cast<int>(ax::mojom::TextDecorationStyle::kSolid));
  }
}

void FillInlineTextBox(AXNodeData& node, const MockCanvasEditor& editor) {
  const std::string& text = editor.text();
  node.role = ax::mojom::Role::kInlineTextBox;
  node.SetName(text);
  std::vector<int32_t> char_offsets;
  char_offsets.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i)
    char_offsets.push_back(static_cast<int32_t>((i + 1) * 7));  // 7px/char mock
  node.AddIntListAttribute(ax::mojom::IntListAttribute::kCharacterOffsets,
                           char_offsets);
  FillRunAttributes(node, editor);
}

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
  // Atomic text field: UIA reads run attributes from the field itself.
  FillRunAttributes(node, editor);
  node.child_ids = {kText};
}

// The bridge: turns the editor model into accessibility tree updates. Caret /
// selection live in AXTreeData (tree level). docs/03 §1.1 (role-only editable
// root), §2.1/§2.2 (selection in AXTreeData).
class Bridge {
 public:
  AXTreeUpdate BuildInitialTree(const MockCanvasEditor& editor) {
    AXTreeUpdate update;
    update.has_tree_data = true;
    // Caret lives on the inline-text-box LEAF (the addressable text), not the
    // StaticText container.
    update.tree_data.sel_anchor_object_id = kInline;
    update.tree_data.sel_anchor_offset = editor.caret();
    update.tree_data.sel_focus_object_id = kInline;
    update.tree_data.sel_focus_offset = editor.caret();
    // Mark the editable field as the tree's focused node so NVDA treats it as
    // the real keyboard-focus target (it filters focus on non-focused controls).
    update.tree_data.focus_id = kField;
    // A valid tree id lets an AXTreeManager register this tree so the platform
    // layer can resolve nodes for UIA navigation.
    update.tree_data.tree_id = AXTreeID::CreateNewAXTreeID();
    update.root_id = kRoot;
    update.nodes.resize(4);
    update.nodes[0].id = kRoot;
    update.nodes[0].role = ax::mojom::Role::kRootWebArea;
    update.nodes[0].child_ids = {kField};
    FillFieldNode(update.nodes[1], editor);
    update.nodes[2].id = kText;
    update.nodes[2].role = ax::mojom::Role::kStaticText;
    update.nodes[2].SetName(editor.text());
    FillRunAttributes(update.nodes[2], editor);  // attrs on the platform leaf
    update.nodes[2].child_ids = {kInline};
    update.nodes[3].id = kInline;
    FillInlineTextBox(update.nodes[3], editor);
    return update;
  }

  // A single-node text delta plus the moved caret -- one atomic AXTreeUpdate,
  // the way the editor would post one keystroke (docs/03 §3.1, §3.2).
  AXTreeUpdate BuildEditDelta(const MockCanvasEditor& editor, AXTree* tree) {
    AXTreeUpdate update;
    update.has_tree_data = true;
    update.tree_data = tree->data();
    update.tree_data.sel_anchor_offset = editor.caret();
    update.tree_data.sel_focus_offset = editor.caret();
    // Rebuild all three nodes fresh (avoids duplicate-attribute issues from
    // copying existing data, and lets run-attribute CHANGES take effect).
    update.nodes.resize(3);
    FillFieldNode(update.nodes[0], editor);  // value + current run attributes
    update.nodes[1].id = kText;
    update.nodes[1].role = ax::mojom::Role::kStaticText;
    update.nodes[1].SetName(editor.text());
    update.nodes[1].child_ids = {kInline};
    FillRunAttributes(update.nodes[1], editor);  // attrs on the platform leaf
    update.nodes[2].id = kInline;
    FillInlineTextBox(update.nodes[2], editor);
    return update;
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
    for (AXNodeID id : {kRoot, kField, kText, kInline}) {
      AXNode* node = tree_->GetFromId(id);
      delegates_[id] = std::make_unique<BliteNodeDelegate>(this, node);
    }
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
    std::cout << "[host] client SetValue applied -> \"" << editor_->text()
              << "\"\n";
    FirePlatformEditEvents(this);
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
    return true;
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

size_t BliteNodeDelegate::GetChildCount() const {
  return node()->GetChildCount();
}

gfx::NativeViewAccessible BliteNodeDelegate::ChildAtIndex(size_t index) const {
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
  editor.set_bold(false);  // also flip formatting bold->normal on this edit, to
                           // exercise rich-text attribute-change detection
  AXTreeUpdate delta = bridge.BuildEditDelta(editor, tree_ptr);
  if (!tree_ptr->Unserialize(delta)) {
    std::cerr << "Unserialize failed: " << tree_ptr->error() << "\n";
    return 1;
  }
  std::cout << "\nafter InsertText(\"!\"): \"" << editor.text()
            << "\" caret=" << editor.caret() << "\n";
  DumpEvents("keystroke", generator);

  // 4. Fire the platform/UIA events so a UIA client surfaces TextChanged +
  //    TextSelectionChanged. This is the half blite_host.cc could not reach.
  FirePlatformEditEvents(&host);

  // 5. Keep pumping so NVDA can process + announce the events, then self-exit
  //    (autonomous capture leaves no orphan window). ~12s is ample for NVDA to
  //    coalesce + speak the TextChanged + TextSelectionChanged.
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
