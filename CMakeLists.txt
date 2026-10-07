cmake_minimum_required(VERSION 3.16)

project(rtp_audio_streaming C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/Platform.cmake)

add_library(ac STATIC
    src/audio/capture.c
    src/audio/playback.c
    src/audio/miniaudio_impl.c
    src/core/codec_l16.c
    src/core/jitter.c
    src/core/ring_buffer.c
    src/core/rtp.c
    ${AC_PLATFORM_SOURCES}
)

target_include_directories(ac
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(ac
    PUBLIC
        ${AC_PLATFORM_LIBS}
)

target_compile_definitions(ac
    PUBLIC
        ${AC_PLATFORM_DEFINES}
)

target_compile_options(ac
    PRIVATE
        ${AC_PLATFORM_COMPILE_OPTIONS}
)

add_executable(ac_sender
    src/sender/main_sender.c
)
target_link_libraries(ac_sender PRIVATE ac)

add_executable(ac_receiver
    src/receiver/main_receiver.c
)
target_link_libraries(ac_receiver PRIVATE ac)

if(NOT MSVC)
    target_compile_options(ac_sender PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ac_receiver PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ac_test_rtp tests/test_rtp.c)
    target_link_libraries(ac_test_rtp PRIVATE ac)
    add_test(NAME ac_test_rtp COMMAND ac_test_rtp)

    add_executable(ac_test_codec_l16 tests/test_codec_l16.c)
    target_link_libraries(ac_test_codec_l16 PRIVATE ac)
    add_test(NAME ac_test_codec_l16 COMMAND ac_test_codec_l16)

    add_executable(ac_test_ring tests/test_ring_buffer.c)
    target_link_libraries(ac_test_ring PRIVATE ac)
    add_test(NAME ac_test_ring COMMAND ac_test_ring)

    add_executable(ac_test_jitter tests/test_jitter.c)
    target_link_libraries(ac_test_jitter PRIVATE ac)
    add_test(NAME ac_test_jitter COMMAND ac_test_jitter)
endif()
