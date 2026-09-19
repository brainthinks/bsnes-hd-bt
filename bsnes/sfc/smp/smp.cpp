#include <sfc/sfc.hpp>
//For RamTrace::AudioSnapshotBytes: this file fills that buffer and is the one
//place that knows what goes in it, so it should also be the one place that
//cannot disagree with the recorder about how long it is.
#include <emulator/ramtrace.hpp>

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
  //The one place an instruction begins. Everything that spends a cycle goes
  //through wait()/waitIdle() and so through stepTimers(), which is where
  //traceClocks moves; so traceClocks - traceInstructionClocks is exactly what
  //the instruction now running has spent, with no table of opcode lengths and
  //nothing assumed. For BSNES_TRACE_APU; inert otherwise.
  traceInstructionClocks = traceClocks;
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
  //The whole buffer, not the first thirty-two bytes. The caller's array is on
  //the stack and the pad bytes at 31, 46 and 47 were never assigned, so every
  //recording before this carried whatever happened to be there. Nothing reads
  //them, but a recording is supposed to be a function of the run and those
  //three bytes were not.
  for(uint n : range(RamTrace::AudioSnapshotBytes)) out[n] = 0;
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
  //How far into the instruction it is executing this snapshot fell, in the
  //same cycles the count above is in, so that `cycles - phase` is the cycle at
  //which that instruction began. Cycles *spent* and not cycles *remaining*:
  //spent is the processor's own bookkeeping and needs nothing but a
  //subtraction, whereas remaining would need the instruction's total length,
  //which this emulator never holds -- an SPC700 instruction's cost here is
  //however many times its implementation happens to call wait(), and it has
  //not finished calling them.
  //
  //Clamped at 255 rather than wrapped. Nothing reaches it in practice -- the
  //longest instruction on this processor is eight cycles, twenty at the
  //glitched wait-state dividers -- but a snapshot taken before the first
  //instruction of a run would otherwise read the whole clock modulo 256, and a
  //saturated byte says "do not believe me" where a wrapped one lies.
  uint64 spent = (traceClocks >> 1) - (traceInstructionClocks >> 1);
  out[48] = spent > 255 ? 255 : spent;
  //The clock the three prescaler bytes above threw away: bit 0 is timer 0's,
  //bit 1 timer 1's, bit 2 timer 2's.
  //
  //Each timer's divider is stage0, a counter of *clocks* running modulo this
  //timer's Frequency (128 for timers 0 and 1, 16 for timer 2), with stage1 a
  //toggle above it -- so the whole phase is `stage1 * Frequency + stage0`
  //clocks. Two clocks are one of this processor's cycles and the trace carries
  //the phase in cycles, which is the unit a reimplementation counts in, so
  //out[22..24] halve it and `stage0 & 1` goes nowhere. That is half a cycle,
  //and it is not noise: the snapshot is always taken inside a mailbox access
  //(see out[48]), so the instant is a fixed point of that access and the
  //dropped clock is the same clock every frame rather than a coin toss.
  //Without this byte a reimplementation restoring the divider is between
  //nought and half a cycle behind the machine for the rest of the run, and the
  //only way to ask what the other half said was to hand the dividers a cycle
  //and see whether the score went up -- which is fitting a constant, not
  //reading a record.
  //
  //Set means the machine's divider stood on the odd clock: half a cycle
  //further on than the halved prescaler beside it says.
  out[49] = (timer0.stage0 & 1) << 0
          | (timer1.stage0 & 1) << 1
          | (timer2.stage0 & 1) << 2;
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
