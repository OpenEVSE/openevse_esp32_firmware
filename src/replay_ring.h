#ifndef REPLAY_RING_H
#define REPLAY_RING_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// Fixed-capacity ring that keeps the newest `capacity` entries. The storage is
// allocated once in begin() (so a build that never records costs nothing) and
// never resized. Not thread safe: written and read from the main loop only.
template <typename T>
class ReplayRing
{
  private:
    T *_items = nullptr;
    size_t _capacity = 0;
    size_t _head = 0;      // next slot to write
    size_t _count = 0;
    uint32_t _overwritten = 0;

  public:
    ~ReplayRing() { free(_items); }

    bool begin(size_t capacity) {
      if(_items) {
        return true;
      }
      _items = (T *)calloc(capacity, sizeof(T));
      if(!_items) {
        return false;
      }
      _capacity = capacity;
      return true;
    }

    bool ready() const { return nullptr != _items; }
    size_t capacity() const { return _capacity; }
    size_t size() const { return _count; }
    // Entries pushed out by newer ones since begin().
    uint32_t overwritten() const { return _overwritten; }

    void push(const T &item) {
      if(!_items) {
        return;
      }
      _items[_head] = item;
      _head = (_head + 1) % _capacity;
      if(_count < _capacity) {
        _count++;
      } else {
        _overwritten++;
      }
    }

    // i = 0 is the oldest entry still held.
    const T &at(size_t i) const {
      size_t start = (_head + _capacity - _count) % _capacity;
      return _items[(start + i) % _capacity];
    }

    const T *newest() const {
      return _count ? &at(_count - 1) : nullptr;
    }

    void clear() {
      _head = 0;
      _count = 0;
      _overwritten = 0;
    }
};

#endif // REPLAY_RING_H
