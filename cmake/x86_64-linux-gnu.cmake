# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "x86_64")
set(TOOLCHAIN "x86_64-linux-gnu")

set(STATIC_UV "/opt/${TOOLCHAIN}/lib/libuv.a")
set(STATIC_SRATOM "/opt/${TOOLCHAIN}/lib/libsratom-0.a")
set(STATIC_SERD "/opt/${TOOLCHAIN}/lib/libserd-0.a")
set(STATIC_SORD "/opt/${TOOLCHAIN}/lib/libsord-0.a")
