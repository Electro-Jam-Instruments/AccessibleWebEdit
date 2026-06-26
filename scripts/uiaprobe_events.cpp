// Native UIA client that SUBSCRIBES to events (the provider->client direction of
// the bidirectional flow). Attaches to the host by HWND, registers handlers for
// focus, Text-changed, Text-selection-changed, and Value property-changed, then
// waits and reports which events actually fired. Same native IUIAutomation /
// CUIAutomation8 API NVDA uses. Usage: uiaprobe_events.exe <hwnd-decimal>
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <atomic>
#include <cstdio>

static std::atomic<int> g_focus{0}, g_textchg{0}, g_selchg{0}, g_valchg{0};

class EventHandler : public IUIAutomationEventHandler,
                     public IUIAutomationFocusChangedEventHandler,
                     public IUIAutomationPropertyChangedEventHandler {
  LONG ref_ = 1;
 public:
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG r = InterlockedDecrement(&ref_); if (!r) delete this; return r;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IUIAutomationEventHandler))
      *ppv = static_cast<IUIAutomationEventHandler*>(this);
    else if (riid == __uuidof(IUIAutomationFocusChangedEventHandler))
      *ppv = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
    else if (riid == __uuidof(IUIAutomationPropertyChangedEventHandler))
      *ppv = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef(); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID eventId) override {
    if (eventId == UIA_Text_TextChangedEventId) { g_textchg++; wprintf(L"[event] Text_TextChanged\n"); }
    else if (eventId == UIA_Text_TextSelectionChangedEventId) { g_selchg++; wprintf(L"[event] Text_TextSelectionChanged\n"); }
    else wprintf(L"[event] automation eventId=%d\n", eventId);
    fflush(stdout); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
    CONTROLTYPEID ct = 0; BSTR nm = nullptr;
    if (sender) { sender->get_CurrentControlType(&ct); sender->get_CurrentName(&nm); }
    g_focus++; wprintf(L"[event] FocusChanged -> ctrlType=%d name='%s'\n", ct, nm ? nm : L"");
    if (nm) SysFreeString(nm); fflush(stdout); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID propertyId, VARIANT newValue) override {
    if (propertyId == UIA_ValueValuePropertyId) {
      g_valchg++; wprintf(L"[event] Value changed -> '%s'\n",
                          (newValue.vt == VT_BSTR && newValue.bstrVal) ? newValue.bstrVal : L"");
    } else wprintf(L"[event] property %d changed\n", propertyId);
    fflush(stdout); return S_OK;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { wprintf(L"usage: uiaprobe_events <hwnd-decimal>\n"); return 1; }
  HWND hwnd = (HWND)(INT_PTR)_atoi64(argv[1]);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation* uia = nullptr;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) || !uia) {
    wprintf(L"CoCreateInstance(CUIAutomation8) failed\n"); return 2;
  }
  IUIAutomationElement* root = nullptr;
  if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) { wprintf(L"ElementFromHandle failed\n"); return 3; }

  EventHandler* h = new EventHandler();
  uia->AddFocusChangedEventHandler(nullptr, h);
  uia->AddAutomationEventHandler(UIA_Text_TextChangedEventId, root, TreeScope_Subtree, nullptr, h);
  uia->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, root, TreeScope_Subtree, nullptr, h);
  PROPERTYID props[1] = { UIA_ValueValuePropertyId };
  uia->AddPropertyChangedEventHandlerNativeArray(root, TreeScope_Subtree, nullptr, h, props, 1);

  wprintf(L"subscribed; waiting 20s for host events (focus ~8s, edit ~12s)...\n"); fflush(stdout);
  DWORD start = GetTickCount();
  while (GetTickCount() - start < 20000) {
    MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    Sleep(50);
  }
  wprintf(L"=== summary: focus=%d textChanged=%d selChanged=%d valueChanged=%d ===\n",
          g_focus.load(), g_textchg.load(), g_selchg.load(), g_valchg.load());
  uia->RemoveAllEventHandlers();
  root->Release(); uia->Release(); CoUninitialize();
  return 0;
}
