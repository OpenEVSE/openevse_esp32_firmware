// Host-side tests for the deep crash backtrace (crash_unwind.cpp).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string.h>
#include <string>
#include <vector>

#include "crash_unwind.h"

namespace {

const uint32_t VADDR = 0x3ffb0000;
const uint32_t SIZE = 0x800;

// XtExcFrame offsets (xtensa_context.h).
const uint32_t STK_PC = 4, STK_A0 = 12, STK_A1 = 16, STK_EXCCAUSE = 80;

bool code(uint32_t pc) { return pc >= 0x40000000 && pc < 0x40400000; }

struct Stack {
  std::vector<uint8_t> mem = std::vector<uint8_t>(SIZE, 0);
  void put(uint32_t addr, uint32_t v) { memcpy(&mem[addr - VADDR], &v, 4); }
};

// A return address as a CALL8 writes it: window increment in the top bits,
// pointing just past the call.
uint32_t ret(uint32_t callsite) { return 0x80000000 | ((callsite + 3) & 0x3fffffff); }

// Exception frame at the base, then `n` frames whose base-save areas chain
// upward. Frame i's caller is callsite 0x400d0000 + 0x100*(i+1).
Stack chain(int n) {
  Stack s;
  uint32_t sp = VADDR + 0x100;
  s.put(VADDR + STK_PC, 0x40081000);
  s.put(VADDR + STK_A0, ret(0x400d0100));
  s.put(VADDR + STK_A1, sp);
  for(int i = 0; i < n; i++) {
    uint32_t caller_sp = sp + 0x20;
    bool last = (i == n - 1);
    s.put(sp - 16, last ? 0 : ret(0x400d0000 + 0x100 * (i + 2)));
    s.put(sp - 12, caller_sp);
    sp = caller_sp;
  }
  return s;
}

} // namespace

TEST_CASE("process_pc matches the IDF") {
  CHECK(crash_unwind_process_pc(0x800d1237) == 0x400d1234);
  CHECK(crash_unwind_process_pc(0x400d1237) == 0x400d1234);
}

TEST_CASE("walks past the IDF's 16 frames") {
  Stack s = chain(30);
  uint32_t out[64];
  bool corrupted = true;
  size_t n = crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted);
  CHECK_FALSE(corrupted);
  REQUIRE(n == 31);   // the faulting pc plus 30 callers
  CHECK(out[0] == 0x40081000 - 3);
  for(int i = 1; i < 31; i++) {
    CHECK(out[i] == 0x400d0000u + 0x100u * i);
  }
}

TEST_CASE("stops at max without calling it corrupted") {
  Stack s = chain(30);
  uint32_t out[8];
  bool corrupted = true;
  CHECK(crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 8, &corrupted) == 8);
  CHECK_FALSE(corrupted);
}

TEST_CASE("a frame pointing outside the stack is corrupted, never read") {
  Stack s = chain(5);
  // Third frame's caller sp leaves the segment.
  uint32_t sp3 = VADDR + 0x100 + 0x20 * 2;
  s.put(sp3 - 12, 0x3fff0000);
  uint32_t out[64];
  bool corrupted = false;
  size_t n = crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted);
  // The pc from the last good save area still counts, as in the IDF; only
  // the walk past it stops.
  CHECK(corrupted);
  CHECK(n == 4);
}

TEST_CASE("an sp too close to the base to hold a save area is corrupted") {
  Stack s = chain(1);
  s.put(VADDR + STK_A1, VADDR + 8);
  uint32_t out[64];
  bool corrupted = false;
  CHECK(crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted) == 1);
  CHECK(corrupted);
}

TEST_CASE("a non-code return address is corrupted") {
  Stack s = chain(5);
  uint32_t sp1 = VADDR + 0x100 + 0x20;
  s.put(sp1 - 16, 0x3ffb1234);
  uint32_t out[64];
  bool corrupted = false;
  size_t n = crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted);
  CHECK(corrupted);
  CHECK(n == 3);
}

TEST_CASE("a bad first pc is tolerated only for InstrFetchProhibited") {
  Stack s = chain(3);
  s.put(VADDR + STK_PC, 0);
  uint32_t out[64];
  bool corrupted = false;
  CHECK(crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted) == 1);
  CHECK(corrupted);

  s.put(VADDR + STK_EXCCAUSE, 20);
  CHECK(crash_unwind_xtensa(s.mem.data(), VADDR, SIZE, code, out, 64, &corrupted) == 4);
  CHECK_FALSE(corrupted);
}

TEST_CASE("a segment too small for the exception frame yields nothing") {
  Stack s = chain(1);
  uint32_t out[4];
  bool corrupted = false;
  CHECK(crash_unwind_xtensa(s.mem.data(), VADDR, 40, code, out, 4, &corrupted) == 0);
  CHECK(corrupted);
}

// --- ELF segment lookup -----------------------------------------------------

namespace {

struct Elf {
  std::vector<uint8_t> img;
  void put32(size_t off, uint32_t v) { memcpy(&img[off], &v, 4); }
  void put16(size_t off, uint16_t v) { memcpy(&img[off], &v, 2); }
};

// 52-byte header, then phdrs (32 bytes each), then segment data.
Elf elf(const std::vector<std::pair<uint32_t, uint32_t>> &segs /* vaddr, size */,
        uint32_t type_override_index = 99) {
  Elf e;
  size_t phoff = 52, data = phoff + 32 * segs.size();
  size_t total = data;
  for(auto &s : segs) total += s.second;
  e.img.assign(total, 0);
  e.img[0] = 0x7f; e.img[1] = 'E'; e.img[2] = 'L'; e.img[3] = 'F';
  e.put32(28, phoff);
  e.put16(42, 32);
  e.put16(44, segs.size());
  for(size_t i = 0; i < segs.size(); i++) {
    size_t ph = phoff + 32 * i;
    e.put32(ph + 0, i == type_override_index ? 4 /* PT_NOTE */ : 1 /* PT_LOAD */);
    e.put32(ph + 4, data);
    e.put32(ph + 8, segs[i].first);
    e.put32(ph + 16, segs[i].second);
    e.put32(ph + 20, segs[i].second);
    e.img[data] = 0xa0 + i;   // marks which segment came back
    data += segs[i].second;
  }
  return e;
}

} // namespace

TEST_CASE("finds the segment after the crashed task's TCB") {
  Elf e = elf({{0x3ffc0000, 0x160}, {0x3ffb0000, 0x200}, {0x3ffd0000, 0x160}, {0x3ffe0000, 0x300}});
  const uint8_t *stack; uint32_t vaddr, size;
  REQUIRE(crash_unwind_find_stack(e.img.data(), e.img.size(), 0x3ffd0000, &stack, &vaddr, &size));
  CHECK(vaddr == 0x3ffe0000);
  CHECK(size == 0x300);
  CHECK(stack[0] == 0xa3);
}

TEST_CASE("skips non-LOAD segments between TCB and stack") {
  Elf e = elf({{0x3ffc0000, 0x160}, {0, 0x20}, {0x3ffb0000, 0x200}}, 1);
  const uint8_t *stack; uint32_t vaddr, size;
  REQUIRE(crash_unwind_find_stack(e.img.data(), e.img.size(), 0x3ffc0000, &stack, &vaddr, &size));
  CHECK(vaddr == 0x3ffb0000);
}

TEST_CASE("rejects a missing TCB, a bad magic and out-of-bounds segments") {
  Elf e = elf({{0x3ffc0000, 0x160}, {0x3ffb0000, 0x200}});
  const uint8_t *stack; uint32_t vaddr, size;
  CHECK_FALSE(crash_unwind_find_stack(e.img.data(), e.img.size(), 0x12345678, &stack, &vaddr, &size));
  // TCB is the last segment: no stack follows.
  CHECK_FALSE(crash_unwind_find_stack(e.img.data(), e.img.size(), 0x3ffb0000, &stack, &vaddr, &size));

  Elf bad = e;
  bad.img[1] = 'X';
  CHECK_FALSE(crash_unwind_find_stack(bad.img.data(), bad.img.size(), 0x3ffc0000, &stack, &vaddr, &size));

  Elf trunc = e;
  trunc.put32(52 + 32 + 16, 0x10000);   // stack filesz runs past the image
  CHECK_FALSE(crash_unwind_find_stack(trunc.img.data(), trunc.img.size(), 0x3ffc0000, &stack, &vaddr, &size));

  Elf phs = e;
  phs.put16(44, 5000);                  // phdr table runs past the image
  CHECK_FALSE(crash_unwind_find_stack(phs.img.data(), phs.img.size(), 0x3ffc0000, &stack, &vaddr, &size));

  CHECK_FALSE(crash_unwind_find_stack(e.img.data(), 20, 0x3ffc0000, &stack, &vaddr, &size));
}

// --- choosing between the IDF's list and the deep one ------------------------

TEST_CASE("the deep walk is used when it extends the IDF's list") {
  uint32_t idf[16], deep[30];
  for(uint32_t i = 0; i < 30; i++) deep[i] = 0x400d0000 + i;
  for(uint32_t i = 0; i < 16; i++) idf[i] = deep[i];
  CHECK(crash_unwind_extends(deep, 30, idf, 16, 16));
}

TEST_CASE("an IDF depth past its own array is clamped, not compared") {
  // The IDF's walk starts its count at 1 and then allows 16 more, so a long
  // stack reports depth 17 with nothing real in a 17th slot.
  uint32_t idf[17], deep[30];
  for(uint32_t i = 0; i < 30; i++) deep[i] = 0x400d0000 + i;
  for(uint32_t i = 0; i < 16; i++) idf[i] = deep[i];
  idf[16] = 17;   // what really sits there: the depth field
  CHECK(crash_unwind_extends(deep, 30, idf, 17, 16));
}

TEST_CASE("a deep walk that disagrees or is no longer is not used") {
  uint32_t idf[16], deep[30];
  for(uint32_t i = 0; i < 30; i++) deep[i] = 0x400d0000 + i;
  for(uint32_t i = 0; i < 16; i++) idf[i] = deep[i];
  CHECK_FALSE(crash_unwind_extends(deep, 16, idf, 16, 16));
  CHECK_FALSE(crash_unwind_extends(deep, 5, idf, 16, 16));
  idf[3] = 0x400e0000;
  CHECK_FALSE(crash_unwind_extends(deep, 30, idf, 16, 16));
}

// --- Task watchdog: the starved task, not the one the panic interrupted -----

namespace {

const char *WDT_REASON =
  "Task watchdog got triggered. The following tasks/users did not reset the "
  "watchdog in time:\n - loopTask (CPU 1)";

// Byte offset of segment i's data in an image built by elf().
size_t seg_data(const std::vector<std::pair<uint32_t, uint32_t>> &segs, size_t i) {
  size_t off = 52 + 32 * segs.size();
  for(size_t k = 0; k < i; k++) off += segs[k].second;
  return off;
}

// Four tasks, TCB then stack, each TCB with its name at 0x34 the way
// FreeRTOS lays out pcTaskName on the ESP32.
const std::vector<std::pair<uint32_t, uint32_t>> TASKS = {
  {0x3ffcdc90, 0x160}, {0x3ffcdaa0, 0x1a0},   // IDLE1
  {0x3ffd562c, 0x160}, {0x3ffd5310, 0x210},   // tiT
  {0x3ffd1950, 0x160}, {0x3ffd0fd0, 0x770},   // loopTask
};

Elf tasks(const char *third = "loopTask") {
  Elf e = elf(TASKS);
  const char *names[] = {"IDLE1", "tiT", third};
  for(size_t t = 0; t < 3; t++) {
    memcpy(&e.img[seg_data(TASKS, 2 * t) + 0x34], names[t], strlen(names[t]) + 1);
  }
  return e;
}

} // namespace

TEST_CASE("the starved task is the first one the reason lists") {
  char name[16];
  REQUIRE(crash_unwind_wdt_task(WDT_REASON, "IDLE1", name, sizeof(name)));
  CHECK(std::string(name) == "loopTask");
}

TEST_CASE("idle tasks and the crashed task itself are skipped") {
  char name[16];
  const char *r = "Task watchdog got triggered. The following tasks/users did not reset "
                  "the watchdog in time:\n - IDLE0 (CPU 0)\n - async_tcp (CPU 0)";
  REQUIRE(crash_unwind_wdt_task(r, "IDLE1", name, sizeof(name)));
  CHECK(std::string(name) == "async_tcp");
  // The panic landed on the starved task: its backtrace is already the bt.
  CHECK_FALSE(crash_unwind_wdt_task(WDT_REASON, "loopTask", name, sizeof(name)));
}

TEST_CASE("anything but a task watchdog names no task") {
  char name[16];
  CHECK_FALSE(crash_unwind_wdt_task("abort() was called at PC 0x4008323b on core 0",
                                    "tiT", name, sizeof(name)));
  CHECK_FALSE(crash_unwind_wdt_task(nullptr, "tiT", name, sizeof(name)));
  CHECK_FALSE(crash_unwind_wdt_task("Task watchdog got triggered.", "IDLE1", name, sizeof(name)));
  // A name that does not fit is not truncated into some other task's name.
  char tiny[4];
  CHECK_FALSE(crash_unwind_wdt_task(WDT_REASON, "IDLE1", tiny, sizeof(tiny)));
}

TEST_CASE("finds another task's TCB by name, learning the offset from the crashed one") {
  Elf e = tasks();
  uint32_t tcb = 0;
  REQUIRE(crash_unwind_find_tcb(e.img.data(), e.img.size(), 0x3ffcdc90, "IDLE1",
                                "loopTask", &tcb));
  CHECK(tcb == 0x3ffd1950);
  const uint8_t *stack; uint32_t vaddr, size;
  REQUIRE(crash_unwind_find_stack(e.img.data(), e.img.size(), tcb, &stack, &vaddr, &size));
  CHECK(vaddr == 0x3ffd0fd0);
}

TEST_CASE("a TCB is matched on the whole name, never a prefix") {
  Elf e = tasks("loopTask2");
  uint32_t tcb = 0;
  CHECK_FALSE(crash_unwind_find_tcb(e.img.data(), e.img.size(), 0x3ffcdc90, "IDLE1",
                                    "loopTask", &tcb));
}

TEST_CASE("no TCB is found when the crashed task's own name is not in its TCB") {
  Elf e = tasks();
  uint32_t tcb = 0;
  CHECK_FALSE(crash_unwind_find_tcb(e.img.data(), e.img.size(), 0x3ffcdc90, "IDLE0",
                                    "loopTask", &tcb));
  CHECK_FALSE(crash_unwind_find_tcb(e.img.data(), e.img.size(), 0x3ff00000, "IDLE1",
                                    "loopTask", &tcb));
}
