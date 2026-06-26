// Native C++ UIA client probe — the faithful test (same native IUIAutomation /
// CUIAutomation8 that NVDA uses; no .NET wrapper). Attaches to a window by HWND,
// dumps the raw UIA subtree. Usage: uiaprobe.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>
#include <cstdlib>

static void Dump(IUIAutomationTreeWalker* walker, IUIAutomationElement* el, int depth) {
  IUIAutomationElement* child = nullptr;
  if (FAILED(walker->GetFirstChildElement(el, &child)) || !child) return;
  while (child) {
    BSTR name = nullptr; child->get_CurrentName(&name);
    CONTROLTYPEID ct = 0; child->get_CurrentControlType(&ct);
    BSTR cls = nullptr; child->get_CurrentClassName(&cls);
    BSTR val = nullptr;  // Value pattern value, if any
    IUIAutomationValuePattern* vp = nullptr;
    if (SUCCEEDED(child->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp))) && vp) {
      vp->get_CurrentValue(&val); vp->Release();
    }
    BOOL hasFocus = FALSE; child->get_CurrentHasKeyboardFocus(&hasFocus);
    BOOL focusable = FALSE; child->get_CurrentIsKeyboardFocusable(&focusable);
    RECT bb = {0,0,0,0}; child->get_CurrentBoundingRectangle(&bb);
    BOOL hasText = FALSE; int selRanges = -1; BSTR selText = nullptr;
    IUIAutomationTextPattern* tp = nullptr;
    if (SUCCEEDED(child->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp) {
      hasText = TRUE;
      IUIAutomationTextRangeArray* sel = nullptr;
      if (SUCCEEDED(tp->GetSelection(&sel)) && sel) {
        sel->get_Length(&selRanges);
        if (selRanges > 0) {
          IUIAutomationTextRange* r0 = nullptr;
          if (SUCCEEDED(sel->GetElement(0, &r0)) && r0) { r0->GetText(40, &selText); r0->Release(); }
        }
        sel->Release();
      }
      tp->Release();
    }
    wprintf(L"%*s- ctrlType=%d name='%s' value='%s' focus=%d focusable=%d textPat=%d selRanges=%d selText='%s' bounds=[%d,%d %dx%d]\n",
            depth * 2, L"", ct, name ? name : L"", val ? val : L"", hasFocus, focusable, hasText,
            selRanges, selText ? selText : L"",
            bb.left, bb.top, bb.right - bb.left, bb.bottom - bb.top);
    if (selText) SysFreeString(selText);
    if (name) SysFreeString(name);
    if (cls) SysFreeString(cls);
    if (val) SysFreeString(val);
    Dump(walker, child, depth + 1);
    IUIAutomationElement* next = nullptr;
    walker->GetNextSiblingElement(child, &next);
    child->Release();
    child = next;
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe <hwnd-decimal>\n"); return 1; }
  HWND hwnd = (HWND)(INT_PTR)_atoi64(argv[1]);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation* uia = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
  if (FAILED(hr) || !uia) { wprintf(L"CoCreateInstance(CUIAutomation8) failed 0x%08x\n", (unsigned)hr); return 2; }
  IUIAutomationElement* root = nullptr;
  hr = uia->ElementFromHandle(hwnd, &root);
  if (FAILED(hr) || !root) { wprintf(L"ElementFromHandle failed 0x%08x\n", (unsigned)hr); return 3; }
  BSTR rn = nullptr; root->get_CurrentName(&rn);
  CONTROLTYPEID rc = 0; root->get_CurrentControlType(&rc);
  BSTR rf = nullptr; root->get_CurrentFrameworkId(&rf);
  wprintf(L"ROOT (native IUIAutomation): name='%s' ctrlType=%d framework='%s'\n",
          rn ? rn : L"", rc, rf ? rf : L"");
  IUIAutomationTreeWalker* walker = nullptr;
  uia->get_RawViewWalker(&walker);
  if (walker) { wprintf(L"--- raw subtree ---\n"); Dump(walker, root, 1); walker->Release(); }
  // Cross-check: focused element
  IUIAutomationElement* focused = nullptr;
  if (SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
    BSTR fn = nullptr; focused->get_CurrentName(&fn);
    CONTROLTYPEID fc = 0; focused->get_CurrentControlType(&fc);
    wprintf(L"FOCUSED element: name='%s' ctrlType=%d\n", fn ? fn : L"", fc);
    if (fn) SysFreeString(fn); focused->Release();
  }
  root->Release(); uia->Release();
  CoUninitialize();
  return 0;
}
