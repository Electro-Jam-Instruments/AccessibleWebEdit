// Native UIA client that verifies CELL SELECTION (Phase 4.2): finds the grid (the
// element exposing the Grid pattern), reads its Selection pattern
// (GetCurrentSelection -> the selected cells), and checks a cell's SelectionItem
// IsSelected. This is what NVDA reads as "selected". Usage:
//   uiaprobe_selection.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

static IUIAutomationTreeWalker* g_walker = nullptr;

// Walk the subtree and report every element whose SelectionItem.IsSelected is
// TRUE -- this is the CELL-LEVEL selection NVDA reads per cell.
static int g_sel_cells = 0;
static void WalkSelected(IUIAutomationElement* el) {
  VARIANT v; VariantInit(&v);
  if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_SelectionItemIsSelectedPropertyId, &v))
      && v.vt == VT_BOOL && v.boolVal) {
    BSTR name = nullptr; el->get_CurrentName(&name);
    CONTROLTYPEID ct = 0; el->get_CurrentControlType(&ct);
    wprintf(L"  SELECTED cell: name='%s' controlType=%d\n", name ? name : L"", ct);
    if (name) SysFreeString(name);
    ++g_sel_cells;
  }
  VariantClear(&v);
  IUIAutomationElement* child = nullptr;
  if (FAILED(g_walker->GetFirstChildElement(el, &child)) || !child) return;
  while (child) {
    WalkSelected(child);
    IUIAutomationElement* next = nullptr;
    g_walker->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
}

// Find the first element that exposes the Grid pattern (our table -> kGrid).
static IUIAutomationElement* FindGrid(IUIAutomationElement* el) {
  IUIAutomationGridPattern* gp = nullptr;
  if (SUCCEEDED(el->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&gp))) && gp) {
    gp->Release(); el->AddRef(); return el;
  }
  IUIAutomationElement* child = nullptr;
  if (FAILED(g_walker->GetFirstChildElement(el, &child)) || !child) return nullptr;
  while (child) {
    IUIAutomationElement* found = FindGrid(child);
    if (found) { child->Release(); return found; }
    IUIAutomationElement* next = nullptr;
    g_walker->GetNextSiblingElement(child, &next);
    child->Release(); child = next;
  }
  return nullptr;
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_selection <hwnd-decimal>\n"); return 1; }
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

  IUIAutomationElement* grid = FindGrid(root);
  if (!grid) { wprintf(L"NO grid (no Grid pattern) found\n"); return 5; }
  CONTROLTYPEID gct = 0; grid->get_CurrentControlType(&gct);
  wprintf(L"Found grid (controlType=%d).\n", gct);

  IUIAutomationSelectionPattern* sel = nullptr;
  if (SUCCEEDED(grid->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&sel))) && sel) {
    BOOL multi = FALSE; sel->get_CurrentCanSelectMultiple(&multi);
    wprintf(L"Selection pattern present. CanSelectMultiple=%d\n", multi);
    IUIAutomationElementArray* arr = nullptr;
    if (SUCCEEDED(sel->GetCurrentSelection(&arr)) && arr) {
      int n = 0; arr->get_Length(&n);
      wprintf(L"GetCurrentSelection -> %d selected cell(s):\n", n);
      for (int i = 0; i < n; ++i) {
        IUIAutomationElement* c = nullptr;
        if (SUCCEEDED(arr->GetElement(i, &c)) && c) {
          BSTR name = nullptr; c->get_CurrentName(&name);
          BOOL is_sel = FALSE;
          IUIAutomationSelectionItemPattern* sip = nullptr;
          if (SUCCEEDED(c->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&sip))) && sip) {
            sip->get_CurrentIsSelected(&is_sel); sip->Release();
          }
          wprintf(L"  selected[%d] name='%s' IsSelected=%d\n", i,
                  name ? name : L"", is_sel);
          if (name) SysFreeString(name);
          c->Release();
        }
      }
      arr->Release();
    } else {
      wprintf(L"GetCurrentSelection failed\n");
    }
    sel->Release();
  } else {
    wprintf(L"grid has NO Selection pattern (not a selection container)\n");
  }
  // Cell-level selection (what NVDA reads per cell): walk for IsSelected cells.
  wprintf(L"Cell-level SelectionItem.IsSelected walk:\n");
  WalkSelected(grid);
  wprintf(L"  -> %d cell(s) report IsSelected=TRUE\n", g_sel_cells);
  grid->Release();
  root->Release(); g_walker->Release(); uia->Release(); CoUninitialize();
  return 0;
}
