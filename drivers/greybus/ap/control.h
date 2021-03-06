#ifndef __GREYBUS_AP_CONTROL_H__
#define __GREYBUS_AP_CONTROL_H__

struct gb_interface;
struct gb_manifest;
struct gb_device;

struct gb_device *gb_control_create_device(struct greybus *bus, unsigned cport);
int gb_control_get_manifest(struct gb_interface *iface,
                            struct gb_manifest *manifest);
int gb_control_connect_cport(struct gb_interface *iface, unsigned cport);
int gb_control_disconnect_cport(struct gb_interface *iface, unsigned cport);

#endif /* __GREYBUS_AP_CONTROL_H__ */

