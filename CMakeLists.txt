# Check if cmake has the required version
CMAKE_MINIMUM_REQUIRED(VERSION 2.6.0...5.0.0 FATAL_ERROR)

# Set project name
PROJECT(serialtalk)

ADD_SUBDIRECTORY(src)

SET(MANDIR "share/man/man1" CACHE FILEPATH "mandir")
INSTALL(FILES serialtalk.1 DESTINATION ${MANDIR})
