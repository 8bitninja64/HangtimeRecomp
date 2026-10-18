cmake_minimum_required(VERSION 3.20)
project(present_test CXX)
# Diagnostics outside RT64: raw DXGI present rate per adapter (D3D11, D3D12) and the rate at
# which a window actually changes on screen (Desktop Duplication).
add_executable(present_test present_test.cpp)
add_executable(present_test12 present_test12.cpp)
add_executable(screen_rate screen_rate.cpp)
