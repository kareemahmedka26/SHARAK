cmake_minimum_required(VERSION 3.16)
project(sharak_gateway LANGUAGES C CXX)

# Two languages on purpose: the wire protocol is C (shared byte-for-byte with
# the node firmware), everything else is C++17. CMake drives both compilers.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror)

# The frozen wire protocol. Compiled from the SAME source the firmware uses, so
# the two ends can never disagree about the bytes. C++ reaches it through the
# extern "C" guards in protocol.h.
add_library(sharak_protocol STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/src/protocol.c)
target_include_directories(sharak_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include)

enable_testing()

add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE sharak_protocol)
add_test(NAME smoke COMMAND test_smoke)
