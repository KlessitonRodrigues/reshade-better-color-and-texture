cmake_minimum_required(VERSION 3.20)
project(PerformanceHUD LANGUAGES CXX)

add_library(performance_hud SHARED src/performance_hud.cpp)
target_include_directories(performance_hud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(performance_hud PRIVATE pdh)

if(MSVC)
	target_compile_options(performance_hud PRIVATE /W4)
	set_target_properties(performance_hud PROPERTIES
		MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
	target_compile_options(performance_hud PRIVATE -Os)
	target_link_options(performance_hud PRIVATE -static)
endif()

# Output to the root reshade-shaders/Addons directory, where ReShade loads add-ons from.
set(ADDON_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../reshade-shaders/Addons")

set_target_properties(performance_hud PROPERTIES
	OUTPUT_NAME "PerformanceHUD"
	PREFIX ""
	SUFFIX ".addon64"
	RUNTIME_OUTPUT_DIRECTORY "${ADDON_OUTPUT_DIR}")

foreach(config DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
	set_target_properties(performance_hud PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_${config} "${ADDON_OUTPUT_DIR}")
endforeach()
