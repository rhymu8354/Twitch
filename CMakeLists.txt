# CMakeLists.txt for Twitch
#
# © 2018 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This Twitch)

set(Headers
    include/Twitch/Connection.hpp
    include/Twitch/Messaging.hpp
    include/Twitch/TimeKeeper.hpp
)

set(Sources
    src/Messaging.cpp
)

add_library(${This} STATIC ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Libraries
)

target_include_directories(${This} PUBLIC include)

add_subdirectory(test)
