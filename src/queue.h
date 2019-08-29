#ifndef __QUEUE_H
#define __QUEUE_H

template <class T> class Queue {
private:
  T *values;
  size_t size;
  size_t head;
  size_t tail;

  size_t nextSlot(size_t i) { return ((i + 1) % size); }

public:
  Queue(T *values, size_t size)
      : values(values), size(size), head(0), tail(0) {}

  bool push(T item) {
    if (!full()) {
      values[head] = item;

      head = nextSlot(head);
      return true;
    }

    return false;
  }

  bool pop(T &item) {
    if (!empty()) {
      item = values[tail];
      tail = nextSlot(tail);
      return true;
    }

    return false;
  }

  bool full() { return (nextSlot(head) == tail); }

  bool empty() { return (head == tail); }

  size_t used() { return head >= tail ? head - tail : (size - tail) + head; }

  size_t free() { return size - used(); }
  void purge() {
    head = 0;
    tail = 0;
  }
};

#endif // __QUEUE_H
