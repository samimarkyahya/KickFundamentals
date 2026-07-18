set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# MinGW's libstdc++ doesn't drag <cstring> into some JUCE headers that use
# bare memset/memcpy; force-include it globally so every translation unit has it.
set(CMAKE_CXX_FLAGS_INIT "-include cstring -include cstdint")

# Statically link libstdc++/libgcc/pthread so the plugin has no runtime DLL deps.
# JUCE auto-links its Win32 dependencies with `#pragma comment(lib, ...)`, which
# only MSVC honours; MinGW ignores it, so link the full set explicitly here.
set(KF_WIN_LIBS "-lole32 -luuid -lgdi32 -luser32 -lkernel32 -lcomdlg32 -ladvapi32 -lshell32 -loleaut32 -lodbc32 -lodbccp32 -lwinmm -lws2_32 -lwsock32 -lversion -limm32 -lcomctl32 -lrpcrt4 -lwininet -lshlwapi -lwinspool -ldwrite -ld2d1")
set(CMAKE_CXX_STANDARD_LIBRARIES "-static -static-libgcc -static-libstdc++ -lstdc++ -lpthread ${KF_WIN_LIBS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
