cmake_minimum_required(VERSION 3.15)
project(Aegis CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(aegis
    src/main.cpp
    src/logger.cpp
    src/watcher.cpp
    src/engine.cpp
    src/quarantine.cpp
    src/attribution.cpp
    src/updater.cpp
    src/autostart.cpp
    src/config.cpp
)

if(WIN32)
    target_link_libraries(aegis PRIVATE user32 shell32 ole32 uuid)
elseif(APPLE)
    # FSEvents (наблюдение за файлами) + CoreFoundation (CFRunLoop/CFString)
    target_link_libraries(aegis PRIVATE
        "-framework CoreServices"
        "-framework CoreFoundation")
endif()
