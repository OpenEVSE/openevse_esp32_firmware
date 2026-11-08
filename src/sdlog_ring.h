// Circular-log position recovery — pure logic, no filesystem.
//
// The ring is a fixed number of SDLOG_RECORD_BYTES slots, and a record with
// sequence `s` always lives at slot `s % capacity`. That invariant is what makes
// recovery cheap and self-checking:
//
//   - The newest record can be found with a binary search rather than a full
//     scan. A 32 MB ring is a million records; reading all of it at boot would
//     take tens of seconds, while ~20 probes take milliseconds.
//   - A record whose `seq % capacity` does not match the slot it was found in is
//     wrong no matter what its CRC says — a stale image, a card from another
//     unit, or a misaligned file. The CRC proves the bytes are intact; the
//     invariant proves they belong here.
//
// Slot states, in the order they are tested:
//   blank    never written (all 0x00 or all 0xFF) -> sorts below every record
//   corrupt  written but failing magic/CRC/invariant -> torn; probe a neighbour
//   valid    a record that belongs in this slot
#ifndef __SDLOG_RING_H
#define __SDLOG_RING_H

#include "sdlog_record.h"

// Reads one slot. Returns false if the read itself failed (card gone, I/O
// error), which is different from the slot being blank or corrupt.
typedef bool (*SdlogSlotReader)(void *ctx, uint32_t index, uint8_t out[SDLOG_RECORD_BYTES]);

struct SdlogRingScan {
  bool     any_records;    // false if the ring is entirely unwritten
  uint32_t newest_seq;     // sequence of the newest intact record
  uint32_t newest_index;   // slot it was found in
  uint32_t next_seq;       // sequence to give the next append
  uint32_t next_index;     // slot the next append goes to
  uint32_t probes;         // slot reads used, for the boot log
  uint32_t corrupt_seen;   // slots that were written but did not verify
};

// Locate the head of the ring. Returns false only if the reader failed in a way
// that leaves the position unknown; a ring full of damage still returns true,
// with corrupt_seen set, because appending after the newest intact record is the
// right recovery either way.
bool sdlog_ring_scan(SdlogSlotReader read, void *ctx, uint32_t capacity, SdlogRingScan &out);

// True if a decoded record actually belongs in `index`. Exposed so the store can
// apply the same check when reading back for queries.
bool sdlog_ring_slot_matches(const SdlogRecord &rec, uint32_t index, uint32_t capacity);

#endif // __SDLOG_RING_H
