// Native replacements for the MSVC CRT's SEH helpers (see
// manifest/msvcr100.manifest). A finished __except handler returns through
// __SEH_epilog4 to the function's own caller; that is a non-local return
// when the handler was reached through a nested exception dispatch, so the
// epilog restores the frame and throws GuestReturn. The call site that
// called the function catches it and continues as after a normal return.

#include "x86/lifted.h"

namespace recomp::gen {

void f_78AB0995(::recomp::Ctx& c) {
  uint8_t* const B = ::recomp::g_guest_base;
  const uint32_t ebp = c.ebp;
  // fs:[0] = the registration before the prolog's own.
  ::recomp::lift::Wr<uint32_t>(B, uint32_t(c.fs_base),
                               ::recomp::lift::Rd<uint32_t>(B, ebp - 0x10));
  // The call to this helper pushed its return address at saved_sp-4, so the
  // saved stack is [ret][cookie][edi][esi][ebx]...
  const uint32_t sp = ::recomp::lift::Rd<uint32_t>(B, ebp - 0x18);
  const uint32_t edi = ::recomp::lift::Rd<uint32_t>(B, sp + 4);
  const uint32_t esi = ::recomp::lift::Rd<uint32_t>(B, sp + 8);
  const uint32_t ebx = ::recomp::lift::Rd<uint32_t>(B, sp + 12);
  const uint32_t new_ebp = ::recomp::lift::Rd<uint32_t>(B, ebp);
  const uint32_t target = ::recomp::lift::Rd<uint32_t>(B, ebp + 4);
  throw ::recomp::GuestReturn{target, ebp + 8u, new_ebp, ebx, esi, edi, c.eax};
}

}  // namespace recomp::gen
