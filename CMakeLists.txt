project( eventually )

cmake_minimum_required( VERSION 2.8 )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11" )

find_library( APP_SERVICES_LIBRARY ApplicationServices )
find_library( COCOA_LIBRARY Cocoa )

add_executable( eventually
	src/main.cpp
)

target_link_libraries( eventually
	${APP_SERVICES_LIBRARY}
	${COCOA_LIBRARY}
)

install( TARGETS
	eventually
	DESTINATION
	bin
)
