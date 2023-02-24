/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "btstack_audio.h"
#include "btstack_event.h"
#include "hal_led.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"


int btstack_main(int argc, const char * argv[]);

const btstack_audio_sink_t * btstack_audio_pico_sink_get_instance(void);

static btstack_packet_callback_registration_t hci_event_callback_registration;

static int led_state = 0;

void hal_led_toggle(void){
    led_state = 1 - led_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
}

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(size);
    UNUSED(channel);
    bd_addr_t local_addr;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            gap_local_bd_addr(local_addr);
            printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
            break;
        default:
            break;
    }
}

int picow_bt_example_init(void) {
    // initialize CYW43 driver architecture (will enable BT if/because CYW43_ENABLE_BLUETOOTH == 1)
    if (cyw43_arch_init()) {
        printf("failed to initialise cyw43_arch\n");
        return -1;
    }

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // setup i2s audio for sink
    btstack_audio_sink_set_instance(btstack_audio_pico_sink_get_instance());
    return 0;
}

void picow_bt_example_main(void) {

    btstack_main(0, NULL);

}
