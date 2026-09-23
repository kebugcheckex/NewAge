find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(roundtrip_test RoundTripTest.cpp)
target_link_libraries(roundtrip_test PRIVATE newage_core Qt6::Test)

add_test(NAME roundtrip_test COMMAND roundtrip_test)
# The data-file test is skipped unless NEWAGE_TEST_DAT (and optionally
# NEWAGE_TEST_VERSION, a VersionProfile key) are set in the environment.
