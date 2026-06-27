// Native UIA client that issues a SELECTION action (client->provider cursor
// routing): finds the Edit's Text pattern, reads the current selection, calls
// ITextRangeProvider::Select on the document range, reads the selection back.
// Tells us whether kSetSelection round-trips. Usage: uiaprobe_select.exe <hwnd>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

static IUIAutomationElement* FindEdit(IUIAutomationTreeWalker* w, IUIAutomationElement* el) {
  IUIAutomationElement* child = nullptr;
  if (FAILED(w->GetFirstChildElement(el, &child)) || !child) return nullptr;
  while (child) {
    CONTROLTYPEID ct = 0; child->get_CurrentControlType(&ct);
    if (ct == UIA_EditControlTypeId) return child;
    IUIAutomationElement* found = FindEdit(w, child);
    if (found) { child->Release(); return found; }
    IUIAutomationElement* next = nullptr;
    w->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
  return nullptr;
}

// Print the current selection's text (or "<degenerate/none>").
static void PrintSelection(IUIAutomationTextPattern* tp, const wchar_t* label) {
  IUIAutomationTextRangeArray* sel = nullptr;
  if (SUCCEEDED(tp->GetSelection(&sel)) && sel) {
    int n = 0; sel->get_Length(&n);
    if (n > 0) {
      IUIAutomationTextRange* r0 = nullptr;
      if (SUCCEEDED(sel->GetElement(0, &r0)) && r0) {
        BSTR t = nullptr; HRESULT gh = r0->GetText(80, &t);
        wprintf(L"%s: ranges=%d GetText hr=0x%08x text='%s'\n", label, n, (unsigned)gh,
                (t && *t) ? t : L"<empty>");
        if (t) SysFreeString(t);
        // The caret/selection rect(s) in SCREEN px (groups of 4 doubles).
        SAFEARRAY* rects = nullptr;
        if (SUCCEEDED(r0->GetBoundingRectangles(&rects)) && rects) {
          LONG lb = 0, ub = -1; SafeArrayGetLBound(rects, 1, &lb); SafeArrayGetUBound(rects, 1, &ub);
          double* d = nullptr; SafeArrayAccessData(rects, reinterpret_cast<void**>(&d));
          LONG cnt = ub - lb + 1;
          if (cnt < 4) wprintf(L"    boundingRects: (none -- degenerate caret returns no rect)\n");
          for (LONG i = 0; i + 3 < cnt; i += 4)
            wprintf(L"    boundingRect screen=[%.0f,%.0f %.0fx%.0f]\n", d[i], d[i+1], d[i+2], d[i+3]);
          SafeArrayUnaccessData(rects); SafeArrayDestroy(rects);
        }
        r0->Release();
      }
    } else wprintf(L"%s: ranges=0\n", label);
    sel->Release();
  } else wprintf(L"%s: GetSelection failed\n", label);
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_select <hwnd-decimal>\n"); return 1; }
  HWND hwnd = (HWND)(INT_PTR)_atoi64(argv[1]);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation* uia = nullptr;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) || !uia) {
    wprintf(L"CoCreateInstance failed\n"); return 2;
  }
  IUIAutomationElement* root = nullptr;
  if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) { wprintf(L"ElementFromHandle failed\n"); return 3; }
  IUIAutomationTreeWalker* walker = nullptr; uia->get_RawViewWalker(&walker);
  IUIAutomationElement* edit = walker ? FindEdit(walker, root) : nullptr;
  if (!edit) { wprintf(L"no Edit found\n"); return 4; }

  IUIAutomationTextPattern* tp = nullptr;
  if (FAILED(edit->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) || !tp) {
    wprintf(L"Edit has no Text pattern\n"); return 5;
  }
  PrintSelection(tp, L"selection BEFORE");

  IUIAutomationTextRange* doc = nullptr;
  if (FAILED(tp->get_DocumentRange(&doc)) || !doc) { wprintf(L"no DocumentRange\n"); return 6; }
  // Does the field's text range report CHILD elements (embedded/"replaced"
  // content)? A flat editable field should report 0 -- children here are what
  // make NVDA's _moveToEdgeOfReplacedContent error on gainFocus.
  {
    IUIAutomationElementArray* kids = nullptr;
    HRESULT hk = doc->GetChildren(&kids);
    int n = -1; if (SUCCEEDED(hk) && kids) kids->get_Length(&n);
    wprintf(L"DocumentRange.GetChildren() hr=0x%08x count=%d\n", (unsigned)hk, n);
    for (int i = 0; i < n; ++i) {
      IUIAutomationElement* k = nullptr;
      if (SUCCEEDED(kids->GetElement(i, &k)) && k) {
        CONTROLTYPEID ct = 0; k->get_CurrentControlType(&ct);
        BSTR kn = nullptr; k->get_CurrentName(&kn);
        wprintf(L"    child[%d] ctrlType=%d name='%s'\n", i, ct, kn ? kn : L"");
        if (kn) SysFreeString(kn); k->Release();
      }
    }
    if (kids) kids->Release();
    IUIAutomationElement* enc = nullptr;
    if (SUCCEEDED(doc->GetEnclosingElement(&enc)) && enc) {
      CONTROLTYPEID ct = 0; enc->get_CurrentControlType(&ct);
      wprintf(L"DocumentRange.GetEnclosingElement() ctrlType=%d\n", ct);
      enc->Release();
    }
  }
  // Exercise grapheme/word break iteration (ICU) the way NVDA does when it
  // navigates -- this is what FATAL-crashed the host before InitializeICU.
  {
    IUIAutomationTextRange* wr = nullptr;
    if (SUCCEEDED(doc->Clone(&wr)) && wr) {
      HRESULT he = wr->ExpandToEnclosingUnit(TextUnit_Word);
      wprintf(L"ExpandToEnclosingUnit(Word) hr=0x%08x (no crash = ICU ok)\n", (unsigned)he);
      BSTR wt = nullptr; wr->GetText(20, &wt);
      wprintf(L"    word-unit text='%s'\n", (wt && *wt) ? wt : L"<empty>");
      if (wt) SysFreeString(wt); wr->Release();
    }
  }
  // Is the DocumentRange itself valid? GetText runs the same validation macro.
  BSTR dtext = nullptr; HRESULT htext = doc->GetText(-1, &dtext);
  wprintf(L"DocumentRange.GetText() hr=0x%08x text='%s'\n",
          (unsigned)htext, (dtext && *dtext) ? dtext : L"<empty>");
  if (dtext) SysFreeString(dtext);
  HRESULT hr = doc->Select();          // client requests: select the whole text
  wprintf(L"DocumentRange.Select() hr=0x%08x\n", (unsigned)hr);
  doc->Release();

  Sleep(800);                          // let the provider's STA apply kSetSelection
  PrintSelection(tp, L"selection AFTER ");

  tp->Release(); edit->Release(); if (walker) walker->Release();
  root->Release(); uia->Release(); CoUninitialize();
  return 0;
}
