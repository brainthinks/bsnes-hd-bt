#include <sfc/sfc.hpp>

namespace SuperFamicom {

DSP dsp;

#include "serialization.cpp"
#include "SPC_DSP.cpp"

auto DSP::main() -> void {
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

auto DSP::registersForTrace() -> const uint8* {
  for(uint address : range(SPC_DSP::register_count)) {
    traceRegisters[address] = spc_dsp.read(address);
  }
  return traceRegisters;
}

auto DSP::echoWritesToAudioRam() const -> bool {
  return !configuration.hacks.dsp.echoShadow;
}

auto DSP::mute() -> bool {
  return spc_dsp.mute();
}

}
