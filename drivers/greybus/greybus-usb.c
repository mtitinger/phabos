#include <errno.h>
#include <phabos/greybus.h>
#include <phabos/utils.h>
#include <phabos/assert.h>
#include <phabos/usb/hcd.h>
#include <phabos/usb/std-requests.h>
#include <phabos/usb/driver.h>

#include "control-gb.h"
#include "gpio-gb.h"

static struct usb_device *usbdev;

void gb_usb_send_complete(struct urb *urb)
{
    kprintf("%s() = %d\n", __func__, urb->status);
}

int gb_usb_send(unsigned int cportid, const void *buf, size_t len)
{
    struct urb *urb = urb_create(usbdev);
    struct gb_operation_hdr *hdr = buf;

    kprintf("%s()\n", __func__);

    hdr->pad[1] = cportid >> 8;
    hdr->pad[0] = cportid & 0xff;

    urb->complete = gb_usb_send_complete;
    urb->pipe = (USB_HOST_PIPE_BULK << 30) | (2 << 15) | (usbdev->address << 8) |
                USB_HOST_DIR_OUT;
    urb->buffer = buf;
    urb->length = len;
    urb->maxpacket = 0x40;
    urb->flags = 1;

    usbdev->hcd->driver->urb_enqueue(usbdev->hcd, urb);

    return 0;
}

static void connect_gpio_cport(void)
{
    struct gb_control_connected_request *req;

    struct gb_operation *operation = gb_operation_create(0, GB_CONTROL_TYPE_CONNECTED, sizeof(*req));
    if (!operation)
        return;

    req = gb_operation_get_request_payload(operation);
    req->cport_id = 3;

    gb_operation_send_request(operation, NULL, false);
    gb_operation_destroy(operation);
}

static void toggle_gpio(void)
{
    struct gb_gpio_direction_out_request *req;

    struct gb_operation *operation = gb_operation_create(1, GB_GPIO_TYPE_DIRECTION_OUT, sizeof(*req));
    if (!operation)
        return;

    req = gb_operation_get_request_payload(operation);
    req->which = 0;
    req->value = 1;

    gb_operation_send_request(operation, NULL, false);
    gb_operation_destroy(operation);
}

void gb_usb_dev(void)
{
    kprintf("%s()\n", __func__);

    connect_gpio_cport();
    toggle_gpio();
}

static struct gb_transport_backend gb_usb_backend = {
    .init = gb_usb_dev,
    .send = gb_usb_send,
};

void print_descriptor(void *raw_descriptor);

static int gb_usb_init_bus(struct usb_device *dev)
{
    void *buffer = malloc(255);
    struct usb_device_descriptor *desc = buffer;

    usbdev = dev;

    usb_control_msg(dev, USB_DEVICE_SET_CONFIGURATION, 1, 0, 0, NULL);

    usb_control_msg(dev, USB_DEVICE_GET_DESCRIPTOR,
                    USB_DESCRIPTOR_DEVICE << 8, 0, sizeof(*desc), desc);

    if (desc->idVendor != 0xffff)
        return -EINVAL;

    usb_control_msg(dev, USB_DEVICE_GET_DESCRIPTOR,
                    USB_DESCRIPTOR_CONFIGURATION << 8, 0, 255, buffer);

    print_descriptor(buffer);

    return gb_init(&gb_usb_backend);
}

static struct usb_class_driver hub_class_driver = {
    .class = 0,
    .init = gb_usb_init_bus,
};

static int gb_usb_init(struct driver *driver)
{
    return usb_register_class_driver(&hub_class_driver);
}

__driver__ struct driver gb_usb_driver = {
    .name = "gb-usb",
    .init = gb_usb_init,
};