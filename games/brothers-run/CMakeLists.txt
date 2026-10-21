yk_add_test(brothers_run tests/brothers_run_tests.cpp LIBS yk::gameplay TIMEOUT 120
    DEFINITIONS YK_BROTHERS_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
