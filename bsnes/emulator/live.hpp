#pragma once

//BSNES_LIVE=<path of a Unix socket>: run in lockstep with a program comparing
//itself against this machine (fzero-rs's compare mode). Inert unless set; with
//it unset nothing here runs and no recording changes.
//
//The whole protocol is written up in fzero/LIVE.md. In short: the emulator
//connects to the socket, says hello, runs its first frame (the lead's second
//entry, nothing held) on its own, and from then on runs one frame per 'F'
//request with the pad the request carries, answering each with the SHA-256 of
//every domain as it stood when that frame's record would have been taken
//(System::frameEvent, the same instant BSNES_TRACE_RAM observes) and, from
//version 2, the audio processor's cycle count at that instant (the count a
//BSNES_TRACE_APU record carries), so the other end can take its own audio
//state on the same cycle, and from version 3 the clock count that cycle count
//is the half of, with the timers' dropped clocks and the sound chip's step, so
//the other end can tell a capture taken on an even clock -- half a cycle
//before the count's odd clock -- from one on an odd clock. A 'D'
//request asks for the bytes of the last frame's domains; 'Q' or the socket
//closing ends the run.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <nall/hash/sha256.hpp>
#include <emulator/hdtrace.hpp>

namespace Live {

  //The domains, in the protocol's order; their bit in a mask is their index.
  enum Domain : unsigned { Ram, Vram, Cgram, Oam, Regs, ApuRam, Dsp, Domains };
  static constexpr unsigned Lengths[Domains] = {
    0x20000,  //work RAM, $7E:0000-$7F:FFFF
    0x10000,  //video memory, words low byte first
    512,      //colour memory, colours low byte first
    544,      //the object table as $2138 reads it back
    0x300,    //the register shadow: $2100-$21FF then $4200-$43FF
    0x10000,  //the audio processor's RAM
    128,      //the sound chip's registers
  };
  //Version 2 (2026-09-29): the 'S' reply ends with the audio processor's cycle
  //count at the capture, u64. Version 1 had no such field.
  //Version 3 (2026-09-29): then the clock count itself (u64, two a cycle, the
  //cycle count's low bit restored), the three timers' dropped clocks (u8,
  //BSNES_TRACE_APU's snapshot byte 49) and the sound chip's step (u8, its
  //byte 42), all three at the same capture.
  static constexpr unsigned Version = 3;
  static constexpr unsigned Lead = 2;

  struct State {
    int fd = -1;
    bool failed = false;
    //the frame the last capture was taken on, and which domains it holds
    unsigned capturedFrame = 0;
    unsigned present = 0;
    bool captured = false;
    //the audio processor's cycles since power-on at the capture: traceClocks
    //over two, BSNES_TRACE_APU's own count (bytes 32-39 of its snapshot)
    uint64_t audioCycle = 0;
    //version 3: the clock count the cycle count halves (smp.traceClocks), and
    //the timers' dropped clocks and the chip's step from the same snapshot
    //BSNES_TRACE_APU takes
    uint64_t audioClocks = 0;
    uint8_t dividerClocks = 0;
    uint8_t dspStep = 0;
    uint8_t* bytes[Domains] = {};
  };

  inline auto state() -> State& {
    static State s;
    return s;
  }

  inline auto path() -> const char* { return getenv("BSNES_LIVE"); }

  inline auto enabled() -> bool { return HdTrace::live(); }

  inline auto fail(const char* what) -> void {
    auto& s = state();
    if(!s.failed) fprintf(stderr, "BSNES_LIVE: %s (%s)\n", what, errno ? strerror(errno) : "no error number");
    s.failed = true;
    if(s.fd >= 0) close(s.fd);
    s.fd = -1;
  }

  inline auto sendAll(const void* data, size_t length) -> bool {
    auto& s = state();
    auto p = (const uint8_t*)data;
    while(length) {
      ssize_t n = send(s.fd, p, length, MSG_NOSIGNAL);
      if(n < 0 && errno == EINTR) continue;
      if(n <= 0) { fail("send"); return false; }
      p += n; length -= (size_t)n;
    }
    return true;
  }

  inline auto recvAll(void* data, size_t length) -> bool {
    auto& s = state();
    auto p = (uint8_t*)data;
    while(length) {
      ssize_t n = recv(s.fd, p, length, 0);
      if(n < 0 && errno == EINTR) continue;
      if(n == 0) { errno = 0; fail("the other end closed the socket"); return false; }
      if(n < 0) { fail("recv"); return false; }
      p += n; length -= (size_t)n;
    }
    return true;
  }

  inline auto put32(uint8_t* p, uint32_t v) -> void {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
  }

  //Connect and say hello, once.
  inline auto connectOnce() -> bool {
    auto& s = state();
    if(s.fd >= 0) return true;
    if(s.failed) return false;
    s.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(s.fd < 0) { fail("socket"); return false; }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if(strlen(path()) >= sizeof(address.sun_path)) { errno = ENAMETOOLONG; fail("the socket path is too long"); return false; }
    strcpy(address.sun_path, path());
    if(connect(s.fd, (sockaddr*)&address, sizeof(address)) != 0) { fail("connect"); return false; }
    for(unsigned d = 0; d < Domains; d++) s.bytes[d] = new uint8_t[Lengths[d]]();
    //hello: magic, version, lead, domain count, each domain's length
    uint8_t hello[8 + 4 + 4 + 1 + 4 * Domains] = {'F', 'Z', 'L', 'I', 'V', 'E', 0x1a, 0};
    put32(hello + 8, Version);
    put32(hello + 12, Lead);
    hello[16] = Domains;
    for(unsigned d = 0; d < Domains; d++) put32(hello + 17 + 4 * d, Lengths[d]);
    return sendAll(hello, sizeof(hello));
  }

  //The pad the running frame holds, in the script file's bit order (the
  //gamepad enum's: up down left right b a y x l r select start from bit 0).
  inline auto pad() -> unsigned& { return HdTrace::livePad(); }

  //System::frameEvent: take the frame's state at the instant a record is
  //taken. The arguments are the recorder's own, plus colour memory.
  inline auto capture(const uint8_t* ram, const uint16_t* video, const uint8_t* colours,
                      const uint8_t* objects, const uint8_t* registers,
                      const uint8_t* apu, const uint8_t* dspRegs, bool audioIsHonest,
                      uint64_t audioCycle, uint64_t audioClocks,
                      uint8_t dividerClocks, uint8_t dspStep) -> void {
    auto& s = state();
    if(s.fd < 0) return;
    s.present = 0;
    memcpy(s.bytes[Ram], ram, Lengths[Ram]);
    s.present |= 1 << Ram;
    if(video) {
      for(unsigned n = 0; n < Lengths[Vram] / 2; n++) {
        s.bytes[Vram][n * 2] = video[n] & 0xff;
        s.bytes[Vram][n * 2 + 1] = video[n] >> 8;
      }
      s.present |= 1 << Vram;
    }
    if(colours) { memcpy(s.bytes[Cgram], colours, Lengths[Cgram]); s.present |= 1 << Cgram; }
    if(objects) { memcpy(s.bytes[Oam], objects, Lengths[Oam]); s.present |= 1 << Oam; }
    if(registers) { memcpy(s.bytes[Regs], registers, Lengths[Regs]); s.present |= 1 << Regs; }
    //The audio domains only where the sound chip runs as the hardware does,
    //the recorder's own condition for BSNES_TRACE_APU.
    if(apu && dspRegs && audioIsHonest) {
      memcpy(s.bytes[ApuRam], apu, Lengths[ApuRam]);
      memcpy(s.bytes[Dsp], dspRegs, Lengths[Dsp]);
      s.present |= 1 << ApuRam | 1 << Dsp;
    }
    s.audioCycle = audioCycle;
    s.audioClocks = audioClocks;
    s.dividerClocks = dividerClocks;
    s.dspStep = dspStep;
    s.capturedFrame = HdTrace::frame();
    s.captured = true;
  }

  //Before the frame the counter now names runs: during the lead nothing is
  //held and nothing is asked; after it, answer 'D' requests about the last
  //frame until an 'F' names this one's pad. False ends the run.
  inline auto beforeFrame(unsigned frame) -> bool {
    auto& s = state();
    if(!connectOnce()) return false;
    s.captured = false;
    if(frame < Lead) { pad() = 0; return true; }
    while(true) {
      uint8_t op;
      if(!recvAll(&op, 1)) return false;
      if(op == 'F') {
        uint8_t p[2];
        if(!recvAll(p, 2)) return false;
        pad() = p[0] | p[1] << 8;
        return true;
      }
      if(op == 'D') {
        uint8_t mask;
        if(!recvAll(&mask, 1)) return false;
        mask &= s.present;
        uint8_t head[2] = {'B', mask};
        if(!sendAll(head, 2)) return false;
        for(unsigned d = 0; d < Domains; d++) {
          if(mask >> d & 1 && !sendAll(s.bytes[d], Lengths[d])) return false;
        }
        continue;
      }
      if(op == 'Q') {
        close(s.fd);
        s.fd = -1;
        return false;
      }
      errno = 0;
      fail("an unknown request");
      return false;
    }
  }

  //After the frame has run: its hashes, from the capture frameEvent took.
  inline auto afterFrame(unsigned frame) -> bool {
    auto& s = state();
    if(frame < Lead) return true;
    if(s.fd < 0) return false;
    uint8_t reply[1 + 4 + 1 + 32 * Domains + 8 + 8 + 1 + 1] = {'S'};
    put32(reply + 1, frame);
    //No capture this frame (the frame event never came) is said as no domain
    //at all, and the other end refuses.
    unsigned present = s.captured && s.capturedFrame == frame ? s.present : 0;
    reply[5] = present;
    for(unsigned d = 0; d < Domains; d++) {
      if(!(present >> d & 1)) continue;
      nall::Hash::SHA256 hash({s.bytes[d], Lengths[d]});
      auto digest = hash.output();
      for(unsigned n = 0; n < 32; n++) reply[6 + 32 * d + n] = digest[n];
    }
    //version 2: the audio processor's cycle count at the capture (nought with
    //no capture)
    uint64_t cycle = present ? s.audioCycle : 0;
    for(unsigned n = 0; n < 8; n++) reply[6 + 32 * Domains + n] = cycle >> (n * 8) & 0xff;
    //version 3: the clock count, the dropped clocks and the chip's step
    //(nought with no capture)
    uint64_t clocks = present ? s.audioClocks : 0;
    for(unsigned n = 0; n < 8; n++) reply[14 + 32 * Domains + n] = clocks >> (n * 8) & 0xff;
    reply[22 + 32 * Domains] = present ? s.dividerClocks : 0;
    reply[23 + 32 * Domains] = present ? s.dspStep : 0;
    return sendAll(reply, sizeof(reply));
  }

}
