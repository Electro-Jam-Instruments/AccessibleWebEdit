// Native UIA client that verifies HEADING semantics (Phase 3.3): walks the tree
// and reports any element exposing a heading -- its Level (UIA_LevelPropertyId),
// HeadingLevel property, localized control type, and name. This is what NVDA
// reads as "heading level N". Usage: uiaprobe_heading.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

static IUIAutomationTreeWalker* g_walker = nullptr;

static int IntProp(IUIAutomationElement* el, PROPERTYID pid) {
  VARIANT v; VariantInit(&v);
  int out = 0;
  if (SUCCEEDED(el->GetCurrentPropertyValue(pid, &v)) && v.vt == VT_I4)
    out = v.lVal;
  VariantClear(&v);
  return out;
}

static int g_found = 0;

static void Walk(IUIAutomationElement* el) {
  // UIA_LevelPropertyId carries the hierarchical level (our heading sets 1).
  const int level = IntProp(el, UIA_LevelPropertyId);
  const int heading_level = IntProp(el, UIA_HeadingLevelPropertyId);
  if (level > 0 || heading_level > 80050 /* HeadingLevel_None */) {
    BSTR name = nullptr; el->get_CurrentName(&name);
    BSTR lct = nullptr; el->get_CurrentLocalizedControlType(&lct);
    wprintf(L"HEADING: name='%s' localizedType='%s' Level=%d HeadingLevelProp=%d\n",
            name ? name : L"", lct ? lct : L"", level, heading_level);
    if (name) SysFreeString(name);
    if (lct) SysFreeString(lct);
    ++g_found;
  }
  IUIAutomationElement* child = nullptr;
  if (FAILED(g_walker->GetFirstChildElement(el, &child)) || !child) return;
  while (child) {
    Walk(child);
    IUIAutomationElement* next = nullptr;
    g_walker->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_heading <hwnd-decimal>\n"); return 1; }
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
  Walk(root);
  if (!g_found)
    wprintf(L"NO heading exposed\n");
  root->Release(); g_walker->Release(); uia->Release(); CoUninitialize();
  return 0;
}
