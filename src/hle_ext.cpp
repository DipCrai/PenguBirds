// Win32 entry points the games import that SwARc's platform does not
// provide yet: the wide-character variants of calls it has as A, and the
// few process/security ones.
//
// A provider that exists as an A call is reused: its thunk is looked up
// and called with narrowed arguments, so its behaviour stays in one place.

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "runtime/guest_alloc.h"
#include "runtime/log.h"
#include "win32/hle.h"
#include "win32/kernel32_state.h"
#include "win32/win32.h"
#include "x86/hle.h"

namespace {

using recomp::Ctx;
using recomp::g_proc;
using recomp::gaddr;
using recomp::hle::Arg;
using recomp::hle::CopyOut;
using recomp::hle::SetLastError;
namespace win32 = recomp::win32;

// A bump arena for argument marshalling: reserved once, never freed.
gaddr Scratch(uint32_t n) {
  static gaddr cur = 0, end = 0;
  n = (n + 15) & ~15u;
  if (cur + n > end) {
    uint64_t base = g_proc->vm.Reserve(0, 0x10000);
    if (!base || !g_proc->vm.Commit(base, 0x10000, recomp::Prot::RW))
      recomp::FatalF("hle: no scratch memory");
    cur = base;
    end = base + 0x10000;
  }
  gaddr a = cur;
  cur += n;
  return a;
}

// An ANSI copy of a guest wide string in the scratch arena.
gaddr NarrowArg(gaddr w) {
  if (!w) return 0;
  std::string s = win32::Narrow(recomp::GWStr(w));
  gaddr a = Scratch(uint32_t(s.size() + 1));
  recomp::PutStr(a, s);
  return a;
}

// Calls a provider that SwARc already has (by its thunk) and returns its
// EAX.
uint64_t CallA(Ctx& c, const char* dll, const char* name,
               std::initializer_list<uint64_t> args) {
  gaddr h = recomp::hle::ModuleHandle(dll);
  gaddr thunk = recomp::hle::ProcAddress(h, name);
  if (!thunk) recomp::FatalF("hle: %s!%s missing for a forward", dll, name);
  return recomp::CallGuest(c, thunk, args);
}

// Writes a guest wide string with a count, as the W calls do: returns the
// number of WCHARs written without the NUL, or the space needed with it.
uint32_t CopyOutW(gaddr buf, uint32_t cap, const std::string& s) {
  if (s.size() + 1 > cap) return uint32_t(s.size() + 1);
  recomp::PutWStr(buf, s);
  return uint32_t(s.size());
}

}  // namespace

// ---- kernel32 ---------------------------------------------------------------

HLE("kernel32.dll", HeapSetInformation, 4) { return 1; }

HLE("kernel32.dll", EncodePointer, 1) { return Arg(c, 0); }
HLE("kernel32.dll", DecodePointer, 1) { return Arg(c, 0); }

// InterlockedCompareExchange(dest, exchange, comparand): the old value;
// the comparison is of the 32-bit slots.
HLE("kernel32.dll", InterlockedCompareExchange, 3) {
  gaddr p = Arg(c, 0);
  uint32_t exchange = Arg(c, 1), comparand = Arg(c, 2);
  auto* slot = reinterpret_cast<std::atomic<uint32_t>*>(recomp::GPtr(p));
  slot->compare_exchange_strong(comparand, exchange);
  return comparand;
}

HLE("kernel32.dll", SleepEx, 2) {
  uint32_t ms = Arg(c, 0);
  if (ms) usleep(ms * 1000);
  return 0;  // no APC ran
}

HLE("kernel32.dll", GetModuleFileNameW, 3) {
  // The A call fills the scratch, the W one widens into the game's buffer.
  uint32_t size = Arg(c, 2) * 2;
  gaddr tmp = Scratch(size);
  uint64_t n = CallA(c, "kernel32.dll", "GetModuleFileNameA",
                     {Arg(c, 0), tmp, size});
  std::string s = recomp::GStr(tmp);
  return n < 0xFFFFFFFFu ? CopyOutW(Arg(c, 1), Arg(c, 2), s) : 0;
}

HLE("kernel32.dll", GetCurrentDirectoryW, 2) {
  uint32_t size = Arg(c, 0);
  gaddr tmp = Scratch(size * 2 + 2);
  uint64_t n = CallA(c, "kernel32.dll", "GetCurrentDirectoryA", {size * 2, tmp});
  return uint32_t(CopyOutW(Arg(c, 1), size, recomp::GStr(tmp)));
}

HLE("kernel32.dll", GetVersionExW, 1) {
  uint32_t vi = Arg(c, 0), size = recomp::Rd32(vi);
  recomp::Wr32(vi + 4, 5);
  recomp::Wr32(vi + 8, 1);
  recomp::Wr32(vi + 12, 2600);
  recomp::Wr32(vi + 16, 2);  // VER_PLATFORM_WIN32_NT
  std::memset(recomp::GPtr(vi + 20), 0, 256);
  recomp::PutWStr(vi + 20, "Service Pack 3");
  if (size >= 284) {  // OSVERSIONINFOEXW
    recomp::Wr16(vi + 276, 3);
    recomp::Wr16(vi + 278, 0);
    recomp::Wr16(vi + 280, 0x100);  // VER_SUITE_SINGLEUSERTS
    recomp::Wr8(vi + 282, 1);       // VER_NT_WORKSTATION
  }
  return 1;
}

HLE("kernel32.dll", CreateMutexW, 3) {
  return CallA(c, "kernel32.dll", "CreateMutexA",
               {Arg(c, 0), Arg(c, 1), NarrowArg(Arg(c, 2))});
}

HLE("kernel32.dll", GetExitCodeProcess, 2) {
  recomp::Wr32(Arg(c, 1), 259);  // STILL_ACTIVE
  return 1;
}

HLE("kernel32.dll", PeekNamedPipe, 6) {
  SetLastError(win32::kErrorInvalidHandle);
  return 0;
}

HLE("kernel32.dll", CreateProcessW, 10) {
  SetLastError(win32::kErrorFileNotFound);
  return 0;
}

// The CRT's FLS is used as a TLS replacement when no static TLS exists;
// the index spaces are independent, so the TLS slots serve it.
HLE("kernel32.dll", FlsAlloc, 1) {
  return CallA(c, "kernel32.dll", "TlsAlloc", {});
}
HLE("kernel32.dll", FlsFree, 1) {
  return CallA(c, "kernel32.dll", "TlsFree", {Arg(c, 0)});
}
HLE("kernel32.dll", FlsGetValue, 1) {
  return CallA(c, "kernel32.dll", "TlsGetValue", {Arg(c, 0)});
}
HLE("kernel32.dll", FlsSetValue, 2) {
  return CallA(c, "kernel32.dll", "TlsSetValue", {Arg(c, 0), Arg(c, 1)});
}

// FILETIME in 100 ns units since 1601. A monotonic host clock offset by
// the epoch difference is enough: the game only takes differences.
namespace {
uint64_t FileTimeNow() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec)) / 100 + 116444736000000000ull;
}
void PutFileTime(gaddr p, uint64_t t) {
  recomp::Wr32(p, uint32_t(t));
  recomp::Wr32(p + 4, uint32_t(t >> 32));
}
}  // namespace

HLE("kernel32.dll", GetThreadTimes, 5) {
  uint64_t t = FileTimeNow();
  PutFileTime(Arg(c, 1), t);
  PutFileTime(Arg(c, 2), t);
  PutFileTime(Arg(c, 3), 0);
  PutFileTime(Arg(c, 4), 0);
  return 1;
}

HLE("kernel32.dll", GetProcessTimes, 5) {
  uint64_t t = FileTimeNow();
  PutFileTime(Arg(c, 1), t);
  PutFileTime(Arg(c, 2), t);
  PutFileTime(Arg(c, 3), 0);
  PutFileTime(Arg(c, 4), 0);
  return 1;
}

// No console is attached: stdio init takes the "not a console" path.
HLE("kernel32.dll", GetConsoleMode, 2) {
  SetLastError(win32::kErrorInvalidHandle);
  return 0;
}
HLE("kernel32.dll", SetConsoleMode, 2) { return 0; }
HLE("kernel32.dll", GetConsoleCP, 0) { return 1252; }
HLE("kernel32.dll", GetConsoleOutputCP, 0) { return 1252; }
HLE("kernel32.dll", SetConsoleOutputCP, 1) { return 1; }

HLE("kernel32.dll", WriteConsoleW, 5) {
  SetLastError(win32::kErrorInvalidHandle);
  return 0;
}

// ---- winmm ---------------------------------------------------------------

// TIMECAPS: the application timer's resolution on Windows is 1 ms.
HLE("winmm.dll", timeGetDevCaps, 2) {
  gaddr caps = Arg(c, 0);
  recomp::Wr32(caps, 1);        // wPeriodMin
  recomp::Wr32(caps + 4, 1000); // wPeriodMax
  return 0;                     // TIMERR_NOERROR
}

// ---- user32: the wide-character window API -------------------------------
//
// Strings the game creates or reads are UTF-8 inside SwARc, so the W calls
// narrow on the way in and widen on the way out; the class registry and
// the window titles are the same data the A calls use.

namespace {

// A copy of a WNDCLASSEXW/WNDCLASSA sized structure with its two string
// pointers narrowed.
gaddr NarrowClass(gaddr wc, uint32_t size) {
  gaddr a = Scratch(size);
  std::memcpy(recomp::GPtr(a), recomp::GPtr(wc), size);
  for (uint32_t off : {36u, 40u}) {  // lpszMenuName, lpszClassName
    if (off >= size) break;
    recomp::Wr32(a + off, NarrowArg(recomp::Rd32(wc + off)));
  }
  return a;
}

uint32_t LowHandle(uint32_t kind, uint32_t id) { return 0x00010000u | (kind << 16) | (id & 0xFFFF); }

}  // namespace

HLE("user32.dll", RegisterClassExW, 1) {
  return CallA(c, "user32.dll", "RegisterClassExA", {NarrowClass(Arg(c, 0), 48)});
}

HLE("user32.dll", UnregisterClassW, 2) {
  return CallA(c, "user32.dll", "UnregisterClassA",
               {NarrowArg(Arg(c, 0)), Arg(c, 1)});
}

HLE("user32.dll", CreateWindowExW, 12) {
  uint32_t style = Arg(c, 3);
  gaddr menu = Arg(c, 9);
  // A top-level window's lpMenuName is a string; a child's is its id.
  if (!(style & 0x40000000u) && menu) menu = NarrowArg(menu);
  return CallA(c, "user32.dll", "CreateWindowExA",
               {Arg(c, 0), NarrowArg(Arg(c, 1)), NarrowArg(Arg(c, 2)), style,
                Arg(c, 4), Arg(c, 5), Arg(c, 6), Arg(c, 7), Arg(c, 8), menu,
                Arg(c, 10), Arg(c, 11)});
}

HLE("user32.dll", AdjustWindowRectEx, 4) {
  return CallA(c, "user32.dll", "AdjustWindowRect",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2)});
}

HLE("user32.dll", LoadCursorW, 2) { return LowHandle(0, Arg(c, 1)); }
HLE("user32.dll", LoadIconW, 2) { return LowHandle(1, Arg(c, 1)); }

HLE("user32.dll", LoadImageW, 6) {
  // A pseudo handle; the icon is only ever set on a window or shown.
  static uint32_t next = 0x00020000;
  static std::atomic<uint32_t> counter{0};
  next = 0x00020000 + counter.fetch_add(1) * 0x10;
  return next;
}

HLE("user32.dll", FindWindowW, 2) {
  return CallA(c, "user32.dll", "FindWindowA",
               {NarrowArg(Arg(c, 0)), NarrowArg(Arg(c, 1))});
}

HLE("user32.dll", SetWindowTextW, 2) {
  return CallA(c, "user32.dll", "SetWindowTextA",
               {Arg(c, 0), NarrowArg(Arg(c, 1))});
}

HLE("user32.dll", GetWindowLongW, 2) {
  return CallA(c, "user32.dll", "GetWindowLongA", {Arg(c, 0), Arg(c, 1)});
}
HLE("user32.dll", SetWindowLongW, 3) {
  return CallA(c, "user32.dll", "SetWindowLongA",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2)});
}

HLE("user32.dll", GetMessageW, 4) {
  return CallA(c, "user32.dll", "GetMessageA",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3)});
}
HLE("user32.dll", PeekMessageW, 5) {
  return CallA(c, "user32.dll", "PeekMessageA",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3), Arg(c, 4)});
}
HLE("user32.dll", DispatchMessageW, 1) {
  return CallA(c, "user32.dll", "DispatchMessageA", {Arg(c, 0)});
}

// The class's window procedure is one function for A and W; the only
// messages whose payload differs are the text ones, converted here.
HLE("user32.dll", DefWindowProcW, 4) {
  uint32_t hwnd = Arg(c, 0), msg = Arg(c, 1), wp = Arg(c, 2), lp = Arg(c, 3);
  switch (msg) {
    case 0x000C:  // WM_SETTEXT
      return CallA(c, "user32.dll", "DefWindowProcA",
                   {hwnd, msg, wp, NarrowArg(lp)});
    case 0x000D: {  // WM_GETTEXT: wp is the capacity in WCHARs
      gaddr tmp = Scratch(wp * 2 + 2);
      uint64_t n = CallA(c, "user32.dll", "DefWindowProcA", {hwnd, msg, wp, tmp});
      CopyOutW(lp, wp, recomp::GStr(tmp));
      return n;
    }
    case 0x000E:  // WM_GETTEXTLENGTH
      return CallA(c, "user32.dll", "DefWindowProcA", {hwnd, msg, wp, lp});
    default:
      return CallA(c, "user32.dll", "DefWindowProcA", {hwnd, msg, wp, lp});
  }
}

HLE("user32.dll", MonitorFromWindow, 2) { return 1; }
HLE("user32.dll", MonitorFromPoint, 3) { return 1; }

// One monitor covering the screen; MonitorFromWindow/Point return the
// pseudo-handle 1 for it.
HLE("user32.dll", GetMonitorInfoW, 2) {
  gaddr mi = Arg(c, 1);
  if (!mi || recomp::Rd32(mi) < 40) return 0;
  uint32_t w = uint32_t(CallA(c, "user32.dll", "GetSystemMetrics", {0}));
  uint32_t h = uint32_t(CallA(c, "user32.dll", "GetSystemMetrics", {1}));
  for (uint32_t off : {4u, 20u}) {
    recomp::Wr32(mi + off + 0, 0);
    recomp::Wr32(mi + off + 4, 0);
    recomp::Wr32(mi + off + 8, w);
    recomp::Wr32(mi + off + 12, h);
  }
  recomp::Wr32(mi + 36, 1);  // MONITORINFOF_PRIMARY
  return 1;
}

HLE("user32.dll", GetTopWindow, 1) {
  return CallA(c, "user32.dll", "GetForegroundWindow", {});
}

HLE("user32.dll", TrackMouseEvent, 1) { return 1; }

// ---- shell32 -------------------------------------------------------------

HLE("shell32.dll", SHGetFolderPathW, 5) {
  return CallA(c, "shfolder.dll", "SHGetFolderPathW",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3), Arg(c, 4)});
}

HLE("shell32.dll", SHGetFolderPathAndSubDirW, 6) {
  gaddr buf = Scratch(520);
  uint64_t r = CallA(c, "shfolder.dll", "SHGetFolderPathW",
                     {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3), buf});
  if (r) return r;
  std::string p = win32::Narrow(recomp::GWStr(buf));
  std::string sub = win32::Narrow(recomp::GWStr(Arg(c, 4)));
  if (!sub.empty()) {
    if (p.back() != '\\') p += '\\';
    p += sub;
  }
  gaddr wp = Scratch(uint32_t(p.size() * 2 + 4));
  recomp::PutWStr(wp, p);
  CallA(c, "kernel32.dll", "CreateDirectoryW", {wp, 0});
  recomp::PutWStr(Arg(c, 5), p);
  return 0;
}

HLE("shell32.dll", ShellExecuteExW, 1) {
  SetLastError(win32::kErrorFileNotFound);
  return 0;
}

// ---- advapi32: the registry in wide characters ---------------------------

HLE("advapi32.dll", RegOpenKeyExW, 5) {
  return CallA(c, "advapi32.dll", "RegOpenKeyExA",
               {Arg(c, 0), NarrowArg(Arg(c, 1)), Arg(c, 2), Arg(c, 3), Arg(c, 4)});
}

HLE("advapi32.dll", RegQueryValueExW, 6) {
  uint32_t hkey = Arg(c, 0), value = NarrowArg(Arg(c, 1)), reserved = Arg(c, 2);
  uint32_t type = Arg(c, 3), data = Arg(c, 4), size = Arg(c, 5);
  uint32_t cap = size ? recomp::Rd32(size) : 0, atype = type ? recomp::Rd32(type) : 0;
  gaddr tmp = Scratch(cap ? cap : 4);
  gaddr tsize = Scratch(4), ttype = Scratch(4);
  recomp::Wr32(tsize, cap);
  if (type) recomp::Wr32(ttype, atype);
  uint64_t r = CallA(c, "advapi32.dll", "RegQueryValueExA",
                     {hkey, value, reserved, ttype, tmp, tsize});
  uint32_t got = recomp::Rd32(tsize), t = recomp::Rd32(ttype);
  if (size) recomp::Wr32(size, got);
  if (type) recomp::Wr32(type, t);
  if (r) return r;
  if ((t == 1 || t == 2) && data) {  // REG_SZ, REG_EXPAND_SZ
    std::string s = recomp::GStr(tmp);
    uint32_t need = uint32_t(s.size() + 1) * 2;
    if (size) recomp::Wr32(size, need);
    if (cap >= need) {
      recomp::PutWStr(data, s);
      if (size) recomp::Wr32(size, need - 2);
    } else {
      SetLastError(win32::kErrorInsufficientBuffer);
      return 234;
    }
    return 0;
  }
  if (data && cap) std::memcpy(recomp::GPtr(data), recomp::GPtr(tmp), cap);
  return 0;
}

// ---- rpcrt4 --------------------------------------------------------------

HLE("rpcrt4.dll", UuidCreate, 1) {
  static std::atomic<uint32_t> counter{1};
  gaddr g = Arg(c, 0);
  uint32_t n = counter.fetch_add(1);
  recomp::Wr32(g + 0, n);
  recomp::Wr32(g + 4, 0x4F1F0000u | (n & 0xFFFF));
  recomp::Wr32(g + 8, uint32_t(0x4000 | (n & 0x3FFF)));
  recomp::Wr32(g + 12, 0x9A000000u | (n & 0xFFFFFF));
  return 0;  // RPC_S_OK
}

HLE("rpcrt4.dll", UuidToStringA, 2) {
  gaddr g = Arg(c, 0), out = Arg(c, 1);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%08X-%04X-%04X-%04X-%04X%08X",
                recomp::Rd32(g), recomp::Rd16(g + 4), recomp::Rd16(g + 6),
                recomp::Rd16(g + 8), recomp::Rd16(g + 10), recomp::Rd32(g + 12));
  gaddr s = recomp::HeapAllocProcess(sizeof buf, true);
  recomp::PutStr(s, buf);
  recomp::Wr32(out, s);
  return 0;
}

HLE("rpcrt4.dll", RpcStringFreeA, 1) {
  gaddr p = recomp::Rd32(Arg(c, 0));
  if (p) recomp::HeapFreeProcess(p);
  return 0;
}

// ---- iphlpapi ------------------------------------------------------------

// ERROR_NO_DATA: no adapter information for the game to read.
HLE("iphlpapi.dll", GetAdaptersInfo, 2) { return 111; }

// ---- winhttp -------------------------------------------------------------

HLE("winhttp.dll", WinHttpOpen, 5) { return 0; }
HLE("winhttp.dll", WinHttpCloseHandle, 1) { return 1; }
HLE("winhttp.dll", WinHttpGetIEProxyConfigForCurrentUser, 1) { return 0; }
HLE("winhttp.dll", WinHttpGetProxyForUrl, 5) { return 0; }

// ---- sw2 additions -------------------------------------------------------

HLE("opengl32.dll", glLineWidth, 1) { return 0; }
HLE("opengl32.dll", glGetTexImage, 5) {
  recomp::WarnF("glGetTexImage: stubbed\n");
  return 0;
}

HLE("shell32.dll", DragAcceptFiles, 2) { return 0; }
HLE("shell32.dll", DragQueryFileW, 4) { return 0; }
HLE("shell32.dll", DragQueryPoint, 2) { return 0; }
HLE("shell32.dll", DragFinish, 1) { return 0; }

HLE("user32.dll", SystemParametersInfoW, 4) {
  return CallA(c, "user32.dll", "SystemParametersInfoA",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3)});
}
HLE("user32.dll", SendMessageW, 4) {
  return CallA(c, "user32.dll", "SendMessageA",
               {Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3)});
}
HLE("user32.dll", GetDlgItem, 2) { return 0; }
HLE("user32.dll", GetDlgItemTextA, 4) { return 0; }
HLE("user32.dll", SendDlgItemMessageA, 5) { return 0; }
HLE("user32.dll", DialogBoxIndirectParamW, 5) { return uint32_t(-1); }
HLE("user32.dll", EndDialog, 2) { return 1; }

HLE("ole32.dll", CoInitializeSecurity, 9) { return 0; }

HLE_NAMED("oleaut32.dll", "#7", SysStringLen, 1) {
  uint32_t b = Arg(c, 0);
  return b ? recomp::Rd32(b - 4) / 2 : 0;
}
HLE_NAMED("oleaut32.dll", "#8", VariantInit, 1) {
  uint32_t v = Arg(c, 0);
  if (v) std::memset(recomp::GPtr(v), 0, 16);
  return 0;
}
HLE_NAMED("oleaut32.dll", "#9", VariantClear, 1) {
  uint32_t v = Arg(c, 0);
  if (v) std::memset(recomp::GPtr(v), 0, 16);
  return 0;
}
HLE_NAMED("oleaut32.dll", "#12", VariantChangeType, 4) {
  return 0x80020005u;  // DISP_E_TYPEMISMATCH
}
HLE_NAMED("oleaut32.dll", "#150", SysAllocStringByteLen, 2) {
  uint32_t src = Arg(c, 0), len = Arg(c, 1);
  uint32_t blk = recomp::HeapAllocProcess(4 + len + 2, true);
  recomp::Wr32(blk, len);
  if (src && len) std::memcpy(recomp::GPtr(blk + 4), recomp::GPtr(src), len);
  return blk + 4;
}
