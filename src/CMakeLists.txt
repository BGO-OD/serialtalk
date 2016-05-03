add_executable(serialtalk serialtalk.cpp)

 
install(TARGETS serialtalk DESTINATION bin)

install(FILES serialtalk.1 DESTINATION man/man1)
