/* RP2350 port of src_2040/usb.c. The USB device-controller register offsets
 * and DPRAM layout are IDENTICAL to RP2040 (same PL022-era USB IP block,
 * confirmed against RP2350 datasheet section 12.7) -- only base addresses,
 * reset-bit positions, and some CLOCKS-block offsets moved (RP2350 inserted
 * an HSTX clock ahead of CLK_USB/CLK_ADC, shifting them and the frequency
 * counter registers down). See README_RP2350.md and the memory note
 * rp2350_gotchas for the full list of differences from the RP2040 build.
 *
 * One genuinely new RP2350 requirement: USB's MAIN_CTRL register resets with
 * PHY_ISO (bit 2) set, isolating the USB PHY until software clears it --
 * directly analogous to the PADS_BANK0 ISO-bit gotcha from Stage 1. Writing
 * the whole register (`USB_MAIN_CTRL = 0x1u`, as below) clears it as a side
 * effect of setting CONTROLLER_EN; this is called out explicitly here so
 * it isn't accidentally lost in some future refactor to `|=`. */

#include <stdint.h>
#include "usb.h"
#include "lcd.h"

#define NULL ((void *)0)

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define CLOCKS_BASE 0x40010000u

/* ================= RESETS =================
 * Bit positions moved vs RP2040 (datasheet table 534). */
#define RESETS_BASE 0x40020000u
#define RESETS_RESET      REG32(RESETS_BASE + 0x00)
#define RESETS_RESET_DONE REG32(RESETS_BASE + 0x08)
#define RESET_BIT_PLL_SYS (1u << 14)
#define RESET_BIT_PLL_USB (1u << 15)
#define RESET_BIT_USBCTRL (1u << 28)

static void reset_block(uint32_t bit) {
    RESETS_RESET |= bit;
    RESETS_RESET &= ~bit;
    while (!(RESETS_RESET_DONE & bit));
}

/* ================= debug LED (temporary bring-up instrumentation) =================
 * Pico Plus 2 W has no plain-GPIO LED (its onboard LED is wired to the
 * CYW43439 WiFi chip, see rp2350_gotchas memory note) -- reuse the attached
 * Waveshare display's backlight pin (GPIO13) as a visible checkpoint signal
 * instead, same as src_rp2350/main.c's Stage 1 boot-proof. */
#define IO_BANK0_BASE   0x40028000u
#define PADS_BANK0_BASE 0x40038000u
#define SIO_BASE        0xd0000000u
#define LED_PIN 13u

static void led_init(void) {
    reset_block((1u << 6) | (1u << 9)); /* IO_BANK0, PADS_BANK0 (bit numbers moved vs RP2040) */

    /* RP2350-only: clear the pad's ISO bit (see rp2350_gotchas) or it will
     * never drive regardless of funcsel/output-enable. */
    REG32(PADS_BANK0_BASE + 0x04u + LED_PIN * 4u) = 0u;

    REG32(IO_BANK0_BASE + 0x04 + LED_PIN * 8u) = 5u; /* GPIO13 func = SIO */
    REG32(SIO_BASE + 0x038) = (1u << LED_PIN); /* GPIO_OE_SET */
    REG32(SIO_BASE + 0x020) = (1u << LED_PIN); /* GPIO_OUT_CLR: start off */
}

static void led_delay(void) {
    /* Deliberately slow (~10x normal) for this diagnostic build: clocks
     * aren't configured yet when checkpoint 1 fires, so clk_sys is still
     * whatever the boot ROM left it at, and a too-short delay risks a real
     * checkpoint blink being too fast to perceive -- indistinguishable from
     * never having fired at all. */
    volatile uint32_t n = 12000000u;
    while (n--);
}

static void led_blink(int times) {
    int i;
    for (i = 0; i < times; i++) {
        REG32(SIO_BASE + 0x018) = (1u << LED_PIN); /* GPIO_OUT_SET: backlight on */
        led_delay();
        REG32(SIO_BASE + 0x020) = (1u << LED_PIN); /* GPIO_OUT_CLR: backlight off */
        led_delay();
    }
    led_delay();
    led_delay();
}

/* Measures a clock (via the RP2350 frequency counter) and blinks it out as
 * 1 slow blink (in range) or 10 rapid blinks (out of range). Offsets here
 * are shifted vs RP2040 -- RP2350 inserted CLK_HSTX ahead of CLK_USB/CLK_ADC
 * in the CLOCKS register map, and added an FC0_DELAY register, pushing the
 * whole frequency-counter block down too (datasheet table 543). */
#define CLOCKS_FC0_REF_KHZ  REG32(CLOCKS_BASE + 0x8c)
#define CLOCKS_FC0_MIN_KHZ  REG32(CLOCKS_BASE + 0x90)
#define CLOCKS_FC0_MAX_KHZ  REG32(CLOCKS_BASE + 0x94)
#define CLOCKS_FC0_INTERVAL REG32(CLOCKS_BASE + 0x9c)
#define CLOCKS_FC0_SRC      REG32(CLOCKS_BASE + 0xa0)
#define CLOCKS_FC0_STATUS   REG32(CLOCKS_BASE + 0xa4)
#define CLOCKS_FC0_RESULT   REG32(CLOCKS_BASE + 0xa8)

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

/* ================= XOSC (same register layout as RP2040, base moved) ================= */
#define XOSC_BASE 0x40048000u
#define XOSC_CTRL    REG32(XOSC_BASE + 0x00)
#define XOSC_STATUS  REG32(XOSC_BASE + 0x04)
#define XOSC_STARTUP REG32(XOSC_BASE + 0x0c)

static void xosc_init(void) {
    XOSC_STARTUP = 47; /* startup delay for a 12MHz crystal */
    XOSC_CTRL = 0xaa0u | (0xfabu << 12); /* FREQ_RANGE 1-15MHz, ENABLE */
    while (!(XOSC_STATUS & (1u << 31))); /* wait STABLE */
}

/* ================= PLL (same register layout as RP2040, bases moved) ================= */
#define PLL_SYS_BASE 0x40050000u
#define PLL_USB_BASE 0x40058000u

static void pll_init(uint32_t base, uint32_t reset_bit, uint32_t refdiv, uint32_t fbdiv, uint32_t postdiv1, uint32_t postdiv2) {
    reset_block(reset_bit);

    REG32(base + 0x00) = refdiv;      /* CS */
    REG32(base + 0x08) = fbdiv;       /* FBDIV_INT */
    REG32(base + 0x04) &= ~((1u << 0) | (1u << 5)); /* PWR: clear PD, VCOPD */

    while (!(REG32(base + 0x00) & (1u << 31))); /* wait CS.LOCK */

    REG32(base + 0x0c) = (postdiv1 << 16) | (postdiv2 << 12); /* PRIM */
    REG32(base + 0x04) &= ~(1u << 3); /* PWR: clear POSTDIVPD */
}

/* ================= CLOCKS =================
 * CLK_REF/CLK_SYS/CLK_PERI offsets unchanged vs RP2040; CLK_USB/CLK_ADC
 * moved (see comment above CLOCKS_FC0_REF_KHZ). */
#define CLK_REF_CTRL     REG32(CLOCKS_BASE + 0x30)
#define CLK_REF_DIV      REG32(CLOCKS_BASE + 0x34)
#define CLK_REF_SELECTED REG32(CLOCKS_BASE + 0x38)
#define CLK_SYS_CTRL     REG32(CLOCKS_BASE + 0x3c)
#define CLK_SYS_DIV      REG32(CLOCKS_BASE + 0x40)
#define CLK_SYS_SELECTED REG32(CLOCKS_BASE + 0x44)
#define CLK_PERI_CTRL    REG32(CLOCKS_BASE + 0x48)
#define CLK_USB_CTRL     REG32(CLOCKS_BASE + 0x60)
#define CLK_USB_DIV      REG32(CLOCKS_BASE + 0x64)
#define CLK_ADC_CTRL     REG32(CLOCKS_BASE + 0x6c)
#define CLK_ADC_DIV      REG32(CLOCKS_BASE + 0x70)
#define CLK_SYS_RESUS_CTRL REG32(CLOCKS_BASE + 0x84)

/* NOTE ON HISTORY: an earlier version of this function tried to speed up
 * clk_sys by repointing CLK_SYS_CTRL's AUXSRC field from ROSC to PLL_SYS
 * *while SRC was still actively selecting aux* (i.e. without ever switching
 * away first), on the theory that avoiding the specific SRC-bit write that a
 * prior debugging session found froze the chip solid was the safe move. That
 * was wrong: that exact "swap the source feeding an actively-selected mux"
 * pattern is precisely what pico-sdk's own clock_configure_internal() calls
 * out as unsafe (see its comment "avoid passing glitches when changing aux
 * mux"). Confirmed by reading pico-sdk's actual boot sequence
 * (runtime_init_clocks.c) after finding a working bare-pico-sdk USB example
 * built for this exact board family (Pico 2 W / RP2350) that this sequence
 * runs on real hardware without hanging: the safe pattern switches clk_sys
 * and clk_ref away from their aux inputs to glitchless *direct* sources
 * (clk_ref itself, and ROSC respectively) FIRST -- while still cheaply
 * clocked off the always-on ROSC, before the PLLs are even touched -- then
 * brings up the PLLs, then glitchlessly switches each clock's SRC back to
 * aux with its AUXSRC field already pointed at the now-locked PLL. The
 * previous "just never touch SRC" workaround likely wasn't hanging because
 * clearing SRC is inherently fatal -- it was avoiding a glitch caused by
 * reconfiguring AUXSRC out from under a live aux-selected mux, which is a
 * real hazard this sequence sidesteps by only ever changing AUXSRC while
 * that clock is parked on its non-aux input. See rp2350_gotchas memory note
 * for the full debugging history. */
/* RP2350's core voltage regulator (VREG) powers up at 1.10V, but every
 * normal pico-sdk RP2350 build bumps it to (at least) 1.15V during boot --
 * SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST defaults to 1 for RP2350 specifically
 * (unlike RP2040, where it's 0), regardless of target clock speed. Found by
 * running TinyOS's own unmodified lcd.c inside a normal pico-sdk
 * environment (where it worked perfectly) versus TinyOS's own boot (where
 * the exact same driver produced a dead-silent panel) -- everything
 * register-level about the LCD/GPIO setup was proven byte-identical between
 * the two, so the remaining difference had to be something about the boot
 * environment itself, not GPIO/SPI configuration. This was it: TinyOS never
 * touched VREG at all, running the whole chip 0.05V under what RP2350
 * apparently needs for fully reliable operation at this clock speed --
 * enough margin for a simple slow GPIO toggle (the backlight blink
 * checkpoints all worked fine) but not for the LCD's timing-sensitive
 * bit-banged protocol. VREG lives in the POWMAN block on RP2350 (moved from
 * a dedicated VREG_AND_CHIP_RESET block on RP2040) and requires a "password"
 * (0x5afe0000) ORed into every write or the write is silently dropped --
 * confirmed against pico-sdk's hardware_vreg/vreg.c and the RP2350 datasheet
 * POWMAN register description. */
#define POWMAN_BASE           0x40100000u
#define POWMAN_VREG_CTRL      REG32(POWMAN_BASE + 0x04)
#define POWMAN_VREG           REG32(POWMAN_BASE + 0x0c)
#define POWMAN_PASSWORD_BITS  0x5afe0000u
#define POWMAN_VREG_CTRL_UNLOCK_BIT (1u << 13)
#define POWMAN_VREG_UPDATE_IN_PROGRESS (1u << 15)
#define POWMAN_VREG_VSEL_LSB  4u
#define POWMAN_VREG_VSEL_MASK (0x1fu << POWMAN_VREG_VSEL_LSB)
#define VREG_VOLTAGE_1_15     0x0cu

static void vreg_bump_voltage(void) {
    POWMAN_VREG_CTRL |= (POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BIT);
    while (POWMAN_VREG & POWMAN_VREG_UPDATE_IN_PROGRESS);
    POWMAN_VREG = (POWMAN_VREG & ~POWMAN_VREG_VSEL_MASK) | POWMAN_PASSWORD_BITS | (VREG_VOLTAGE_1_15 << POWMAN_VREG_VSEL_LSB);
    while (POWMAN_VREG & POWMAN_VREG_UPDATE_IN_PROGRESS);
}

static void clocks_init(void) {
    vreg_bump_voltage();
    led_blink(23); /* checkpoint 23: VREG bumped to 1.15V */

    CLK_SYS_RESUS_CTRL = 0; /* disable resus in case left enabled by a previous boot */

    xosc_init();
    led_blink(3); /* checkpoint: XOSC stable */

    /* Before touching the PLLs, switch clk_sys and clk_ref cleanly away from
     * their aux inputs (both land on glitchless, always-running direct
     * sources: clk_ref-direct for clk_sys, ROSC for clk_ref) so that PLL
     * relocking below can't glitch a mux that's actively selecting aux. */
    CLK_SYS_CTRL &= ~0x1u; /* SRC = clk_ref (0) */
    while (!(CLK_SYS_SELECTED & 0x1u));
    CLK_REF_CTRL &= ~0x3u; /* SRC = ROSC (0) */
    while (!(CLK_REF_SELECTED & 0x1u));
    led_blink(21); /* checkpoint 21: clk_sys/clk_ref parked on direct (non-aux) sources */

    pll_init(PLL_SYS_BASE, RESET_BIT_PLL_SYS, 1, 125, 6, 2); /* 12MHz *125 /12 = 125MHz */
    led_blink(4); /* checkpoint 4: PLL_SYS locked */
    pll_init(PLL_USB_BASE, RESET_BIT_PLL_USB, 1, 100, 5, 5); /* 12MHz *100 /25 = 48MHz */
    led_blink(5); /* checkpoint 5: PLL_USB locked */

    /* clk_ref: switch its glitchless mux directly to XOSC (still not aux, so
     * this is a same-hazard-class-free transition between two of the mux's
     * non-aux direct inputs). */
    CLK_REF_CTRL = (CLK_REF_CTRL & ~0x3u) | 0x2u; /* SRC = XOSC (2) */
    while (!(CLK_REF_SELECTED & (1u << 2)));

    /* clk_sys: now safe to point AUXSRC at PLL_SYS (clk_sys is currently
     * parked on clk_ref-direct, not aux, so this doesn't touch a live mux)
     * and then glitchlessly switch SRC back to aux. */
    CLK_SYS_CTRL = (CLK_SYS_CTRL & ~(0x7u << 5)); /* AUXSRC = clksrc_pll_sys (0) */
    CLK_SYS_CTRL |= 0x1u; /* SRC = aux (1) */
    while (!(CLK_SYS_SELECTED & (1u << 1)));
    CLK_SYS_DIV = (1u << 8);
    led_blink(22); /* checkpoint 22: clk_sys running from PLL_SYS via aux */
    check_khz(0x09u /* CLK_SYS */, 125000u, 5000u); /* 1 slow blink = ~125MHz confirmed, 10 rapid = still slow/wrong */

    CLK_USB_CTRL &= ~(1u << 11); /* disable before reconfiguring (non-glitchless clock) */
    CLK_USB_CTRL = (CLK_USB_CTRL & ~(0x7u << 5)); /* aux = clksrc_pll_usb */
    CLK_USB_DIV = (1u << 8);
    CLK_USB_CTRL |= (1u << 11); /* enable */
    led_blink(6); /* checkpoint: clk_usb switched to PLL_USB */
    check_khz(0x0bu /* CLK_USB */, 48000u, 2000u); /* 1 slow blink = ~48MHz confirmed, 10 rapid = wrong (would explain enumeration failure) */

    /* clk_adc: same non-glitchless bring-up as clk_usb above. Required for
     * adc.c -- without it, adc_read_channel()'s busy-wait on ADC_CS_READY
     * never returns (see adc.h). */
    CLK_ADC_CTRL &= ~(1u << 11);
    CLK_ADC_CTRL = (CLK_ADC_CTRL & ~(0x7u << 5)); /* aux = clksrc_pll_usb */
    CLK_ADC_DIV = (1u << 8);
    CLK_ADC_CTRL |= (1u << 11);
}

/* ================= USB controller =================
 * Base addresses, register offsets, and DPRAM layout confirmed identical to
 * RP2040 (datasheet section 12.7). */
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
 * Descriptor below, not at the device level.
 *
 * idVendor/idProduct: was 0x0000/0x0001 -- confirmed via a side-by-side test
 * against a known-good pico-sdk TinyUSB CDC example (which enumerated fine
 * on this same board/cable/Mac port, ruling out hardware) that idVendor=0
 * is likely why the host never enumerated this device at all: 0x0000 is not
 * a valid assigned USB vendor ID, and host USB stacks (macOS's IOUSBHost
 * included) are known to silently refuse to bind/enumerate a device that
 * advertises it, with no visible error -- exactly the symptom seen (no
 * /dev/cu.usbmodem*, no error dialog either). Changed to Raspberry Pi's
 * actual registered VID (0x2E8A) plus an arbitrary-but-nonzero PID, matching
 * pico-sdk's own stdio_usb_descriptors.c convention. */
static const usb_device_descriptor_t device_descriptor = {
    18, 0x01, 0x0200, 0xef, 0x02, 0x01, 64, 0x2e8a, 0x000a, 0x0100, 1, 2, 0, 1
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
    const char *s = (index == 1) ? "TinyOS" : (index == 2) ? "TinyOS Console (RP2350)" : "";
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

/* Must comfortably exceed main.c's shell input_buffer (512 bytes): a fast
 * burst write (e.g. `write <file> <long content>` sent in one shot, not
 * typed by hand) can arrive faster than the shell's character-at-a-time
 * echo loop drains it, and a too-small ring buffer here silently drops the
 * overflow instead of erroring. */
#define RX_BUF_SIZE 1024
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

/* Diagnostic: does the host ever attempt a bus reset at all? This is the
 * very first thing a host does once it notices a pulled-up device, so if
 * this never flips true, the host isn't reacting to our presence at all
 * (an even earlier-stage problem than descriptor content). */
static volatile uint8_t saw_bus_reset;

static void usb_bus_reset(void) {
    saw_bus_reset = 1;
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
    led_blink(8); /* checkpoint 8: USBCTRL reset done */

    uint32_t off;
    for (off = 0; off < 4096; off += 4) REG32(USBCTRL_DPRAM_BASE + off) = 0;

    USB_USB_MUXING = 0x9u; /* TO_PHY | SOFTCON */
    USB_USB_PWR = 0xcu;    /* VBUS_DETECT | VBUS_DETECT_OVERRIDE_EN */
    USB_MAIN_CTRL = 0x1u;  /* CONTROLLER_EN=1; also clears RP2350's PHY_ISO (bit2), which resets to 1 -- see file header comment */
    USB_SIE_CTRL = (1u << 29); /* EP0_INT_1BUF */
    USB_INTE = (1u << 4) | (1u << 12) | (1u << 16); /* BUFF_STATUS, BUS_RESET, SETUP_REQ */

    USB_EP_CTRL_IN(1) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x3u << EP_CTRL_BUFFER_TYPE_LSB) | EP1_NOTIFY_OFF;
    USB_EP_CTRL_OUT(2) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x2u << EP_CTRL_BUFFER_TYPE_LSB) | EP2_OUT_OFF;
    USB_EP_CTRL_IN(2) = EP_CTRL_ENABLE | EP_CTRL_INTERRUPT_PER_BUFFER | (0x2u << EP_CTRL_BUFFER_TYPE_LSB) | EP2_IN_OFF;

    USB_SIE_CTRL |= (1u << 16); /* PULLUP_EN: present device to host */

    /* Diagnostic: confirm the SIE itself thinks VBUS is present and the
     * device is electrically connected, independent of whether the host
     * ever actually enumerates it. 1 slow blink = bit set (good), 10 rapid
     * = bit clear (bad) -- same convention as check_khz(). If CONNECTED
     * never comes up, the bug is in this function's register sequence
     * (muxing/pwr/pullup); if it does come up but the host still never
     * enumerates, the bug is more likely in descriptor content/logic. */
    {
        volatile uint32_t settle;
        for (settle = 0; settle < 2000000u; settle++);
        led_blink((USB_SIE_STATUS & (1u << 0)) ? 1 : 10); /* checkpoint: VBUS_DETECTED */
        led_pause_long();
        led_blink((USB_SIE_STATUS & (1u << 16)) ? 1 : 10); /* checkpoint: CONNECTED */
        led_pause_long();
    }
}

/* ================= public API ================= */

void console_init(void) {
    led_init();
    led_blink(1); /* checkpoint 1: reached console_init, GPIO/pad bring-up done */

    clocks_init(); /* checkpoints 21, 4, 5, 22 fire inside here, see clocks_init() */
    led_blink(7); /* checkpoint 7: clocks_init() returned */

    /* From here on, mirror every diagnostic onto the LCD too (much higher
     * bandwidth than counting blinks) -- see lcd.h. Row layout, top to
     * bottom: measured clk_sys kHz, measured clk_usb kHz, USB_SIE_STATUS
     * right after usb_device_init(), USB_INTS/USB_SIE_STATUS refreshed live
     * every heartbeat tick while waiting, a loop-liveness counter, and a
     * marker square (red = no bus reset seen yet, green = host reset the
     * bus at least once, blue = host completed enumeration). */
    lcd_init();
    led_blink(15); /* checkpoint 15: lcd_init() returned (screen should be solid red now) */
    lcd_hex32(0, 0, 4, measure_khz(0x09u), LCD_WHITE, LCD_BLACK);  /* clk_sys kHz */
    lcd_hex32(0, 24, 4, measure_khz(0x0bu), LCD_WHITE, LCD_BLACK); /* clk_usb kHz */

    usb_device_init();
    led_blink(9); /* checkpoint 9: USB controller configured, pullup enabled */
    lcd_hex32(0, 48, 4, USB_SIE_STATUS, LCD_YELLOW, LCD_BLACK);
    lcd_marker(200, 48, 16, (USB_SIE_STATUS & (1u << 0)) ? LCD_GREEN : LCD_RED);  /* VBUS_DETECTED */
    lcd_marker(200, 72, 16, (USB_SIE_STATUS & (1u << 16)) ? LCD_GREEN : LCD_RED); /* CONNECTED */

    /* Diagnostic heartbeat: toggle the LED roughly every ~1.5M loop
     * iterations while waiting for the host to finish enumeration, so a
     * hang here (host never enumerates) is visibly distinct -- a slow,
     * continuous blink -- from a hang earlier in console_init (which would
     * leave the LED solid, since it'd never reach this loop at all).
     *
     * Once a bus reset is ever seen (saw_bus_reset -- the very first thing a
     * host does once it notices a pulled-up device), switch to a much
     * faster toggle (~150k iterations instead of 1.5M) so it's visually
     * obvious whether the host ever reacted to our presence at all, versus
     * never getting past the slow "waiting" cadence. */
    {
        uint32_t heartbeat_count = 0;
        uint32_t loop_counter = 0;
        while (!configured) {
            usb_task();
            if (++heartbeat_count >= (saw_bus_reset ? 150000u : 1500000u)) {
                heartbeat_count = 0;
                REG32(SIO_BASE + 0x028) ^= (1u << LED_PIN); /* GPIO_OUT_XOR */
                lcd_hex32(0, 96, 4, ++loop_counter, LCD_GREEN, LCD_BLACK);
                lcd_hex32(0, 120, 4, USB_INTS, LCD_YELLOW, LCD_BLACK);
                lcd_hex32(0, 144, 4, USB_SIE_STATUS, LCD_YELLOW, LCD_BLACK);
                lcd_marker(0, 168, 16, saw_bus_reset ? LCD_GREEN : LCD_RED);
            }
        }
    }
    led_blink(10); /* checkpoint 10: host completed enumeration */
    lcd_marker(0, 168, 16, LCD_BLUE);

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
