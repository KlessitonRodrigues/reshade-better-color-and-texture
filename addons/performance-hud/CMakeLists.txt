cmake_minimum_required(VERSION 3.20)
project(PerformanceHUD LANGUAGES CXX)

add_library(performance_hud SHARED src/performance_hud.cpp)
target_include_directories(performance_hud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(performance_hud PRIVATE pdh powrprof)

if(MSVC)
	target_compile_options(performance_hud PRIVATE /W4)
	set_target_properties(performance_hud PROPERTIES
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
	target_compile_options(performance_hud PRIVATE -Os)
	target_link_options(performance_hud PRIVATE -static)
endif()

set_target_properties(performance_hud PROPERTIES
	OUTPUT_NAME "PerformanceHUD"
	PREFIX ""
	SUFFIX ".addon64")
