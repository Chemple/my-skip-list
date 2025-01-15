file(GLOB_RECURSE SRCS "*.cpp")

add_library(skiplist SHARED ${SRCS})

target_include_directories(skiplist PUBLIC ../include)
