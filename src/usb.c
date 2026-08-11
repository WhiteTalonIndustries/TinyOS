#include <stdint.h>
#include "usb.h"

#define NULL ((void *)0)

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define CLOCKS_BASE 0x40008000u

/* ================= RESETS ================= */
#define RESETS_BASE 0x4000c000u
#define RESETS_RESET      REG32(RESETS_BASE + 0x00)
#define RESETS_RESET_DONE REG32(RESETS_BASE + 0x08)
#define RESET_BIT_PLL_SYS (1u << 12)
#define RESET_BIT_PLL_USB (1u << 13)
#define RESET_BIT_USBCTRL (1u << 24)

static void reset_block(uint32_t bit) {
    RESETS_RESET |= bit;
    RESETS_RESET &= ~bit;
    while (!(RESETS_RESET_DONE & bit));
}

/* ================= debug LED (temporary bring-up instrumentation) =================
 * Pimoroni Tiny 2040: green LED on GPIO19, active-low. */
#define IO_BANK0_BASE 0x40014000u
#define SIO_BASE      0xd0000000u
#define LED_PIN 19u

static void led_init(void) {
    reset_block((1u << 5) | (1u << 8)); /* IO_BANK0, PADS_BANK0 */
    REG32(IO_BANK0_BASE + 0x04 + LED_PIN * 8u) = 5u; /* GPIO19 func = SIO */
    REG32(SIO_BASE + 0x24) = (1u << LED_PIN); /* GPIO_OE_SET */
    REG32(SIO_BASE + 0x14) = (1u << LED_PIN); /* start high = off (active-low) */
}

static void led_delay(void) {
    volatile uint32_t n = 1500000u;
    while (n--);
}

static void led_blink(int times) {
    int i;
    for (i = 0; i < times; i++) {
        REG32(SIO_BASE + 0x18) = (1u << LED_PIN); /* CLR = low = on */
        led_delay();
        REG32(SIO_BASE + 0x14) = (1u << LED_PIN); /* SET = high = off */
        led_delay();
    }
    led_delay();
    led_delay();
}

/* Measures a clock (via the RP2040 frequency counter) and blinks it out as
 * two digit groups (tens, then ones) of the frequency in MHz. */
#define CLOCKS_FC0_REF_KHZ  REG32(CLOCKS_BASE + 0x80)
#define CLOCKS_FC0_MIN_KHZ  REG32(CLOCKS_BASE + 0x84)
#define CLOCKS_FC0_MAX_KHZ  REG32(CLOCKS_BASE + 0x88)
#define CLOCKS_FC0_INTERVAL REG32(CLOCKS_BASE + 0x90)
#define CLOCKS_FC0_SRC      REG32(CLOCKS_BASE + 0x94)
#define CLOCKS_FC0_STATUS   REG32(CLOCKS_BASE + 0x98)
#define CLOCKS_FC0_RESULT   REG32(CLOCKS_BASE + 0x9c)

static uint32_t measure_khz(uint32_t src) {
    while (CLOCKS_FC0_STATUS & (1u << 8)); /* RUNNING */
    CLOCKS_FC0_REF_KHZ = 12000u;
    CLOCKS_FC0_INTERVAL = 10u;
    CLOCKS_FC0_MIN_KHZ = 0u;
    CLOCKS_FC0_MAX_KHZ = 0xffffffffu;
    CLOCKS_FC0_SRC = src;
    while (!(CLOCKS_FC0_STATUS & (1u << 4))); /* DONE */
    return CLOCKS_FC0_RESULT >> 5;
}

static void led_pause_long(void) {
    int i;
    for (i = 0; i < 6; i++) led_delay();
}

/* 1 slow blink = measured frequency is within tolerance (good).
 * 10 rapid blinks = measured frequency is out of range (bad). */
static void check_khz(uint32_t src, uint32_t expect_khz, uint32_t tolerance_khz) {
    uint32_t khz = measure_khz(src);
    uint32_t lo = expect_khz - tolerance_khz;
    uint32_t hi = expect_khz + tolerance_khz;
    if (khz > lo && khz < hi) {
        led_blink(1);
    } else {
        led_blink(10);
    }
    led_pause_long();
}

/* ================= XOSC ================= */
#define XOSC_BASE 0x40024000u
#define XOSC_CTRL    REG32(XOSC_BASE + 0x00)
#define XOSC_STATUS  REG32(XOSC_BASE + 0x04)
#define XOSC_STARTUP REG32(XOSC_BASE + 0x0c)

static void xosc_init(void) {
    XOSC_STARTUP = 47; /* startup delay for a 12MHz crystal */
    XOSC_CTRL = 0xaa0u | (0xfabu << 12); /* FREQ_RANGE 1-15MHz, ENABLE */
    while (!(XOSC_STATUS & (1u << 31))); /* wait STABLE */
}

/* ================= PLL (shared layout for PLL_SYS and PLL_USB) ================= */
#define PLL_SYS_BASE 0x40028000u
#define PLL_USB_BASE 0x4002c000u

static void pll_init(uint32_t base, uint32_t reset_bit, uint32_t refdiv, uint32_t fbdiv, uint32_t postdiv1, uint32_t postdiv2) {
    reset_block(reset_bit);

    REG32(base + 0x00) = refdiv;      /* CS */
    REG32(base + 0x08) = fbdiv;       /* FBDIV_INT */
    REG32(base + 0x04) &= ~((1u << 0) | (1u << 5)); /* PWR: clear PD, VCOPD */

    while (!(REG32(base + 0x00) & (1u << 31))); /* wait CS.LOCK */

    REG32(base + 0x0c) = (postdiv1 << 16) | (postdiv2 << 12); /* PRIM */
    REG32(base + 0x04) &= ~(1u << 3); /* PWR: clear POSTDIVPD */
}

/* ================= CLOCKS ================= */
#define CLK_REF_CTRL     REG32(CLOCKS_BASE + 0x30)
#define CLK_REF_DIV      REG32(CLOCKS_BASE + 0x34)
#define CLK_REF_SELECTED REG32(CLOCKS_BASE + 0x38)
#define CLK_SYS_CTRL     REG32(CLOCKS_BASE + 0x3c)
#define CLK_SYS_DIV      REG32(CLOCKS_BASE + 0x40)
#define CLK_SYS_SELECTED REG32(CLOCKS_BASE + 0x44)
#define CLK_PERI_CTRL    REG32(CLOCKS_BASE + 0x48)
#define CLK_USB_CTRL     REG32(CLOCKS_BASE + 0x54)
#define CLK_USB_DIV      REG32(CLOCKS_BASE + 0x58)
#define CLK_ADC_CTRL     REG32(CLOCKS_BASE + 0x60)
#define CLK_ADC_DIV      REG32(CLOCKS_BASE + 0x64)

static void clocks_init(void) {
    /* Switch clk_sys/clk_ref glitchlessly away from their aux inputs before
     * touching the PLLs that feed those inputs. */
    CLK_SYS_CTRL &= ~0x1u;
    while (CLK_SYS_SELECTED != 0x1u);
    CLK_REF_CTRL &= ~0x3u;
    while (CLK_REF_SELECTED != 0x1u);

    xosc_init();

    pll_init(PLL_SYS_BASE, RESET_BIT_PLL_SYS, 1, 125, 6, 2); /* 12MHz *125 /12 = 125MHz */
    pll_init(PLL_USB_BASE, RESET_BIT_PLL_USB, 1, 100, 5, 5); /* 12MHz *100 /25 = 48MHz */

    CLK_REF_CTRL = (CLK_REF_CTRL & ~0x3u) | 0x2u; /* src = xosc */
    while (!(CLK_REF_SELECTED & (1u << 2)));
    CLK_REF_DIV = (1u << 8);

    CLK_SYS_CTRL = (CLK_SYS_CTRL & ~(0x7u << 5)); /* aux = clksrc_pll_sys */
    CLK_SYS_CTRL |= 0x1u;                         /* src = clksrc_clk_sys_aux */
    while (!(CLK_SYS_SELECTED & (1u << 1)));
    CLK_SYS_DIV = (1u << 8);

    CLK_USB_CTRL = (CLK_USB_CTRL & ~(0x7u << 5)); /* aux = clksrc_pll_usb */
    CLK_USB_DIV = (1u << 8);
    CLK_USB_CTRL |= (1u << 11); /* enable */

    CLK_PERI_CTRL = (CLK_PERI_CTRL & ~(0x7u << 5)); /* aux = clk_sys */
    CLK_PERI_CTRL |= (1u << 11);                    /* enable */

    CLK_ADC_CTRL = (CLK_ADC_CTRL & ~(0x7u << 5)); /* aux = clksrc_pll_usb */
    CLK_ADC_DIV = (1u << 8);
    CLK_ADC_CTRL |= (1u << 11); /* enable -- without this, adc.c's busy-wait on ADC_CS_READY never returns */
}

/* ================= USB controller ================= */
#define USBCTRL_DPRAM_BASE 0x50100000u
#define USBCTRL_REGS_BASE  0x50110000u

#define USB_DEV_ADDR_CTRL REG32(USBCTRL_REGS_BASE + 0x00)
#define USB_MAIN_CTRL     REG32(USBCTRL_REGS_BASE + 0x40)
#define USB_SIE_CTRL      REG32(USBCTRL_REGS_BASE + 0x4c)
#define USB_SIE_STATUS    REG32(USBCTRL_REGS_BASE + 0x50)
#define USB_BUFF_STATUS   REG32(USBCTRL_REGS_BASE + 0x58)
#define USB_EP_STALL_ARM  REG32(USBCTRL_REGS_BASE + 0x68)
#define USB_USB_MUXING    REG32(USBCTRL_REGS_BASE + 0x74)
#define USB_USB_PWR       REG32(USBCTRL_REGS_BASE + 0x78)
#define USB_INTE          REG32(USBCTRL_REGS_BASE + 0x90)
#define USB_INTS          REG32(USBCTRL_REGS_BASE + 0x98)

/* DPRAM layout: setup_packet[8] @0, ep_ctrl[EP1..EP15] @0x008, ep_buf_ctrl[EP0..EP15] @0x080,
 * ep0_buf_a[64] @0x100, epx_data @0x180 (see hardware_structs/usb_dpram.h) */
#define USB_EP_CTRL_IN(epnum)      REG32(USBCTRL_DPRAM_BASE + 0x008 + ((epnum) - 1) * 8 + 0)
#define USB_EP_CTRL_OUT(epnum)     REG32(USBCTRL_DPRAM_BASE + 0x008 + ((epnum) - 1) * 8 + 4)
#define USB_EP_BUF_CTRL_IN(epnum)  REG32(USBCTRL_DPRAM_BASE + 0x080 + (epnum) * 8 + 0)
#define USB_EP_BUF_CTRL_OUT(epnum) REG32(USBCTRL_DPRAM_BASE + 0x080 + (epnum) * 8 + 4)
#define DPRAM_PTR(off) ((volatile uint8_t *)(USBCTRL_DPRAM_BASE + (off)))

#define EP0_BUF_OFF       0x100u
#define EP1_NOTIFY_OFF    0x180u
#define EP2_OUT_OFF       0x1c0u
#define EP2_IN_OFF        0x200u

#define USB_BUF_CTRL_FULL      0x00008000u
#define USB_BUF_CTRL_DATA1_PID 0x00002000u
#define USB_BUF_CTRL_STALL     0x00000800u
#define USB_BUF_CTRL_AVAIL     0x00000400u
#define USB_BUF_CTRL_LEN_MASK  0x000003ffu

#define EP_CTRL_ENABLE               (1u << 31)
#define EP_CTRL_INTERRUPT_PER_BUFFER (1u << 29)
#define EP_CTRL_BUFFER_TYPE_LSB      26u

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

/* bDeviceClass/SubClass/Protocol = Misc/Common/IAD: signals that the actual
 * class (CDC-ACM) is described per-interface via the Interface Association
 * Descriptor below, not at the device level. */
static const usb_device_descriptor_t device_descriptor = {
    18, 0x01, 0x0200, 0xef, 0x02, 0x01, 64, 0x0000, 0x0001, 0x0100, 1, 2, 0, 1
};

/* CDC-ACM: IAD + control interface (header/call-mgmt/ACM/union functional
 * descriptors + interrupt notify EP1 IN) + data interface (bulk EP2 IN/OUT).
 * Configuration(9) + IAD(8) + control-if(9) + header(5) + call-mgmt(5) +
 * ACM(4) + union(5) + notify EP(7) + data-if(9) + bulk OUT(7) + bulk IN(7)
 * = 75 bytes */
static const uint8_t config_descriptor[] = {
    9, 0x02, 75, 0, 2, 1, 0, 0xc0, 0x32,
    8, 0x0b, 0, 2, 0x02, 0x02, 0x01, 0,
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 1,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 0, 1,
    7, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
    9, 0x04, 1, 0, 2, 0x0a, 0, 0, 0,
    7, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    7, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,
};

/* 115200 baud, 1 stop bit, no parity, 8 data bits */
static const uint8_t line_coding[7] = { 0x00, 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08 };

static uint8_t string_buf[64];

static uint8_t build_string_descriptor(uint8_t index) {
    if (index == 0) {
        string_buf[0] = 4;
        string_buf[1] = 0x03;
        string_buf[2] = 0x09;
        string_buf[3] = 0x04;
        return 4;
    }
    const char *s = (index == 1) ? "TinyOS" : (index == 2) ? "TinyOS Console" : "";
    uint8_t len = 0;
    while (s[len]) len++;
    string_buf[0] = 2 + len * 2;
    string_buf[1] = 0x03;
    uint8_t i;
    for (i = 0; i < len; i++) {
        string_buf[2 + i * 2] = (uint8_t)s[i];
        string_buf[3 + i * 2] = 0;
    }
    return string_buf[0];
}

static void mem_copy(volatile uint8_t *dst, const uint8_t *src, uint16_t n) {
    while (n--) *dst++ = *src++;
}

static void arm_in(volatile uint32_t *bufctrl, uint16_t len, uint8_t *pid) {
    uint32_t val = len | USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_FULL;
    val |= (*pid) ? USB_BUF_CTRL_DATA1_PID : 0;
    *pid ^= 1;
    *bufctrl = val;
}

static void arm_out(volatile uint32_t *bufctrl, uint16_t len, uint8_t *pid) {
    uint32_t val = len | USB_BUF_CTRL_AVAIL;
    val |= (*pid) ? USB_BUF_CTRL_DATA1_PID : 0;
    *pid ^= 1;
    *bufctrl = val;
}

static void usb_stall_ep0(void) {
    USB_EP_BUF_CTRL_IN(0) = USB_BUF_CTRL_STALL;
    USB_EP_STALL_ARM |= 0x1u;
}

/* -------- device state -------- */
static uint8_t dev_addr;
static volatile uint8_t should_set_address;
static volatile uint8_t configured;
static volatile uint8_t awaiting_line_coding;
static volatile uint8_t tx_busy;

static uint8_t ep0_in_pid, ep0_out_pid, ep2_in_pid, ep2_out_pid;
static const uint8_t *ep0_in_data;
static uint16_t ep0_in_remaining;
static uint8_t ep0_in_zlp_pending;

#define RX_BUF_SIZE 256
static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

static void rx_push(uint8_t b) {
    uint16_t next = (uint16_t)((rx_head + 1) % RX_BUF_SIZE);
    if (next != rx_tail) {
        rx_buf[rx_head] = b;
        rx_head = next;
    }
}

static void usb_acknowledge_out_request(void) {
    ep0_in_remaining = 0;
    ep0_in_zlp_pending = 0;
    arm_in(&USB_EP_BUF_CTRL_IN(0), 0, &ep0_in_pid);
}

static void ep0_start_descriptor_reply(const uint8_t *data, uint16_t total_len, uint16_t requested_len) {
    uint16_t actual = total_len < requested_len ? total_len : requested_len;
    ep0_in_data = data;
    ep0_in_remaining = actual;
    ep0_in_zlp_pending = (actual < requested_len) && (actual % 64 == 0);
    if (ep0_in_remaining > 0) {
        uint16_t chunk = ep0_in_remaining > 64 ? 64 : ep0_in_remaining;
        mem_copy(DPRAM_PTR(EP0_BUF_OFF), ep0_in_data, chunk);
        ep0_in_data += chunk;
        ep0_in_remaining -= chunk;
        arm_in(&USB_EP_BUF_CTRL_IN(0), chunk, &ep0_in_pid);
    } else if (ep0_in_zlp_pending) {
        ep0_in_zlp_pending = 0;
        arm_in(&USB_EP_BUF_CTRL_IN(0), 0, &ep0_in_pid);
    } else {
        arm_out(&USB_EP_BUF_CTRL_OUT(0), 0, &ep0_out_pid);
    }
}

static void ep0_in_continue(void) {
    if (ep0_in_remaining > 0) {
        uint16_t chunk = ep0_in_remaining > 64 ? 64 : ep0_in_remaining;
        mem_copy(DPRAM_PTR(EP0_BUF_OFF), ep0_in_data, chunk);
        ep0_in_data += chunk;
        ep0_in_remaining -= chunk;
        arm_in(&USB_EP_BUF_CTRL_IN(0), chunk, &ep0_in_pid);
    } else if (ep0_in_zlp_pending) {
        ep0_in_zlp_pending = 0;
        arm_in(&USB_EP_BUF_CTRL_IN(0), 0, &ep0_in_pid);
    } else {
        arm_out(&USB_EP_BUF_CTRL_OUT(0), 0, &ep0_out_pid);
    }
}

static void handle_setup_packet(void) {
    volatile usb_setup_packet_t *pkt = (volatile usb_setup_packet_t *)(USBCTRL_DPRAM_BASE);
    uint8_t bmRequestType = pkt->bmRequestType;
    uint8_t bRequest = pkt->bRequest;
    uint16_t wValue = pkt->wValue;
    uint16_t wLength = pkt->wLength;
    uint8_t dir_in = bmRequestType & 0x80u;
    uint8_t type = bmRequestType & 0x60u;

    ep0_in_pid = 1;

    if (!dir_in) {
        if (type == 0x00 && bRequest == 0x05) { /* SET_ADDRESS */
            dev_addr = (uint8_t)(wValue & 0xff);
            should_set_address = 1;
            usb_acknowledge_out_request();
        } else if (type == 0x00 && bRequest == 0x09) { /* SET_CONFIGURATION */
            usb_acknowledge_out_request();
            configured = 1;
        } else if (type == 0x20 && bRequest == 0x20 && wLength == 7) { /* SET_LINE_CODING */
            awaiting_line_coding = 1;
            arm_out(&USB_EP_BUF_CTRL_OUT(0), 7, &ep0_out_pid);
        } else {
            usb_acknowledge_out_request();
        }
    } else {
        if (type == 0x00 && bRequest == 0x06) { /* GET_DESCRIPTOR */
            uint8_t desc_type = (uint8_t)(wValue >> 8);
            uint8_t desc_index = (uint8_t)(wValue & 0xff);
            if (desc_type == 0x01) {
                ep0_start_descriptor_reply((const uint8_t *)&device_descriptor, sizeof(device_descriptor), wLength);
            } else if (desc_type == 0x02) {
                ep0_start_descriptor_reply(config_descriptor, sizeof(config_descriptor), wLength);
            } else if (desc_type == 0x03) {
                uint8_t len = build_string_descriptor(desc_index);
                ep0_start_descriptor_reply(string_buf, len, wLength);
            } else {
                usb_stall_ep0();
            }
        } else if (type == 0x20 && bRequest == 0x21) { /* GET_LINE_CODING */
            ep0_start_descriptor_reply(line_coding, sizeof(line_coding), wLength);
        } else {
            usb_stall_ep0();
        }
    }
}

static void ep0_in_handler(void) {
    if (should_set_address) {
        USB_DEV_ADDR_CTRL = dev_addr;
        should_set_address = 0;
        return;
    }
    ep0_in_continue();
}

static void ep0_out_handler(void) {
    if (awaiting_line_coding) {
        awaiting_line_coding = 0;
        usb_acknowledge_out_request();
    }
}

static void handle_buff_status(void) {
    uint32_t bits = USB_BUFF_STATUS;

    if (bits & 0x01u) {
        USB_BUFF_STATUS = 0x01u;
        ep0_in_handler();
    }
    if (bits & 0x02u) {
        USB_BUFF_STATUS = 0x02u;
        ep0_out_handler();
    }
    if (bits & 0x10u) { /* EP2 IN done */
        USB_BUFF_STATUS = 0x10u;
        tx_busy = 0;
    }
    if (bits & 0x20u) { /* EP2 OUT done */
        USB_BUFF_STATUS = 0x20u;
        uint32_t bc = USB_EP_BUF_CTRL_OUT(2);
        uint16_t len = (uint16_t)(bc & USB_BUF_CTRL_LEN_MASK);
        volatile uint8_t *buf = DPRAM_PTR(EP2_OUT_OFF);
        uint16_t i;
        for (i = 0; i < len; i++) rx_push(buf[i]);
        arm_out(&USB_EP_BUF_CTRL_OUT(2), 64, &ep2_out_pid);
    }
}

static void usb_bus_reset(void) {
    dev_addr = 0;
    should_set_address = 0;
    USB_DEV_ADDR_CTRL = 0;
    configured = 0;
    awaiting_line_coding = 0;
}

static void usb_task(void) {
    uint32_t status = USB_INTS;

    if (status & (1u << 16)) { /* SETUP_REQ */
        USB_SIE_STATUS = (1u << 17); /* clear SETUP_REC (WC) */
        handle_setup_packet();
    }
    if (status & (1u << 4)) { /* BUFF_STATUS */
        handle_buff_status();
    }
    if (status & (1u << 12)) { /* BUS_RESET */
        USB_SIE_STATUS = (1u << 19); /* clear BUS_RESET (WC) */
        usb_bus_reset();
    }
}

void USBCTRL_Handler(void) {
    usb_task();
}

static void usb_device_init(void) {
    reset_block(RESET_BIT_USBCTRL);

    uint32_t off;
    for (off = 0; off < 4096; off += 4) REG32(USBCTRL_DPRAM_BASE + off) = 0;

    /* Power the analog transceiver down, then back up via the plain SIE_CTRL
     * write below. A RESETS-block reset alone doesn't reliably re-kick the
     * analog PHY after a prior USB session (e.g. the BOOTSEL bootloader's). */
    USB_SIE_CTRL |= (1u << 18); /* TRANSCEIVER_PD */

    USB_USB_MUXING = 0x9u; /* TO_PHY | SOFTCON */
    USB_USB_PWR = 0xcu;    /* VBUS_DETECT | VBUS_DETECT_OVERRIDE_EN */
    USB_MAIN_CTRL = 0x1u;  /* CONTROLLER_EN */
    USB_SIE_CTRL = (1u << 29); /* EP0_INT_1BUF */
    USB_INTE = (1u << 4) | (1u << 12) | (1u << 16); /* BUFF_STATUS, BUS_RESET, SETUP_REQ */

    USB_EP_CTRL_IN(1) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x3u << EP_CTRL_BUFFER_TYPE_LSB) | EP1_NOTIFY_OFF;
    USB_EP_CTRL_OUT(2) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x2u << EP_CTRL_BUFFER_TYPE_LSB) | EP2_OUT_OFF;
    USB_EP_CTRL_IN(2) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x2u << EP_CTRL_BUFFER_TYPE_LSB) | EP2_IN_OFF;

    USB_SIE_CTRL |= (1u << 16); /* PULLUP_EN: present device to host */
}

/* ================= public API ================= */

void console_init(void) {
    led_init();
    led_blink(1); /* checkpoint: reached console_init */

    clocks_init();
    led_blink(2); /* checkpoint: clocks configured */

    usb_device_init();
    led_blink(3); /* checkpoint: USB controller configured, pullup enabled */

    while (!configured) {
        usb_task();
    }
    led_blink(4); /* checkpoint: host completed enumeration */

    arm_out(&USB_EP_BUF_CTRL_OUT(2), 64, &ep2_out_pid);
}

void console_putc(char c) {
    while (tx_busy) usb_task();
    DPRAM_PTR(EP2_IN_OFF)[0] = (uint8_t)c;
    tx_busy = 1;
    arm_in(&USB_EP_BUF_CTRL_IN(2), 1, &ep2_in_pid);
}

void console_puts(const char *s) {
    while (*s) {
        if (*s == '\n') console_putc('\r');
        console_putc(*s++);
    }
}

void console_write(const char *buf, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') console_putc('\r');
        console_putc(buf[i]);
    }
}

int console_has_input(void) {
    usb_task();
    return rx_head != rx_tail;
}

void console_disconnect(void) {
    USB_SIE_CTRL &= ~(1u << 16); /* PULLUP_EN off: soft-disconnect from host */
    while (1) {
        __asm volatile("wfi");
    }
}

char console_getc(void) {
    while (rx_head == rx_tail) usb_task();
    uint8_t b = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_BUF_SIZE);
    return (char)b;
}
