#include "crash_unwind.h"

#include <string.h>

namespace {

// XtExcFrame field offsets (xtensa_context.h).
const uint32_t STK_PC = 4;
const uint32_t STK_A0 = 12;
const uint32_t STK_A1 = 16;
const uint32_t STK_EXCCAUSE = 80;
const uint32_t STK_MIN = STK_EXCCAUSE + 4;

const uint32_t EXCCAUSE_INSTR_PROHIBITED = 20;

// Elf32 offsets.
const size_t EH_PHOFF = 28, EH_PHENTSIZE = 42, EH_PHNUM = 44, EH_SIZE = 52;
const size_t PH_TYPE = 0, PH_OFFSET = 4, PH_VADDR = 8, PH_FILESZ = 16, PH_SIZE = 32;
const uint32_t PT_LOAD = 1;

// The image is mapped flash and its fields are not guaranteed aligned.
uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

// The IDF's esp_stack_ptr_is_sane also range-checks DRAM; the walk below does
// better than that by refusing any sp outside the dumped segment before
// reading through it.
bool sp_sane(uint32_t sp) { return 0 != sp && 0 == (sp & 0xf); }

} // namespace

uint32_t crash_unwind_process_pc(uint32_t pc)
{
  if(pc & 0x80000000) {
    pc = (pc & 0x3fffffff) | 0x40000000;
  }
  return pc - 3;
}

bool crash_unwind_find_stack(const uint8_t *elf, size_t len, uint32_t tcb,
                             const uint8_t **stack, uint32_t *vaddr,
                             uint32_t *size)
{
  if(!elf || len < EH_SIZE || 0 != memcmp(elf, "\x7f" "ELF", 4)) {
    return false;
  }
  uint32_t phoff = rd32(elf + EH_PHOFF);
  uint16_t phentsize = rd16(elf + EH_PHENTSIZE);
  uint16_t phnum = rd16(elf + EH_PHNUM);
  if(phentsize != PH_SIZE || phoff > len || (size_t)phnum * PH_SIZE > len - phoff) {
    return false;
  }

  // Same assumption as the IDF's own summary: a task's stack segment is the
  // next LOAD segment after its TCB.
  bool next = false;
  for(uint16_t i = 0; i < phnum; i++) {
    const uint8_t *ph = elf + phoff + (size_t)i * PH_SIZE;
    if(PT_LOAD != rd32(ph + PH_TYPE)) {
      continue;
    }
    uint32_t va = rd32(ph + PH_VADDR);
    if(next) {
      uint32_t off = rd32(ph + PH_OFFSET);
      uint32_t sz = rd32(ph + PH_FILESZ);
      if(off > len || sz > len - off) {
        return false;
      }
      *stack = elf + off;
      *vaddr = va;
      *size = sz;
      return true;
    }
    next = (va == tcb);
  }
  return false;
}

size_t crash_unwind_xtensa(const uint8_t *stack, uint32_t vaddr, uint32_t size,
                           crash_pc_ok_fn pc_ok, uint32_t *out, size_t max,
                           bool *corrupted)
{
  *corrupted = true;
  if(!stack || size < STK_MIN || 0 == max) {
    return 0;
  }

  uint32_t pc = rd32(stack + STK_PC);
  uint32_t sp = rd32(stack + STK_A1);
  uint32_t next_pc = rd32(stack + STK_A0);

  // A jump to a bad address faults on the fetch, so the first pc is garbage
  // by definition there and the frames above it are still good.
  bool bad = !(sp_sane(sp) &&
               (pc_ok(crash_unwind_process_pc(pc)) ||
                EXCCAUSE_INSTR_PROHIBITED == rd32(stack + STK_EXCCAUSE)));

  size_t n = 0;
  out[n++] = crash_unwind_process_pc(pc);

  while(!bad && n < max && 0 != next_pc) {
    // The base save area is the 16 bytes under sp; all of it must be inside
    // the dumped segment before anything is read through it.
    if(sp < vaddr + 16 || sp > vaddr + size) {
      bad = true;
      break;
    }
    const uint8_t *save = stack + (sp - vaddr) - 16;
    pc = next_pc;
    next_pc = rd32(save);
    sp = rd32(save + 4);
    if(!(sp_sane(sp) && pc_ok(crash_unwind_process_pc(pc)))) {
      bad = true;
      break;
    }
    out[n++] = crash_unwind_process_pc(pc);
  }

  *corrupted = bad;
  return n;
}

bool crash_unwind_wdt_task(const char *reason, const char *crashed,
                           char *out, size_t outLen)
{
  static const char WDT[] = "Task watchdog";
  if(!reason || 0 != strncmp(reason, WDT, sizeof(WDT) - 1) || !out || 0 == outLen) {
    return false;
  }
  // The reason string is truncated to fit the caller's buffer, so a line is
  // used only when its " (CPU" survived: a cut-off name is not a name.
  for(const char *line = strstr(reason, "\n - "); line; line = strstr(line + 1, "\n - ")) {
    const char *name = line + 4;
    const char *end = strstr(name, " (CPU");
    const char *nl = strchr(name, '\n');
    if(!end || (nl && nl < end)) {
      continue;
    }
    size_t n = end - name;
    if(0 == n || n >= outLen) {
      continue;
    }
    if(0 == strncmp(name, "IDLE", 4) ||
       (crashed && strlen(crashed) == n && 0 == strncmp(name, crashed, n))) {
      continue;
    }
    memcpy(out, name, n);
    out[n] = '\0';
    return true;
  }
  return false;
}

bool crash_unwind_find_tcb(const uint8_t *elf, size_t len, uint32_t knownTcb,
                           const char *knownName, const char *name,
                           uint32_t *tcb)
{
  if(!elf || len < EH_SIZE || 0 != memcmp(elf, "\x7f" "ELF", 4) ||
     !knownName || !name || '\0' == knownName[0] || '\0' == name[0]) {
    return false;
  }
  uint32_t phoff = rd32(elf + EH_PHOFF);
  uint16_t phentsize = rd16(elf + EH_PHENTSIZE);
  uint16_t phnum = rd16(elf + EH_PHNUM);
  if(phentsize != PH_SIZE || phoff > len || (size_t)phnum * PH_SIZE > len - phoff) {
    return false;
  }

  // Bounds-checked data of LOAD segment i, or null.
  auto seg = [&](uint16_t i, uint32_t *va, uint32_t *sz) -> const uint8_t * {
    const uint8_t *ph = elf + phoff + (size_t)i * PH_SIZE;
    if(PT_LOAD != rd32(ph + PH_TYPE)) {
      return nullptr;
    }
    uint32_t off = rd32(ph + PH_OFFSET);
    *sz = rd32(ph + PH_FILESZ);
    *va = rd32(ph + PH_VADDR);
    return (off > len || *sz > len - off) ? nullptr : elf + off;
  };

  // Where the name lives in a TCB: find the crashed task's, NUL included.
  size_t knownLen = strlen(knownName) + 1, nameLen = strlen(name) + 1;
  uint32_t tcbSize = 0;
  size_t nameOff = 0;
  bool found = false;
  for(uint16_t i = 0; i < phnum && !found; i++) {
    uint32_t va, sz;
    const uint8_t *d = seg(i, &va, &sz);
    if(!d || va != knownTcb) {
      continue;
    }
    for(size_t o = 0; o + knownLen <= sz; o++) {
      if(0 == memcmp(d + o, knownName, knownLen)) {
        tcbSize = sz;
        nameOff = o;
        found = true;
        break;
      }
    }
    if(!found) {
      return false;
    }
  }
  if(!found || nameOff + nameLen > tcbSize) {
    return false;
  }

  for(uint16_t i = 0; i < phnum; i++) {
    uint32_t va, sz;
    const uint8_t *d = seg(i, &va, &sz);
    if(d && sz == tcbSize && 0 == memcmp(d + nameOff, name, nameLen)) {
      *tcb = va;
      return true;
    }
  }
  return false;
}

bool crash_unwind_extends(const uint32_t *deep, size_t depth,
                          const uint32_t *idf, uint32_t idfDepth,
                          uint32_t idfCap)
{
  if(idfDepth > idfCap) {
    idfDepth = idfCap;
  }
  if(depth <= idfDepth) {
    return false;
  }
  return 0 == memcmp(deep, idf, idfDepth * sizeof(uint32_t));
}
