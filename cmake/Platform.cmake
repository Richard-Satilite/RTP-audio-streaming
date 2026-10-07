include_guard(GLOBAL)

set(AC_PLATFORM_SOURCES "")
set(AC_PLATFORM_LIBS "")
set(AC_PLATFORM_DEFINES "")
set(AC_PLATFORM_COMPILE_OPTIONS "")

if(WIN32)
    list(APPEND AC_PLATFORM_SOURCES
        src/core/net_win.c
        src/core/thread_win.c
    )

    list(APPEND AC_PLATFORM_LIBS
        ws2_32
    )

    list(APPEND AC_PLATFORM_DEFINES
        AC_PLATFORM_WINDOWS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    if(MSVC)
        list(APPEND AC_PLATFORM_COMPILE_OPTIONS
            /W4
        )
    else()
        list(APPEND AC_PLATFORM_COMPILE_OPTIONS
            -Wall
            -Wextra
        )
    endif()
elseif(UNIX)
    find_package(Threads REQUIRED)

    list(APPEND AC_PLATFORM_SOURCES
        src/core/net_posix.c
        src/core/thread_posix.c
    )

    list(APPEND AC_PLATFORM_LIBS
        Threads::Threads
        m
        dl
    )

    list(APPEND AC_PLATFORM_DEFINES
        AC_PLATFORM_POSIX
    )

    list(APPEND AC_PLATFORM_COMPILE_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
    )
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()
