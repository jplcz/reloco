# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_reloco_find_package_consumer LANGUAGES CXX)

find_package(jplcz_reloco CONFIG REQUIRED)

add_executable(find_package_consumer main.cpp)
target_link_libraries(find_package_consumer
    PRIVATE jplcz_reloco::reloco)
