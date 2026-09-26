#include <emulator/ramtrace.hpp>
#include <sfc/sfc.hpp>

namespace SuperFamicom {

CPU cpu;
#include "dma.cpp"
#include "memory.cpp"
#include "io.cpp"
#include "timing.cpp"
#include "irq.cpp"
#include "serialization.cpp"

auto CPU::synchronizeSMP() -> void {
  if(smp.clock < 0) scheduler.resume(smp.thread);
}

auto CPU::synchronizePPU() -> void {
  if(ppu.clock < 0) scheduler.resume(ppu.thread);
}

auto CPU::synchronizeCoprocessors() -> void {
  for(auto coprocessor : coprocessors) {
    if(coprocessor->clock < 0) scheduler.resume(coprocessor->thread);
  }
}

auto CPU::Enter() -> void {
  while(true) {
    scheduler.synchronize();
    cpu.main();
  }
}

auto CPU::main() -> void {
  if(r.wai) return instructionWait();
  if(r.stp) return instructionStop();
  //An interrupt pending here is taken instead of this instruction, and the
  //same PC comes round again after the rti. Noted out here, an entry was
  //snapshotted twice on a one-in-two-thousand coincidence of scanline and
  //PC, and a paired bracket died on its own duplicate ("repeated entry
  //before paired exit"), truncating the recording. So note an instruction
  //only when it is the one about to execute.
  if(!status.interruptPending) {
    //BSNES_TRACE_EXEC: here the PC is an instruction start and the flags are the
    //widths it runs with, which is exactly what a disassembler cannot infer
    //BSNES_TRACE_EXEC_BRACKET: every instruction start, whether or not this
    //frame is being recorded, so a handler's return is seen when it happens
    if(RamTrace::coverageBracketed()) RamTrace::noteStack(r.s.w);
    if(RamTrace::tracingExec()) RamTrace::noteExec(r.pc.d, r.p.m, r.p.x);
    //BSNES_TRACE_RAM_AT: snapshot RAM as a routine finds it, which is the only
    //way to capture inputs that later code in the same frame overwrites
    if(RamTrace::snapshotting() && RamTrace::atEntry(r.pc.d)) {
      //registers as well as RAM: a routine's arguments are as often in X as in
      //memory, and the car routines are indexed by it
      RamTrace::Snapshot regs;
      regs.a = r.a.w; regs.x = r.x.w; regs.y = r.y.w;
      regs.s = r.s.w; regs.d = r.d.w; regs.db = r.b; regs.p = r.p;
      uint8 audioRegs[RamTrace::AudioSnapshotBytes];
      smp.snapshotForTrace(audioRegs);
      //the object table at the same instant as video memory, from the core
      //that is running
      uint8 oam[RamTrace::ObjectBytes];
      ppu.oamForTrace(oam);
      RamTrace::recorder().observe(wram, sizeof(wram), regs, ppu.vramForTrace(),
                                   cartridge.ram.data(), cartridge.ram.size(),
                                   dsp.apuramForTrace(), dsp.registersForTrace(),
                                   dsp.echoWritesToAudioRam() && dsp.runsStepByStep(),
                                   audioRegs, oam);
    }
    if(RamTrace::timingTrace().enabled()) {
      RamTrace::timingTrace().note(r.pc.d, counter.cpu, vcounter(), hcounter());
    }
    return instruction();
  }

  //BSNES_TRACE_EXEC_BRACKET: the stack pointer before the interrupt pushes its
  //frame is the level the handler stays below until its rti
  if(status.nmiPending || status.irqPending) RamTrace::noteInterrupt(r.s.w);
  if(status.nmiPending) {
    status.nmiPending = 0;
    r.vector = r.e ? 0xfffa : 0xffea;
    return interrupt();
  }

  if(status.irqPending) {
    status.irqPending = 0;
    r.vector = r.e ? 0xfffe : 0xffee;
    return interrupt();
  }

  if(status.resetPending) {
    status.resetPending = 0;
    for(uint repeat : range(22)) step<6,0>();  //step(132);
    r.vector = 0xfffc;
    return interrupt();
  }

  status.interruptPending = 0;
}

auto CPU::load() -> bool {
  version = configuration.system.cpu.version;
  if(version < 1) version = 1;
  if(version > 2) version = 2;
  return true;
}

auto CPU::power(bool reset) -> void {
  WDC65816::power();
  Thread::create(Enter, system.cpuFrequency());
  coprocessors.reset();
  PPUcounter::reset();
  PPUcounter::scanline = {&CPU::scanline, this};

  function<uint8 (uint, uint8)> reader;
  function<void  (uint, uint8)> writer;

  reader = {&CPU::readRAM, this};
  writer = {&CPU::writeRAM, this};
  bus.map(reader, writer, "00-3f,80-bf:0000-1fff", 0x2000);
  bus.map(reader, writer, "7e-7f:0000-ffff", 0x20000);

  reader = {&CPU::readAPU, this};
  writer = {&CPU::writeAPU, this};
  bus.map(reader, writer, "00-3f,80-bf:2140-217f");

  reader = {&CPU::readCPU, this};
  writer = {&CPU::writeCPU, this};
  bus.map(reader, writer, "00-3f,80-bf:2180-2183,4016-4017,4200-421f");

  reader = {&CPU::readDMA, this};
  writer = {&CPU::writeDMA, this};
  bus.map(reader, writer, "00-3f,80-bf:4300-437f");

  if(!reset) random.array(wram, sizeof(wram));

  if(configuration.hacks.hotfixes) {
    //Dirt Racer (Europe) relies on uninitialized memory containing certain values to boot without freezing.
    //the game itself is broken and will fail to run sometimes on real hardware, but for the sake of expedience,
    //WRAM is initialized to a constant value that will allow this game to always boot in successfully.
    if(cartridge.headerTitle() == "DIRT RACER") {
      for(auto& byte : wram) byte = 0xff;
    }
  }

  for(uint n : range(8)) {
    channels[n] = {};
    if(n != 7) channels[n].next = channels[n + 1];
  }

  counter = {};
  io = {};
  alu = {};

  status = {};
  status.dramRefreshPosition = (version == 1 ? 530 : 538);
  status.hdmaSetupPosition = (version == 1 ? 12 + 8 - dmaCounter() : 12 + dmaCounter());
  status.hdmaPosition = 1104;
  status.resetPending = 1;
  status.interruptPending = 1;
}


//BSNES_TRACE_REGS (2026-09-26): $4200-$43FF at a state load, as the bytes a
//store would have put there, and - because a transfer writes the B bus the way
//a store does - every B-bus register a channel pointed from A to B could have
//written last is withdrawn from `known`. See System::seedRegistersForTrace for
//what each register is and why the ones left out are.
auto CPU::registersForTrace(uint8* values, uint8* known) const -> void {
  auto set = [&](uint address, uint value) {
    values[0x100 + address - 0x4200] = value;
    known[0x100 + address - 0x4200] = 1;
  };
  set(0x4200, io.nmiEnable << 7 | io.virqEnable << 5 | io.hirqEnable << 4 | io.autoJoypadPoll << 0);
  set(0x4201, io.pio);
  set(0x4202, io.wrmpya);
  set(0x4203, io.wrmpyb);
  set(0x4204, io.wrdiva >> 0 & 0xff);
  set(0x4205, io.wrdiva >> 8 & 0xff);
  set(0x4206, io.wrdivb);
  {
    uint htime = ((io.htime >> 2) - 1) & 0x1ff;
    set(0x4207, htime & 0xff);
    set(0x4208, htime >> 8 & 1);
  }
  set(0x4209, io.vtime & 0xff);
  set(0x420a, io.vtime >> 8 & 1);
  {
    uint enabled = 0;
    for(uint n : range(8)) enabled |= channels[n].hdmaEnable << n;
    set(0x420c, enabled);
  }
  set(0x420d, io.fastROM);
  for(uint n : range(8)) {
    auto& channel = channels[n];
    uint base = 0x4300 + n * 0x10;
    set(base + 0x0, channel.transferMode << 0 | channel.fixedTransfer << 3
                  | channel.reverseTransfer << 4 | channel.unused << 5
                  | channel.indirect << 6 | channel.direction << 7);
    set(base + 0x1, channel.targetAddress);
    set(base + 0x4, channel.sourceBank);
    set(base + 0x7, channel.indirectBank);
    set(base + 0xb, channel.unknown);
    set(base + 0xf, channel.unknown);
    //A general transfer walks the source address on and counts the size down
    //to nought; a channel HDMA is using does neither (indirect HDMA reloads
    //the size, which is its indirect address). So these are the stores only
    //on a channel HDMA owns, and only the size of a direct one.
    if(channel.hdmaEnable) {
      set(base + 0x2, channel.sourceAddress >> 0 & 0xff);
      set(base + 0x3, channel.sourceAddress >> 8 & 0xff);
      if(!channel.indirect) {
        set(base + 0x5, channel.transferSize >> 0 & 0xff);
        set(base + 0x6, channel.transferSize >> 8 & 0xff);
      }
    }
    //Every register a channel pointed from A to B writes holds the channel's
    //byte, not a store, whenever the channel wrote it last.
    if(channel.direction == 0) {
      static const uint8 offsets[8][4] = {
        {0, 0, 0, 0}, {0, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
      };
      for(uint k : range(4)) {
        uint b = (channel.targetAddress + offsets[channel.transferMode][k]) & 0xff;
        known[b] = 0;
      }
    }
  }
}

}
