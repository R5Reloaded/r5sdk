cmake_minimum_required( VERSION 3.16 )
add_module( "lib" "materialsystem" "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Private"
    "cmaterialglue.cpp"
    "cmaterialglue.h"
    "cmaterialsystem.cpp"
    "cmaterialsystem.h"
    "cmatrendercontext.cpp"
    "cmatrendercontext.h"
    "cmatqueuedrendercontext.cpp"
    "cmatqueuedrendercontext.h"
    "constbuffer.cpp"
    "constbuffer.h"
    "cshaderglue.cpp"
    "cshaderglue.h"
    "meshbuilder.cpp"
    "texturestreaming.cpp"
    "texturestreaming.h"
)

add_sources( SOURCE_GROUP "Public"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/imaterial.h"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/imaterialinternal.h"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/imaterialsystem.h"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/meshbuilder.h"
)

end_sources()

add_module( "lib" "materialsystem_nodx" "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Private"
    "cmaterialsystem.cpp"
    "cmaterialsystem.h"
    "meshbuilder.cpp"
)

add_sources( SOURCE_GROUP "Public"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/imaterialsystem.h"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/imatrendercontext.h"
    "${ENGINE_SOURCE_DIR}/public/materialsystem/meshbuilder.h"
)

end_sources()

target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "MATERIALSYSTEM_NODX"
)