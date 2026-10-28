#include "sdlog_record.h"

#include <string.h>

uint32_t sdlog_crc32(const uint8_t *data, size_t len)
{
  // Bitwise reflected CRC-32; no 1 KB table, and at 32 bytes per record once a
  // minute the cost is irrelevant next to the SD transaction it protects.
  uint32_t crc = 0xFFFFFFFFUL;
  for(size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for(int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1)));
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

static void put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void sdlog_encode(const SdlogRecord &rec, uint8_t out[SDLOG_RECORD_BYTES])
{
  memset(out, 0, SDLOG_RECORD_BYTES);
  put_u32(out + 0, SDLOG_RECORD_MAGIC);
  put_u32(out + 4, rec.seq);
  put_u32(out + 8, rec.timestamp);
  for(int i = 0; i < SDLOG_RECORD_COLS; i++)
  {
    uint16_t v = (uint16_t)rec.cols[i];
    out[12 + i * 2]     = (uint8_t)(v);
    out[12 + i * 2 + 1] = (uint8_t)(v >> 8);
  }
  // bytes 26..27 stay zero (reserved)
  put_u32(out + 28, sdlog_crc32(out, SDLOG_RECORD_BYTES - 4));
}

bool sdlog_decode(const uint8_t in[SDLOG_RECORD_BYTES], SdlogRecord &out)
{
  if(get_u32(in) != SDLOG_RECORD_MAGIC) {
    return false;
  }
  if(get_u32(in + 28) != sdlog_crc32(in, SDLOG_RECORD_BYTES - 4)) {
    return false;
  }

  out.seq       = get_u32(in + 4);
  out.timestamp = get_u32(in + 8);
  for(int i = 0; i < SDLOG_RECORD_COLS; i++)
  {
    uint16_t v = (uint16_t)in[12 + i * 2] | ((uint16_t)in[12 + i * 2 + 1] << 8);
    out.cols[i] = (int16_t)v;
  }
  return true;
}

bool sdlog_is_blank(const uint8_t in[SDLOG_RECORD_BYTES])
{
  uint8_t all_and = 0xFF;
  uint8_t all_or  = 0x00;
  for(int i = 0; i < SDLOG_RECORD_BYTES; i++)
  {
    all_and &= in[i];
    all_or  |= in[i];
  }
  return all_or == 0x00 || all_and == 0xFF;
}

bool sdlog_seq_newer(uint32_t a, uint32_t b)
{
  // Signed difference: correct across the wrap for any pair less than 2^31 apart,
  // which every pair in a ring smaller than that necessarily is.
  return (int32_t)(a - b) > 0;
}
