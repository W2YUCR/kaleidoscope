add_library(Token Token.cpp Token.hpp)

add_library(Parser Parser.cpp Parser.hpp)
target_link_libraries(Parser PUBLIC Token ${llvm_libs})

add_library(Compiler INTERFACE)
target_link_libraries(Compiler INTERFACE Parser)
