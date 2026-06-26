// Native UIA client that issues a WRITE action (client->provider direction):
// finds the Edit, reads its Value, calls IValueProvider::SetValue, reads back.
// Tells us whether our provider applies a client action or no-ops/errors.
// Usage: uiaprobe_action.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

// Walk the raw subtree to the first Edit (ControlType 50004).
static IUIAutomationElement* FindEdit(IUIAutomationTreeWalker* w, IUIAutomationElement* el) {
  IUIAutomationElement* child = nullptr;
  if (FAILED(w->GetFirstChildElement(el, &child)) || !child) return nullptr;
  while (child) {
    CONTROLTYPEID ct = 0; child->get_CurrentControlType(&ct);
    if (ct == UIA_EditControlTypeId) return child;  // caller releases
    IUIAutomationElement* found = FindEdit(w, child);
    if (found) { child->Release(); return found; }
    IUIAutomationElement* next = nullptr;
    w->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
  return nullptr;
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_action <hwnd-decimal>\n"); return 1; }
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

  IUIAutomationValuePattern* vp = nullptr;
  if (FAILED(edit->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp))) || !vp) {
    wprintf(L"Edit has no Value pattern\n"); return 5;
  }
  BSTR before = nullptr; vp->get_CurrentValue(&before);
  wprintf(L"value BEFORE: '%s'\n", before ? before : L"");

  BSTR target = SysAllocString(L"client-typed");
  HRESULT hr = vp->SetValue(target);
  wprintf(L"SetValue('client-typed') hr=0x%08x\n", (unsigned)hr);
  SysFreeString(target);

  Sleep(800);  // let the provider's STA dispatch + apply the action
  BSTR after = nullptr; vp->get_CurrentValue(&after);
  wprintf(L"value AFTER:  '%s'\n", after ? after : L"");
  wprintf(L"=== round-trip %s ===\n",
          (after && wcscmp(after, L"client-typed") == 0) ? L"APPLIED" : L"NOT applied (no-op/error)");

  if (before) SysFreeString(before);
  if (after) SysFreeString(after);
  vp->Release(); edit->Release(); if (walker) walker->Release();
  root->Release(); uia->Release(); CoUninitialize();
  return 0;
}
