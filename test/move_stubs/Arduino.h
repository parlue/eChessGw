#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <deque>
#include <vector>
#include <string>
using std::min;
extern uint32_t fakeNow;
inline uint32_t millis() { return fakeNow; }
inline void delay(unsigned ms) { fakeNow+=ms; }
struct FakeQueue { size_t size,capacity; std::deque<std::vector<uint8_t>> packets; };
using QueueHandle_t = FakeQueue*;
inline QueueHandle_t xQueueCreate(size_t capacity,size_t size) { return new FakeQueue{size,capacity,{}}; }
inline void xQueueReset(QueueHandle_t q) { q->packets.clear(); }
inline int xQueueSend(QueueHandle_t q,const void* p,int) {
  if (q->packets.size()==q->capacity) return 0;
  auto b=static_cast<const uint8_t*>(p); q->packets.emplace_back(b,b+q->size); return 1;
}
inline int xQueueReceive(QueueHandle_t q,void* p,int) {
  if (q->packets.empty()) return 0;
  memcpy(p,q->packets.front().data(),q->size); q->packets.pop_front(); return 1;
}
constexpr int pdTRUE=1;
struct FakeLog {
  template<class... T> void printf(const char*,T...) {}
  void println(const char*) {}
};
extern FakeLog Serial;
