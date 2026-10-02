#include "SPC_DSP.h"

struct DSP {
  shared_pointer<Emulator::Stream> stream;
  uint8 apuram[64 * 1024] = {};

  auto main() -> void;
  auto read(uint8 address) -> uint8;
  auto write(uint8 address, uint8 data) -> void;

  auto load() -> bool;
  auto power(bool reset) -> void;
  auto mute() -> bool;

  //For BSNES_TRACE_APU. Audio RAM is the one memory the 65816 cannot reach:
  //it talks to the sound processor only through $2140-$2143, so a recording
  //that does not carry this cannot say anything about what the sound driver
  //the cartridge uploaded is doing.
  auto apuramForTrace() const -> const uint8* { return apuram; }

  //The sound chip's 128 registers, copied out for the recorder. They live
  //inside the DSP core rather than in a buffer of their own, so collecting
  //them means asking for them one at a time.
  auto registersForTrace() -> const uint8*;

  //Where in the echo buffer the sound chip is writing, which lives inside the
  //chip and appears nowhere a driver could look.
  auto echoOffsetForTrace() const -> uint { return spc_dsp.echoOffsetForTrace(); }
  auto phaseForTrace() const -> uint { return spc_dsp.phaseForTrace(); }
  auto rateCounterForTrace() const -> uint { return spc_dsp.rateCounterForTrace(); }
  auto alternateForTrace() const -> uint { return spc_dsp.alternateForTrace(); }
  auto envelopeForTrace(uint v) const -> uint { return spc_dsp.envelopeForTrace(v); }
  auto outputForTrace(uint v) const -> int { return spc_dsp.outputForTrace(v); }
  auto interpForTrace(uint v) const -> int { return spc_dsp.interpForTrace(v); }
  auto decodedForTrace(uint v, uint n) const -> int { return spc_dsp.decodedForTrace(v, n); }
  auto decodedAtForTrace(uint v) const -> int { return spc_dsp.decodedAtForTrace(v); }
  auto windowForTrace(uint v, uint n) const -> int { return spc_dsp.windowForTrace(v, n); }
  auto mainOutForTrace(uint c) const -> int { return spc_dsp.traceMainOut[c]; }
  auto echoInForTrace(uint c) const -> int { return spc_dsp.traceEchoIn[c]; }

  //BSNES_DUMP_STAGES, which writes what the final sample was made of.
  auto dumpStages() -> void;
  auto envelopePhaseForTrace(uint v) const -> uint { return spc_dsp.envelopePhaseForTrace(v); }
  auto hiddenEnvelopeForTrace(uint v) const -> int { return spc_dsp.hiddenEnvelopeForTrace(v); }
  auto keyOnDelayForTrace(uint v) const -> uint { return spc_dsp.keyOnDelayForTrace(v); }
  auto blockForTrace(uint v) const -> uint { return spc_dsp.blockForTrace(v); }
  auto blockOffsetForTrace(uint v) const -> uint { return spc_dsp.blockOffsetForTrace(v); }
  auto blockHeaderForTrace(uint v) const -> uint { return spc_dsp.blockHeaderForTrace(v); }

  //BSNES_TRACE_APU (trace v13). The eight per-voice blocks, taken at the
  //boundary the sample in flight began at rather than wherever in that sample
  //the snapshot happens to fall.
  //
  //Why the buffer exists at all: this chip walks its eight voices three steps
  //apart, so a snapshot taken part-way through a sample catches some of them
  //on the near side of their own step and some on the far side. Written out
  //as they stand, the eight blocks are eight instants, and a reimplementation
  //that stands them all at one boundary is a sample short for every voice the
  //chip had already carried. Nothing in the record said so, and version 12
  //recordings carry that split unannounced.
  //
  //So the recorder keeps the blocks it would have written at the last
  //boundary and writes *those*: one instant, before any of the eight was
  //carried, and the instant a restored chip picks up from -- it then makes
  //the sample in flight for itself, exactly as this one is doing.
  auto captureVoicesForTrace() -> void;
  auto voiceBlocksForTrace() const -> const uint8* { return traceVoiceBlocks; }
  //BSNES_TRACE_APU (trace v16): the echo filter's history, kept at the same
  //boundary as the voice blocks and for the same reason -- the chip reads one
  //channel's sample at step 22 and the other's at 23, so a history read off it
  //inside a sample can be two instants. Thirty-two bytes: eight samples,
  //oldest first, each a left and a right int16, little-endian.
  auto echoHistoryForTrace() const -> const uint8* { return traceEchoHistory; }
  static constexpr uint EchoHistoryBytes = 32;
  uint8 traceEchoHistory[EchoHistoryBytes] = {};
  //And which voices the chip has carried since that boundary, one bit a
  //voice, so the version 12 split can be read out of a version 13 recording
  //rather than inferred from the step number.
  auto carriedForTrace() const -> uint { return spc_dsp.carriedForTrace(); }
  //Forty bytes a voice, eight voices, laid out by SMP::snapshotForTrace's
  //documentation of the record.
  static constexpr uint VoiceBlockBytes = 40;
  uint8 traceVoiceBlocks[8 * VoiceBlockBytes] = {};

  //BSNES_DUMP_SAMPLES, which writes this chip's output where a reimplementation
  //of it can be compared against it one sample at a time.
  auto dumpSamples(const int16* samples, uint count) -> void;

  //BSNES_DUMP_VOICES, which writes each voice's envelope and output once a
  //sample, so a wrong envelope can be told from a wrong waveform.
  auto dumpVoices() -> void;

  //BSNES_DUMP_CONSOLE, which writes the console's own read-only tables out
  //once at power so a reimplementation can be given them rather than carry
  //them in its source.
  auto dumpConsoleTables() -> void;

  //Whether apuram is the truth. With the echo-shadow hack on, the echo buffer
  //is written to a private copy instead of into audio RAM, so a recording made
  //that way would differ from the machine over whatever the echo covers.
  auto echoWritesToAudioRam() const -> bool;
  auto runsStepByStep() const -> bool;

  auto serialize(serializer&) -> void;

  //BSNES_LIVE (live protocol version 4). The steps the chip has taken since
  //power-on, one a step whatever main() is asked to run; never serialized,
  //like smp.traceClocks, which it is held against, so the two count from the
  //same instant. The processor's cycle c begins with the chip's step for it
  //(SMP::step brings the chip up before the processor's access), so the chip
  //has taken c + 1 steps once cycle c has begun.
  uint64 traceSteps = 0;
  //The audio RAM and the 128 registers as they would stand with the chip
  //`steps` steps further on, the machine itself left exactly where it is: the
  //chip, audio RAM and the sample buffer are kept, the chip run on in place,
  //the two domains copied out, and all three put back byte for byte. Nothing
  //else is touched and no port is read, so the processor's course is not
  //moved. `steps` nought copies the two domains as they stand.
  auto wholeCycleForTrace(uint steps, uint8* ram, uint8* registers) -> void;

  int64 clock = 0;

private:
  bool fastDSP = false;
  SPC_DSP spc_dsp;
  int16 samplebuffer[8192];

//unserialized:
  uint8 echoram[64 * 1024] = {};
  uint8 traceRegisters[SPC_DSP::register_count] = {};
};

extern DSP dsp;
