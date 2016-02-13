cmake_minimum_required(VERSION 2.8)

project(zyn_ext_gui.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

# check for visibility support in compiler on Unices
include(CheckCCompilerFlag)
if(NOT WIN32)
	check_c_compiler_flag(-fvisibility=hidden GCC_SUPPORTS_VISIBILITY)
	if(NOT ${GCC_SUPPORTS_VISIBILITY})
		message(FATAL_ERROR "compiler does not support visibility attributes")
	endif()
endif()

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
endif()
add_definitions("-D_GNU_SOURCE=1") # asprintf

if(NOT DEFINED PLUGIN_DEST)
	set(PLUGIN_DEST lib/lv2/zyn_ext_gui.lv2)
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS_UI ${LIBS_UI} ${LV2_LDFLAGS})

pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})
if(DEFINED STATIC_UV)
	set(LIBS_UI ${STATIC_UV} ${LIBS_UI})
else()
	set(LIBS_UI ${LIBS_UI} ${UV_LDFLAGS})
endif()

add_library(zyn_ext_gui MODULE
	zyn_ext_gui.c)
target_link_libraries(zyn_ext_gui ${LIBS_UI})
set_target_properties(zyn_ext_gui PROPERTIES PREFIX "")
install(TARGETS zyn_ext_gui DESTINATION ${PLUGIN_DEST})

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${PLUGIN_DEST})

# CPack
set(CPACK_GENERATOR "ZIP")

set(CPACK_PACKAGE_NAME "zyn_ext_gui-lv2")
set(CPACK_PACKAGE_VERSION "2.5.3")
set(CPACK_PACKAGE_VERSION_MAJOR 2)
set(CPACK_PACKAGE_VERSION_MINOR 5)
set(CPACK_PACKAGE_VERSION_PATCH 3)
set(CPACK_PACKAGE_CONTACT "Hanspeter Portner <dev@open-music-kontrollers.ch>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "External LV2 UI for ZynAddSubFx")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_STRIP_FILES "lib/lv2/zyn_ext_gui.lv2/zyn_ext_gui.so")

include(CPack)
