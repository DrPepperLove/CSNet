cmake_minimum_required(VERSION 3.22)

project(2025-lab1-myFTP)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


add_executable(ftp_client ftp_client.cpp define.cpp)
add_executable(ftp_server ftp_server.cpp define.cpp)

target_link_libraries(ftp_server pthread)
