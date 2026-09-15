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
