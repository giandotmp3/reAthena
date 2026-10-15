# SQLite 3.53.4 amalgamation (public domain), vendored so every platform builds the embedded
# database engine with no download or install. sqlite3.c matches the SHA3-256 published in the
# official release log (67f423e9ebbbdc473cbc4772c872ee6b89f31fde4ed0279a5c25d5f65c043a16).
message( STATUS "Creating target sqlite3" )
add_library( sqlite3 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c" )
target_compile_definitions( sqlite3 PRIVATE
	SQLITE_DQS=0                   # "text" is always an identifier, never a string literal
	SQLITE_THREADSAFE=1
	SQLITE_OMIT_LOAD_EXTENSION     # no runtime extension loading
	SQLITE_OMIT_DEPRECATED
	SQLITE_DEFAULT_MEMSTATUS=0
	SQLITE_LIKE_DOESNT_MATCH_BLOBS
)
find_package( Threads REQUIRED )
target_link_libraries( sqlite3 PRIVATE Threads::Threads )
set_target_properties( sqlite3 PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON )
set( SQLITE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "" )
set( SQLITE_LIBRARIES sqlite3 CACHE INTERNAL "" )
message( STATUS "Creating target sqlite3 - done" )
