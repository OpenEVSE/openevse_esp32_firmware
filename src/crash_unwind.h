#ifndef _OPENEVSE_CRASH_UNWIND_H
#define _OPENEVSE_CRASH_UNWIND_H

// The IDF core-dump summary stops its Xtensa backtrace at 16 frames, which on
// a LittleFS watchdog is still inside littlefs: the caller -- the line worth
// fixing -- is cut off. These walk the crashed task's stack straight out of
// the stored ELF image, the same way the IDF does, for as many frames as the
// caller asks for.
//
// Only return addresses come out. They point into the published firmware, so
// they say nothing about the charger; the stack words themselves are a copy of
// RAM and never leave (spec §15 R1).
//
// Pure: no IDF calls, so the host tests can drive them with synthetic images.

#include <stddef.h>
#include <stdint.h>

// Is this (already processed) address executable code? The device passes
// esp_ptr_executable; tests pass a range check.
typedef bool (*crash_pc_ok_fn)(uint32_t pc);

// The return address as written by a windowed call, with the window-increment
// bits replaced and moved back onto the call instruction. Identical to the
// IDF's esp_cpu_process_stack_pc.
uint32_t crash_unwind_process_pc(uint32_t pc);

// Find the crashed task's stack segment in an ELF core dump. The IDF writes
// each task as a TCB segment followed by its stack segment, and the summary's
// exc_tcb names the crashed one. `elf` is the image after core_dump_header_t.
// False if the image is malformed or the TCB is not in it.
bool crash_unwind_find_stack(const uint8_t *elf, size_t len, uint32_t tcb,
                             const uint8_t **stack, uint32_t *vaddr,
                             uint32_t *size);

// Walk the stack segment, which begins with the XtExcFrame the panic handler
// saved. Writes at most `max` addresses to `out` and returns how many; sets
// *corrupted when the chain broke rather than ended.
size_t crash_unwind_xtensa(const uint8_t *stack, uint32_t vaddr, uint32_t size,
                           crash_pc_ok_fn pc_ok, uint32_t *out, size_t max,
                           bool *corrupted);

#endif // _OPENEVSE_CRASH_UNWIND_H
