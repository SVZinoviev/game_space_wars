cmake_minimum_required(VERSION 3.22)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    .
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ft6336u.c
)
