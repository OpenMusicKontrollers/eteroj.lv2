cmake_minimum_required(VERSION 2.8)

project(varchunk)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# pkg-config
find_package(PkgConfig REQUIRED)
	
# libuv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

include(CTest)
if(${BUILD_TESTING})
	add_executable(test_varchunk
		test_varchunk.c)
	target_link_libraries(test_varchunk ${UV_LDFLAGS})
	
	add_test(API-Test test_varchunk)
endif()
