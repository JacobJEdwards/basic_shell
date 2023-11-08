cmake_minimum_required(VERSION 3.26)
project(basic_shell C)

set(CMAKE_C_STANDARD 11)

add_executable(basic_shell
        prompt.c)
