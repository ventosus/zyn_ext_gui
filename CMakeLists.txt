cmake_minimum_required(VERSION 2.8)

project(zyn_ext_gui.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,defs ${CMAKE_MODULE_LINKER_FLAGS}")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
endif()
add_definitions("-D_GNU_SOURCE=1") # asprintf

if(NOT DEFINED PLUGIN_DEST)
	set(PLUGIN_DEST lib/lv2/zyn_ext_gui.lv2)
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

add_library(zyn_ext_gui MODULE
	zyn_ext_gui.c)
set_target_properties(zyn_ext_gui PROPERTIES PREFIX "")
install(TARGETS zyn_ext_gui DESTINATION ${PLUGIN_DEST})

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${PLUGIN_DEST})
