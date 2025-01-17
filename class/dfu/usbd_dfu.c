#include "usbd_core.h"
#include "usbd_dfu.h"

/* Device data structure */
struct dfu_cfg_private {
    struct dfu_info info;
} usbd_dfu_cfg;

int dfu_class_request_handler(struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    USBD_LOG_DBG("DFU Class request: "
                 "bRequest 0x%02x\r\n",
                 setup->bRequest);

    switch (setup->bRequest) {
        case DFU_REQUEST_DETACH:
            break;
        case DFU_REQUEST_DNLOAD:
            break;
        case DFU_REQUEST_UPLOAD:
            break;
        case DFU_REQUEST_GETSTATUS:
            break;
        case DFU_REQUEST_CLRSTATUS:
            break;
        case DFU_REQUEST_GETSTATE:
            break;
        case DFU_REQUEST_ABORT:
            break;
        default:
            USBD_LOG_WRN("Unhandled DFU Class bRequest 0x%02x\r\n", setup->bRequest);
            return -1;
    }

    return 0;
}

void dfu_notify_handler(uint8_t event, void *arg)
{
    switch (event) {
        case USB_EVENT_RESET:

            break;

        default:
            break;
    }
}

void usbd_dfu_add_interface(usbd_class_t *class, usbd_interface_t *intf)
{
    static usbd_class_t *last_class = NULL;

    if (last_class != class) {
        last_class = class;
        usbd_class_register(class);
    }

    intf->class_handler = dfu_class_request_handler;
    intf->custom_handler = NULL;
    intf->vendor_handler = NULL;
    intf->notify_handler = dfu_notify_handler;
    usbd_class_add_interface(class, intf);
}