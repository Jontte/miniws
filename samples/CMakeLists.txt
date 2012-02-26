
PROJECT(samples)

add_executable(basic basic.cpp)
target_link_libraries(basic miniws)
target_link_libraries(basic ${Boost_LIBRARIES})

add_executable(chat chat.cpp)
target_link_libraries(chat miniws)
target_link_libraries(chat ${Boost_LIBRARIES})


