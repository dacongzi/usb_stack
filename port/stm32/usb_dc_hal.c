#include "usbd_core.h"
#ifdef STM32F1
#include "stm32f1xx_hal.h" //chanage this header for different soc
#elif defined(STM32F4)
#include "stm32f4xx_hal.h" //chanage this header for different soc
#elif defined(STM32H7)
#include "stm32h7xx_hal.h" //chanage this header for different soc
#endif

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 6
#endif

#ifdef USB
#ifndef USB_RAM_SIZE
#define USB_RAM_SIZE 512
#endif
extern PCD_HandleTypeDef hpcd_USB_FS;
#define PCD_HANDLE &hpcd_USB_FS
#else
#ifndef USB_RAM_SIZE
#define USB_RAM_SIZE 4096
#endif
#ifdef CONFIG_USB_HS
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
#define PCD_HANDLE &hpcd_USB_OTG_HS
#else
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
#define PCD_HANDLE &hpcd_USB_OTG_HS
//extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
//#define PCD_HANDLE &hpcd_USB_OTG_FS
#endif
#endif
/*
 * USB and USB_OTG_FS are defined in STM32Cube HAL and allows to distinguish
 * between two kind of USB DC. STM32 F0, F3, L0 and G4 series support USB device
 * controller. STM32 F4 and F7 series support USB_OTG_FS device controller.
 * STM32 F1 and L4 series support either USB or USB_OTG_FS device controller.
 *
 * WARNING: Don't mix USB defined in STM32Cube HAL and CONFIG_USB from Zephyr
 * Kconfig system.
 */
#ifdef USB

#define EP0_MPS 64U
#define EP_MPS  64U

/*
 * USB BTABLE is stored in the PMA. The size of BTABLE is 4 bytes
 * per endpoint.
 *
 */
#define USB_BTABLE_SIZE (8 * USB_NUM_BIDIR_ENDPOINTS)

#else /* USB_OTG_FS */
#define EP0_MPS USB_OTG_MAX_EP0_SIZE

#ifdef CONFIG_USB_HS
#define EP_MPS USB_OTG_HS_MAX_PACKET_SIZE
#else
#define EP_MPS USB_OTG_FS_MAX_PACKET_SIZE
#endif


#define CONTROL_EP_NUM   1
/*this should user make config*/
#define OUT_EP_NUM       2
#define OUT_EP_MPS       1024
#define EP_RX_FIFO_WORDS ((4 * CONTROL_EP_NUM + 6) + ((OUT_EP_MPS / 4) + 1) + 2 * OUT_EP_NUM + 1)
#define EP_TX_FIFO_WORDS 0x40

#endif /* USB */

/* Endpoint state */
struct usb_dc_ep_state {
    uint16_t ep_mps; /** Endpoint max packet size */
#ifdef USB
    uint16_t ep_pma_buf_len; /** Previously allocated buffer size */
#endif
    uint8_t ep_type;      /** Endpoint type (STM32 HAL enum) */
    uint8_t ep_stalled;   /** Endpoint stall flag */
    uint32_t read_count;  /** Number of bytes in read buffer  */
    uint32_t read_offset; /** Current offset in read buffer */
};

/* Driver state */
struct usb_dc_pcd_state {
    struct usb_dc_ep_state out_ep_state[USB_NUM_BIDIR_ENDPOINTS];
    struct usb_dc_ep_state in_ep_state[USB_NUM_BIDIR_ENDPOINTS];
    uint8_t ep_buf[USB_NUM_BIDIR_ENDPOINTS][EP_MPS];

#ifdef USB
    uint32_t pma_offset;
#endif /* USB */
};

static struct usb_dc_pcd_state usb_dc_pcd_state;

/* Internal functions */

static struct usb_dc_ep_state *usb_dc_stm32_get_ep_state(uint8_t ep)
{
    struct usb_dc_ep_state *ep_state_base;

    if (USB_EP_GET_IDX(ep) >= USB_NUM_BIDIR_ENDPOINTS) {
        return NULL;
    }

    if (USB_EP_DIR_IS_OUT(ep)) {
        ep_state_base = usb_dc_pcd_state.out_ep_state;
    } else {
        ep_state_base = usb_dc_pcd_state.in_ep_state;
    }

    return ep_state_base + USB_EP_GET_IDX(ep);
}

int usb_dc_init(void)
{
    HAL_StatusTypeDef status;
    unsigned int i;

    /*pcd has init*/
    status = HAL_PCD_Start(PCD_HANDLE);
    if (status != HAL_OK) {
        return -2;
    }

    usb_dc_pcd_state.out_ep_state[0].ep_mps = EP0_MPS;
    usb_dc_pcd_state.out_ep_state[0].ep_type = EP_TYPE_CTRL;
    usb_dc_pcd_state.in_ep_state[0].ep_mps = EP0_MPS;
    usb_dc_pcd_state.in_ep_state[0].ep_type = EP_TYPE_CTRL;

#ifdef USB
    /* Start PMA configuration for the endpoints after the BTABLE. */
    usb_dc_pcd_state.pma_offset = USB_BTABLE_SIZE;
#else /* USB_OTG_FS */
    /* TODO: make this dynamic (depending usage) */
    HAL_PCDEx_SetRxFiFo(PCD_HANDLE, EP_RX_FIFO_WORDS);
    for (i = 0U; i < USB_NUM_BIDIR_ENDPOINTS; i++) {
        HAL_PCDEx_SetTxFiFo(PCD_HANDLE, i,
                            EP_TX_FIFO_WORDS);
    }
#endif /* USB */
    return 0;
}

void usb_dc_deinit(void)
{
}

int usbd_set_address(const uint8_t addr)
{
    HAL_StatusTypeDef status;

    status = HAL_PCD_SetAddress(PCD_HANDLE, addr);
    if (status != HAL_OK) {
        return -2;
    }

    return 0;
}

int usbd_ep_open(const struct usbd_endpoint_cfg *ep_cfg)
{
    HAL_StatusTypeDef status;
    uint8_t ep = ep_cfg->ep_addr;
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);

    if (!ep_state) {
        return -1;
    }

#ifdef USB
    if (ep_cfg->ep_mps > ep_state->ep_pma_buf_len) {
        if (USB_RAM_SIZE <=
            (usb_dc_pcd_state.pma_offset + ep_cfg->ep_mps)) {
            return -1;
        }
        HAL_PCDEx_PMAConfig(&hpcd_USB_FS, ep, PCD_SNG_BUF,
                            usb_dc_pcd_state.pma_offset);
        ep_state->ep_pma_buf_len = ep_cfg->ep_mps;
        usb_dc_pcd_state.pma_offset += ep_cfg->ep_mps;
    }
#endif
    ep_state->ep_mps = ep_cfg->ep_mps;

    switch (ep_cfg->ep_type) {
        case USB_DC_EP_CONTROL:
            ep_state->ep_type = EP_TYPE_CTRL;
            break;
        case USB_DC_EP_ISOCHRONOUS:
            ep_state->ep_type = EP_TYPE_ISOC;
            break;
        case USB_DC_EP_BULK:
            ep_state->ep_type = EP_TYPE_BULK;
            break;
        case USB_DC_EP_INTERRUPT:
            ep_state->ep_type = EP_TYPE_INTR;
            break;
        default:
            return -1;
    }

    status = HAL_PCD_EP_Open(PCD_HANDLE, ep,
                             ep_state->ep_mps, ep_state->ep_type);
    if (status != HAL_OK) {
        return -1;
    }

    if (USB_EP_DIR_IS_OUT(ep) && ep != USB_CONTROL_OUT_EP0) {
        return HAL_PCD_EP_Receive(PCD_HANDLE, ep,
                                  usb_dc_pcd_state.ep_buf[USB_EP_GET_IDX(ep)],
                                  EP_MPS);
    }
    return 0;
}
int usbd_ep_close(const uint8_t ep)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);
    HAL_StatusTypeDef status;

    if (!ep_state) {
        return -1;
    }

    status = HAL_PCD_EP_Close(PCD_HANDLE, ep);
    if (status != HAL_OK) {
        return -2;
    }

    return 0;
}
int usbd_ep_set_stall(const uint8_t ep)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);
    HAL_StatusTypeDef status;

    if (!ep_state) {
        return -1;
    }

    status = HAL_PCD_EP_SetStall(PCD_HANDLE, ep);
    if (status != HAL_OK) {
        return -2;
    }

    ep_state->ep_stalled = 1U;

    return 0;
}
int usbd_ep_clear_stall(const uint8_t ep)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);
    HAL_StatusTypeDef status;

    if (!ep_state) {
        return -1;
    }

    status = HAL_PCD_EP_ClrStall(PCD_HANDLE, ep);
    if (status != HAL_OK) {
        return -2;
    }

    ep_state->ep_stalled = 0U;
    ep_state->read_count = 0U;

    return 0;
}
int usbd_ep_is_stalled(const uint8_t ep, uint8_t *stalled)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);

    if (!ep_state || !stalled) {
        return -1;
    }

    *stalled = ep_state->ep_stalled;

    return 0;
}

int usbd_ep_write(const uint8_t ep, const uint8_t *data, uint32_t data_len, uint32_t *ret_bytes)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);
    HAL_StatusTypeDef status;
    int ret = 0;

    if (!ep_state || !USB_EP_DIR_IS_IN(ep)) {
        return -1;
    }

    if (ep == USB_CONTROL_IN_EP0 && data_len > 64) {
        data_len = 64;
    }

    status = HAL_PCD_EP_Transmit(PCD_HANDLE, ep,
                                 (void *)data, data_len);
    if (status != HAL_OK) {
        ret = -2;
    }

    if (!ret && ep == USB_CONTROL_IN_EP0 && data_len > 0) {
        /* Wait for an empty package as from the host.
		 * This also flushes the TX FIFO to the host.
		 */
        status = HAL_PCD_EP_Receive(PCD_HANDLE, ep,
                                    usb_dc_pcd_state.ep_buf[USB_EP_GET_IDX(ep)],
                                    0);
        if (status != HAL_OK) {
            return -2;
        }
    }

    if (!ret && ret_bytes) {
        *ret_bytes = data_len;
    }

    return ret;
}

int usbd_ep_read(const uint8_t ep, uint8_t *data, uint32_t max_data_len, uint32_t *read_bytes)
{
    struct usb_dc_ep_state *ep_state = usb_dc_stm32_get_ep_state(ep);
    uint32_t read_count;
    HAL_StatusTypeDef status;

    if (!ep_state) {
        return -1;
    }
    if (!USB_EP_DIR_IS_OUT(ep)) { /* check if OUT ep */

        return -1;
    }
    if (!data && max_data_len) {
        return -1;
    }

    ep_state->read_count = HAL_PCD_EP_GetRxCount(PCD_HANDLE, ep);
    ep_state->read_offset = 0U;
    read_count = ep_state->read_count;

    if (max_data_len == 0) {
        /* If no more data in the buffer, start a new read transaction.
	 * DataOutStageCallback will called on transaction complete.
	 */
        if (!ep_state->read_count) {
            status = HAL_PCD_EP_Receive(PCD_HANDLE, ep,
                                        usb_dc_pcd_state.ep_buf[USB_EP_GET_IDX(ep)],
                                        EP_MPS);
            if (status != HAL_OK) {
                return -2;
            }
        }
        return 0;
    }
    /* When both buffer and max data to read are zero, just ingore reading
	 * and return available data in buffer. Otherwise, return data
	 * previously stored in the buffer.
	 */
    if (data) {
        read_count = MIN(read_count, max_data_len);
        memcpy(data, usb_dc_pcd_state.ep_buf[USB_EP_GET_IDX(ep)] + ep_state->read_offset, read_count);
        ep_state->read_count -= read_count;
        ep_state->read_offset += read_count;
    }

    /* If no more data in the buffer, start a new read transaction.
	 * DataOutStageCallback will called on transaction complete.
	 */
    if (!ep_state->read_count) {
        status = HAL_PCD_EP_Receive(PCD_HANDLE, ep,
                                    usb_dc_pcd_state.ep_buf[USB_EP_GET_IDX(ep)],
                                    EP_MPS);
        if (status != HAL_OK) {
            return -2;
        }
    }
    if (read_bytes) {
        *read_bytes = read_count;
    }

    return 0;
}

/* Callbacks from the STM32 Cube HAL code */
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    struct usbd_endpoint_cfg ep0_cfg;
    /* Configure control EP */
    ep0_cfg.ep_mps = EP0_MPS;
    ep0_cfg.ep_type = USB_DC_EP_CONTROL;

    ep0_cfg.ep_addr = USB_CONTROL_OUT_EP0;
    usbd_ep_open(&ep0_cfg);

    ep0_cfg.ep_addr = USB_CONTROL_IN_EP0;
    usbd_ep_open(&ep0_cfg);
    usbd_event_notify_handler(USB_EVENT_RESET, NULL);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    struct usb_setup_packet *setup = (void *)hpcd->Setup;

    memcpy(&usb_dc_pcd_state.ep_buf[0],
           hpcd->Setup, 8);

    usbd_event_notify_handler(USB_EVENT_SETUP_NOTIFY, NULL);
    if (!(setup->wLength == 0U) &&
        !(REQTYPE_GET_DIR(setup->bmRequestType) ==
          USB_REQUEST_DEVICE_TO_HOST)) {
        HAL_PCD_EP_Receive(PCD_HANDLE, 0x00,
                           usb_dc_pcd_state.ep_buf[0],
                           setup->wLength);
    }
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    if (epnum == 0) {
        usbd_event_notify_handler(USB_EVENT_EP0_OUT_NOTIFY, NULL);
    } else {
        usbd_event_notify_handler(USB_EVENT_EP_OUT_NOTIFY, (void *)(epnum | USB_EP_DIR_OUT));
    }
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    if (epnum == 0) {
        usbd_event_notify_handler(USB_EVENT_EP0_IN_NOTIFY, NULL);
    } else {
        usbd_event_notify_handler(USB_EVENT_EP_IN_NOTIFY, (void *)(epnum | USB_EP_DIR_IN));
    }
}