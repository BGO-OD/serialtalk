# Check if cmake has the required version
CMAKE_MINIMUM_REQUIRED(VERSION 2.6.0 FATAL_ERROR)

# Set project name
PROJECT(serialtalk)

ADD_SUBDIRECTORY(src)
install(FILES serialtalk.1 DESTINATION man/man1)
