#include "participant_client/participant_client.h"

#include "transport/protocol.h"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <variant>

int main() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
  assert(::listen(listener, 1) == 0);
  socklen_t address_size = sizeof(address);
  assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);
  const auto tcp_port = ntohs(address.sin_port);
  constexpr std::uint16_t udp_port = 39001;

  std::thread server([&] {
    const int accepted = ::accept(listener, nullptr, nullptr);
    assert(accepted >= 0);
    simex::transport::WireRequest request{};
    assert(::recv(accepted, &request, sizeof(request), MSG_WAITALL) == static_cast<ssize_t>(sizeof(request)));
    simex::transport::WireResponse response{};
    response.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::RESPONSE);
    response.header.payload_size = sizeof(response.response);
    response.response.type = simex::common::ResponseType::ACCEPTED;
    response.response.client_id = request.request.client_id;
    response.response.ticker_id = request.request.ticker_id;
    response.response.client_order_id = request.request.client_order_id;
    assert(::send(accepted, &response, sizeof(response), MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(response)));

    const int udp = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(udp >= 0);
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    destination.sin_port = htons(udp_port);
    simex::transport::WireUpdate update{};
    update.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::UPDATE);
    update.header.payload_size = sizeof(update.update);
    update.header.sequence = 1;
    update.update.type = simex::common::MarketUpdateType::ADD;
    assert(::sendto(udp, &update, sizeof(update), 0,
                    reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) ==
           static_cast<ssize_t>(sizeof(update)));
    ::close(udp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ::close(accepted);
  });

  simex::participant::ClientConfig config;
  config.tcp.port = tcp_port;
  config.udp.port = udp_port;
  config.client_id = 7;
  config.queue_capacity = 8;
  simex::participant::ParticipantClient client(config);
  assert(client.connect());
  const auto result = client.submit(
      simex::participant::RequestBuilder::limit(1, simex::common::Side::BUY, 1000, 1));
  assert(result.accepted_for_send);
  auto response = client.wait(std::chrono::seconds(1));
  assert(response.has_value());
  assert(std::holds_alternative<simex::exchange::ClientResponse>(*response));
  assert(std::get<simex::exchange::ClientResponse>(*response).type == simex::common::ResponseType::ACCEPTED);
  auto update = client.wait(std::chrono::seconds(1));
  assert(update.has_value());
  assert(std::holds_alternative<simex::exchange::MarketUpdate>(*update));
  client.close();
  server.join();
  ::close(listener);
  return 0;
}
