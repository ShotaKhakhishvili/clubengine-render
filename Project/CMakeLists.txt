cmake_minimum_required(VERSION 3.20)
project(CERender LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Engine checkout (submodule) and the folder its build wrote binaries to
set(CE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../External/KIU_ClubEngine/ClubEngine" CACHE PATH "ClubEngine root")
set(CE_BIN  "" CACHE PATH "ClubEngine binaries folder (Binaries/<Platform>/<Config>/<Version>)")
if(NOT CE_BIN)
    message(FATAL_ERROR "Set CE_BIN to the engine's binaries folder")
endif()

# Engine modules, imported directly so it works on both Windows (MinGW) and Linux
set(CE_RT "${CE_ROOT}/Engine/Source/Runtime")
set(CE_MODULES Core Project CoreUObject Asset AssetImport RenderCore RHI-OpenGL Renderer Platform Engine Launch)
add_library(ClubEngineAll INTERFACE)
foreach(MOD ${CE_MODULES})
    set(T "CE_${MOD}")
    add_library(${T} SHARED IMPORTED)
    if(WIN32)
        set_target_properties(${T} PROPERTIES
            IMPORTED_LOCATION "${CE_BIN}/libClubEngine-${MOD}.dll"
            IMPORTED_IMPLIB   "${CE_BIN}/libClubEngine-${MOD}.dll.a")
    else()
        set_target_properties(${T} PROPERTIES IMPORTED_LOCATION "${CE_BIN}/libClubEngine-${MOD}.so")
    endif()
    string(REPLACE "RHI-OpenGL" "RHI.OpenGL" DIR "${MOD}")
    target_include_directories(ClubEngineAll INTERFACE "${CE_RT}/${DIR}/Public")
    target_link_libraries(ClubEngineAll INTERFACE ${T})
endforeach()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.h)
add_executable(CERender ${SOURCES})

target_include_directories(CERender PRIVATE
    Source/CERender/Public
    Source/CERender/Private
    "${CE_ROOT}/Engine/ThirdParty/nlohmann")

target_link_libraries(CERender PRIVATE ClubEngineAll)

if(UNIX)
    set_target_properties(CERender PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
# Project root is the folder above bin/, so a build here runs the same way as the shipped zip
set_target_properties(CERender PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(CFG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CFG} CFGU)
    set_target_properties(CERender PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()
