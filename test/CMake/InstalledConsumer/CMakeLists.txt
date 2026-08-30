cmake_minimum_required(VERSION 3.15)

project(YonaInstalledConsumer LANGUAGES C CXX)

find_package(Yona CONFIG REQUIRED)

add_executable(yona_c_consumer CConsumer.c)
target_link_libraries(yona_c_consumer PRIVATE Yona::Compiler)

add_executable(yona_cpp_consumer CppConsumer.cpp)
target_compile_features(yona_cpp_consumer PRIVATE cxx_std_23)
target_link_libraries(yona_cpp_consumer PRIVATE Yona::Compiler)

yona_add_executable(yona_language_consumer SOURCE hello.yona)
