cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)
project(vk_restir)

#--------------------------------------------------------------------------------------------------
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find CMakeLists_include.txt yet
if(NOT BASE_DIRECTORY)
   SET(BASE_DIRECTORY "" CACHE FILEPATH "folder containing shared_sources")
  SET(ADD_SUBDIR_BELOW 1)
  find_path(BASE_DIRECTORY2
    NAMES shared_sources
    PATHS  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    REQUIRED
    DOC "Couldn't find shared_source directory'"
    )
  SET(BASE_DIRECTORY ${BASE_DIRECTORY2} )
endif()

## Various functions and macros REQUIRED
include(${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
set(TUTO_KHR_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# presets and utility functions used in all CMakeLists
include(utilities.cmake)

#--------------------------------------------------------------------------------------------------
# Package shared by all projects
_add_package_VulkanSDK()
_add_package_OpenGL()
_add_package_ImGUI()
_add_package_ZLIB()
_add_shared_sources_lib()

message(STATUS "COPY ${CMAKE_CURRENT_SOURCE_DIR}/media  to  ${EXECUTABLE_OUTPUT_PATH}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/media DESTINATION ${EXECUTABLE_OUTPUT_PATH})


#--------------------------------------------------------------------------------------------------
# Project setting
get_filename_component(PROJNAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
SET(PROJNAME ${PROJNAME}_KHR)
project(${PROJNAME} LANGUAGES C CXX)
message(STATUS "-------------------------------")
message(STATUS "Processing Project ${PROJNAME}:")


#--------------------------------------------------------------------------------------------------
# C++ target and defines
set(CMAKE_CXX_STANDARD 17)
add_executable(${PROJNAME})
_add_project_definitions(${PROJNAME})


#--------------------------------------------------------------------------------------------------
# Source files for this project
#
file(GLOB SOURCE_FILES src/*.cpp src/*.hpp src/*.inl src/*.h src/*.c)
file(GLOB PASS_FILES src/passes/*.cpp src/passes/*.hpp src/passes/*.inl src/passes/*.h src/passes/*.c)



#--------------------------------------------------------------------------------------------------
# GLSL to SPIR-V custom build
#
SET(VULKAN_TARGET_ENV vulkan1.2)
UNSET(GLSL_SOURCES)
UNSET(SPV_OUTPUT)
file(GLOB_RECURSE GLSL_HEADER_FILES "src/shaders/headers/*.h" "src/shaders/headers/*.glsl")
file(GLOB_RECURSE GLSL_STRUCTURE_FILES "src/shaders/structs/*.h" "src/shaders/structs/*.glsl")
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "src/shaders/*.comp"
    "src/shaders/*.frag"
    "src/shaders/*.vert"
    "src/shaders/*.rchit"
    "src/shaders/*.rahit"
    "src/shaders/*.rint"
    "src/shaders/*.rmiss"
    "src/shaders/*.rgen"
    "src/shaders/*.rcall"
    )
foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    _compile_GLSL(${GLSL} "src/shaders/${FILE_NAME}.spv" GLSL_SOURCES SPV_OUTPUT)
endforeach(GLSL)
#list(APPEND GLSL_SOURCES ${GLSL_HEADER_FILES})


#--------------------------------------------------------------------------------------------------
# Sources
target_sources(${PROJNAME} PUBLIC ${SOURCE_FILES} ${HEADER_FILES})
target_sources(${PROJNAME} PUBLIC ${PASS_FILES})
target_sources(${PROJNAME} PUBLIC ${PACKAGE_SOURCE_FILES})
target_sources(${PROJNAME} PUBLIC ${GLSL_SOURCES})
target_sources(${PROJNAME} PUBLIC ${GLSL_HEADER_FILES})
target_sources(${PROJNAME} PUBLIC ${GLSL_STRUCTURE_FILES})

#--------------------------------------------------------------------------------------------------
# Sub-folders in Visual Studio
#
source_group("Common"       FILES  ${PACKAGE_SOURCE_FILES})
source_group("Sources"      FILES ${SOURCE_FILES})
source_group("Passes"      FILES ${PASS_FILES})
source_group("Headers"      FILES ${HEADER_FILES})
source_group("Shader_Files" FILES ${GLSL_SOURCES})
source_group("Shader_Headers" FILES ${GLSL_HEADER_FILES})
source_group("Shader_Structures" FILES ${GLSL_STRUCTURE_FILES})

#--------------------------------------------------------------------------------------------------
# Linkage
#
target_link_libraries(${PROJNAME} ${PLATFORM_LIBRARIES} shared_sources)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${PROJNAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${PROJNAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

#--------------------------------------------------------------------------------------------------
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#
_copy_binaries_to_target( ${PROJNAME} )


#install(FILES ${SPV_OUTPUT} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}/shaders")
#install(FILES ${SPV_OUTPUT} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}/shaders")
#install(FILES ${CUBIN_SOURCES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}")
#install(FILES ${CUBIN_SOURCES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}")
#install(DIRECTORY "../media" CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}")
#install(DIRECTORY "../media" CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}")

#----------------------------------------------------------------------------------------------------
# Copying elements
# Media
# target_copy_to_output_dir(TARGET ${PROJECT_NAME} FILES "${TUTO_KHR_DIR}/media")
# Spir-V Shaders
target_copy_to_output_dir(
  TARGET ${PROJECT_NAME} 
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  DEST_SUBFOLDER "${PROJECT_NAME}/"
  FILES ${SPV_OUTPUT}
  )


