#include <emulator/ramtrace.hpp>
#include <sfc/sfc.hpp>
//after sfc.hpp, so the headers sfc.hpp brings in keep their own order
#include <emulator/live.hpp>

namespace SuperFamicom {

System system;
Scheduler scheduler;
Random random;
Cheat cheat;
#include "serialization.cpp"

auto System::run() -> void {
  scheduler.mode = Scheduler::Mode::Run;
  scheduler.enter();
  if(scheduler.event == Scheduler::Event::Frame) frameEvent();
}

auto System::runToSave() -> void {
  // Enable coprocessor delayed sync if it is off - this is extremely important
  // for coprocessor games, as many will not sync correctly for states when the
  // option is off.
  bool delay_sync_prev = configuration.hacks.coprocessor.delayedSync;
  configuration.hacks.coprocessor.delayedSync = true;

  auto method = configuration.system.serialization.method;

  //these games will periodically deadlock when using "Fast" synchronization
  if(cartridge.headerTitle() == "Star Ocean") method = "Strict";
  if(cartridge.headerTitle() == "TALES OF PHANTASIA") method = "Strict";

  //fast serialization corrupts the Super Game Boy implementation
  if(cartridge.has.ICD) method = "Strict";

  //fallback in case of unrecognized method specified
  if(method != "Fast" && method != "Strict") method = "Fast";

  scheduler.mode = Scheduler::Mode::Synchronize;
  if(method == "Fast") runToSaveFast();
  if(method == "Strict") runToSaveStrict();

  scheduler.mode = Scheduler::Mode::Run;
  scheduler.active = cpu.thread;

  // Restore coprocessor delayed sync to whatever it was previous to the state save operation
  configuration.hacks.coprocessor.delayedSync = delay_sync_prev;
}

auto System::runToSaveFast() -> void {
  //run the emulator normally until the CPU thread naturally hits a synchronization point
  while(true) {
    scheduler.enter();
    if(scheduler.event == Scheduler::Event::Frame) frameEvent();
    if(scheduler.event == Scheduler::Event::Synchronized) {
      if(scheduler.active != cpu.thread) continue;
      break;
    }
    if(scheduler.event == Scheduler::Event::Desynchronized) continue;
  }

  //ignore any desynchronization events to force all other threads to their synchronization points
  auto synchronize = [&](cothread_t thread) -> void {
    scheduler.active = thread;
    while(true) {
      scheduler.enter();
      if(scheduler.event == Scheduler::Event::Frame) frameEvent();
      if(scheduler.event == Scheduler::Event::Synchronized) break;
      if(scheduler.event == Scheduler::Event::Desynchronized) continue;
    }
  };

  synchronize(smp.thread);
  synchronize(ppu.thread);
  for(auto coprocessor : cpu.coprocessors) {
    synchronize(coprocessor->thread);
  }
}

auto System::runToSaveStrict() -> void {
  //run every thread until it cleanly hits a synchronization point
  //if it fails, start resynchronizing every thread again
  auto synchronize = [&](cothread_t thread) -> bool {
    scheduler.active = thread;
    while(true) {
      scheduler.enter();
      if(scheduler.event == Scheduler::Event::Frame) frameEvent();
      if(scheduler.event == Scheduler::Event::Synchronized) break;
      if(scheduler.event == Scheduler::Event::Desynchronized) return false;
    }
    return true;
  };

  while(true) {
    //SMP thread is synchronized twice to ensure the CPU and SMP are closely aligned:
    //this is extremely critical for Tales of Phantasia and Star Ocean.
    if(!synchronize(smp.thread)) continue;
    if(!synchronize(cpu.thread)) continue;
    if(!synchronize(smp.thread)) continue;
    if(!synchronize(ppu.thread)) continue;

    bool synchronized = true;
    for(auto coprocessor : cpu.coprocessors) {
      if(!synchronize(coprocessor->thread)) { synchronized = false; break; }
    }
    if(!synchronized) continue;

    break;
  }
}

auto System::frameEvent() -> void {
  ppu.refresh();

  //record work RAM for verifying a reimplementation; inert unless enabled.
  //BSNES_TRACE_RAM_AT moves the snapshot to a routine's entry instead, and the
  //two must not both fire or the records would interleave
  //BSNES_POKE: forge work RAM once, before the record of that frame is written,
  //so the recording's first record already holds the forged state.
  RamTrace::poke().applyIfDue(cpu.wram);
  //BSNES_TRACE_REGS_SEED_CHECK: the seed's reading against the shadow, before
  //this frame's record is written; inert unless set.
  checkRegisterSeedForTrace();
  if(!RamTrace::snapshotting()) {
    uint8 audioRegs[RamTrace::AudioSnapshotBytes];
    smp.snapshotForTrace(audioRegs);
    //the object table at the same instant as video memory, from the core that
    //is running: ppu.refresh() above has just drawn the frame from it
    uint8 oam[RamTrace::ObjectBytes];
    ppu.oamForTrace(oam);
    RamTrace::recorder().observe(cpu.wram, sizeof(cpu.wram), {}, ppu.vramForTrace(),
                                 cartridge.ram.data(), cartridge.ram.size(),
                                 dsp.apuramForTrace(), dsp.registersForTrace(),
                                 dsp.echoWritesToAudioRam() && dsp.runsStepByStep(),
                                 audioRegs, oam);
  }
  //BSNES_LIVE: the same instant, for the program comparing itself against
  //this machine (emulator/live.hpp); inert unless set.
  if(Live::enabled()) {
    uint8 oam[RamTrace::ObjectBytes];
    ppu.oamForTrace(oam);
    uint8 colours[512];
    ppu.cgramForTrace(colours);
    Live::capture(cpu.wram, ppu.vramForTrace(), colours, oam, RamTrace::registerFile().bytes,
                  dsp.apuramForTrace(), dsp.registersForTrace(),
                  dsp.echoWritesToAudioRam() && dsp.runsStepByStep());
  }

  //refresh all cheat codes once per frame
  Memory::GlobalWriteEnable = true;
  for(auto& code : cheat.codes) {
    if(code.enable) {
      bus.write(code.address, code.data);
    }
  }
  Memory::GlobalWriteEnable = false;
}

//BSNES_TRACE_REGS at a state load (2026-09-26): the register file as the
//loaded machine holds it, for RamTrace::RegisterFile::seed.
//
//The recorder's register shadow is the game's own stores, and a recording that
//begins at a loaded state used to begin with a shadow holding none of the
//stores made before the state was cut. Here each part of the machine answers
//for the registers it keeps, as the byte a store would have put there. What is
//answered, and what is not and why:
//
//  $2100-$2133 (PPU, the fast core the recorder runs): every register the core
//    keeps whole, inverted from its decoded fields, bits it does not keep read
//    as nought. Not answered: $2104, $2118, $2119, $2122 and $2180 are data
//    ports - a store to one goes into memory, and which byte was last is kept
//    nowhere; $2116/$2117 (VRAM address) and $2121 (CGRAM address) are
//    stepped on by every data-port access, so what the chip holds is not what
//    was stored; $2115 when its increment is 128, which two settings give;
//    $2132 (COLDATA), whose last store's three select bits are kept nowhere;
//    $210D/$210E when the background and Mode 7 halves disagree about the one
//    byte both took from the last store. The accurate and HD cores are not
//    taught this and answer for no PPU register at all (it says so).
//  $2134-$213F: read-only; a store to one changes nothing the chip keeps.
//  $2140-$2143: what the 65816 last stored to each port, as the audio
//    processor holds it for $F4-$F7 - unless the driver has since cleared the
//    pair through $F1 bits 4-5, when it holds nought (measured, not assumed:
//    BSNES_TRACE_REGS_SEED_CHECK below).
//  $2181-$2183: the WRAM address, stepped on by every $2180 access.
//  $4200-$420D (CPU): NMITIMEN, WRIO, the multiplier and divider operands (as
//    last accepted), HTIME/VTIME, HDMAEN, MEMSEL. Not answered: $420B (MDMAEN),
//    which the hardware clears when the transfer ends.
//  $4300-$437F (DMA): each channel's $43x0, $43x1, $43x4, $43x7, $43xB/$43xF
//    always; $43x2/$43x3 only on a channel HDMA owns ($420C), because a
//    general transfer walks the source address on; $43x5/$43x6 only on a
//    direct HDMA channel, because a general transfer counts the size down and
//    indirect HDMA reloads it. Never $43x8-$43xA, which HDMA itself walks.
//  And any B-bus register a channel pointed from A to B writes: whenever the
//    channel wrote it last, what the chip holds is the channel's byte and not
//    a store. That is the shadow's own rule (RamTrace::RegisterFile: stores,
//    never DMA or HDMA).
//  Every other address in $2184-$21FF, $420E-$42FF and $43xC-$43xE holds
//    nothing a store sets.
//
//A byte not answered for is left as it was - nought, before the first
//instruction - so a seeded recording differs from an unseeded one only in
//bytes the unseeded one had no value for yet.
auto System::registersForTrace(uint8* values, uint8* known) -> bool {
  for(uint n : range(RamTrace::RegisterFile::Bytes)) values[n] = 0, known[n] = 0;
  bool ppuAnswered = ppu.registersForTrace(values, known);
  for(uint n : range(4)) {
    values[0x40 + n] = smp.cpuPortForTrace(n);
    known[0x40 + n] = 1;
  }
  cpu.registersForTrace(values, known);
  return ppuAnswered;
}

auto System::seedRegistersForTrace() -> void {
  uint8 values[RamTrace::RegisterFile::Bytes];
  uint8 known[RamTrace::RegisterFile::Bytes];
  bool ppuAnswered = registersForTrace(values, known);
  uint seeded = RamTrace::registerFile().seed(values, known);
  fprintf(stderr, "[regs] seeded %u of %u register bytes from the loaded state%s; not seeded:",
          seeded, RamTrace::RegisterFile::Bytes,
          ppuAnswered ? "" : " (this PPU core answers for none of $2100-$213F)");
  //The bytes left out, as runs of addresses, so a run says what its record 0
  //cannot speak for.
  for(uint n = 0; n < RamTrace::RegisterFile::Bytes;) {
    if(known[n]) { n++; continue; }
    uint first = n;
    while(n < RamTrace::RegisterFile::Bytes && !known[n]
          && (n == first || (n & 0xff) != 0)) n++;
    uint a = first < 0x100 ? 0x2100 + first : 0x4200 + first - 0x100;
    uint b = (n - 1) < 0x100 ? 0x2100 + (n - 1) : 0x4200 + (n - 1) - 0x100;
    if(a == b) fprintf(stderr, " $%04X", a);
    else fprintf(stderr, " $%04X-$%04X", a, b);
  }
  fprintf(stderr, "\n");
}

//BSNES_TRACE_REGS_SEED_CHECK=1 (with BSNES_TRACE_REGS): at every frame, what
//the seed would say against what the shadow holds, over every byte the seed
//answers for and a store has reached. On a cold boot the two are meant to
//agree wherever the seed answers, so each disagreement is the seed claiming
//something about a register that is not true, and the summary at exit names
//every register that disagreed and on how many frames. It changes nothing
//recorded.
auto System::checkRegisterSeedForTrace() -> void {
  static int on = -1;
  static uint32 frames = 0, compared[RamTrace::RegisterFile::Bytes] = {};
  static uint32 differed[RamTrace::RegisterFile::Bytes] = {};
  static uint8 firstSeed[RamTrace::RegisterFile::Bytes], firstShadow[RamTrace::RegisterFile::Bytes];
  static uint32 firstFrame[RamTrace::RegisterFile::Bytes];
  if(on < 0) {
    on = getenv("BSNES_TRACE_REGS_SEED_CHECK") && RamTrace::tracingRegisters() ? 1 : 0;
    if(on) atexit([] {
      uint registers = 0;
      for(uint n : range(RamTrace::RegisterFile::Bytes)) registers += compared[n] != 0;
      fprintf(stderr, "[regs-seed-check] %u frames, %u registers compared\n", frames, registers);
      for(uint n : range(RamTrace::RegisterFile::Bytes)) {
        if(!differed[n]) continue;
        uint a = n < 0x100 ? 0x2100 + n : 0x4200 + n - 0x100;
        fprintf(stderr, "[regs-seed-check] $%04X differs on %u of %u frames; first at frame %u: seed %02x, shadow %02x\n",
                a, differed[n], compared[n], firstFrame[n], firstSeed[n], firstShadow[n]);
      }
    });
  }
  if(!on) return;
  uint8 values[RamTrace::RegisterFile::Bytes];
  uint8 known[RamTrace::RegisterFile::Bytes];
  registersForTrace(values, known);
  auto& file = RamTrace::registerFile();
  for(uint n : range(RamTrace::RegisterFile::Bytes)) {
    if(!known[n] || !file.written[n]) continue;
    compared[n]++;
    if(values[n] == file.bytes[n]) continue;
    if(!differed[n]++) {
      firstFrame[n] = frames;
      firstSeed[n] = values[n];
      firstShadow[n] = file.bytes[n];
    }
  }
  frames++;
}

auto System::load(Emulator::Interface* interface) -> bool {
  information = {};

  bus.reset();
  if(!cpu.load()) return false;
  if(!smp.load()) return false;
  if(!ppu.load()) return false;
  if(!dsp.load()) return false;
  if(!cartridge.load()) return false;

  if(cartridge.region() == "NTSC") {
    information.region = Region::NTSC;
    information.cpuFrequency = Emulator::Constants::Colorburst::NTSC * 6.0;
  }
  if(cartridge.region() == "PAL") {
    information.region = Region::PAL;
    information.cpuFrequency = Emulator::Constants::Colorburst::PAL * 4.8;
  }

  if(configuration.hacks.hotfixes) {
    //due to poor programming, Rendering Ranger R2 will rarely lock up at 32040 * 768hz.
    if(cartridge.headerTitle() == "RENDERING RANGER R2") {
      information.apuFrequency = 32000.0 * 768.0;
    }
  }

  if(cartridge.has.ICD) {
    if(!icd.load()) return false;
  }
  if(cartridge.has.BSMemorySlot) bsmemory.load();

  this->interface = interface;
  return information.loaded = true;
}

auto System::save() -> void {
  if(!loaded()) return;

  cartridge.save();
}

auto System::unload() -> void {
  if(!loaded()) return;

  controllerPort1.unload();
  controllerPort2.unload();
  expansionPort.unload();

  if(cartridge.has.ICD) icd.unload();
  if(cartridge.has.MCC) mcc.unload();
  if(cartridge.has.Event) event.unload();
  if(cartridge.has.SA1) sa1.unload();
  if(cartridge.has.SuperFX) superfx.unload();
  if(cartridge.has.HitachiDSP) hitachidsp.unload();
  if(cartridge.has.SPC7110) spc7110.unload();
  if(cartridge.has.SDD1) sdd1.unload();
  if(cartridge.has.OBC1) obc1.unload();
  if(cartridge.has.MSU1) msu1.unload();
  if(cartridge.has.BSMemorySlot) bsmemory.unload();
  if(cartridge.has.SufamiTurboSlotA) sufamiturboA.unload();
  if(cartridge.has.SufamiTurboSlotB) sufamiturboB.unload();

  cartridge.unload();
  information.loaded = false;
}

auto System::power(bool reset) -> void {
  hacks.hdPPU = configuration.hacks.ppu.hd;
  hacks.fastPPU = configuration.hacks.ppu.fast && !hacks.hdPPU;

  Emulator::audio.reset(interface);

  random.entropy(Random::Entropy::Low);  //fallback case
  if(configuration.hacks.entropy == "None") random.entropy(Random::Entropy::None);
  if(configuration.hacks.entropy == "Low" ) random.entropy(Random::Entropy::Low );
  if(configuration.hacks.entropy == "High") random.entropy(Random::Entropy::High);

  cpu.power(reset);
  smp.power(reset);
  dsp.power(reset);
  ppu.power(reset);

  if(cartridge.has.ICD) icd.power();
  if(cartridge.has.MCC) mcc.power();
  if(cartridge.has.DIP) dip.power();
  if(cartridge.has.Event) event.power();
  if(cartridge.has.SA1) sa1.power();
  if(cartridge.has.SuperFX) superfx.power();
  if(cartridge.has.ARMDSP) armdsp.power();
  if(cartridge.has.HitachiDSP) hitachidsp.power();
  if(cartridge.has.NECDSP) necdsp.power();
  if(cartridge.has.EpsonRTC) epsonrtc.power();
  if(cartridge.has.SharpRTC) sharprtc.power();
  if(cartridge.has.SPC7110) spc7110.power();
  if(cartridge.has.SDD1) sdd1.power();
  if(cartridge.has.OBC1) obc1.power();
  if(cartridge.has.MSU1) msu1.power();
  if(cartridge.has.Cx4) cx4.power();
  if(cartridge.has.DSP1) dsp1.power();
  if(cartridge.has.DSP2) dsp2.power();
  if(cartridge.has.DSP4) dsp4.power();
  if(cartridge.has.ST0010) st0010.power();
  if(cartridge.has.BSMemorySlot) bsmemory.power();
  if(cartridge.has.SufamiTurboSlotA) sufamiturboA.power();
  if(cartridge.has.SufamiTurboSlotB) sufamiturboB.power();

  if(cartridge.has.ICD) cpu.coprocessors.append(&icd);
  if(cartridge.has.Event) cpu.coprocessors.append(&event);
  if(cartridge.has.SA1) cpu.coprocessors.append(&sa1);
  if(cartridge.has.SuperFX) cpu.coprocessors.append(&superfx);
  if(cartridge.has.ARMDSP) cpu.coprocessors.append(&armdsp);
  if(cartridge.has.HitachiDSP) cpu.coprocessors.append(&hitachidsp);
  if(cartridge.has.NECDSP) cpu.coprocessors.append(&necdsp);
  if(cartridge.has.EpsonRTC) cpu.coprocessors.append(&epsonrtc);
  if(cartridge.has.SharpRTC) cpu.coprocessors.append(&sharprtc);
  if(cartridge.has.SPC7110) cpu.coprocessors.append(&spc7110);
  if(cartridge.has.MSU1) cpu.coprocessors.append(&msu1);
  if(cartridge.has.BSMemorySlot) cpu.coprocessors.append(&bsmemory);

  scheduler.active = cpu.thread;

  controllerPort1.power(ID::Port::Controller1);
  controllerPort2.power(ID::Port::Controller2);
  expansionPort.power();

  controllerPort1.connect(settings.controllerPort1);
  controllerPort2.connect(settings.controllerPort2);
  expansionPort.connect(settings.expansionPort);

  information.serializeSize[0] = serializeInit(0);
  information.serializeSize[1] = serializeInit(1);
}

}
