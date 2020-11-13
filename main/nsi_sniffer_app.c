/* ESP32 RMT NSI Sniffer
 *
 * ESP32 app demonstrating sniffing Nintendo's serial interface via RMT peripheral.
 *
*/

#include <esp_timer.h>
#include <driver/rmt.h>

#define NSI_FRAME_MAX 64
#define NSI_BIT_PERIOD_TICKS 8

static const uint8_t NSI_CMD_LEN[256] = {
/* 0x00 */ 0x01, 0x01, 0x03, 0x23, 0x00, 0x00, 0x00, 0x00,
/* 0x08 */ 0x01, 0x03, 0x17, 0x03, 0x07, 0x03, 0x00, 0x00,
/* 0x10 */ 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
/* 0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x30 */ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x40 */ 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x48 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
/* 0x50 */ 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
/* 0x58 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x60 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x68 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x70 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x78 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x88 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x90 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x98 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xA0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xA8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xB0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xB8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xC0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xC8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xD0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xD8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xE0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xE8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xF0 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0xF8 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static uint32_t cur_us = 0;
static uint8_t nsi_frame[NSI_FRAME_MAX];

static uint8_t IRAM_ATTR nsi_bit_parser(rmt_item32_t *items, uint8_t *data) {
    uint8_t byte;
    uint16_t item, bit, skip, stop, duration;

    for (byte = 0, item = 0, bit = 0, skip = 8, stop = 0;
         items[item].duration1;
         item++, bit++, byte = bit >> 3) {
        duration = items[item].duration0 + items[item].duration1;
        if (item == 8)
            skip = NSI_CMD_LEN[data[byte - 1]] * 8;
        if (duration < 7 || duration > 9) {
            stop = item;
            if (!skip && item > 8)
                skip = stop;
        }
        if (item == skip)
            item++;
        data[byte] <<= 1;
        if (items[item].duration1 > (NSI_BIT_PERIOD_TICKS / 2))
            data[byte] |= 1;
        if (!((bit + 1) % 8))
            ets_printf("%02X", data[byte]);
    }
    ets_printf(" %d\n", stop);
    return byte;
}

static void IRAM_ATTR rmt_isr(void *arg) {
    const uint32_t intr_st = RMT.int_st.val;
    uint32_t status = intr_st;
    uint8_t i, channel;
    uint32_t pre_us;

    while (status) {
        i = __builtin_ffs(status) - 1;
        status &= ~(1 << i);
        channel = i / 3;
        switch (i % 3) {
            /* RX End */
            case 1:
                RMT.conf_ch[channel].conf1.rx_en = 0;
                RMT.conf_ch[channel].conf1.mem_owner = RMT_MEM_OWNER_TX;

                pre_us = cur_us;
                cur_us = xthal_get_ccount();;
                ets_printf("+%07u: ", (cur_us - pre_us)/CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ);
                nsi_bit_parser((rmt_item32_t *)RMTMEM.chan[channel].data32, nsi_frame);

                RMT.conf_ch[channel].conf1.mem_wr_rst = 1;
                RMT.conf_ch[channel].conf1.mem_owner = RMT_MEM_OWNER_RX;
                RMT.conf_ch[channel].conf1.rx_en = 1;
                break;
            /* Error */
            case 2:
                ets_printf("RMT ERR INT\n");
                RMT.int_ena.val &= (~(BIT(i)));
                break;
            default:
                break;
        }
    }
    RMT.int_clr.val = intr_st;
}

void app_main() {
    rmt_config_t config;

    config.channel       = RMT_CHANNEL_0;
    config.gpio_num      = GPIO_NUM_19;
    config.clk_div       = 40; /* 80MHz (APB CLK) / 40 = 0.5us TICK */
    config.mem_block_num = 8;  /* Assign all channels RMTMEM to channel 0 */
    config.rmt_mode      = RMT_MODE_RX;

    config.rx_config.filter_en           = 0; /* No minimum length */
    config.rx_config.filter_ticks_thresh = 0;
    config.rx_config.idle_threshold      = (NSI_BIT_PERIOD_TICKS * 4);

    rmt_config(&config);
    rmt_set_rx_intr_en(config.channel, 1);
    rmt_set_err_intr_en(config.channel, 1);
    rmt_isr_register(rmt_isr, NULL, 0, NULL);
    rmt_rx_start(config.channel, 1);
}
