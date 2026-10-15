#
# setup
#
set( MAP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}  CACHE INTERNAL "" )


#
# map server
#
if( BUILD_SERVERS )
message( STATUS "Creating target map-server" )
file(GLOB_RECURSE MAP_HEADERS ${MAP_SOURCE_DIR}/*.hpp)
list(FILTER MAP_HEADERS EXCLUDE REGEX ".*/skills/.*")
file(GLOB_RECURSE MAP_SOURCES ${MAP_SOURCE_DIR}/*.cpp)
list(FILTER MAP_SOURCES EXCLUDE REGEX ".*/skills/.*")
# Add skill factory/impl files as separate translation units per job category
# Individual skill files are NOT compiled directly - they are #included by their job factory TUs
file(GLOB SKILL_FACTORY_SOURCES ${MAP_SOURCE_DIR}/skills/skill_factory.cpp ${MAP_SOURCE_DIR}/skills/skill_impl.cpp)
file(GLOB SKILL_JOB_FACTORY_SOURCES ${MAP_SOURCE_DIR}/skills/*/skill_factory_*.cpp)
list(APPEND MAP_SOURCES ${SKILL_FACTORY_SOURCES} ${SKILL_JOB_FACTORY_SOURCES})
# Pure rule library the legacy call sites delegate to (REIMAGINED_ARCHITECTURE.md §3.2);
# compiled into the server like the other sources (src/map/rules_bridge.* connects them).
set( MAP_RULES_DIR "${CMAKE_SOURCE_DIR}/modernized/rathena-reimagined/map-rules" )
file(GLOB MAP_RULES_SOURCES ${MAP_RULES_DIR}/src/*.cpp)
file(GLOB MAP_RULES_HEADERS ${MAP_RULES_DIR}/include/rathena/map_rules/*.hpp)
list(APPEND MAP_SOURCES ${MAP_RULES_SOURCES})
list(APPEND MAP_HEADERS ${MAP_RULES_HEADERS})
file(GLOB_RECURSE SKILL_HEADERS ${MAP_SOURCE_DIR}/skills/*.hpp)
list(APPEND MAP_HEADERS ${SKILL_HEADERS})
set( DEPENDENCIES common ryml)
set( LIBRARIES ${GLOBAL_LIBRARIES} )
set( INCLUDE_DIRS ${GLOBAL_INCLUDE_DIRS} ${COMMON_BASE_INCLUDE_DIRS} ${RA_INCLUDE_DIRS} ${MAP_RULES_DIR}/include )
set( DEFINITIONS "${GLOBAL_DEFINITIONS} ${COMMON_BASE_DEFINITIONS}" )
if( WITH_PCRE )
	message( STATUS "Enabled PCRE code" )
	set( LIBRARIES ${LIBRARIES} ${PCRE_LIBRARIES} )
	set( INCLUDE_DIRS ${INCLUDE_DIRS} ${PCRE_INCLUDE_DIRS} )
	set( DEFINITIONS "${DEFINITIONS} -DPCRE_SUPPORT" )
else()
	message( STATUS "Disabled PCRE code" )
endif()
set( SOURCE_FILES ${COMMON_BASE_HEADERS} ${COMMON_HEADERS} ${MAP_HEADERS} ${MAP_SOURCES} )
source_group( common FILES ${COMMON_BASE_HEADERS} ${COMMON_HEADERS} )
source_group( map FILES ${MAP_HEADERS} ${MAP_SOURCES} )
include_directories( ${INCLUDE_DIRS} )

#message( STATUS "map-server SOURCE_FILES=${SOURCE_FILES}")
add_executable( map-server ${SOURCE_FILES} )
#message( STATUS "map-server LIBRARIES=${LIBRARIES}, DEPENDENCIES=${DEPENDENCIES} DEFINITIONS=${DEFINITIONS}")
add_dependencies( map-server ${DEPENDENCIES} )
target_link_libraries( map-server ${LIBRARIES} ${DEPENDENCIES} )
set_target_properties( map-server PROPERTIES COMPILE_FLAGS "${DEFINITIONS}" )
if( INSTALL_COMPONENT_RUNTIME )
	cpack_add_component( Runtime_mapserver DESCRIPTION "map-server" DISPLAY_NAME "map-server" GROUP Runtime )
	install( TARGETS map-server
		DESTINATION "."
		COMPONENT Runtime_mapserver )
endif( INSTALL_COMPONENT_RUNTIME )
set( TARGET_LIST ${TARGET_LIST} map-server  CACHE INTERNAL "" )
message( STATUS "Creating target map-server - done" )
endif( BUILD_SERVERS )
