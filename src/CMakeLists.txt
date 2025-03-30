add_library(Token)
target_sources(Token PUBLIC FILE_SET CXX_MODULES FILES Token.cppm)

add_library(Parser)
target_sources(Parser PUBLIC FILE_SET CXX_MODULES FILES Parser.cppm)
target_link_libraries(Parser PUBLIC Token ${llvm_libs})

add_executable(Repl Repl.cpp)
target_link_libraries(Repl Parser Token ${llvm_libs})
