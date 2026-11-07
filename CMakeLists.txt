cmake_minimum_required(VERSION 4.3)
project(untitled)

set(CMAKE_CXX_STANDARD 20)

add_executable(mobileLabel main.cpp
        TextFileQueue.h
        SerialPort.h)
