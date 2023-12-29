cmake_minimum_required (VERSION 3.10)

project (AirlineTycoon)

set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")
set(GAMEFILES_FOLDER "${PROJECT_SOURCE_DIR}/gamefiles")

if (WIN32)
    # Set and bind SDL folders
    set(SDL2_PATH "${CMAKE_MODULE_PATH}/sdl2")
    set(SDL2_MIXER_PATH "${CMAKE_MODULE_PATH}/sdl2-mixer")
    set(SDL2_IMAGE_PATH "${CMAKE_MODULE_PATH}/sdl2-image")
    set(SDL2_TTF_PATH "${CMAKE_MODULE_PATH}/sdl2-ttf")

    # Download when SDL is not found
    if(NOT EXISTS ${SDL2_TTF_PATH})
        execute_process(
            COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_MODULE_PATH}/dl-sdl2.ps1
            WORKING_DIRECTORY ${CMAKE_MODULE_PATH}
        )
    endif()

    # Copy dependencies for Release
    set(DEPENDENCIES_PATH ${DEPENDENCIES_PATH} "${CMAKE_BINARY_DIR}/dependencies")
    file(MAKE_DIRECTORY ${DEPENDENCIES_PATH})
    file(GLOB_RECURSE dll_files ${SDL2_PATH}/*.dll ${SDL2_MIXER_PATH}/*.dll ${SDL2_IMAGE_PATH}/*.dll ${SDL2_TTF_PATH}/*.dll ${PROJECT_SOURCE_DIR}/libs/*.dll ${PROJECT_SOURCE_DIR}/libs/*.exe)
    list(FILTER dll_files INCLUDE REGEX "x64")
    foreach(file ${dll_files})
        file(COPY ${file} DESTINATION ${DEPENDENCIES_PATH})
    endforeach()
endif()

find_package(SDL2main REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Include external libs.
add_subdirectory (libs)
include_directories(libs/jansson/include)

# Include sub-projects.
add_subdirectory (src)

install(TARGETS AT DESTINATION "${PROJECT_INSTALL_DIR}")