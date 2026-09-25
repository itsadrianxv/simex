#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "common/lf_queue.h"
#include "exchange/messages.h"

namespace Exchange {

/// Collects a batch of requests and publishes them in rx_time order.
class FIFOSequencer final {
 public:
  FIFOSequencer(Common::LFQueue<ClientRequest> *outgoing,
                std::size_t pending_capacity = 1024)
      : outgoing_(outgoing), pending_capacity_(pending_capacity) {
    if (outgoing_ == nullptr || pending_capacity_ == 0) {
      throw std::invalid_argument("FIFOSequencer requires a queue and capacity");
    }
  }

  auto addClientRequest(const ClientRequest &request) -> bool {
    if (pending_.size() >= pending_capacity_) return false;
    pending_.push_back(request);
    return true;
  }

  auto sequenceAndPublish() -> bool {
    if (outgoing_->capacity() - outgoing_->size() < pending_.size()) {
      return false;
    }
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const auto &left, const auto &right) {
                       return left.rx_time < right.rx_time;
                     });
    for (const auto &request : pending_) {
      if (!outgoing_->tryPush(request)) return false;
    }
    pending_.clear();
    return true;
  }

  [[nodiscard]] auto pendingSize() const noexcept -> std::size_t {
    return pending_.size();
  }

 private:
  Common::LFQueue<ClientRequest> *outgoing_ = nullptr;
  std::size_t pending_capacity_ = 0;
  std::vector<ClientRequest> pending_;
};

}  // namespace Exchange
