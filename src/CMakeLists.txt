add_library(Token Token.cpp Token.hpp)

add_library(Parser Parser.cpp Parser.hpp)
target_link_libraries(Parser PUBLIC Token ${llvm_libs})

add_executable(Repl Repl.cpp)
target_link_libraries(Repl Parser Token ${llvm_libs})
