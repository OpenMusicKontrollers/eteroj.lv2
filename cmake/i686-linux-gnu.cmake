# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "i686")
set(TOOLCHAIN "i686-linux-gnu")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "c++ flags")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -m32" CACHE STRING "c flags")

set(STATIC_UV "/opt/${TOOLCHAIN}/lib/libuv.a")
set(STATIC_SRATOM "/opt/${TOOLCHAIN}/lib/libsratom-0.a")
set(STATIC_SERD "/opt/${TOOLCHAIN}/lib/libserd-0.a")
set(STATIC_SORD "/opt/${TOOLCHAIN}/lib/libsord-0.a")
