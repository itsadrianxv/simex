#pragma once

#include <cstddef>
#include <atomic>
#include <stdexcept>
#include <optional>
#include <utility>
#include <vector>

namespace simex::common {

/// A bounded single-producer/single-consumer queue for the first vertical slice.
/// The queue reports full/empty explicitly and never overwrites unread data.
template <typename T>
class LFQueue final {
 public:
  explicit LFQueue(std::size_t capacity)
      : slots_(capacity), capacity_(capacity) {
    if (capacity == 0) throw std::invalid_argument("Queue capacity must be positive");
  }

  LFQueue(const LFQueue &) = delete;
  LFQueue(LFQueue &&) = delete;
  auto operator=(const LFQueue &) -> LFQueue & = delete;
  auto operator=(LFQueue &&) -> LFQueue & = delete;

  [[nodiscard]] auto capacity() const noexcept -> std::size_t { return capacity_; }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_.load(std::memory_order_acquire); }
  [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }
  [[nodiscard]] auto full() const noexcept -> bool { return size() == capacity_; }

  auto tryPush(const T &value) -> bool {
    if (full()) {
      return false;
    }
    slots_[write_index_] = value;
    write_index_ = (write_index_ + 1) % capacity_;
    size_.fetch_add(1, std::memory_order_release);
    return true;
  }

  auto tryPush(T &&value) -> bool {
    if (full()) {
      return false;
    }
    slots_[write_index_] = std::move(value);
    write_index_ = (write_index_ + 1) % capacity_;
    size_.fetch_add(1, std::memory_order_release);
    return true;
  }

  auto tryPop(T *value) -> bool {
    if (empty() || value == nullptr) {
      return false;
    }
    *value = std::move(*slots_[read_index_]);
    slots_[read_index_].reset();
    read_index_ = (read_index_ + 1) % capacity_;
    size_.fetch_sub(1, std::memory_order_release);
    return true;
  }

 private:
  std::vector<std::optional<T>> slots_;
  std::size_t capacity_ = 0;
  std::size_t read_index_ = 0;
  std::size_t write_index_ = 0;
  std::atomic<std::size_t> size_{0};
};

}  // namespace simex::common
