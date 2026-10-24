// On-card energy record format — pure logic, no Arduino, no filesystem.
//
// This is the format the board design doc's §7 asks for: fixed-size records in a
// circular log, each carrying a sequence number and a CRC, so that a record torn
// by power loss fails its CRC and is skipped rather than being read back as
// plausible data.
//
// The card path needs this in a way the internal-flash path does not. There is no
// power-fail warning on this board and hold-up is ~400 us against an SD card's
// 250 ms worst-case busy, so any write can be cut in flight. That is not fixable
// in hardware; it has to be survivable in the format.
//
// Layout (32 bytes, little-endian):
//
//   0   4  magic     SDLOG_RECORD_MAGIC, the scan sync word
//   4   4  seq       monotonic, never reset; ring position is seq % capacity
//   8   4  timestamp unix seconds, UTC
//   12 14  cols[7]   int16 columns, same order and scaling as tsdb_sample.h
//   26  2  reserved  zero; keeps the record 32 bytes and 4-byte aligned
//   28  4  crc32     IEEE 802.3 over bytes 0..27
//
// 32 bytes is chosen so that a 512-byte SD sector holds exactly 16 records and no
// record ever straddles a sector boundary. A torn write damages whole sectors, so
// every record in the damaged region fails its own CRC independently and the scan
// can step past it without losing alignment.
//
// The column order is deliberately the same as the internal tsdb's, so the two
// stores describe the same sample and neither has to translate.
#ifndef __SDLOG_RECORD_H
#define __SDLOG_RECORD_H

#include <stdint.h>
#include <stddef.h>

#define SDLOG_RECORD_MAGIC   0x5356454FUL   // reads as "OEVS" in a hex dump
#define SDLOG_RECORD_BYTES   32
#define SDLOG_RECORD_COLS    7
#define SDLOG_SECTOR_BYTES   512
#define SDLOG_RECS_PER_SECTOR (SDLOG_SECTOR_BYTES / SDLOG_RECORD_BYTES)   // 16

struct SdlogRecord {
  uint32_t seq;
  uint32_t timestamp;
  int16_t  cols[SDLOG_RECORD_COLS];
};

// IEEE 802.3 CRC-32, exposed for the store's own integrity checks.
uint32_t sdlog_crc32(const uint8_t *data, size_t len);

// Serialise into exactly SDLOG_RECORD_BYTES.
void sdlog_encode(const SdlogRecord &rec, uint8_t out[SDLOG_RECORD_BYTES]);

// Parse and verify. Returns false on a bad magic or a failed CRC — i.e. a slot
// that was never written, or one torn mid-write.
bool sdlog_decode(const uint8_t in[SDLOG_RECORD_BYTES], SdlogRecord &out);

// True if `in` is a slot that has never been written (all 0x00 or all 0xFF).
// Distinguishing "empty" from "corrupt" matters on first run, when the whole ring
// is unwritten and every slot would otherwise look like damage.
bool sdlog_is_blank(const uint8_t in[SDLOG_RECORD_BYTES]);

// Sequence comparison that tolerates the uint32 wrap. Returns true if `a` is
// newer than `b`. At 1/min a wrap takes ~8000 years, so this is belt-and-braces
// rather than a live concern -- but a plain `a > b` would silently pick the
// oldest record as newest for the one comparison that spans the wrap.
bool sdlog_seq_newer(uint32_t a, uint32_t b);

#endif // __SDLOG_RECORD_H
