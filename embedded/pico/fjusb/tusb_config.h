// Both USB roles at once. The native controller stays the device side and
// carries the serial console to a host PC; the second root port is the
// PIO-USB host on GP1/GP2, where the board's own hub fans out to the three
// A sockets.
//
// The SDK ships its own tusb_config.h for stdio_usb, but that file skips
// its whole body once tinyusb_host is linked, so the device half has to be
// spelled out here as well.

#ifndef _FRUITJAM_TUSB_CONFIG_H
#define _FRUITJAM_TUSB_CONFIG_H

#include "pico/stdio_usb.h"

#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)

// Deliberately no CFG_TUSB_RHPORT1_MODE. That macro is what makes
// tusb_init bring the host up as a side effect of stdio's init, on
// whichever core got there first and with the default pins. Enabling the
// host directly instead leaves tuh_init to us, on the core that will
// service it, after the pins are set.
#define CFG_TUH_ENABLED         1
#define CFG_TUH_MAX_SPEED       OPT_MODE_FULL_SPEED
#define CFG_TUH_RPI_PIO_USB     1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))
#endif

// --- device: the console ---

#define CFG_TUD_CDC             (1)
#define CFG_TUD_VENDOR          (0)

#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE  (64)
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE  (64)
#endif
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE  (64)
#endif

// --- host: keyboard and mouse behind the on-board hub ---

#define CFG_TUH_HUB             (1)
#define CFG_TUH_HID             (4)
#define CFG_TUH_CDC             (0)
#define CFG_TUH_MSC             (0)
#define CFG_TUH_VENDOR          (0)

// The hub itself takes one address, so the sockets need the rest.
#define CFG_TUH_DEVICE_MAX      (CFG_TUH_HUB + 3)

#define CFG_TUH_ENUMERATION_BUFSIZE (256)
#define CFG_TUH_HID_EPIN_BUFSIZE    (64)
#define CFG_TUH_HID_EPOUT_BUFSIZE   (64)

#endif
