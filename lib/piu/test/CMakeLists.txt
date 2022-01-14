set(PIU_TEST_MODULES
    modules/loss_test.c
)

add_executable(main
    ${PIU_TEST_MODULES}
    main.c
)

target_include_directories(main
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

target_link_libraries(main
    pthread
    piu
)
