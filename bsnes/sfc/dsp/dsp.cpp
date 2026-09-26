#include <sfc/sfc.hpp>

namespace SuperFamicom {

DSP dsp;

#include "serialization.cpp"
#include "SPC_DSP.cpp"

auto DSP::main() -> void {
  //BSNES_TRACE_APU (trace v13): at a sample boundary, and before this sample's
  //first step runs, keep the eight per-voice blocks as they stand. That is the
  //one instant in a sample at which all eight voices are on the same side of
  //their own step, and it is the instant a snapshot taken anywhere inside this
  //sample is restored to. Cheap enough to leave on: 320 bytes a sample.
  if(spc_dsp.phaseForTrace() == 0) captureVoicesForTrace();
  if(!configuration.hacks.dsp.fast) {
    spc_dsp.run(1);
    clock += 2;
  } else {
    spc_dsp.run(32);
    clock += 2 * 32;
  }

  int count = spc_dsp.sample_count();
  if(count > 0) {
    if(!system.runAhead)
    for(uint n = 0; n < count; n += 2) {
      float left  = samplebuffer[n + 0] / 32768.0f;
      float right = samplebuffer[n + 1] / 32768.0f;
      stream->sample(left, right);
    }
    //BSNES_DUMP_SAMPLES: the sound chip's own output, as signed sixteen bit
    //pairs. Nothing else lets a reimplementation of the chip be compared
    //sample by sample rather than once a frame, which is five hundred samples
    //too coarse to find anything.
    if(!system.runAhead) dumpSamples(samplebuffer, count);
    if(!system.runAhead) dumpVoices();
    if(!system.runAhead) dumpStages();
    spc_dsp.set_output(samplebuffer, 8192);
  }
}

auto DSP::read(uint8 address) -> uint8 {
  return spc_dsp.read(address);
}

auto DSP::write(uint8 address, uint8 data) -> void {
  if(configuration.hacks.dsp.echoShadow) {
    if(address == 0x6c && (data & 0x20)) {
      memset(echoram, 0x00, 65536);
    }
  }

  spc_dsp.write(address, data);
}

auto DSP::load() -> bool {
  return true;
}

auto DSP::power(bool reset) -> void {
  clock = 0;
  stream = Emulator::audio.createStream(2, system.apuFrequency() / 768.0);

  if(!reset) {
    if(!configuration.hacks.dsp.echoShadow) {
      spc_dsp.init(apuram, apuram);
    } else {
      memset(echoram, 0x00, 65536);
      spc_dsp.init(apuram, echoram);
    }
    spc_dsp.reset();
    spc_dsp.set_output(samplebuffer, 8192);
  } else {
    spc_dsp.soft_reset();
    spc_dsp.set_output(samplebuffer, 8192);
  }

  dumpConsoleTables();

  if(configuration.hacks.hotfixes) {
    //Magical Drop (Japan) does not initialize the DSP registers at startup:
    //tokoton mode will hang forever in some instances even on real hardware.
    if(cartridge.headerTitle() == "MAGICAL DROP") {
      for(uint address : range(0x80)) spc_dsp.write(address, 0xff);
    }
  }
}

//BSNES_DUMP_CONSOLE: write out the two read-only tables that live in the
//console rather than in a cartridge - the audio processor's sixty-four byte
//boot ROM, and the sound chip's five hundred and twelve interpolation
//coefficients. A reimplementation needs both and can have neither in its own
//source tree, the same way it cannot have the cartridge. Whoever owns this
//machine owns these; the file belongs beside the ROM and travels no further.
auto DSP::dumpConsoleTables() -> void {
  auto path = getenv("BSNES_DUMP_CONSOLE");
  if(!path) return;
  auto file = fopen(path, "wb");
  if(!file) return;
  uint8 header[8] = {'F', 'Z', 'C', 'N', 64, 0, 0, 2};  //magic, then the two lengths
  fwrite(header, 1, sizeof(header), file);
  fwrite(smp.iplrom, 1, sizeof(smp.iplrom), file);
  auto gauss = spc_dsp_gauss_table();
  for(uint n : range(512)) {
    uint8 word[2] = {(uint8)(gauss[n] & 0xff), (uint8)(gauss[n] >> 8 & 0xff)};
    fwrite(word, 1, 2, file);
  }
  fclose(file);
  fprintf(stderr, "BSNES_DUMP_CONSOLE: wrote %s\n", path);
}

auto DSP::dumpSamples(const int16* samples, uint count) -> void {
  static FILE* file = nullptr;
  static bool tried = false;
  if(!tried) {
    tried = true;
    if(auto path = getenv("BSNES_DUMP_SAMPLES")) file = fopen(path, "wb");
  }
  if(!file) return;
  for(uint n = 0; n < count; n++) {
    uint8 word[2] = {(uint8)(samples[n] & 0xff), (uint8)(samples[n] >> 8 & 0xff)};
    fwrite(word, 1, 2, file);
  }
}

//BSNES_TRACE_APU (trace v13): the eight per-voice blocks as of the boundary
//the sample now being made began at, and the start of a new carried mask.
//
//The layout is the record's and is documented where the record is documented,
//in SMP::snapshotForTrace: forty bytes a voice, indexed rather than parsed.
//It is written here rather than there because *when* it is taken is the whole
//point -- a snapshot can fall anywhere in a sample, and this can only be taken
//at the boundary.
auto DSP::captureVoicesForTrace() -> void {
  for(uint v : range(8)) {
    uint8* b = traceVoiceBlocks + v * VoiceBlockBytes;
    uint env = spc_dsp.envelopeForTrace(v);
    b[0] = env & 0xff;
    b[1] = env >> 8 & 0xff;
    b[2] = spc_dsp.envelopePhaseForTrace(v) & 3;
    b[3] = spc_dsp.keyOnDelayForTrace(v) & 0xff;
    //Signed on the chip -- a linear decrease that has gone past nought is how
    //the two-slope mode's test is reached -- and carried as the low sixteen
    //bits of it, which is every value the arithmetic can leave there.
    uint hidden = (uint)spc_dsp.hiddenEnvelopeForTrace(v);
    b[4] = hidden & 0xff;
    b[5] = hidden >> 8 & 0xff;
    uint block = spc_dsp.blockForTrace(v);
    b[6] = block & 0xff;
    b[7] = block >> 8 & 0xff;
    b[8] = spc_dsp.blockOffsetForTrace(v) & 0xff;
    b[9] = spc_dsp.blockHeaderForTrace(v) & 0xff;
    b[10] = spc_dsp.decodedAtForTrace(v) & 0xff;
    b[11] = 0;
    uint interp = (uint)spc_dsp.interpForTrace(v);
    b[12] = interp & 0xff;
    b[13] = interp >> 8 & 0xff;
    b[14] = 0; b[15] = 0;
    for(uint n : range(12)) {
      uint sample = (uint)spc_dsp.decodedForTrace(v, n);
      b[16 + n * 2] = sample & 0xff;
      b[17 + n * 2] = sample >> 8 & 0xff;
    }
  }
  //And the echo filter's eight samples a channel (trace v16), at the same
  //boundary: oldest first, left then right, as the chip holds them.
  for(uint n : range(8)) {
    for(uint ch : range(2)) {
      uint sample = (uint)spc_dsp.echoHistoryForTrace(1 + n, ch);
      traceEchoHistory[n * 4 + ch * 2 + 0] = sample & 0xff;
      traceEchoHistory[n * 4 + ch * 2 + 1] = sample >> 8 & 0xff;
    }
  }
  //A new sample, so nothing has been carried into it yet. The chip sets the
  //bits itself as each voice's step runs.
  spc_dsp.clearCarriedForTrace();
}

//BSNES_DUMP_VOICES: each voice's envelope and its own output, per sample.
//Sixteen bytes a sample, which is what separates a wrong envelope from a wrong
//waveform: the two look the same in the mix and nothing else tells them apart.
auto DSP::dumpVoices() -> void {
  static FILE* file = nullptr;
  static bool tried = false;
  if(!tried) {
    tried = true;
    if(auto path = getenv("BSNES_DUMP_VOICES")) file = fopen(path, "wb");
  }
  if(!file) return;
  //Sixteen bytes of what a driver could read, then sixteen of what it could
  //not: each voice's envelope to its full eleven bits and which of the four
  //phases it is in. Two implementations whose readable registers agree can
  //still be a few hundredths apart underneath, and this is where that shows.
  uint8 row[128];
  for(uint v : range(8)) {
    row[v] = spc_dsp.read(v * 0x10 + 8);
    row[8 + v] = spc_dsp.read(v * 0x10 + 9);
    uint env = spc_dsp.envelopeForTrace(v);
    row[16 + v * 2] = env & 0xff;
    row[17 + v * 2] = (env >> 8 & 0x07) | (spc_dsp.envelopePhaseForTrace(v) & 3) << 3;
    //And the output at its full width, which the register above shows only the
    //top eight bits of.
    int out = spc_dsp.outputForTrace(v);
    row[32 + v * 2] = out & 0xff;
    row[33 + v * 2] = out >> 8 & 0xff;
    //And where the voice is between two decoded samples.
    int interp = spc_dsp.interpForTrace(v);
    row[48 + v * 2] = interp & 0xff;
    row[49 + v * 2] = interp >> 8 & 0xff;
  }
  //And the four samples each voice's curve is drawn through, which can be
  //compared without agreeing about how the buffer is laid out.
  for(uint v : range(8)) {
    for(uint n : range(4)) {
      int sample = spc_dsp.windowForTrace(v, n);
      row[64 + v * 8 + n * 2] = sample & 0xff;
      row[65 + v * 8 + n * 2] = sample >> 8 & 0xff;
    }
  }
  fwrite(row, 1, sizeof(row), file);
}

//BSNES_DUMP_STAGES: the voice sum before the master volume and the echo after
//its filter, left then right, as signed sixteen bit words.
auto DSP::dumpStages() -> void {
  static FILE* file = nullptr;
  static bool tried = false;
  if(!tried) {
    tried = true;
    if(auto path = getenv("BSNES_DUMP_STAGES")) file = fopen(path, "wb");
  }
  if(!file) return;
  uint8 row[8];
  for(uint c : range(2)) {
    int main = spc_dsp.traceMainOut[c];
    int echo = spc_dsp.traceEchoIn[c];
    row[c * 2] = main & 0xff;
    row[c * 2 + 1] = main >> 8 & 0xff;
    row[4 + c * 2] = echo & 0xff;
    row[5 + c * 2] = echo >> 8 & 0xff;
  }
  fwrite(row, 1, sizeof(row), file);
}

auto DSP::registersForTrace() -> const uint8* {
  for(uint address : range(SPC_DSP::register_count)) {
    traceRegisters[address] = spc_dsp.read(address);
  }
  return traceRegisters;
}

auto DSP::echoWritesToAudioRam() const -> bool {
  return !configuration.hacks.dsp.echoShadow;
}

//Whether this chip is being run as the hardware runs it. The fast path does a
//whole sample's thirty-two steps in one go, which is close enough to listen to
//and not close enough to check against: a reimplementation cannot be in step
//with something that does not keep the steps.
auto DSP::runsStepByStep() const -> bool {
  return !configuration.hacks.dsp.fast;
}

auto DSP::mute() -> bool {
  return spc_dsp.mute();
}

}
