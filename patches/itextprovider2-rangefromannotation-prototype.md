# Prototype: implement UIA ITextProvider2 / RangeFromAnnotation

Date: 2026-06-15. Against Chromium tag 149.0.7827.115. BROWSER-WORK item from results/NEXT-QUESTIONS.md #8 and docs/10 (text-core "Annotation-to-range navigation"). This corrects docs/10's seeded assumption: `ITextProvider2` is **not** implemented.

**The gap (verified):**
- The text provider COM map exposes only `ITextProvider` + `ITextEditProvider` (ax_platform_node_textprovider_win.h:22-23).
- `UIA_TextPattern2Id` is in the "not currently implemented" `break` list (ax_platform_node_win.cc:8733).
- `grep -rn RangeFromAnnotation ui/accessibility/` → zero hits.

Consequence: an AT can find a comment's text *attributes* (forward direction works) but cannot navigate **from a comment annotation element back to the text range it annotates** — `ITextProvider2::RangeFromAnnotation`. The forward data already exists: a comment is the target of some node's `kDetailsIds`, and `IAnnotationProvider::get_Target` (ax_platform_node_win.cc:2691) already resolves that reverse relation.

## Size

| Change | Lines |
|---|---|
| ax_platform_node_textprovider_win.h: inherit `ITextProvider2` + 2 COM entries + 2 method decls | 5 |
| ax_platform_node_textprovider_win.cc: implement `GetCaretRange` + `RangeFromAnnotation` | ~55 |
| ax_platform_node_win.cc: expose `UIA_TextPattern2Id` (move out of not-implemented list) | ~4 |
| **Total** | **~64 lines, 3 files** |

## Diff

### ax_platform_node_textprovider_win.h
```diff
   BEGIN_COM_MAP(AXPlatformNodeTextProviderWin)
   COM_INTERFACE_ENTRY(ITextProvider)
+  COM_INTERFACE_ENTRY(ITextProvider2)
   COM_INTERFACE_ENTRY(ITextEditProvider)
   ...
   IFACEMETHODIMP GetConversionTarget(ITextRangeProvider** range) override;
+
+  // ITextProvider2 implementation.
+  IFACEMETHODIMP GetCaretRange(BOOL* is_active,
+                               ITextRangeProvider** range) override;
+  IFACEMETHODIMP RangeFromAnnotation(IRawElementProviderSimple* annotation,
+                                     ITextRangeProvider** range) override;
```
(class inheritance list also gains `public ITextProvider2`.)

### ax_platform_node_textprovider_win.cc
```cpp
IFACEMETHODIMP AXPlatformNodeTextProviderWin::GetCaretRange(
    BOOL* is_active, ITextRangeProvider** range) {
  UIA_VALIDATE_TEXTPROVIDER_CALL();
  *is_active = FALSE;
  *range = nullptr;
  // Active when the owning node (or a descendant) holds the document focus.
  AXPlatformNodeWin* focus = /* owner's delegate focused node, as in
                                GetRangeFromActiveComposition helper */;
  if (focus) {
    *is_active = TRUE;
    // Degenerate range at the caret = collapsed selection endpoint, the same
    // endpoint GetSelection already computes from AXTreeData sel_ fields.
    *range = AXPlatformNodeTextRangeProviderWin::CreateTextRangeProvider(
        /* caret position from owner's selection focus */);
  }
  return S_OK;
}

IFACEMETHODIMP AXPlatformNodeTextProviderWin::RangeFromAnnotation(
    IRawElementProviderSimple* annotation, ITextRangeProvider** range) {
  UIA_VALIDATE_TEXTPROVIDER_CALL();
  *range = nullptr;
  // The annotation element (e.g. a comment) is the target of some node's
  // kDetailsIds. Resolve the source (the annotated node) via the existing
  // reverse-relation machinery, then return that node's text range.
  AXPlatformNodeWin* annotation_node = /* from IRawElementProviderSimple */;
  if (!annotation_node)
    return S_OK;
  std::vector<AXPlatformNode*> sources =
      annotation_node->GetDelegate()->GetSourceNodesForReverseRelations(
          ax::mojom::IntListAttribute::kDetailsIds);
  if (sources.empty())
    return S_OK;
  AXPlatformNodeWin* annotated =
      static_cast<AXPlatformNodeWin*>(sources.front());
  *range = AXPlatformNodeTextRangeProviderWin::CreateTextRangeProvider(
      /* full range of `annotated` */);
  return S_OK;
}
```

### ax_platform_node_win.cc (pattern exposure)
```diff
-    case UIA_TextPattern2Id:
+    // moved out of the not-implemented list:
     case UIA_TransformPatternId:
@@ near the Text pattern exposure (~8697)
     case UIA_TextEditPatternId:
     case UIA_TextPatternId:
       if (IsText() || IsTextField() || GetRole() == kRootWebArea) {
         return &PatternProvider<ITextProvider>;
       }
       break;
+    case UIA_TextPattern2Id:
+      if (IsText() || IsTextField() || GetRole() == ax::mojom::Role::kRootWebArea) {
+        return &PatternProvider<ITextProvider>;  // QI yields ITextProvider2
+      }
+      break;
```

## Notes for the VM build
- Confirm the exact helper names: `GetRangeFromActiveComposition` (used by `GetActiveComposition`, ax_platform_node_textprovider_win.cc) shows the focus-and-range idiom to copy for `GetCaretRange`; `CreateTextRangeProvider` is the existing factory in `AXPlatformNodeTextRangeProviderWin`.
- `RangeFromAnnotation` reuses `GetSourceNodesForReverseRelations(kDetailsIds)` — the *same* call `IAnnotationProvider::get_Target` already uses (ax_platform_node_win.cc:2691) — so no new data plumbing.
- Resolving an `IRawElementProviderSimple*` back to an `AXPlatformNodeWin*` uses the existing `AXPlatformNode::FromNativeViewAccessible` / QI pattern used elsewhere in the file.

## VM verification (TASK-00)
1. Build; unit-test `RangeFromAnnotation` on a doc where a comment node is the `kDetailsIds` target of a text range — assert the returned range covers the annotated text.
2. NVDA: navigate to a comment and confirm "jump to annotated text" works (the round-trip the forward-only path can't do today).
3. `git diff --stat` to confirm the ~64-line size.
