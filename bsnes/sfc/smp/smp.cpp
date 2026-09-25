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
  //And where it is being fetched from. r.pc has not moved yet here: the
  //opcode is read by instruction() below, and every operand after it.
  traceInstructionPC = r.pc.w;
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

  //And eight forty-byte blocks, one a voice, carrying what that voice is in
  //the middle of (trace v12). Everything above this line is the audio
  //processor; everything below it is the sound chip's own insides, and none of
  //it is readable through a register -- which is why a reimplementation handed
  //RAM and all 128 registers still has to start its voices from silence.
  //
  //The layout is fixed and the same for every voice, so a reader indexes it
  //rather than parsing it: block = 50 + voice * 40.
  //
  //  +0  uint16  envelope level, 0..0x7FF. The chip carries eleven bits of it
  //              and the register a driver can read ($x8) shows the top seven,
  //              so a level restored from the register alone is up to fifteen
  //              short and every step after it is taken from the wrong place.
  //  +2  uint8   which of the four phases the envelope is in: 0 release,
  //              1 attack, 2 decay, 3 sustain. No register carries it at all.
  //              The phase decides which rate the next step uses and whether
  //              the step is added or subtracted.
  //  +3  uint8   how many samples of the key-on delay are left, 0..5. The chip
  //              does not begin a voice on the sample the driver asks for it;
  //              it counts down, fills the decode buffer while counting, and
  //              sounds afterwards. A restored voice that ignores this begins
  //              its note up to five samples from where the machine begins it.
  //  +4  uint16  the envelope the ADSR/GAIN arithmetic carries before it is
  //              clamped to 0..0x7FF. The two-slope GAIN mode reads this
  //              rather than the clamped level to decide which of its two
  //              slopes it is on, so a chip restored without it takes the
  //              wrong slope for as long as that mode is selected.
  //  +6  uint16  where in audio RAM the BRR block being decoded starts.
  //  +8  uint8   how far into that block the decoder has got, in bytes. A
  //              block is nine bytes -- a header and sixteen four-bit
  //              differences -- and four samples come out of two bytes, so
  //              this runs 1, 3, 5, 7 and the block is left at 9.
  //  +9  uint8   that block's header byte: the shift in the top four bits,
  //              the filter in bits 2-3, and whether the block loops or ends
  //              in the bottom two. It is readable from audio RAM at the
  //              address at +6, and is carried beside it so that a reader can
  //              check the block walk against the RAM the record already
  //              holds rather than re-deriving where the walk had got to.
  // +10  uint8   where in the twelve-sample decode buffer the next four go,
  //              0, 4 or 8. It is also where the interpolation window starts,
  //              because the group the position is inside is the one the
  //              decoder will overwrite last.
  // +11  uint8   nought. Reserved, and written rather than left, because a
  //              recording is meant to be a function of the run.
  // +12  uint16  where the voice is between two decoded samples. The low
  //              twelve bits are the fraction the interpolation curve is read
  //              at and the bits above say which of the buffer's samples the
  //              four taps start on, counting from +10. This is the one field
  //              that says *where in its own waveform* a sounding note is;
  //              without it a restored voice is anywhere up to four samples
  //              from where the machine has it and stays there.
  // +14  2 bytes nought. Reserved.
  // +16  12 x int16  the twelve decoded samples themselves, oldest group
  //              first in buffer order (not in playing order -- +10 and +12
  //              between them say where the window starts). The chip decodes
  //              four at a time and draws its interpolation curve through four
  //              consecutive ones, so a voice restored with an empty buffer
  //              plays silence into the middle of a note. The last two before
  //              the write position are also the block filter's memory, which
  //              is why they are not kept separately.
  //Taken at the boundary of the sample the chip is in the middle of, not here
  //(trace v13). See DSP::captureVoicesForTrace: this chip walks its eight
  //voices three steps apart, so the eight blocks read off it *here* would be
  //eight instants -- some voices on the near side of their own step and some
  //on the far side -- and a reimplementation can only stand them at one
  //boundary. It is handed the boundary, and makes the sample in flight for
  //itself from there, which is what the machine is doing at this instant too.
  //With one exception, which the record itself found: a snapshot taken with
  //the chip *on* a boundary (out[42] nought) is at a boundary, and the kept
  //blocks are then the previous one -- a whole sample early. The chip is
  //standing exactly where the blocks are meant to be taken, so they are taken
  //now. (What found it: the carried bytes below said every voice had been
  //carried at step nought, which is the mask of the sample just finished.)
  if(dsp.phaseForTrace() == 0) dsp.captureVoicesForTrace();
  memcpy(out + 50, dsp.voiceBlocksForTrace(), 8 * 40);

  //And which voices the chip has carried into the sample in flight since that
  //boundary: one byte a voice, nought or one, at 370 + voice.
  //
  //Nothing in the restore needs it -- the blocks above are one instant now --
  //and it is recorded because it is the measurement version 12's split was
  //argued from: the chip says which voices it has reached rather than a reader
  //working it out from out[42] and the three-step stagger. A reader can hold
  //the two against each other in any recording that carries both.
  uint carried = dsp.carriedForTrace();
  for(uint v : range(8)) out[370 + v] = carried >> v & 1;

  //And where the instruction this snapshot fell inside was fetched from
  //(trace v15). out[0..1] above is the counter as it stands *now*, which is
  //the opcode plus however many operand bytes the cycles at out[48] have paid
  //for -- `start + min(length, 1 + phase)`, and at a phase of nought that is
  //the operand and not the next instruction. This is the start itself, taken
  //in main() before the fetch, so a reimplementation that can only stand
  //between instructions has a real boundary to begin at whatever the phase.
  //It needs no opcode-length table here and none there.
  out[378] = traceInstructionPC & 0xff;
  out[379] = traceInstructionPC >> 8 & 0xff;
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
