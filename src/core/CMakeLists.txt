set(SOURCES
    cdc.cpp
    dmac.cpp
    emulator.cpp
    gpu.cpp
    intc.cpp
    io.cpp
    scheduler.cpp
    spu.cpp
    timer.cpp
    cpu/code_buffer.cpp
    cpu/core.cpp
    cpu/decode.cpp
    cpu/disassembler.cpp
    cpu/gte.cpp
    cpu/interpreter.cpp
    cpu/recompiler.cpp
    disc/bin.cpp
    disc/disc.cpp
    joypad/digital.cpp
    joypad/joypad.cpp
)

set(HEADERS
    cdc.hpp
    dmac.hpp
    emulator.hpp
    error.hpp
    gpu.hpp
    intc.hpp
    io.hpp
    scheduler.hpp
    spu.hpp
    timer.hpp
    cpu/code_buffer.hpp
    cpu/core.hpp
    cpu/decode.hpp
    cpu/gte.hpp
    cpu/recompiler.hpp
    disc/bin.hpp
    disc/disc.hpp
    joypad/digital.hpp
    joypad/joypad.hpp
)

add_library(core STATIC ${SOURCES} ${HEADERS})
add_library(${PROJECT_NAME}::core ALIAS core)

target_compile_options(core PRIVATE -fno-operator-names)

target_include_directories(core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(core PUBLIC btpsx::common)
target_link_libraries(core PUBLIC fmt::fmt spdlog::spdlog xbyak::xbyak)
