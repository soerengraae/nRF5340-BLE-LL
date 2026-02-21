#include "nrf5340_application.h"

#define LED1_PIN 28

int main(void)
{
    /* Start the low-frequency clock required for RTC */
    NRF_CLOCK_S->TASKS_LFCLKSTART = 1;
    while (!NRF_CLOCK_S->EVENTS_LFCLKSTARTED);

    /**
     * Assign P0.28 (LED1) to the network core:
     * 1. Mark pin as non-secure in SPU so network core can access it
     * 2. Set MCUSEL to NetworkMCU so the pin is routed to the network core
     */
    NRF_SPU_S->GPIOPORT[0].PERM &= ~(1 << LED1_PIN);
    NRF_P0_S->PIN_CNF[LED1_PIN] = (1 << 28);

    NRF_SPU_S->PERIPHID[20].PERM &= ~(1UL << 4); // Mark RTC0 as non-secure for network core
    // NRF_SPU_S->PERIPHID[15].PERM &= ~(1UL << 4); // Mark TIMER0 as non-secure for network core

    NRF_RESET_S->NETWORK.FORCEOFF = 0;

    /* Application core has nothing else to do */
    while (1) {
        __WFE();
    }

    return 0;
}