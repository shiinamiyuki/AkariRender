IF(WIN32)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "C:/Program Files (x86)/LLVM/lib/cmake")
ELSE()
   set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/usr/lib64/cmake/")
ENDIF()
set(LLVM_ROOT "" CACHE STRING "")
if(LLVM_ROOT STREQUAL  "")
    find_package(LLVM 10.0)
else()
    find_package(LLVM PATHS ${LLVM_ROOT})
endif()
if(NOT LLVM_FOUND)
    message(FATAL_ERROR "LLVM not found")
else()
    message(STATUS "Found LLVM ${LLVM_VERSION}")
    message(STATUS "LLVM_INCLUDE_DIR " ${LLVM_INCLUDE_DIR})
    message(STATUS "LLVM_LIBRARY_DIRS    " ${LLVM_LIBRARY_DIRS})
endif()
