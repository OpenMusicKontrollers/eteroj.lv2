cmake_minimum_required(VERSION 2.8)

project(eteroj.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc_stream)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/timely.lv2)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/netatom.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/tlsf-3.0)
include_directories(${PROJECT_SOURCE_DIR}/jsmn)
include_directories(${PROJECT_BINARY_DIR})

set(CMAKE_C_FLAGS "-fdata-sections -ffunction-sections ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes -Wno-attributes ${CMAKE_C_FLAGS}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,defs ${CMAKE_MODULE_LINKER_FLAGS}")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
elseif(WIN32)
	set(CMAKE_C_FLAGS "-mstackrealign ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	if(APPLE)
		set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-dead_strip ${CMAKE_MODULE_LINKER_FLAGS}")
	else()
		set(CMAKE_MODULE_LINKER_FLAGS "-Wl,--gc-sections -Wl,-s ${CMAKE_MODULE_LINKER_FLAGS}")
	endif()
endif()

add_definitions("-D_GNU_SOURCE=1") # asprintf

file(STRINGS "VERSION" ETEROJ_VERSION)
string(REPLACE "." ";" VERSION_LIST ${ETEROJ_VERSION})
list(GET VERSION_LIST 0 ETEROJ_MAJOR_VERSION)
list(GET VERSION_LIST 1 ETEROJ_MINOR_VERSION)
list(GET VERSION_LIST 2 ETEROJ_MICRO_VERSION)
add_definitions("-DETEROJ_VERSION=\"${ETEROJ_VERSION}\"")

if(NOT DEFINED PLUGIN_DEST)
	set(PLUGIN_DEST lib/lv2/eteroj.lv2)
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})
if(DEFINED STATIC_UV)
	set(LIBS ${STATIC_UV} ${LIBS})
else()
	set(LIBS ${LIBS} ${UV_LDFLAGS})
endif()

pkg_search_module(LV2 REQUIRED lv2>=1.10.0)
include_directories(${LV2_INCLUDE_DIRS})

# eteroj
add_library(eteroj MODULE
	eteroj.c
	eteroj_io.c
	eteroj_disk.c
	eteroj_query.c
	eteroj_cloak.c
	eteroj_pack.c
	eteroj_ninja.c
	eteroj_control.c
	tlsf-3.0/tlsf.c
	jsmn/jsmn.c)
target_link_libraries(eteroj m ${LIBS})
set_target_properties(eteroj PROPERTIES PREFIX "")
install(TARGETS eteroj DESTINATION ${PLUGIN_DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/eteroj.ttl DESTINATION ${PLUGIN_DEST})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${PLUGIN_DEST})
