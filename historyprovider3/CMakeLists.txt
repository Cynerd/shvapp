add_library(libhistoryprovider STATIC
    src/appclioptions.cpp
    src/leafnode.cpp
    src/historyapp.cpp
    src/shvjournalnode.cpp
    src/valuecachenode.cpp
    src/utils.cpp
    )

target_include_directories(libhistoryprovider PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(libhistoryprovider PUBLIC libshv::libshviotqt Qt::Concurrent)

add_executable(historyprovider3
    src/main.cpp
    )

target_link_libraries(historyprovider3 libhistoryprovider)

install(TARGETS historyprovider3)

if(BUILD_TESTING)
    add_library(mockrpcconnection STATIC
        tests/mockrpcconnection.cpp
        tests/utils.cpp
        )
    target_link_libraries(mockrpcconnection libhistoryprovider)

    function(history_test name)
        set(TEST_NAME test_historyprovider_${name})
        add_executable(${TEST_NAME}
            tests/${name}.cpp
            )

        target_link_libraries(${TEST_NAME} libhistoryprovider mockrpcconnection)
        add_test(NAME ${TEST_NAME} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME})
    endfunction()

    history_test(synclog)
    history_test(slavehp)
    history_test(hp_getlog)
    history_test(pushlog)
    history_test(legacy_getlog)
    history_test(sanitizer)
    history_test(dirty_log_trimming)
    history_test(sites_timeout)
    history_test(connection_failure)
    history_test(value_cache)
    history_test(alarms)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vars.h.in ${CMAKE_CURRENT_BINARY_DIR}/test_vars.h @ONLY)
    target_include_directories(libhistoryprovider PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
endif()
