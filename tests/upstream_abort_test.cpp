// Exercise the actual abort method with a host-terminated composition and a
// candidate UI that is still registered. No COM, Rime sessions or windows.
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr bool TRUE = true;
struct WeaselTSF {
  std::vector<std::string> events;
  struct Client {
    WeaselTSF* owner;
    void ClearComposition() { owner->events.push_back("clear"); }
  } m_client{this};
  struct Candidates {
    WeaselTSF* owner;
    bool registered = true;
    void EndUI() { registered = false; owner->events.push_back("end-ui"); }
    // Destroying an HWND does not end the UIElement registered with TSF.
    void Destroy() { owner->events.push_back("destroy-window"); }
  } candidates{this};
  Candidates* _cand = &candidates;
  bool composing = false, _committed = false, clearArgument = false;
  int _pEditSessionContext = 42;
  bool _IsComposing() { return composing; }
  void _EndComposition(int context, bool clear) {
    if (context != 42) throw std::runtime_error("wrong edit context");
    clearArgument = clear; composing = false; _cand->EndUI();
  }
  void _AbortComposition(bool clear);
};
#include "abort_under_test.inc"

int main() try {
  for (bool active : {false, true}) for (bool clear : {false, true}) {
    WeaselTSF subject;
    subject.composing = active;
    subject._AbortComposition(clear);
    if (!subject._committed || subject.candidates.registered ||
        subject.events != std::vector<std::string>{"clear", "end-ui", "destroy-window"} ||
        (active && subject.clearArgument != clear))
      throw std::runtime_error("abort left a registered UIElement or changed cleanup order");
  }
  std::cout << "PASS candidate abort: active and host-terminated compositions, clear/preserve\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
