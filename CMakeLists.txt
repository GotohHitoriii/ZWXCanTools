cmake_minimum_required(VERSION 3.24)

project(ZWXCanTools VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets Svg)

qt_add_executable(ZWXCanTools WIN32
    src/ai_command_bridge.cpp
    src/ai_command_bridge.h
    src/can_device_backend.cpp
    src/can_device_backend.h
    src/device_controller.cpp
    src/device_controller.h
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    resources/resources.qrc
)

target_link_libraries(ZWXCanTools
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::WebSockets
        Qt6::Svg
)

target_compile_definitions(ZWXCanTools PRIVATE
    QT_NO_KEYWORDS
)

set(CAN_SDK_DIR
    "${CMAKE_SOURCE_DIR}/third_party/can_sdk/x64"
    CACHE PATH "Local path to the third-party CAN SDK runtime directory. This directory is not redistributed."
)

set(CAN_SDK_DRIVER_DLL
    "${CAN_SDK_DIR}/can_driver.dll"
    CACHE FILEPATH "Local path to the third-party CAN SDK driver DLL."
)

set(CAN_SDK_KERNEL_DIR
    "${CAN_SDK_DIR}/kerneldlls"
    CACHE PATH "Local path to the third-party CAN SDK runtime dependency directory."
)

if(EXISTS "${CAN_SDK_DRIVER_DLL}" AND EXISTS "${CAN_SDK_KERNEL_DIR}")
    add_custom_command(TARGET ZWXCanTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CAN_SDK_DRIVER_DLL}"
            "$<TARGET_FILE_DIR:ZWXCanTools>/can_driver.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CAN_SDK_KERNEL_DIR}"
            "$<TARGET_FILE_DIR:ZWXCanTools>/kerneldlls"
    )
else()
    message(WARNING
        "Third-party CAN SDK runtime files were not found. "
        "The application can still be built, but CAN hardware access will require local third-party runtime files."
    )
endif()
