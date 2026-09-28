# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

find_package(Boost 1.77 CONFIG QUIET)

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
add_jplcz_reloco_demo(bucket_vector)
add_jplcz_reloco_demo(tls_local_slots_demo)
add_jplcz_reloco_demo(keyed_intrusive_registry_demo)

if(Boost_FOUND)
    add_jplcz_reloco_demo(intrusive_iteration_demo)
    target_link_libraries(intrusive_iteration_demo PRIVATE Boost::boost)
endif()

add_custom_target(jplcz_reloco_demos
    DEPENDS
        scoped_workers
        channel_pipeline
        once_lock_config
        park_ping_pong
)
