#include "sdlog_ring.h"

// Bounded neighbour probing. A torn write damages whole sectors, so a bad probe
// is likely to have bad neighbours; stepping more than a sector's worth away
// turns a targeted search into a linear scan without finding anything new.
#define SDLOG_NEIGHBOUR_LIMIT (SDLOG_RECS_PER_SECTOR * 2)

enum SlotState { SLOT_BLANK, SLOT_CORRUPT, SLOT_VALID, SLOT_IO_ERROR };

bool sdlog_ring_slot_matches(const SdlogRecord &rec, uint32_t index, uint32_t capacity)
{
  return capacity != 0 && (rec.seq % capacity) == index;
}

static SlotState read_slot(SdlogSlotReader read, void *ctx, uint32_t index,
                           uint32_t capacity, SdlogRecord &rec)
{
  uint8_t raw[SDLOG_RECORD_BYTES];
  if(!read(ctx, index, raw)) {
    return SLOT_IO_ERROR;
  }
  if(sdlog_is_blank(raw)) {
    return SLOT_BLANK;
  }
  if(!sdlog_decode(raw, rec)) {
    return SLOT_CORRUPT;
  }
  if(!sdlog_ring_slot_matches(rec, index, capacity)) {
    // Intact bytes in the wrong place: not this ring's record.
    return SLOT_CORRUPT;
  }
  return SLOT_VALID;
}

// Read `index`, and if it is unusable walk outwards for a slot that is not, so a
// binary-search probe landing in a torn sector does not abort the search. Sets
// `found_index` to whatever was actually used.
static SlotState probe(SdlogSlotReader read, void *ctx, uint32_t index, uint32_t capacity,
                       SdlogRecord &rec, uint32_t &found_index, SdlogRingScan &scan)
{
  for(uint32_t step = 0; step <= SDLOG_NEIGHBOUR_LIMIT; step++)
  {
    // Try index+step then index-step, staying inside the ring.
    for(int dir = 0; dir < (step == 0 ? 1 : 2); dir++)
    {
      uint32_t at = (dir == 0) ? index + step : index - step;
      if(step > index && dir == 1) { continue; }
      if(at >= capacity) { continue; }

      scan.probes++;
      SlotState st = read_slot(read, ctx, at, capacity, rec);
      if(st == SLOT_IO_ERROR) {
        return SLOT_IO_ERROR;
      }
      if(st == SLOT_CORRUPT) {
        scan.corrupt_seen++;
        continue;
      }
      found_index = at;
      return st;   // BLANK or VALID
    }
  }
  return SLOT_CORRUPT;
}

bool sdlog_ring_scan(SdlogSlotReader read, void *ctx, uint32_t capacity, SdlogRingScan &out)
{
  out.any_records  = false;
  out.newest_seq   = 0;
  out.newest_index = 0;
  out.next_seq     = 0;
  out.next_index   = 0;
  out.probes       = 0;
  out.corrupt_seen = 0;

  if(capacity == 0) {
    return false;
  }

  // Slot 0 anchors the search: it is written first, so if it is blank the ring
  // has never been used, and if it holds a record the ring is either partly
  // filled (sequences ascend to a blank tail) or wrapped (they ascend, drop by
  // one capacity at the head, then ascend again).
  SdlogRecord first;
  uint32_t first_index = 0;
  SlotState st = probe(read, ctx, 0, capacity, first, first_index, out);

  if(st == SLOT_IO_ERROR) {
    return false;
  }
  if(st == SLOT_BLANK && first_index == 0) {
    // Nothing written at all. Start at the beginning.
    return true;
  }
  if(st != SLOT_VALID) {
    // Slot 0 and its whole neighbourhood are damaged. Rather than guess, restart
    // the ring: the alternative is appending at an unknown offset and
    // interleaving new records with old ones at unpredictable positions.
    return true;
  }

  // Binary search for the boundary: the first slot whose record is NOT part of
  // the same ascending run as `first`. Everything below the boundary is newer
  // than or equal to first; the boundary itself is where the sequence drops (a
  // wrapped ring) or runs out (a partly-filled one).
  uint32_t lo = first_index;
  uint32_t hi = capacity;      // exclusive; boundary is in (lo, hi]
  SdlogRecord best = first;
  uint32_t best_index = first_index;

  while(lo + 1 < hi)
  {
    uint32_t mid = lo + (hi - lo) / 2;

    SdlogRecord rec;
    uint32_t at = mid;
    SlotState mst = probe(read, ctx, mid, capacity, rec, at, out);

    if(mst == SLOT_IO_ERROR) {
      return false;
    }

    // Narrowing is always done on `mid`, never on the slot the probe actually
    // landed on. A probe may step outside (lo, hi) to get clear of a torn sector,
    // and narrowing on that index can move a bound the wrong way -- hi jumping
    // forwards past its old value -- which turns the search into an oscillation
    // that never terminates. Narrowing on mid guarantees the window shrinks by at
    // least one slot every iteration.
    //
    // The cost is that a neighbour's verdict is attributed to mid, so a torn
    // sector straddling the boundary can leave the head under-estimated by up to
    // the neighbour limit. That is safe: `seq % capacity == index` still holds, so
    // resuming slightly early overwrites a few of the oldest records rather than
    // corrupting anything. `best` still records the true maximum of everything
    // actually read.
    if(mst == SLOT_VALID && sdlog_seq_newer(rec.seq, best.seq)) {
      best = rec;
      best_index = at;
    }

    if(mst == SLOT_VALID && sdlog_seq_newer(rec.seq, first.seq)) {
      // Still in the run that contains slot 0; the head is further on.
      lo = mid;
    } else {
      // Blank tail, damage we could not step out of, or a sequence that has
      // dropped -- in every case the head is at or below mid.
      hi = mid;
    }
  }

  out.any_records  = true;
  out.newest_seq   = best.seq;
  out.newest_index = best_index;
  out.next_seq     = best.seq + 1;
  out.next_index   = out.next_seq % capacity;
  return true;
}
