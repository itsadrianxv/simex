#include <cassert>
#include <cstdint>
#include "transport/exchange_service.h"
#include "transport/protocol.h"
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
static auto instrument() -> simex::common::InstrumentConfig { simex::common::InstrumentConfig c; c.symbol="RB"; c.exchange="SHFE"; c.tick_size=1; c.price_limit_percent=0.1; c.max_price_levels=1000; c.supported_order_combinations={{simex::common::OrderType::LIMIT,simex::common::TimeInForce::DAY}}; return c; }
int main() {
#ifdef _WIN32
 return 0;
#else
 simex::transport::ExchangeServiceConfig cfg; cfg.runtime.instrument=instrument(); cfg.runtime.reference_price=1000; cfg.runtime.trading_day=1; cfg.runtime.initial_phase=simex::common::SessionPhase::CONTINUOUS; cfg.runtime.queue_capacity=16; cfg.tcp.port=0; cfg.udp_destination_port=9;
 simex::transport::ExchangeService service(cfg); assert(service.start()); assert(service.ready());
 int fd=::socket(AF_INET,SOCK_STREAM,0); assert(fd>=0); sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK); addr.sin_port=htons(service.tcpPort()); assert(::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0);
 simex::transport::WireRequest req{}; req.header.kind=static_cast<std::uint8_t>(simex::transport::MessageKind::REQUEST); req.header.payload_size=sizeof(req.request); req.header.sequence=0; req.request.type=simex::common::RequestType::NEW; req.request.client_id=1; req.request.ticker_id=0; req.request.client_order_id=7; req.request.side=simex::common::Side::BUY; req.request.order_type=simex::common::OrderType::LIMIT; req.request.time_in_force=simex::common::TimeInForce::DAY; req.request.position_effect=simex::common::PositionEffect::OPEN; req.request.price_ticks=1000; req.request.qty=1; req.request.rx_time=1;
 assert(::send(fd,&req,sizeof(req),0)==static_cast<ssize_t>(sizeof(req))); simex::transport::WireResponse resp{}; std::size_t got=0; while(got<sizeof(resp)){auto n=::recv(fd,reinterpret_cast<char*>(&resp)+got,sizeof(resp)-got,0); assert(n>0); got+=static_cast<std::size_t>(n);} assert(resp.response.type==simex::common::ResponseType::ACCEPTED); ::close(fd); service.stop(); return 0;
#endif
}
