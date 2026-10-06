cmake_minimum_required( VERSION 3.16 )
add_module( "lib" "networksystem" "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Management"
    "bansystem.cpp"
    "bansystem.h"
    "hostmanager.cpp"
    "hostmanager.h"
    "listmanager.cpp"
    "listmanager.h"
)

add_sources( SOURCE_GROUP "Services"
    "pylon.cpp"
    "pylon.h"
    "stryder.cpp"
    "datacenters.cpp"
    "matchmaking.cpp"
    "party.cpp"
    "party.h"
)

add_sources( SOURCE_GROUP "Public"
    "serverlisting.h"
    "sm_protocol.h"
    "persistence.h"
    "datacenters.h"
    "matchmaking.h"
    "pylon.h"
)

end_sources()
