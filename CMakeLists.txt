cmake_minimum_required(VERSION 3.25)

project(simex LANGUAGES CXX)

option(SIMEX_FETCH_NLOHMANN_JSON
       "Fetch nlohmann/json v3.11.3 when no system package is available"
       OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simex_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(simex_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror
  )
endif()

include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  if(SIMEX_FETCH_NLOHMANN_JSON)
    FetchContent_Declare(
      nlohmann_json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
  else()
    find_package(nlohmann_json CONFIG REQUIRED)
  endif()
endif()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(simex_common STATIC
  src/common/config.cpp
)
target_include_directories(simex_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_common PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(simex_common PUBLIC cxx_std_20)
target_link_libraries(simex_common PUBLIC simex_warnings)

add_library(simex_exchange STATIC
  src/front_clearing/front_clearing.cpp
  src/market_data/market_data_publisher.cpp
  src/market_data/snapshot_synthesizer.cpp
  src/matching_engine/me_order_book.cpp
  src/matching_engine/matching_engine.cpp
  src/session_calendar/session_calendar.cpp
)
target_include_directories(simex_exchange PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_exchange PUBLIC simex_common)
target_compile_features(simex_exchange PUBLIC cxx_std_20)
target_link_libraries(simex_exchange PUBLIC simex_warnings)

add_library(simex_trace STATIC
  src/trace/trace.cpp
)
target_include_directories(simex_trace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_trace PUBLIC simex_common OpenSSL::Crypto nlohmann_json::nlohmann_json)
target_compile_features(simex_trace PUBLIC cxx_std_20)
target_link_libraries(simex_trace PUBLIC simex_warnings)

add_library(simex_harness STATIC
  src/participant_harness/participant_harness.cpp
)
target_include_directories(simex_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_harness PUBLIC simex_exchange simex_trace)
target_compile_features(simex_harness PUBLIC cxx_std_20)
target_link_libraries(simex_harness PUBLIC simex_warnings)

add_library(simex_participant STATIC
  src/participant_simulator/participant_simulator.cpp
)
target_include_directories(simex_participant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_participant PUBLIC simex_common)
target_compile_features(simex_participant PUBLIC cxx_std_20)
target_link_libraries(simex_participant PUBLIC simex_warnings)

add_library(simex_runtime STATIC
  src/runtime/exchange_runtime.cpp
)
target_include_directories(simex_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_runtime PUBLIC simex_harness simex_participant)
target_compile_features(simex_runtime PUBLIC cxx_std_20)
target_link_libraries(simex_runtime PUBLIC simex_warnings)

add_library(simex_transport STATIC
  src/transport/tcp_order_gateway.cpp
  src/transport/udp_market_data_publisher.cpp
  src/transport/exchange_service.cpp
)
target_include_directories(simex_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_transport PUBLIC simex_exchange simex_runtime)
target_compile_features(simex_transport PUBLIC cxx_std_20)
target_link_libraries(simex_transport PUBLIC simex_warnings)

add_executable(simex_demo src/main.cpp)
add_executable(simex_server src/server_main.cpp)
target_link_libraries(simex_server PRIVATE simex_transport)
target_link_libraries(simex_demo PRIVATE simex_exchange simex_trace)
target_compile_features(simex_demo PRIVATE cxx_std_20)
target_link_libraries(simex_demo PRIVATE simex_warnings)

if(UNIX)
  add_library(simex_tui_core STATIC
    src/tui/market_observer.cpp
    src/tui/terminal_ui.cpp
  )
  target_include_directories(simex_tui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(simex_tui_core PUBLIC simex_warnings)
  target_compile_features(simex_tui_core PUBLIC cxx_std_20)

  add_executable(simex_tui src/simex_tui.cpp)
  target_link_libraries(simex_tui PRIVATE simex_tui_core)
  target_compile_features(simex_tui PRIVATE cxx_std_20)
  target_link_libraries(simex_tui PRIVATE simex_warnings)
endif()

include(CTest)
if(BUILD_TESTING)
  # The current tests use assert for both checks and setup calls. Keep those
  # assertions active in Release test builds until they are made side-effect free.
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-UNDEBUG>)

  add_executable(test_virtual_clock src/tests/test_virtual_clock.cpp)
  target_link_libraries(test_virtual_clock PRIVATE simex_common)
  add_test(NAME virtual_clock COMMAND test_virtual_clock)

  add_executable(test_config src/tests/test_config.cpp)
  target_link_libraries(test_config PRIVATE simex_common)
  add_test(NAME config COMMAND test_config ${CMAKE_CURRENT_SOURCE_DIR}/simex.json)

  add_executable(test_session_calendar src/tests/test_session_calendar.cpp)
  target_link_libraries(test_session_calendar PRIVATE simex_exchange)
  add_test(NAME session_calendar COMMAND test_session_calendar)

  add_executable(test_front_clearing src/tests/test_front_clearing.cpp)
  target_link_libraries(test_front_clearing PRIVATE simex_exchange)
  add_test(NAME front_clearing COMMAND test_front_clearing)

  add_executable(test_matching_engine src/tests/test_matching_engine.cpp)
  target_link_libraries(test_matching_engine PRIVATE simex_exchange)
  add_test(NAME matching_engine COMMAND test_matching_engine)

  add_executable(test_call_auction src/tests/test_call_auction.cpp)
  target_link_libraries(test_call_auction PRIVATE simex_exchange)
  add_test(NAME call_auction COMMAND test_call_auction)

  add_executable(test_market_data src/tests/test_market_data.cpp)
  target_link_libraries(test_market_data PRIVATE simex_exchange)
  add_test(NAME market_data COMMAND test_market_data)

  add_executable(test_participant_harness src/tests/test_participant_harness.cpp)
  target_link_libraries(test_participant_harness PRIVATE simex_harness)
  add_test(NAME participant_harness COMMAND test_participant_harness)

  add_executable(test_trace src/tests/test_trace.cpp)
  target_link_libraries(test_trace PRIVATE simex_trace)
  add_test(NAME trace COMMAND test_trace)

  add_executable(test_exchange_runtime src/tests/test_exchange_runtime.cpp)
  target_link_libraries(test_exchange_runtime PRIVATE simex_runtime)
  add_test(NAME exchange_runtime COMMAND test_exchange_runtime)

  add_executable(test_participant_runtime src/tests/test_participant_runtime.cpp)
  target_link_libraries(test_participant_runtime PRIVATE simex_runtime)
  add_test(NAME participant_runtime COMMAND test_participant_runtime)

  add_executable(test_participant_simulator src/tests/test_participant_simulator.cpp)
  target_link_libraries(test_participant_simulator PRIVATE simex_participant)
  add_test(NAME participant_simulator COMMAND test_participant_simulator)

  add_executable(test_transport src/tests/test_transport.cpp)
  target_link_libraries(test_transport PRIVATE simex_transport)
  add_test(NAME transport COMMAND test_transport)
endif()

if(BUILD_TESTING)
  add_executable(test_exchange_service src/tests/test_exchange_service.cpp)
  target_link_libraries(test_exchange_service PRIVATE simex_transport)
  add_test(NAME exchange_service COMMAND test_exchange_service)
endif()

if(UNIX AND BUILD_TESTING)
  add_executable(test_tui_observer src/tests/test_tui_observer.cpp)
  target_link_libraries(test_tui_observer PRIVATE simex_tui_core)
  add_test(NAME tui_observer COMMAND test_tui_observer)
  add_test(NAME tui_e2e COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/tui_e2e.sh
           $<TARGET_FILE:simex_server> $<TARGET_FILE:simex_tui> ${CMAKE_CURRENT_SOURCE_DIR})
endif()
