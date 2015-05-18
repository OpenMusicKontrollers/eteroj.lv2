include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../bin)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc_stream)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h
include_directories(${PROJECT_SOURCE_DIR}/varchunk)

set(DEST lib/lv2/synthpod.lv2)
if(WIN32)
	set(LIB_EXT ".dll")
elseif(APPLE)
	set(LIB_EXT ".so")
else()
	set(LIB_EXT ".so")
endif()

option(BUILD_RTMIDI OFF)
if(BUILD_RTMIDI)
# rtmidi
	pkg_search_module(RTMIDI REQUIRED librtmidi>=2.1)
	include_directories(${RTMIDI_INCLUDE_DIRS})

# RtMidi
	add_library(rtmidi MODULE
		rtmidi.c
		rtmidi_in.cpp
		rtmidi_out.cpp)
	target_link_libraries(rtmidi ${RTMIDI_LDFLAGS})
	set_target_properties(rtmidi PROPERTIES PREFIX "")
	install(TARGETS rtmidi DESTINATION ${DEST})
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi.ttl DESTINATION ${DEST})
endif()

# OSC
add_library(osc_io MODULE
	libosc/osc.c
	libosc_stream/osc_stream.c
	libosc_stream/osc_stream_udp.c
	libosc_stream/osc_stream_tcp.c
	libosc_stream/osc_stream_pipe.c
	libosc_stream/osc_stream_slip.c
	../varchunk/varchunk.c
	osc_io.c
	osc_io_io.c)
target_link_libraries(osc_io ${UV_LDFLAGS})
if(WIN32)
	target_link_libraries(osc_io "-lws2_32")
endif()
set_target_properties(osc_io PROPERTIES PREFIX "")
install(TARGETS osc_io DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/osc_io.ttl DESTINATION ${DEST})

# synthpod
add_library(synthpod.lv2 MODULE
	synthpod_lv2.c
	synthpod_stereo.c
	synthpod_sinksource.c
	synthpod_keyboard.c)
target_link_libraries(synthpod.lv2 synthpod.lib)
set_target_properties(synthpod.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lv2 DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.ttl DESTINATION ${DEST})

if(BUILD_UI)
	if(${ECORE_X_FOUND} AND ((${ELM_VERSION} VERSION_EQUAL "1.13.0") OR (${ELM_VERSION} VERSION_GREATER "1.13.0")))
		set(X11_UI_WRAP "")
		add_definitions("-DX11_UI_WRAP")
	else()
		set(X11_UI_WRAP "#")
	endif()

	set(EO_UI_WRAP "")
	set(UI_UI_WRAP "")
	set(KX_UI_WRAP "")

# synthpod_ui
	add_library(synthpod_ui.lv2 MODULE
		synthpod_lv2_ui.c
		synthpod_common_ui.c
		synthpod_keyboard_ui.c)
	target_link_libraries(synthpod_ui.lv2 synthpod_ui.lib)
	set_target_properties(synthpod_ui.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_ui")
	install(TARGETS synthpod_ui.lv2 DESTINATION ${DEST})
else()
	set(EO_UI_WRAP "#")
	set(UI_UI_WRAP "#")
	set(KX_UI_WRAP "#")
	set(X11_UI_WRAP "#")
endif()

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${DEST})
