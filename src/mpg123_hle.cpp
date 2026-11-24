#include <mpg123.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "x86/hle.h"
#include "x86/runtime.h"

namespace {

using recomp::Ctx;
using recomp::GPtr;
using recomp::GStr;
using recomp::Wr32;
using recomp::hle::Arg;

std::mutex g_mu;
uint32_t g_next_handle = 0x71000001u;
std::unordered_map<uint32_t, mpg123_handle*> g_handles;

mpg123_handle* Lookup(uint32_t h) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_handles.find(h);
  return it == g_handles.end() ? nullptr : it->second;
}

}  // namespace

namespace recomp::gen {

// int mpg123_init(void)
void MpgInit(Ctx& c) {
  static std::once_flag once;
  static int init_res = MPG123_OK;
  std::call_once(once, [] { init_res = mpg123_init(); });
  c.eax = uint32_t(init_res);
  c.esp += 4u;
}

// mpg123_handle* mpg123_new(const char* decoder, int* error)
void MpgNew(Ctx& c) {
  uint32_t dec_va = Arg(c, 0);
  uint32_t err_va = Arg(c, 1);
  std::string dec = dec_va ? GStr(dec_va) : std::string();
  int err = MPG123_OK;
  mpg123_handle* mh = mpg123_new(dec_va ? dec.c_str() : nullptr, &err);
  if (err_va) Wr32(err_va, uint32_t(err));
  uint32_t guest_h = 0;
  if (mh) {
    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0);
    std::lock_guard<std::mutex> lock(g_mu);
    guest_h = g_next_handle++;
    g_handles[guest_h] = mh;
  }
  c.eax = guest_h;
  c.esp += 4u;
}

// void mpg123_delete(mpg123_handle* mh)
void MpgDelete(Ctx& c) {
  uint32_t h = Arg(c, 0);
  mpg123_handle* mh = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_handles.find(h);
    if (it != g_handles.end()) {
      mh = it->second;
      g_handles.erase(it);
    }
  }
  if (mh) mpg123_delete(mh);
  c.esp += 4u;
}

// int mpg123_open_feed(mpg123_handle* mh)
void MpgOpenFeed(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  c.eax = uint32_t(mh ? mpg123_open_feed(mh) : MPG123_ERR);
  c.esp += 4u;
}

// int mpg123_feed(mpg123_handle* mh, const unsigned char* in, size_t size)
void MpgFeed(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  uint32_t in_va = Arg(c, 1);
  uint32_t sz = Arg(c, 2);
  int r = mh ? mpg123_feed(mh, in_va ? reinterpret_cast<const unsigned char*>(GPtr(in_va)) : nullptr, sz)
             : MPG123_ERR;
  c.eax = uint32_t(r);
  c.esp += 4u;
}

// int mpg123_decode(mh, in, insize, out, outsize, done)
void MpgDecode(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  uint32_t in_va = Arg(c, 1), in_sz = Arg(c, 2);
  uint32_t out_va = Arg(c, 3), out_sz = Arg(c, 4);
  uint32_t done_va = Arg(c, 5);
  size_t done = 0;
  int r = mh ? mpg123_decode(
                   mh,
                   in_va ? reinterpret_cast<const unsigned char*>(GPtr(in_va)) : nullptr, in_sz,
                   out_va ? reinterpret_cast<unsigned char*>(GPtr(out_va)) : nullptr, out_sz,
                   done_va ? &done : nullptr)
             : MPG123_ERR;
  if (done_va) Wr32(done_va, uint32_t(done));
  c.eax = uint32_t(r);
  c.esp += 4u;
}

// int mpg123_getformat(mh, long* rate, int* channels, int* encoding)
void MpgGetFormat(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  uint32_t rate_va = Arg(c, 1), ch_va = Arg(c, 2), enc_va = Arg(c, 3);
  long rate = 0;
  int ch = 0, enc = 0;
  int r = mh ? mpg123_getformat(mh, &rate, &ch, &enc) : MPG123_ERR;
  if (r == MPG123_OK) {
    if (rate_va) Wr32(rate_va, uint32_t(rate));
    if (ch_va) Wr32(ch_va, uint32_t(ch));
    if (enc_va) Wr32(enc_va, uint32_t(enc));
  }
  c.eax = uint32_t(r);
  c.esp += 4u;
}

// off_t mpg123_tell(mh)
void MpgTell(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  off_t r = mh ? mpg123_tell(mh) : MPG123_ERR;
  c.eax = uint32_t(int32_t(r));
  c.esp += 4u;
}

// off_t mpg123_feedseek(mh, off_t sampleoff, int whence, off_t* input_offset)
void MpgFeedSeek(Ctx& c) {
  mpg123_handle* mh = Lookup(Arg(c, 0));
  int32_t pos = int32_t(Arg(c, 1));
  int whence = int(Arg(c, 2));
  uint32_t off_va = Arg(c, 3);
  off_t in_off = 0;
  off_t r = mh ? mpg123_feedseek(mh, pos, whence, off_va ? &in_off : nullptr) : MPG123_ERR;
  if (off_va) Wr32(off_va, uint32_t(in_off));
  c.eax = uint32_t(int32_t(r));
  c.esp += 4u;
}

#ifdef AB_GAME_SW1
void f_005067C0(Ctx& c) { MpgInit(c); }
void f_00507070(Ctx& c) { MpgNew(c); }
void f_00507010(Ctx& c) { MpgDelete(c); }
void f_00507090(Ctx& c) { MpgOpenFeed(c); }
void f_00506C00(Ctx& c) { MpgFeed(c); }
void f_00506C50(Ctx& c) { MpgDecode(c); }
void f_00506D70(Ctx& c) { MpgGetFormat(c); }
void f_00506DD0(Ctx& c) { MpgTell(c); }
void f_00506E80(Ctx& c) { MpgFeedSeek(c); }
#endif  // AB_GAME_SW1
}  // namespace recomp::gen

#ifdef AB_GAME_SW2
// Angry Birds Star Wars II: the same libmpg123 build, its functions 0x10180
// bytes later in the EXE. The implementations above are shared.
namespace recomp::gen {
void f_00516940(Ctx& c) { MpgInit(c); }  // mpg123_init
void f_005171F0(Ctx& c) { MpgNew(c); }  // mpg123_new
void f_00517190(Ctx& c) { MpgDelete(c); }  // mpg123_delete
void f_00517210(Ctx& c) { MpgOpenFeed(c); }  // mpg123_open_feed
void f_00516D80(Ctx& c) { MpgFeed(c); }  // mpg123_feed
void f_00516DD0(Ctx& c) { MpgDecode(c); }  // mpg123_decode
void f_00516EF0(Ctx& c) { MpgGetFormat(c); }  // mpg123_getformat
void f_00516F50(Ctx& c) { MpgTell(c); }  // mpg123_tell
void f_00517000(Ctx& c) { MpgFeedSeek(c); }  // mpg123_feedseek
}  // namespace recomp::gen
#endif  // AB_GAME_SW2

#ifdef AB_GAME_CLASSIC
// Angry Birds Classic 4.0.0: the same libmpg123 build.
namespace recomp::gen {
void f_004E3F40(Ctx& c) { MpgInit(c); }  // mpg123_init
void f_004E47F0(Ctx& c) { MpgNew(c); }  // mpg123_new
void f_004E4790(Ctx& c) { MpgDelete(c); }  // mpg123_delete
void f_004E4810(Ctx& c) { MpgOpenFeed(c); }  // mpg123_open_feed
void f_004E4380(Ctx& c) { MpgFeed(c); }  // mpg123_feed
void f_004E43D0(Ctx& c) { MpgDecode(c); }  // mpg123_decode
void f_004E44F0(Ctx& c) { MpgGetFormat(c); }  // mpg123_getformat
void f_004E4550(Ctx& c) { MpgTell(c); }  // mpg123_tell
void f_004E4600(Ctx& c) { MpgFeedSeek(c); }  // mpg123_feedseek
}  // namespace recomp::gen
#endif  // AB_GAME_CLASSIC

#ifdef AB_GAME_SEASONS
// Angry Birds Seasons.
namespace recomp::gen {
void f_004E5550(Ctx& c) { MpgInit(c); }  // init
void f_004E5E00(Ctx& c) { MpgNew(c); }  // new
void f_004E5DA0(Ctx& c) { MpgDelete(c); }  // delete
void f_004E5E20(Ctx& c) { MpgOpenFeed(c); }  // open_feed
void f_004E5990(Ctx& c) { MpgFeed(c); }  // feed
void f_004E59E0(Ctx& c) { MpgDecode(c); }  // decode
void f_004E5B00(Ctx& c) { MpgGetFormat(c); }  // getformat
void f_004E5B60(Ctx& c) { MpgTell(c); }  // tell
void f_004E5C10(Ctx& c) { MpgFeedSeek(c); }  // feedseek
}  // namespace recomp::gen
#endif  // AB_GAME_SEASONS

#ifdef AB_GAME_RIO
// Angry Birds Rio.
namespace recomp::gen {
void f_004D5C30(Ctx& c) { MpgInit(c); }  // init
void f_004D64E0(Ctx& c) { MpgNew(c); }  // new
void f_004D6480(Ctx& c) { MpgDelete(c); }  // delete
void f_004D6500(Ctx& c) { MpgOpenFeed(c); }  // open_feed
void f_004D6070(Ctx& c) { MpgFeed(c); }  // feed
void f_004D60C0(Ctx& c) { MpgDecode(c); }  // decode
void f_004D61E0(Ctx& c) { MpgGetFormat(c); }  // getformat
void f_004D6240(Ctx& c) { MpgTell(c); }  // tell
void f_004D62F0(Ctx& c) { MpgFeedSeek(c); }  // feedseek
}  // namespace recomp::gen
#endif  // AB_GAME_RIO

#ifdef AB_GAME_SPACE
// Angry Birds Space.
namespace recomp::gen {
void f_0050D410(Ctx& c) { MpgInit(c); }  // init
void f_0050DCC0(Ctx& c) { MpgNew(c); }  // new
void f_0050DC60(Ctx& c) { MpgDelete(c); }  // delete
void f_0050DCE0(Ctx& c) { MpgOpenFeed(c); }  // open_feed
void f_0050D850(Ctx& c) { MpgFeed(c); }  // feed
void f_0050D8A0(Ctx& c) { MpgDecode(c); }  // decode
void f_0050D9C0(Ctx& c) { MpgGetFormat(c); }  // getformat
void f_0050DA20(Ctx& c) { MpgTell(c); }  // tell
void f_0050DAD0(Ctx& c) { MpgFeedSeek(c); }  // feedseek
}  // namespace recomp::gen
#endif  // AB_GAME_SPACE
