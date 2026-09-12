cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "$ENV{USERPROFILE}/.pico-sdk/sdk/2.3.1" CACHE PATH "Path to the Raspberry Pi Pico SDK")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(FUJIFILM_MR_Option_Loader C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/FatFs_SPI)

add_executable(FUJIFILM_MR_Option_Loader
    main.c
    usb_descriptors.c
)

target_include_directories(FUJIFILM_MR_Option_Loader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(FUJIFILM_MR_Option_Loader pico_stdlib hardware_i2c hardware_adc tinyusb_device tinyusb_board FatFs_SPI)

pico_enable_stdio_uart(FUJIFILM_MR_Option_Loader 0)
pico_enable_stdio_usb(FUJIFILM_MR_Option_Loader 0)

pico_add_extra_outputs(FUJIFILM_MR_Option_Loader)
