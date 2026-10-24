#include <WeaselPerformance.h>

weasel::performance::Probe* ProcessProbeFromPeer() noexcept {
  return &weasel::performance::GetProcessProbe();
}

void RecordProcessProbeFromPeer() noexcept {
  weasel::performance::Scope scope(
      weasel::performance::GetProcessProbe(),
      weasel::performance::Stage::ContextResponse);
}
