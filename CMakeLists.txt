cmake_minimum_required (VERSION 3.24.0 FATAL_ERROR)

file (READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" GAME_VERSION)
set (GAME_NAME Shunt)
set (BUNDLE_ID com.socalabs.shunt)

set (CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set (CMAKE_SUPPRESS_REGENERATION true)
set (CMAKE_SKIP_INSTALL_RULES YES)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property (GLOBAL PROPERTY DEBUG_CONFIGURATIONS "Debug")

project (${GAME_NAME} VERSION ${GAME_VERSION} LANGUAGES CXX C HOMEPAGE_URL "https://socalabs.com/")

set_property (DIRECTORY APPEND PROPERTY LABELS ${GAME_NAME})
set_property (DIRECTORY APPEND PROPERTY LABELS SocaLabs)
set_property (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${GAME_NAME})

set (CMAKE_OSX_DEPLOYMENT_TARGET 10.15)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_OBJCXX_STANDARD 20)
set (CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set (BUILD_SHARED_LIBS OFF)

if (APPLE)
	set (CMAKE_OSX_ARCHITECTURES arm64 x86_64)
endif()

set_property (GLOBAL PROPERTY USE_FOLDERS YES)
set_property (GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER utility)

# JUCE
set (JUCE_MODULES_ONLY OFF)
set (JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
set (JUCE_BUILD_EXTRAS OFF)
set (JUCE_BUILD_EXAMPLES OFF)

add_subdirectory (modules/juce)
set_property (DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/modules/juce" APPEND PROPERTY LABELS JUCE)

# Gin modules
foreach (module_name IN ITEMS gin gin_controllers gin_graphics gin_gui)
	juce_add_module ("${CMAKE_CURRENT_LIST_DIR}/modules/gin/modules/${module_name}")
	set_property (TARGET "${module_name}" APPEND PROPERTY LABELS Gin)
endforeach()

# The game
juce_add_gui_app (${GAME_NAME}
                  PRODUCT_NAME ${GAME_NAME}
                  VERSION ${GAME_VERSION}
                  COMPANY_NAME SocaLabs
                  COMPANY_WEBSITE "https://socalabs.com/"
                  BUNDLE_ID ${BUNDLE_ID})

file (GLOB_RECURSE source_files CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

target_sources (${GAME_NAME} PRIVATE ${source_files})
source_group (TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX Source FILES ${source_files})

# Bake Assets/ into BinaryData so the game ships self-contained.
file (GLOB_RECURSE asset_files CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/Assets/*)

juce_add_binary_data (${GAME_NAME}_Assets
	HEADER_NAME BinaryData.h
	NAMESPACE   BinaryData
	SOURCES     ${asset_files})

juce_generate_juce_header (${GAME_NAME})

target_compile_definitions (${GAME_NAME} PRIVATE
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
	JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${GAME_NAME},JUCE_PROJECT_NAME>"
	JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${GAME_NAME},JUCE_VERSION>"
	_CRT_SECURE_NO_WARNINGS
)

target_link_libraries (${GAME_NAME} PRIVATE
	${GAME_NAME}_Assets

	gin
	gin_controllers
	gin_graphics
	gin_gui

	juce::juce_audio_basics
	juce::juce_audio_devices
	juce::juce_audio_formats
	juce::juce_audio_utils
	juce::juce_core
	juce::juce_data_structures
	juce::juce_events
	juce::juce_graphics
	juce::juce_gui_basics
	juce::juce_gui_extra

	juce::juce_recommended_config_flags
	juce::juce_recommended_lto_flags
	juce::juce_recommended_warning_flags
)

if (APPLE)
	set_target_properties (${GAME_NAME} PROPERTIES
		XCODE_ATTRIBUTE_CLANG_LINK_OBJC_RUNTIME NO
		XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
		XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release] "dwarf-with-dsym")
endif()

if (WIN32)
	set_property (TARGET ${GAME_NAME} APPEND_STRING PROPERTY LINK_FLAGS_DEBUG " /INCREMENTAL:NO")
	set_target_properties (${GAME_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")
endif()
