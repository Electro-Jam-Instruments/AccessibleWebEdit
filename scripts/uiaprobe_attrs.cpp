// Native UIA client that reads rich-text run attributes off the field's
// DocumentRange via ITextRangeProvider::GetAttributeValue. Verifies bold /
// italic / underline surface through UIA. Usage: uiaprobe_attrs.exe <hwnd>
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

static void ReadAttr(IUIAutomationTextRange* r, TEXTATTRIBUTEID id, const wchar_t* name) {
  VARIANT v; VariantInit(&v);
  HRESULT hr = r->GetAttributeValue(id, &v);
  if (FAILED(hr)) { wprintf(L"  %s: GetAttributeValue hr=0x%08x\n", name, (unsigned)hr); return; }
  switch (v.vt) {
    case VT_I4:   wprintf(L"  %s = %d (VT_I4)\n", name, v.lVal); break;
    case VT_BOOL: wprintf(L"  %s = %s (VT_BOOL)\n", name, v.boolVal ? L"TRUE" : L"FALSE"); break;
    case VT_UNKNOWN: wprintf(L"  %s = <mixed/notsupported>\n", name); break;
    default: wprintf(L"  %s = <vt=%d>\n", name, v.vt); break;
  }
  VariantClear(&v);
}

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_attrs <hwnd-decimal>\n"); return 1; }
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
    wprintf(L"no Text pattern\n"); return 5;
  }
  IUIAutomationTextRange* doc = nullptr;
  if (FAILED(tp->get_DocumentRange(&doc)) || !doc) { wprintf(L"no DocumentRange\n"); return 6; }

  BSTR t = nullptr; doc->GetText(-1, &t);
  wprintf(L"run text = '%s'\n", (t && *t) ? t : L"<empty>"); if (t) SysFreeString(t);
  wprintf(L"run attributes:\n");
  ReadAttr(doc, UIA_FontWeightAttributeId, L"FontWeight (bold=700)");
  ReadAttr(doc, UIA_IsItalicAttributeId, L"IsItalic");
  ReadAttr(doc, UIA_UnderlineStyleAttributeId, L"UnderlineStyle (Single=1)");

  doc->Release(); tp->Release(); edit->Release(); if (walker) walker->Release();
  root->Release(); uia->Release(); CoUninitialize();
  return 0;
}
