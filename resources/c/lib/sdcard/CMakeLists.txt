aux_source_directory(. DIR_SDCARD_SRCS)

include_directories(../config)

add_library(sdcard ${DIR_SDCARD_SRCS})
target_link_libraries(sdcard PUBLIC config)
