cmake_minimum_required(VERSION 2.8)

project(props.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

set(CMAKE_C_FLAGS "-std=gnu99 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(PROPS_MAJOR_VERSION 0)
set(PROPS_MINOR_VERSION 1)
set(PROPS_MICRO_VERSION 1)
set(PROPS_VERSION "${PROPS_MAJOR_VERSION}.${PROPS_MINOR_VERSION}.${PROPS_MICRO_VERSION}")
add_definitions("-DPROPS_VERSION=\"${PROPS_VERSION}\"")

set(DEST lib/lv2/props.lv2)
if(WIN32)
	set(LIB_EXT ".dll")
elseif(APPLE)
	set(LIB_EXT ".so")
else()
	set(LIB_EXT ".so")
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

# props
add_library(props MODULE
	test/props.c)
target_link_libraries(props ${LIBS})
set_target_properties(props PROPERTIES PREFIX "")
install(TARGETS props DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/test/props.ttl DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/test/chunk.bin DESTINATION ${DEST})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
