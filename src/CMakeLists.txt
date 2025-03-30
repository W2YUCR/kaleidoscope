add_subdirectory(compiler)

add_executable(Repl Repl.cpp)
target_link_libraries(Repl Compiler Token ${llvm_libs})
