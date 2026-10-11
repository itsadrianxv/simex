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

  [[nodiscard]] auto capacity() const noexcept -> std::size_t { return capacity_.load(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_.load(); }
  [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }
  [[nodiscard]] auto full() const noexcept -> bool { return size() == capacity_.load(); }

  auto tryPush(const T &value) -> bool {
    if (full()) {
      return false;
    }
    const std::size_t index = write_index_.load();
    slots_[index] = value;
    write_index_.store((index + 1) % capacity_.load());
    size_.fetch_add(1);
    return true;
  }

  auto tryPush(T &&value) -> bool {
    if (full()) {
      return false;
    }
    const std::size_t index = write_index_.load();
    slots_[index] = std::move(value);
    write_index_.store((index + 1) % capacity_.load());
    size_.fetch_add(1);
    return true;
  }

  auto tryPop(T *value) -> bool {
    if (empty() || value == nullptr) {
      return false;
    }
    const std::size_t index = read_index_.load();
    *value = std::move(*slots_[index]);
    slots_[index].reset();
    read_index_.store((index + 1) % capacity_.load());
    size_.fetch_sub(1);
    return true;
  }

 private:
  std::vector<std::optional<T>> slots_;
  std::atomic<std::size_t> capacity_{0};
  std::atomic<std::size_t> read_index_{0};
  std::atomic<std::size_t> write_index_{0};
  std::atomic<std::size_t> size_{0};
};

}  // namespace simex::common
