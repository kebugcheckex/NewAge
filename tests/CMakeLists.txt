find_package(Qt6 REQUIRED COMPONENTS Test)

# Tests that need game data use the sample .dat files in the gitignored data/
# folder (empires2_x1_p1.dat = The Conquerors, empires2_x2_p1.dat = HD Edition)
# and skip themselves when those are missing. Small checked-in inputs live in
# tests/data/.
function(newage_add_test name source)
  qt_add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${ARGN} Qt6::Test)
  target_compile_definitions(${name} PRIVATE
    NEWAGE_SAMPLE_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    NEWAGE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Also runs on NEWAGE_TEST_DAT (and NEWAGE_TEST_VERSION, a VersionProfile key)
# when set in the environment.
newage_add_test(roundtrip_test RoundTripTest.cpp newage_core)
newage_add_test(config_test ConfigTest.cpp newage_core)
# Also runs on a real install in NEWAGE_TEST_GAME_DIR when set.
newage_add_test(gamedata_test GameDataTest.cpp newage_core)
newage_add_test(model_test ModelTest.cpp newage_model)
newage_add_test(ui_test UnitBrowserTest.cpp newage_ui)
newage_deploy_qt_plugins(ui_test)
