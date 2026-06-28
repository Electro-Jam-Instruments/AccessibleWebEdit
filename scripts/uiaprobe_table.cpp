// Native UIA client that verifies TABLE structure (Phase 4.1): finds the Table,
// reads the Grid pattern (RowCount/ColumnCount), and pulls each cell via
// GetItem(r,c) -> name + GridItem Row/Column. This is what NVDA reads as "table",
// row/column, cell content. Usage: uiaprobe_table.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <cstdio>

static IUIAutomationTreeWalker* g_walker = nullptr;

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

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_table <hwnd-decimal>\n"); return 1; }
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

  IUIAutomationElement* table = FindByType(root, UIA_TableControlTypeId);
  if (!table) {
    wprintf(L"NO Table (50036) found\n");
    return 5;
  }
  wprintf(L"Found Table (50036).\n");

  IUIAutomationGridPattern* grid = nullptr;
  if (SUCCEEDED(table->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&grid))) && grid) {
    int rows = 0, cols = 0;
    grid->get_CurrentRowCount(&rows);
    grid->get_CurrentColumnCount(&cols);
    wprintf(L"Grid pattern: RowCount=%d ColumnCount=%d\n", rows, cols);
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        IUIAutomationElement* cell = nullptr;
        if (SUCCEEDED(grid->GetItem(r, c, &cell)) && cell) {
          BSTR name = nullptr; cell->get_CurrentName(&name);
          CONTROLTYPEID ct = 0; cell->get_CurrentControlType(&ct);
          int gr = -1, gc = -1;
          IUIAutomationGridItemPattern* gi = nullptr;
          if (SUCCEEDED(cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_PPV_ARGS(&gi))) && gi) {
            gi->get_CurrentRow(&gr); gi->get_CurrentColumn(&gc); gi->Release();
          }
          wprintf(L"  cell(%d,%d) ctrlType=%d name='%s' GridItem[row=%d col=%d]\n",
                  r, c, ct, name ? name : L"", gr, gc);
          if (name) SysFreeString(name);
          cell->Release();
        }
      }
    }
    grid->Release();
  } else {
    wprintf(L"Table has NO Grid pattern\n");
  }
  table->Release();
  root->Release(); g_walker->Release(); uia->Release(); CoUninitialize();
  return 0;
}
