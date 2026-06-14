# Prototype: implement UIA SelectionPattern2 (`ISelectionProvider2`) in Chromium

Date: 2026-06-14. Against Chromium tag 149.0.7827.115. This is the BROWSER-WORK item #8 from results/NEXT-QUESTIONS.md and the headline standards exhibit from docs/10: the UIA platform API defines `ISelectionProvider2` (FirstSelectedItem / LastSelectedItem / CurrentSelectedItem / ItemCount — what Narrator uses for "N of M selected" summaries), but Chromium implements only `ISelectionProvider` (v1). Verified: `grep -rn ISelectionProvider2 ui/accessibility/` returns zero hits.

**Status:** prototype written and reviewed against source; **not compiled here** — `ax_platform_node_win.cc` is `#if BUILDFLAG(IS_WIN)` and we hold the platform layer for the Windows VM (results/ENVIRONMENT.md). Build + test belongs on the VM. The deliverable here is the concrete diff and its measured size, demonstrating the patch is small because all four properties derive from data the existing v1 `GetSelection` already enumerates.

## Size

| Change | Lines added |
|---|---|
| `ax_platform_node_win.h`: inherit + COM entry + 4 method decls | 6 |
| `ax_platform_node_win.cc`: 4 method implementations | ~78 |
| **Total** | **~84 lines, 2 files** |

No new data plumbing, no schema change, no new pattern exposure (ISelectionProvider2 is reached by QueryInterface from the existing SelectionPattern provider). This is the "platform API outruns what the browser surfaces" exhibit: ~84 lines to close a Narrator-visible gap.

## Diff

### ui/accessibility/platform/ax_platform_node_win.h

```diff
@@ class AXPlatformNodeWin : ... ,
       public ISelectionItemProvider,
       public ISelectionProvider,
+      public ISelectionProvider2,
       ...
@@ BEGIN_COM_MAP / interface entries
     COM_INTERFACE_ENTRY(ISelectionProvider)
+    COM_INTERFACE_ENTRY(ISelectionProvider2)
@@ ISelectionProvider method declarations (~line 738)
   IFACEMETHODIMP get_IsSelectionRequired(BOOL* result) override;
+
+  // ISelectionProvider2 implementation.
+  IFACEMETHODIMP get_FirstSelectedItem(IRawElementProviderSimple** result) override;
+  IFACEMETHODIMP get_LastSelectedItem(IRawElementProviderSimple** result) override;
+  IFACEMETHODIMP get_CurrentSelectedItem(IRawElementProviderSimple** result) override;
+  IFACEMETHODIMP get_ItemCount(int* result) override;
```

### ui/accessibility/platform/ax_platform_node_win.cc

Inserted after `get_IsSelectionRequired` (after ~line 3286). Reuses the exact selection enumeration the v1 `GetSelection` uses (`GetMaxSelectableItems` + `GetSelectedItems`), so behavior is consistent by construction.

```cpp
//
// ISelectionProvider2 implementation.
//

namespace {
// Shared helper: return the selected children of this container, in tree order,
// using the same enumeration ISelectionProvider::GetSelection relies on.
}  // namespace

IFACEMETHODIMP AXPlatformNodeWin::get_FirstSelectedItem(
    IRawElementProviderSimple** result) {
  WIN_ACCESSIBILITY_API_TRACE_EVENT("get_FirstSelectedItem");
  UIA_VALIDATE_CALL_1_ARG(result);
  *result = nullptr;
  std::vector<AXPlatformNodeBase*> selected_children;
  int max_items = GetMaxSelectableItems();
  if (max_items)
    GetSelectedItems(max_items, &selected_children);
  if (selected_children.empty())
    return UIA_E_ELEMENTNOTAVAILABLE;
  return static_cast<AXPlatformNodeWin*>(selected_children.front())
      ->QueryInterface(IID_PPV_ARGS(result));
}

IFACEMETHODIMP AXPlatformNodeWin::get_LastSelectedItem(
    IRawElementProviderSimple** result) {
  WIN_ACCESSIBILITY_API_TRACE_EVENT("get_LastSelectedItem");
  UIA_VALIDATE_CALL_1_ARG(result);
  *result = nullptr;
  std::vector<AXPlatformNodeBase*> selected_children;
  int max_items = GetMaxSelectableItems();
  if (max_items)
    GetSelectedItems(max_items, &selected_children);
  if (selected_children.empty())
    return UIA_E_ELEMENTNOTAVAILABLE;
  return static_cast<AXPlatformNodeWin*>(selected_children.back())
      ->QueryInterface(IID_PPV_ARGS(result));
}

IFACEMETHODIMP AXPlatformNodeWin::get_CurrentSelectedItem(
    IRawElementProviderSimple** result) {
  WIN_ACCESSIBILITY_API_TRACE_EVENT("get_CurrentSelectedItem");
  UIA_VALIDATE_CALL_1_ARG(result);
  *result = nullptr;
  // "Current" = the selected item that currently has focus, if any; otherwise
  // fall back to the first selected item. This matches Narrator's expectation
  // of "the item the user is acting on" within a multi-selection.
  std::vector<AXPlatformNodeBase*> selected_children;
  int max_items = GetMaxSelectableItems();
  if (max_items)
    GetSelectedItems(max_items, &selected_children);
  if (selected_children.empty())
    return UIA_E_ELEMENTNOTAVAILABLE;
  for (AXPlatformNodeBase* child : selected_children) {
    if (child->GetDelegate()->IsFocused()) {
      return static_cast<AXPlatformNodeWin*>(child)->QueryInterface(
          IID_PPV_ARGS(result));
    }
  }
  return static_cast<AXPlatformNodeWin*>(selected_children.front())
      ->QueryInterface(IID_PPV_ARGS(result));
}

IFACEMETHODIMP AXPlatformNodeWin::get_ItemCount(int* result) {
  WIN_ACCESSIBILITY_API_TRACE_EVENT("get_ItemCount");
  UIA_VALIDATE_CALL_1_ARG(result);
  std::vector<AXPlatformNodeBase*> selected_children;
  int max_items = GetMaxSelectableItems();
  if (max_items)
    GetSelectedItems(max_items, &selected_children);
  *result = static_cast<int>(selected_children.size());
  return S_OK;
}
```

Notes for the VM build:
- `IsFocused()` accessor name to confirm against `AXPlatformNodeDelegate` on the pinned tag (the focus check in `get_CurrentSelectedItem` is the only non-trivial bit; if no per-item focus query is convenient, fall back to first-selected, which is spec-acceptable).
- `UIA_E_ELEMENTNOTAVAILABLE` is the documented return when there is no selection, matching how other providers signal "no element".
- No change to `GetPatternProviderFactoryMethod`: `ISelectionProvider2` is obtained via `QueryInterface` on the object already returned for `UIA_SelectionPatternId`, so adding it to the COM map is sufficient.

## VM verification (when TASK-00 exists)

1. Build `ax_platform_node_win_unittest` (or the host) with this patch; add a unit test mirroring the existing `ISelectionProvider` tests for the four new properties on a multiselectable grid with 2-of-N selected.
2. With NVDA (docs/TASK-00a speech log), select multiple cells and confirm an N-of-M summary is announced that was absent before the patch.
3. Measure the real diff size with `git diff --stat` to confirm the ~84-line estimate.
