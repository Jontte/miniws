cmake_minimum_required(VERSION 2.4)
	
FIND_LIBRARY(BOOST >= 1.46.0)
ADD_LIBRARY(miniws server.cpp connection.cpp base64.cpp)
find_package(Boost COMPONENTS system REQUIRED)

target_link_libraries(mytarget
	${Boost_SYSTEM_LIBRARY}
)
