cmake_minimum_required(VERSION 2.8)

project(eteroj.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)
include_directories(${PROJECT_SOURCE_DIR}/libosc_stream)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_BINARY_DIR})

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
	libosc/osc.c
	libosc_stream/osc_stream.c
	libosc_stream/osc_stream_udp.c
	libosc_stream/osc_stream_tcp.c
	libosc_stream/osc_stream_pipe.c
	libosc_stream/osc_stream_slip.c
	varchunk/varchunk.c)
target_link_libraries(eteroj ${LIBS})
if(WIN32)
	target_link_libraries(eteroj "-lws2_32")
endif()
set_target_properties(eteroj PROPERTIES PREFIX "")
install(TARGETS eteroj DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/eteroj.ttl DESTINATION ${DEST})

pkg_search_module(ELM REQUIRED elementary>=1.8)
include_directories(${ELM_INCLUDE_DIRS})
set(LIBS_UI ${LIBS_UI} ${ELM_LDFLAGS})

pkg_search_module(ECORE_X OPTIONAL ecore-x)

if(${ECORE_X_FOUND} AND ((${ELM_VERSION} VERSION_EQUAL "1.13.0") OR (${ELM_VERSION} VERSION_GREATER "1.13.0")))
	set(X11_UI_WRAP "")
	add_definitions("-DX11_UI_WRAP")
else()
	set(X11_UI_WRAP "#")
endif()

# eteroj_ui
add_library(eteroj_ui MODULE
	eteroj_ui.c
	eteroj_io_ui.c)
target_link_libraries(eteroj_ui ${LIBS_UI})
set_target_properties(eteroj_ui PROPERTIES PREFIX "")
install(TARGETS eteroj_ui DESTINATION ${DEST})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
