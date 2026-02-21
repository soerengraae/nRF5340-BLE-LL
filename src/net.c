#include "nrf5340_network.h"

typedef enum {
  LL_STANDBY,
  LL_ADVERTISING,
  LL_SCANNING,
  LL_INITIATING,
  LL_CONNECTION,
  LL_SYNCHRONIZATION,
  LL_ISOCHRONOUS
} LL_state;

/* BLE advertising PDU stored in RAM.
 * Layout: [S0=header][LENGTH][payload...] as configured by PCNF0.
 */
typedef struct __attribute__((packed)) {
    uint8_t header; /* PDU type | TxAdd << 6 | RxAdd << 7 */
    uint8_t length; /* Payload length (RADIO reads this for on-air length) */
    uint8_t adv_addr[6]; /* Advertiser address */
    uint8_t adv_data[31]; /* AD structures */
} BleAdvPdu;

LL_state state = LL_STANDBY;

volatile uint8_t timeout = 0;

static const uint8_t adv_ch_freq[] = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42,
                                      44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80};
static const uint8_t adv_ch_num[] = {37, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 38, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                     20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 39};

uint32_t adv_interval = 100; // Interval in 10ms
uint32_t adv_window = 10; // Window in 10ms

static void build_adv_pdu(BleAdvPdu *pdu)
{
    /* Read device address from FICR */
    uint32_t mac0 = NRF_FICR_NS->DEVICEADDR[0];
    uint16_t mac1 = NRF_FICR_NS->DEVICEADDR[1];

    /* ADV_NONCONN_IND (0x02), TxAdd=1 (random address) */
    pdu->header = 0x02 | (1 << 6);

    /* AdvA: 6-byte advertiser address (little-endian from FICR) */
    pdu->adv_addr[0] = (mac0 >>  0) & 0xFF;
    pdu->adv_addr[1] = (mac0 >>  8) & 0xFF;
    pdu->adv_addr[2] = (mac0 >> 16) & 0xFF;
    pdu->adv_addr[3] = (mac0 >> 24) & 0xFF;
    pdu->adv_addr[4] = (mac1 >>  0) & 0xFF;
    pdu->adv_addr[5] = (mac1 >>  8) & 0xFF;
    pdu->adv_addr[5] |= 0xC0; /* Set two MSBs for random static address */

    /* AD structure 1: Flags (LE General Discoverable + BR/EDR Not Supported) */
    uint8_t i = 0;
    pdu->adv_data[i++] = 0x02;  /* AD Length */
    pdu->adv_data[i++] = 0x01;  /* AD Type: Flags */
    pdu->adv_data[i++] = 0x06;  /* Flags value */

    /* AD structure 2: Complete Local Name "nRF5340" */
    pdu->adv_data[i++] = 0x08;  /* AD Length (1 type + 7 name chars) */
    pdu->adv_data[i++] = 0x09;  /* AD Type: Complete Local Name */
    pdu->adv_data[i++] = 'n';
    pdu->adv_data[i++] = 'R';
    pdu->adv_data[i++] = 'F';
    pdu->adv_data[i++] = '5';
    pdu->adv_data[i++] = '3';
    pdu->adv_data[i++] = '4';
    pdu->adv_data[i++] = '0';

    /* Total payload length: 6 (Address) + AD data bytes */
    pdu->length = 6 + i;
}

static void timer0_configure(void) {
    NVIC_SetPriority(TIMER0_IRQn, 3);
    NVIC_EnableIRQ(TIMER0_IRQn);

    NRF_TIMER0_NS->MODE = 0; // Timer mode
    NRF_TIMER0_NS->PRESCALER = 4; // 1 MHz counter (1us)
    NRF_TIMER0_NS->BITMODE = 0; // 16-bit CC register
    NRF_TIMER0_NS->CC[0] = 1000; // CC event at 1ms
    NRF_TIMER0_NS->INTEN |= (1 << 16); // Enable interrupts for CC0
}

static void rtc0_configure(void) {
    NVIC_SetPriority(RTC0_IRQn, 4);
    NVIC_EnableIRQ(RTC0_IRQn);

    NRF_RTC0_NS->PRESCALER = 327; // 100 Hz counter (10ms)
    NRF_RTC0_NS->CC[0] = adv_interval; // CC0 event at interval
    NRF_RTC0_NS->CC[1] = adv_window + adv_interval; // CC1 event at window after the interval
    NRF_RTC0_NS->INTENSET |= (0x3 << 16); // Enable interrupts for CC0 and CC1
}

static void radio_configure(void) {
  /* BLE 1 Mbit/s mode (MODE.MODE = 3) */
  NRF_RADIO_NS->MODE = 3UL;

  /* Packet config 0:
   * LFLEN[3:0]  = 8  (length field = 8 bits)
   * S0LEN[8]    = 1  (S0 field = 1 byte, maps to PDU header)
   * PLEN[25:24] = 0  (8-bit preamble for BLE 1M)
   */
  NRF_RADIO_NS->PCNF0 = (8UL << 0)    /* LFLEN */
                        | (1UL << 8); /* S0LEN */

  /* Packet config 1:
   * MAXLEN[7:0]  = 37 (max advertising payload)
   * BALEN[18:16] = 3  (3 base + 1 prefix = 4 byte access address)
   * WHITEEN[25]  = 1  (data whitening enabled, required by BLE)
   */
  NRF_RADIO_NS->PCNF1 = (37UL << 0)    /* MAXLEN */
                        | (3UL << 16)  /* BALEN */
                        | (1UL << 25); /* WHITEEN: Enabled */

  /* BLE advertising access address: 0x8E89BED6
   * (Bluetooth Core Spec 5.4, Vol 6, Part B, Section 2.1.2)
   * PREFIX0.AP0 = 0x8E (most significant byte)
   * BASE0       = 0x89BED600 (remaining 3 bytes, left-aligned)
   */
  NRF_RADIO_NS->BASE0 = 0x89BED600UL;
  NRF_RADIO_NS->PREFIX0 = 0x0000008EUL;
  NRF_RADIO_NS->TXADDRESS = 0; /* Use logical address 0 for TX */

  /* CRC: 3 bytes, skip address field
   * LEN[1:0]      = 3 (three byte CRC)
   * SKIPADDR[9:8]  = 1 (exclude address from CRC calculation)
   * BLE CRC polynomial: x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1
   * BLE advertising CRC init: 0x555555
   */
  NRF_RADIO_NS->CRCCNF = (3UL << 0)    /* LEN: Three */
                       | (1UL << 8); /* SKIPADDR: Skip */
  NRF_RADIO_NS->CRCPOLY = 0x00065BUL;
  NRF_RADIO_NS->CRCINIT = 0x555555UL;

  /* TX power: 0 dBm */
  NRF_RADIO_NS->TXPOWER = 0UL;

  // (Bluetooth Core Spec 5.4, Vol 6, Part B, Section 4.1.1)
  NRF_RADIO_NS->TIFS = 150UL;
}

int main(void)
{
    NRF_CLOCK_NS->TASKS_HFCLKSTART = 1;
    while(!NRF_CLOCK_NS->EVENTS_HFCLKSTARTED);

    NRF_P0_NS->DIRSET |= (1 << 28);
    NRF_P0_NS->OUTSET |= (1 << 28);

    BleAdvPdu tx_pdu = {0};
    BleAdvPdu rx_pdu = {0};

    timer0_configure();
    rtc0_configure();
    radio_configure();
    build_adv_pdu(&tx_pdu);

    NRF_RTC0_NS->TASKS_START = 1;

    while (1)
    {
        switch (state)
        {
        case LL_STANDBY:
        break;

        case LL_ADVERTISING:
        for (uint8_t ch = 0; ch < 3; ch++)
        {
            NRF_RADIO_NS->FREQUENCY = adv_ch_freq[ch];
            NRF_RADIO_NS->DATAWHITEIV = adv_ch_num[ch];

            /* Clear events before starting */
            NRF_RADIO_NS->EVENTS_READY = 0;
            NRF_RADIO_NS->EVENTS_END = 0;
            NRF_RADIO_NS->EVENTS_DISABLED = 0;

            /* Set pointer to TX pdu */
            NRF_RADIO_NS->PACKETPTR = (uint32_t)&tx_pdu;

            NRF_RADIO_NS->SHORTS = (1 << 0) // READY_START
                                 |  (1 << 1) // END_DISABLE
                                 |  (1 << 3); // DISABLED_RXEN

            /* Ramp up TX - shortcuts handle the rest up until RXEN */
            NRF_RADIO_NS->TASKS_TXEN = 1;

            while(NRF_RADIO_NS->STATE != 11);
            // Now in TX - safe to re-set pointer
            NRF_RADIO_NS->PACKETPTR = (uint32_t)&rx_pdu;

            while(NRF_RADIO_NS->STATE != 3);
            // Now in RX - safe to disable shortcut DISABLED_RXEN
            NRF_RADIO_NS->SHORTS &= ~(1 << 3);

            NRF_TIMER0_NS->TASKS_START = 1;
            while(!NRF_RADIO_NS->EVENTS_END && !timeout);
            if (NRF_RADIO_NS->EVENTS_END) {
                NRF_RADIO_NS->EVENTS_END = 0;
            } else {
                timeout = 0;
            }
            NRF_RADIO_NS->TASKS_DISABLE = 1;
            while(!NRF_RADIO_NS->EVENTS_DISABLED);
        }
        break;

        default:
            state = LL_STANDBY;
        break;
        }
    }

    return 0;
}

// RX timeout handler
void TIMER0_IRQHandler(void) {
    NRF_TIMER0_NS->TASKS_STOP = 1;
    NRF_TIMER0_NS->TASKS_CLEAR = 1;
    NRF_TIMER0_NS->EVENTS_COMPARE[0] = 0;
    timeout = 1;
}

// Window and interval handler
void RTC0_IRQHandler(void) {
    if (NRF_RTC0_NS->EVENTS_COMPARE[0]) {
        // Interval reached - start advertising
        NRF_RTC0_NS->EVENTS_COMPARE[0] = 0;
        NRF_P0_NS->OUTCLR |= (1 << 28);
        state = LL_ADVERTISING;
    } else if (NRF_RTC0_NS->EVENTS_COMPARE[1]) {
        // Window limit reached - stop advertising
        NRF_RTC0_NS->TASKS_CLEAR = 1;
        NRF_RTC0_NS->EVENTS_COMPARE[1] = 0;
        NRF_P0_NS->OUTSET |= (1 << 28);
        state = LL_STANDBY;
    }
}