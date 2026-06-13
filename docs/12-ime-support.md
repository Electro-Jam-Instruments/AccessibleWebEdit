# 12 — IME / Text Input Support: Why It's Needed and What It Takes

Date: 2026-06-13. Captures the IME deep dive for the AccessibleWebEdit custom-surface editor. Companion to docs/09 Finding 2 (the source-level mechanism), docs/11 (cross-platform shell), and the T2-4 verification item in results/NEXT-QUESTIONS.md.

## What "IME" means here — not just CJK

IME = Input Method Editor: the system layer that turns *input* (keystrokes, touch, handwriting, voice, candidate picks) into *committed text* whenever the mapping is not a simple 1-to-1 keystroke -> character. CJK is the most visible case but IME is the umbrella for a whole class of text input a naive "read key, append char" editor cannot handle.

## Why raw key events are not enough

A naive editor reads key-down events and appends characters. That works only for simple Latin scripts. It silently breaks for, roughly in order of severity:

1. **CJK composition.** Japanese: type `n i h o n` -> the IME shows an underlined *composition* (pre-edit) + candidate list -> you pick 日本 -> it *commits*. Keystrokes are not the text; there is an intermediate, cancelable composition phase. Without IME you cannot type Chinese, Japanese, or Korean — three of the largest software markets.
2. **Dead keys / accents.** `´` then `e` -> `é`. The `´` produces no character until the next key. Composition again.
3. **Mobile is entirely IME.** On Android/iOS the on-screen keyboard *is* an IME — swipe-typing, autocorrect, predictive text — and there are often no key events at all; text arrives only as composition/commit via `InputConnection` / `UITextInput`.
4. **Autocorrect / autocomplete / suggestions.** Even on desktop, platform text services deliver *replacement* text (the `kInsertReplacementText` intent, tested in T2) through the input framework, not as keystrokes.
5. **Emoji pickers, handwriting, voice dictation.** All route text in through the same framework.

## IME is bidirectional — the editor must participate

It is not "the IME pushes text at you." The IME continuously queries and controls the editor. The editor must answer:

- **What is the text around the caret?** (context for candidates / reconversion)
- **Where is the selection/caret, as offsets?**
- **Where is the caret/composition on screen, in pixels?** (so the candidate window appears at the caret, not the screen corner)

And the IME drives the editor: sets the composition (underlined pre-edit), updates it per keystroke, commits the final string, and can reconvert committed text. So the editor must implement a stateful "text input client" contract the OS text service talks to.

## Why a custom canvas surface makes IME mandatory, not optional

A native text control (Win32 EDIT, NSTextView, HTML textarea) gets all of the above for free — it already speaks the platform text-input protocol. AccessibleWebEdit's premise is a **custom canvas surface**: we draw our own text, so *we are the text control*. The moment we opt out of the OS's built-in editable controls, we inherit the obligation to implement every platform's text-input protocol ourselves. The OS does not even know the canvas is editable text until we register as a text input client. For a custom-surface editor, IME is the price of admission for being an editor at all.

## The accessibility tie-in (the reason it matters for THIS project)

Per docs/09 Finding 2: composition state does NOT flow from the tree producer — it flows from the input stack. The Chromium path on Windows:

```
TSFTextStore  (Windows text service)
  -> TextInputClient::SetActiveCompositionForAccessibility   (ui/base/ime/win/tsf_text_store.cc)
    -> AXPlatformNodeWin::OnActiveComposition                (ax_platform_node_win.cc:823)
      -> UIA ITextEditProvider::GetActiveComposition / GetConversionTarget
        -> screen reader announces the composition
```

If IME is not wired, we don't merely lose CJK typing — we lose the **accessibility of composition entirely**: a blind user typing Japanese hears nothing as they navigate candidates; autocorrect replacements are not announced. For an accessibility product that is a fatal gap. This is exactly the **T2-4** verification item (Windows VM): confirm `OnActiveComposition` -> `GetActiveComposition`/`GetConversionTarget` round-trips and that committed compositions do not double-announce (they deliberately defer to the standard text-changed path, ax_platform_node_win.cc:836-848).

EditContext angle: a web editor using the `EditContext` API gets IME for free because EditContext rides the normal IME pipeline. The standalone B-lite host has no IME wiring unless built — minimum viable is calling `OnActiveComposition` directly; full fidelity is integrating `TSFTextStore`.

## What breaks if IME is skipped

- No CJK input -> cannot ship in China/Japan/Korea.
- No dead-key/accented input -> European languages broken.
- Mobile typing broken or impossible (mobile text *is* the IME).
- No autocorrect, autocomplete, predictive text, emoji, handwriting, dictation.
- **Composition invisible to screen readers** -> accessibility regression in an accessibility product.
- Candidate/suggestion popups mispositioned (appear at screen origin, not the caret).

## Per-platform contract + minimum-viable vs full-fidelity

| Platform | Contract to implement | Minimum viable | Full fidelity |
|---|---|---|---|
| Windows (1st) | **TSF** (`ITextStoreACP` / Chromium `TSFTextStore`) | Commit-only via legacy IMM32 or a thin TSF store; call `OnActiveComposition` directly so a11y sees composition | Full `TSFTextStore`: composition phases, reconversion, candidate-window positioning, a11y composition hook |
| macOS (2nd) | `NSTextInputClient` | Implement `insertText:` + marked-text basics | Full marked-text ranges, `firstRectForCharacterRange:` for candidate placement, attributed substrings |
| Linux (3rd) | IBus / fcitx (via toolkit / Ozone) | Commit via toolkit input context | Pre-edit display + candidate positioning over AT-SPI |
| Android (mobile) | `InputConnection` | `commitText` + `setComposingText` | Full composing regions, batch edits, autocorrect/suggestions surface |
| iOS (mobile) | `UITextInput` | `insertText:` + marked text | Full `UITextInput` protocol incl. tokenizer, geometry for the system caret/candidate UI |

"Minimum viable" gets characters in and composition announced; "full fidelity" gets correct candidate-window placement, reconversion, and the autocorrect/suggestion surfaces real editors have. T2-4 verifies the Windows accessibility path at minimum-viable depth first.

## Why IME drives the build-strategy decision (docs/11 A vs B)

Because the contract is different and stateful on all five platforms, IME is the single biggest argument for **Strategy (A): build inside the Chromium tree.** Chromium's `ui/base/ime` already implements `TextInputClient` with TSF, `NSTextInputClient`, IBus, etc., AND already contains the accessibility hook (`SetActiveCompositionForAccessibility`) wired to `AXPlatformNode`. Strategy (B) standalone means writing and debugging that text-input contract plus its a11y bridge ourselves, five times.

## Open decisions / verification queue

1. IME depth per platform — start minimum-viable (commit + composition-announce) or invest in full fidelity early? Ties to T2-4.
2. Reuse `ui/base/ime` (Strategy A) vs own implementation (Strategy B) — IME is the dominant input to this choice.
3. T2-4 (Windows VM): verify `OnActiveComposition` round-trip and no double-announce on commit.
4. Mobile IME (InputConnection / UITextInput) is deferred with the rest of the mobile shells (docs/11 sequencing).
