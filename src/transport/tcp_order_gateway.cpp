#include "transport/tcp_order_gateway.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "transport/protocol.h"

namespace simex::transport {
namespace {
constexpr int kPollTimeoutMs = 10;
constexpr auto kResponseTimeout = std::chrono::seconds(5);
}  // namespace

TcpOrderGateway::~TcpOrderGateway() { stop(); }

auto TcpOrderGateway::start(RequestHandler handler, ResponseSource source) -> bool {
  if (thread_.joinable()) return false;
  request_handler_ = std::move(handler);
  response_source_ = std::move(source);
  event_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (event_fd_ < 0) return false;
  if (!server_.start(endpoint_.port)) {
    ::close(event_fd_);
    event_fd_ = -1;
    return false;
  }
  if (!server_.registerExternalFd(event_fd_, EPOLLIN | EPOLLET, [this](std::uint32_t) {
        std::uint64_t value = 0;
        const auto count = ::read(event_fd_, &value, sizeof(value));
        (void)count;
      })) {
    server_.stop();
    ::close(event_fd_);
    event_fd_ = -1;
    return false;
  }
  server_.recv_callback_ = [this](TCPSocket *socket, simex::common::Nanos) { handleRecv(socket); };
  server_.accept_policy_ = [this](TCPSocket *socket) { return admitSession(socket); };
  server_.disconnect_callback_ = [this](TCPSocket *socket) { handleDisconnect(socket); };
  session_ = nullptr;
  session_seen_ = false;
  expected_sequence_ = 0;
  stopping_.store(false);
  healthy_.store(true);
  peer_closed_.store(false);
  thread_ = std::jthread([this] { run(); });
  return true;
}

auto TcpOrderGateway::stop() -> void {
  stopping_.store(true);
  if (event_fd_ >= 0) {
    const std::uint64_t wake = 1;
    const auto wake_count = ::write(event_fd_, &wake, sizeof(wake));
    (void)wake_count;
  }
  if (thread_.joinable()) thread_.join();
  server_.stop();
  if (event_fd_ >= 0) {
    ::close(event_fd_);
    event_fd_ = -1;
  }
}

auto TcpOrderGateway::admitSession(TCPSocket *socket) -> bool {
  if (session_ != nullptr || session_seen_) return false;
  session_ = socket;
  session_seen_ = true;
  return true;
}

void TcpOrderGateway::handleRecv(TCPSocket *socket) {
  std::size_t consumed = 0;
  while (socket->inboundSize() - consumed >= sizeof(WireRequest)) {
    WireRequest request{};
    std::memcpy(&request, socket->inboundData() + consumed, sizeof(request));
    const bool valid = request.header.version == kProtocolVersion &&
                       request.header.reserved == 0 &&
                       request.header.kind == static_cast<std::uint8_t>(MessageKind::REQUEST) &&
                       request.header.payload_size == sizeof(request.request) &&
                       request.header.sequence == expected_sequence_ && request_handler_ &&
                       request_handler_(request.request, expected_sequence_);
    if (!valid) {
      healthy_.store(false);
      socket->markDead();
      return;
    }
    ++expected_sequence_;
    consumed += sizeof(request);
  }
  socket->consume(consumed);
}

void TcpOrderGateway::handleDisconnect(TCPSocket *socket) {
  if (socket != session_) return;
  session_ = nullptr;
  if (socket->peerClosed()) peer_closed_.store(true);
  healthy_.store(false);
}

void TcpOrderGateway::run() {
  WireResponse output{};
  bool pending_output = false;
  bool response_queued = false;
  auto last_progress = std::chrono::steady_clock::now();
  while (!stopping_.load() && healthy_.load()) {
    if (!server_.poll(kPollTimeoutMs)) {
      healthy_.store(false);
      break;
    }
    if (session_ != nullptr && !pending_output && response_source_) {
      simex::exchange::ClientResponse response;
      std::uint64_t sequence = 0;
      if (response_source_(&response, &sequence)) {
        output = {};
        output.header.kind = static_cast<std::uint8_t>(MessageKind::RESPONSE);
        output.header.payload_size = sizeof(output.response);
        output.header.sequence = sequence;
        output.response = response;
        pending_output = true;
        response_queued = false;
        last_progress = std::chrono::steady_clock::now();
      }
    }
    if (pending_output && !response_queued) {
      if (session_ == nullptr || !session_->send(&output, sizeof(output))) {
        healthy_.store(false);
        break;
      }
      response_queued = true;
    }
    server_.sendAndRecv();
    if (session_ != nullptr && pending_output) {
      if (session_->lastRound().sent_any) last_progress = std::chrono::steady_clock::now();
      if (session_->outboundPendingSize() == 0) {
        pending_output = false;
        response_queued = false;
      }
    }
    if (pending_output && std::chrono::steady_clock::now() - last_progress > kResponseTimeout) {
      healthy_.store(false);
      break;
    }
  }
}

}  // namespace simex::transport
