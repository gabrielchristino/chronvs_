# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

set(chronvs_requires espressif__esp-sr)
if(CHRONVS_TAILTINY)
    list(APPEND chronvs_requires tailtiny)
endif()
idf_component_register(SRCS ${app_sources} REQUIRES ${chronvs_requires})
