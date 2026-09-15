#include <sfc/sfc.hpp>

namespace SuperFamicom {

SMP smp;
#include "memory.cpp"
#include "io.cpp"
#include "timing.cpp"
#include "serialization.cpp"

auto SMP::synchronizeCPU() -> void {
  if(clock >= 0) scheduler.resume(cpu.thread);
}

auto SMP::synchronizeDSP() -> void {
  while(dsp.clock < 0) dsp.main();
}

auto SMP::Enter() -> void {
  while(true) {
    scheduler.synchronize();
    smp.main();
  }
}

auto SMP::main() -> void {
  if(r.wait) return instructionWait();
  if(r.stop) return instructionStop();
  instruction();
}

auto SMP::load() -> bool {
  if(auto fp = platform->open(ID::System, "ipl.rom", File::Read, File::Required)) {
    fp->read(iplrom, 64);
    return true;
  }
  return false;
}

auto SMP::snapshotForTrace(uint8* out) -> void {
  for(uint n : range(32)) out[n] = 0;
  out[0] = r.pc.byte.l;
  out[1] = r.pc.byte.h;
  out[2] = r.ya.byte.l;
  out[3] = r.x;
  out[4] = r.ya.byte.h;
  out[5] = r.s;
  out[6] = r.p;
  //$F1 is write-only, so the bits it set are read back out of where they
  //ended up rather than out of a copy of what was written.
  out[7] = (timer0.enable << 0) | (timer1.enable << 1) | (timer2.enable << 2)
         | (io.iplromEnable << 7);
  out[8] = io.dspAddr;
  out[9] = io.apu0;  out[10] = io.apu1; out[11] = io.apu2; out[12] = io.apu3;
  out[13] = io.cpu0; out[14] = io.cpu1; out[15] = io.cpu2; out[16] = io.cpu3;
  out[17] = io.aux4; out[18] = io.aux5;
  out[19] = timer0.target; out[20] = timer1.target; out[21] = timer2.target;
  //The prescaler here counts clocks and a toggle, and two clocks are one of
  //this processor's cycles; the trace carries it in cycles, which is the unit
  //a reimplementation counts in.
  out[22] = (timer0.stage1 * 64) + (timer0.stage0 >> 1);
  out[23] = (timer1.stage1 * 64) + (timer1.stage0 >> 1);
  out[24] = (timer2.stage1 *  8) + (timer2.stage0 >> 1);
  out[25] = timer0.stage2; out[26] = timer1.stage2; out[27] = timer2.stage2;
  out[28] = timer0.stage3; out[29] = timer1.stage3; out[30] = timer2.stage3;
  uint64 cycles = traceClocks >> 1;
  for(uint n : range(8)) out[32 + n] = cycles >> (n * 8) & 0xff;
  uint echoOffset = dsp.echoOffsetForTrace();
  out[40] = echoOffset & 0xff;
  out[41] = echoOffset >> 8 & 0xff;
  out[42] = dsp.phaseForTrace() & 0x1f;
  uint rateCounter = dsp.rateCounterForTrace();
  out[43] = rateCounter & 0xff;
  out[44] = rateCounter >> 8 & 0xff;
  out[45] = dsp.alternateForTrace() & 1;
}

auto SMP::power(bool reset) -> void {
  SPC700::power();
  create(Enter, system.apuFrequency() / 12.0);

  r.pc.byte.l = iplrom[62];
  r.pc.byte.h = iplrom[63];

  io = {};
  timer0 = {};
  timer1 = {};
  timer2 = {};
}

}
