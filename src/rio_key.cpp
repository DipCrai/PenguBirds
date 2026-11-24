// Rio's old activator validates the key as four groups of four characters
// before enabling its button. The legacy server accepted any key, so this
// replacement returns true for every input; the actual /drm/consumeKey
// request is answered by the runtime's in-process activation responder.

#include "x86/lifted.h"

namespace recomp::gen {

void f_004BB3B0(::recomp::Ctx& c) {
  c.eax = 1;   // al = true
  c.esp += 4;  // pop the return address (cdecl)
}

}  // namespace recomp::gen
