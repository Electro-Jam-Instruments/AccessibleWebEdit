# Prototype: fire a UIA event for TEXT_ATTRIBUTE_CHANGED

Date: 2026-06-15. Against Chromium tag 149.0.7827.115. BROWSER-WORK item from results/NEXT-QUESTIONS.md #8 and docs/10 (text-core "Formatting change NOTIFICATION on UIA").

**The gap (verified):** `TEXT_ATTRIBUTE_CHANGED` is generated cross-platform (ax_event_generator.cc:529), but the Windows manager fires only the IA2 event — no UIA counterpart. So a UIA client (NVDA/Narrator) is *not* notified when bold/italic/underline/spelling-style/etc. changes on a range; it only finds out by re-reading. Source, browser_accessibility_manager_win.cc:681-683:

```cpp
case AXEventGenerator::Event::TEXT_ATTRIBUTE_CHANGED:
  FireWinAccessibilityEvent(IA2_EVENT_TEXT_ATTRIBUTE_CHANGED, wrapper);
  break;
```

Contrast: `EDITABLE_TEXT_CHANGED` and `DOCUMENT_SELECTION_CHANGED` are enqueued into `text_changed_nodes_` / `selection_changed_nodes_` and finalized into real UIA events (browser_accessibility_manager_win.cc:1300-1314). The attribute case simply never joins that path.

## Size

| Change | Lines |
|---|---|
| browser_accessibility_manager_win.cc: call `EnqueueTextChangedEvent(*wrapper)` in the TEXT_ATTRIBUTE_CHANGED case | 1–2 |
| **Total** | **~2 lines, 1 file** |

## Diff (minimum-viable)

> **Correction (expert review 2026-06-16):** an earlier draft of this patch did `text_changed_nodes_.insert(wrapper)` with a *raw* leaf wrapper. That is wrong: the real code never inserts a raw wrapper — `EnqueueTextChangedEvent` (browser_accessibility_manager_win.cc:1229-1234) inserts `GetUiaTextPatternProvider(node)`, resolving to a node that actually supports `UIA_TextPatternId`. Because `FinalizeAccessibilityEvents` fires `UIA_Text_TextChangedEventId` **unconditionally** on the set (no Text-pattern guard, unlike the selection set), a raw leaf insert risks firing on a node with no Text pattern. Use the existing helper, mirroring the EDITABLE_TEXT_CHANGED case (line 476):

```diff
     case AXEventGenerator::Event::TEXT_ATTRIBUTE_CHANGED:
       FireWinAccessibilityEvent(IA2_EVENT_TEXT_ATTRIBUTE_CHANGED, wrapper);
+      // Also notify UIA clients: an attribute change is a text-pattern change.
+      // EnqueueTextChangedEvent resolves to the UIA Text-pattern provider, so
+      // finalize fires UIA_Text_TextChangedEventId on a node that supports it.
+      EnqueueTextChangedEvent(*wrapper);
       break;
```

`FinalizeAccessibilityEvents` (browser_accessibility_manager_win.cc:1311-1314) then fires `FireUiaAccessibilityEvent(UIA_Text_TextChangedEventId, ...)` over the queued (Text-pattern-resolved) nodes — no other change needed.

## Notes / richer alternative

- Minimum-viable above fires `UIA_Text_TextChangedEventId`, which tells the AT "re-read this text's attributes." That's the same coarse signal UIA uses for content changes and is what most ATs act on.
- Richer fidelity: raise a TextEdit change via `UiaRaiseTextEditTextChangedEvent` with a `TextEditChangeType` (e.g. `TextEditChangeType_AutoCorrect`/composition), carrying the changed range. That needs the changed-range plumbed from the generator's intent (the `kFormat*` intents exist in the mojom but are unconsumed on the UIA path today — docs/09 Finding 1), so it's a larger change and depends on wiring intents through.
- **Producer tie-in (docs/03 §1.2):** this only fires when the changed node carries `State::kRichlyEditable` — the canvas producer must set it on text nodes (verified by the matrix FormatBold test). The patch is moot without that.

## VM verification (TASK-00)

1. Build with the patch; add a unit test asserting `UIA_Text_TextChangedEventId` fires when a richly-editable node's `kTextStyle` changes (mirror the existing text-changed test).
2. With NVDA (docs/TASK-00a speech log), toggle bold on a selection and confirm an attribute/text-change announcement that was absent before.
3. `git diff --stat` to confirm the ~2-line size.
