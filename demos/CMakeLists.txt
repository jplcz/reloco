# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

function(add_jplcz_reloco_demo NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE jplcz_reloco)
    if(TARGET Threads::Threads)
        target_link_libraries(${NAME} PRIVATE Threads::Threads)
    endif()
    jplcz_reloco_enable_warnings(${NAME})
endfunction()

add_jplcz_reloco_demo(scoped_workers)
add_jplcz_reloco_demo(channel_pipeline)
add_jplcz_reloco_demo(once_lock_config)
add_jplcz_reloco_demo(park_ping_pong)

add_custom_target(jplcz_reloco_demos
    DEPENDS
        scoped_workers
        channel_pipeline
        once_lock_config
        park_ping_pong
)
