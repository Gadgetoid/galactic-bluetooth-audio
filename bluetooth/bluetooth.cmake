set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)
set(BTSTACK_EXAMPLE_PATH ${BTSTACK_ROOT}/example)
set(BTSTACK_3RD_PARTY_PATH ${BTSTACK_ROOT}/3rd-party)

set(BTSTACK_CONFIG_PATH ${CMAKE_CURRENT_LIST_DIR}/config)

add_library(picow_bt_example_common INTERFACE)

target_sources(picow_bt_example_common INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/common.cpp
    ${PICO_BTSTACK_PATH}/src/btstack_audio.c
)

target_link_libraries(picow_bt_example_common INTERFACE
    pico_stdlib
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
    pico_btstack_sbc_decoder
    pico_cyw43_arch_threadsafe_background
)

target_include_directories(picow_bt_example_common INTERFACE
    ${BTSTACK_CONFIG_PATH}/ # Use our own config
    ${BTSTACK_EXAMPLE_PATH}/
)

target_compile_definitions(picow_bt_example_common INTERFACE
    TEST_AUDIO=1
    CYW43_LWIP=0
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000
    #WANT_HCI_DUMP=1 # This enables btstack debug
    #ENABLE_SEGGER_RTT=1
)