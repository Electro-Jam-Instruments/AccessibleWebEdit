# Prototype: back IAnnotationProvider get_Author / get_DateTime with real data

Date: 2026-06-15. Against Chromium tag 149.0.7827.115. BROWSER-WORK item from results/NEXT-QUESTIONS.md #8 and docs/10 (comments/collaboration). This is the one prototype in the set that needs a small **schema** addition, not just platform code — worth calling out.

**The gap (verified):** `IAnnotationProvider::get_Author` and `get_DateTime` are empty-string stubs (ax_platform_node_win.cc:2670-2688):

```cpp
IFACEMETHODIMP AXPlatformNodeWin::get_Author(BSTR* author) {
  ...
  // This method is optional, and currently does not have a mapping. So we
  // return S_OK with empty string.
  *author = SysAllocString(L"");
  return S_OK;
}
// get_DateTime: identical empty-string stub.
```

So a screen reader reading a comment cannot announce *who* wrote it or *when* — directly limiting the comments/collaboration story (docs/10). The pattern plumbing (`IAnnotationProvider`, `get_Target`) already works; only the author/datetime *values* are missing, and there is no AX attribute carrying them today.

## Size

| Change | Lines |
|---|---|
| ax_enums.mojom: add `StringAttribute::kAnnotationAuthor`, `kAnnotationDateTime` | 2 |
| ax_platform_node_win.cc: map them in `get_Author` / `get_DateTime` | ~6 |
| **Total** | **~8 lines, 2 files** (+ regenerated mojom bindings) |

This is the only patch touching the cross-platform schema, so it's also the one that feeds straight into the **bridge contract** (docs/03): the canvas producer would set these on comment nodes.

## Diff

### ui/accessibility/ax_enums.mojom
```diff
   // ... existing StringAttributes ...
+  // Author display name of an annotation (e.g. a comment). Consumed by the
+  // Windows UIA IAnnotationProvider::get_Author mapping.
+  kAnnotationAuthor,
+  // Human-readable timestamp of an annotation, for IAnnotationProvider::get_DateTime.
+  kAnnotationDateTime,
```

### ui/accessibility/platform/ax_platform_node_win.cc
```diff
 IFACEMETHODIMP AXPlatformNodeWin::get_Author(BSTR* author) {
   ...
   UIA_VALIDATE_CALL_1_ARG(author);
-  // This method is optional, and currently does not have a mapping. So we
-  // return S_OK with empty string.
-  *author = SysAllocString(L"");
+  *author = SysAllocString(base::as_wcstr(GetString16Attribute(
+      ax::mojom::StringAttribute::kAnnotationAuthor)));
   return S_OK;
 }

 IFACEMETHODIMP AXPlatformNodeWin::get_DateTime(BSTR* date_time) {
   ...
   UIA_VALIDATE_CALL_1_ARG(date_time);
-  *date_time = SysAllocString(L"");
+  *date_time = SysAllocString(base::as_wcstr(GetString16Attribute(
+      ax::mojom::StringAttribute::kAnnotationDateTime)));
   return S_OK;
 }
```
(`GetString16Attribute` returns empty string when unset, preserving the current behavior for producers that don't supply the values — so this is non-breaking.)

## Notes
- `GetString16Attribute` is the standard accessor used throughout ax_platform_node_win.cc; confirm exact name on the pinned tag (may be `GetString16Attribute` on `AXPlatformNodeBase`).
- Producer tie-in (docs/03): comment nodes carry `kAnnotationAuthor` / `kAnnotationDateTime`. On the web, the analogous fields would ride a comment's accessible representation; this is the kind of attribute a future delta-based web child-tree API (docs/13) would also need.
- Standards angle: UIA's Annotation pattern *defines* Author/DateTime; Chromium just never mapped them. Like SelectionPattern2, this is "the platform API outruns what the browser surfaces" — ~8 lines to close it.

## VM verification (TASK-00)
1. Regenerate mojom bindings; build; unit-test that a comment node with the two attributes returns them via `get_Author`/`get_DateTime`.
2. NVDA: confirm a comment announces author + timestamp.
3. `git diff --stat` to confirm size.
