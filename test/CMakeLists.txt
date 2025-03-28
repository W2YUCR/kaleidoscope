add_executable(TestToken TestToken.cpp)
target_link_libraries(TestToken PUBLIC Token)

foreach(test Number Function)
    add_test(NAME TestToken_${test} COMMAND TestToken ${test})
endforeach()
