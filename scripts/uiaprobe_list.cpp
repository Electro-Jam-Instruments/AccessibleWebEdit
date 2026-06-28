// Native UIA client that verifies LIST structure (Phase 3.2): walks the tree
// from the HWND, finds the List, enumerates its ListItems, and reads each item's
// PositionInSet / SizeOfSet + its marker + text. This is what NVDA reads to say
// "list", "bullet", "N of M". Usage: uiaprobe_list.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

static IUIAutomationTreeWalker* g_walker = nullptr;

// Depth-first find the first element of a given control type.
static IUIAutomationElement* FindByType(IUIAutomationElement* el, CONTROLTYPEID want) {
  CONTROLTYPEID ct = 0; el->get_CurrentControlType(&ct);
  if (ct == want) { el->AddRef(); return el; }
  IUIAutomationElement* child = nullptr;
  if (FAILED(g_walker->GetFirstChildElement(el, &child)) || !child) return nullptr;
  while (child) {
    IUIAutomationElement* found = FindByType(child, want);
    if (found) { child->Release(); return found; }
    IUIAutomationElement* next = nullptr;
    g_walker->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
  return nullptr;
}

static int IntProp(IUIAutomationElement* el, PROPERTYID pid) {
  VARIANT v; VariantInit(&v);
  int out = -1;
  if (SUCCEEDED(el->GetCurrentPropertyValue(pid, &v)) && v.vt == VT_I4)
    out = v.lVal;
  VariantClear(&v);
  return out;
}

// Print an element's control type + name; recurse to show structure (marker/text).
static void Dump(IUIAutomationElement* el, int depth) {
  CONTROLTYPEID ct = 0; el->get_CurrentControlType(&ct);
  BSTR name = nullptr; el->get_CurrentName(&name);
  wprintf(L"%*s- ctrlType=%d name='%s'", depth * 2, L"", ct, name ? name : L"");
  if (ct == UIA_ListItemControlTypeId)
    wprintf(L" PositionInSet=%d SizeOfSet=%d",
            IntProp(el, UIA_PositionInSetPropertyId),
            IntProp(el, UIA_SizeOfSetPropertyId));
  wprintf(L"\n");
  if (name) SysFreeString(name);
  IUIAutomationElement* child = nullptr;
  if (FAILED(g_walker->GetFirstChildElement(el, &child)) || !child) return;
  while (child) {
    Dump(child, depth + 1);
    IUIAutomationElement* next = nullptr;
    g_walker->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_list <hwnd-decimal>\n"); return 1; }
  HWND hwnd = (HWND)(INT_PTR)_atoi64(argv[1]);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation* uia = nullptr;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&uia))) || !uia) {
    wprintf(L"CoCreateInstance failed\n"); return 2;
  }
  IUIAutomationElement* root = nullptr;
  if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) {
    wprintf(L"ElementFromHandle failed\n"); return 3;
  }
  uia->get_RawViewWalker(&g_walker);
  if (!g_walker) { wprintf(L"no walker\n"); return 4; }

  IUIAutomationElement* list = FindByType(root, UIA_ListControlTypeId);
  if (!list) {
    wprintf(L"NO List found -- field is still flat (list not exposed)\n");
    return 5;
  }
  wprintf(L"Found List (50008). SizeOfSet=%d. Structure:\n",
          IntProp(list, UIA_SizeOfSetPropertyId));
  Dump(list, 0);
  list->Release();

  root->Release(); g_walker->Release(); uia->Release(); CoUninitialize();
  return 0;
}
