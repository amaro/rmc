cmake_minimum_required(VERSION 3.19)

project(
    RMC
    VERSION 0.1
    DESCRIPTION "RMC project"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_compile_options(-Wall -Werror -fcoroutines -fno-exceptions -fno-rtti)

#set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# for clang
#add_compile_options(-fcoroutines-ts -stdlib=libc++)
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()
message("***Build type is ${CMAKE_BUILD_TYPE}***")

# for clang-tidy
#set(CMAKE_CXX_CLANG_TIDY
#  clang-tidy-13;
#  -header-filter=.;
#  -checks=performance*,google*,fuchsia*,cppcoreguidelines*,bugprone*,modernize*;)


add_subdirectory(src)
