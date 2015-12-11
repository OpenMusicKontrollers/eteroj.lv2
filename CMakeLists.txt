cmake_minimum_required(VERSION 2.8)

project(eteroj.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)
include_directories(${PROJECT_SOURCE_DIR}/libosc_stream)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/timely.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/tlsf-3.0)
include_directories(${PROJECT_SOURCE_DIR}/jsmn)
include_directories(${PROJECT_BINARY_DIR})

set(CMAKE_C_FLAGS "-std=gnu99 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(ETEROJ_MAJOR_VERSION 0)
set(ETEROJ_MINOR_VERSION 1)
set(ETEROJ_MICRO_VERSION 0)
set(ETEROJ_VERSION "${ETEROJ_MAJOR_VERSION}.${ETEROJ_MINOR_VERSION}.${ETEROJ_MICRO_VERSION}")
add_definitions("-DETEROJ_VERSION=\"${ETEROJ_VERSION}\"")

set(DEST lib/lv2/eteroj.lv2)
if(WIN32)
	set(LIB_EXT ".dll")
elseif(APPLE)
	set(LIB_EXT ".so")
else()
	set(LIB_EXT ".so")
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})
set(LIBS ${LIBS} ${UV_LDFLAGS})

# eteroj
add_library(eteroj MODULE
	eteroj.c
	eteroj_io.c
	eteroj_disk.c
	eteroj_query.c
	eteroj_cloak.c
	eteroj_decloak.c
	eteroj_pack.c
	eteroj_unpack.c
	tlsf-3.0/tlsf.c
	jsmn/jsmn.c)
target_link_libraries(eteroj ${LIBS})
if(WIN32)
	target_link_libraries(eteroj "-lws2_32")
endif()
set_target_properties(eteroj PROPERTIES PREFIX "")
install(TARGETS eteroj DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/eteroj.ttl DESTINATION ${DEST})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
