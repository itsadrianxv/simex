#pragma once

#include "common/lf_queue.h"
#include "exchange/messages.h"
#include "order_server/fifo_sequencer.h"

namespace simex::exchange {

/// In-process order-server seam for the deterministic harness.
/// TCP/epoll transport can be attached later without changing the sequencer.
class OrderServer final {
 public:
  explicit OrderServer(simex::common::LFQueue<ClientRequest> *outgoing_requests)
      : sequencer_(outgoing_requests) {}

  auto receive(const ClientRequest &request) -> bool {
    return sequencer_.addClientRequest(request);
  }

  auto flush() -> bool { return sequencer_.sequenceAndPublish(); }

  [[nodiscard]] auto pendingSize() const noexcept -> std::size_t {
    return sequencer_.pendingSize();
  }

 private:
  FIFOSequencer sequencer_;
};

}  // namespace simex::exchange
