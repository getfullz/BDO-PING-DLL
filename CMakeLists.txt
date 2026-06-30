cmake_minimum_required(VERSION 3.15)
project(BDO_Ping_Plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/exports.def")

add_library(${PROJECT_NAME} SHARED 
    src/dllmain.cpp
    src/ETWMonitor.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    tdh.lib
    ws2_32.lib
    iphlpapi.lib
)

# Installation path for MSI Afterburner
set(AFTERBURNER_PATH "C:/Program Files (x86)/MSI Afterburner" CACHE PATH "Path to MSI Afterburner installation")
set(INSTALL_TO_AFTERBURNER ON CACHE BOOL "Automatically copy DLL to Afterburner plugins folder after build")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "BDO_Ping_x64")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "BDO_Ping_x86")
endif()

# Post-build actions: Copy DLL to Afterburner and delete log
if(INSTALL_TO_AFTERBURNER AND EXISTS "${AFTERBURNER_PATH}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        # Copy the compiled DLL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:${PROJECT_NAME}>" 
            "${AFTERBURNER_PATH}/Plugins/Monitoring/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        
        # Delete the old log file (does not error if file is missing)
        COMMAND ${CMAKE_COMMAND} -E remove 
            "${AFTERBURNER_PATH}/BDO_Ping_Plugin_Log.txt"
        
        COMMENT "Copying $<TARGET_FILE_NAME:${PROJECT_NAME}> to MSI Afterburner and cleaning up log..."
    )
endif()
