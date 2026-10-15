#
# Database layer tests (src/common/sql.cpp, sql_dialect.cpp). Enabled with -DBUILD_TESTING=ON.
#
message( STATUS "Creating target sql-test" )
add_executable( sql-test "${CMAKE_CURRENT_SOURCE_DIR}/sql_test.cpp" )
add_dependencies( sql-test common )
target_link_libraries( sql-test common common_base ${SQLITE_LIBRARIES} ${POSTGRESQL_LIBRARIES} ${GLOBAL_LIBRARIES} )
target_include_directories( sql-test PRIVATE ${GLOBAL_INCLUDE_DIRS} ${COMMON_BASE_INCLUDE_DIRS} ${RA_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src" )
set( SQL_TEST_DEFINITIONS "${GLOBAL_DEFINITIONS} ${COMMON_BASE_DEFINITIONS} -DRATHENA_SOURCE_DIR=\\\"${CMAKE_SOURCE_DIR}\\\"" )
if( HAVE_POSTGRESQL )
	set( SQL_TEST_DEFINITIONS "${SQL_TEST_DEFINITIONS} -DHAVE_POSTGRESQL" )
endif()
set_target_properties( sql-test PROPERTIES COMPILE_FLAGS "${SQL_TEST_DEFINITIONS}" )
add_test( NAME sql_test COMMAND sql-test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )
message( STATUS "Creating target sql-test - done" )
