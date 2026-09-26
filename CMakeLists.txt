cmake_minimum_required(VERSION 3.24)

project(simex LANGUAGES CXX)

option(JEV_FETCH_NLOHMANN_JSON
       "Fetch nlohmann/json v3.11.3 when no system package is available"
       OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  if(JEV_FETCH_NLOHMANN_JSON)
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

add_library(simex_trace STATIC
  src/trace/trace.cpp
)
target_include_directories(simex_trace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_trace PUBLIC simex_common OpenSSL::Crypto nlohmann_json::nlohmann_json)
target_compile_features(simex_trace PUBLIC cxx_std_20)

add_library(simex_harness STATIC
  src/participant_harness/participant_harness.cpp
)
target_include_directories(simex_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(simex_harness PUBLIC simex_exchange simex_trace)
target_compile_features(simex_harness PUBLIC cxx_std_20)

add_executable(simex_demo src/apps/simex_demo.cpp)
target_link_libraries(simex_demo PRIVATE simex_exchange simex_trace)
target_compile_features(simex_demo PRIVATE cxx_std_20)

include(CTest)
if(BUILD_TESTING)
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
endif()
