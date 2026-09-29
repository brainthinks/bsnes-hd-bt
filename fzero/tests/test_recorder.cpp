#include <cassert>
#include <cstdlib>
#include "ramtrace.hpp"
int main(int argc, char**) {
  setenv("BSNES_TRACE_RAM_AT", "00e8e4,00e9af", 1);
  unsetenv("BSNES_TRACE_RAM_PAIRED");
  RamTrace::EntryTrigger legacy;
  assert(legacy.enabled());
  assert(legacy.matches(0xe9af));
  assert(legacy.matches(0xe8e4));
  setenv("BSNES_TRACE_RAM_PAIRED", "1", 1);
  RamTrace::EntryTrigger paired;
  assert(paired.enabled());
  assert(!paired.matches(0xe9af));
  assert(paired.matches(0xe8e4));
  assert(!paired.matches(0x1234));
  if(argc > 1) { paired.matches(0xe8e4); return 2; }
  assert(paired.matches(0xe9af));
  assert(!paired.matches(0xe9af));
  assert(paired.matches(0xe8e4));
  assert(paired.matches(0xe9af));
}
