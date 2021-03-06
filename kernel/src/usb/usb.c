/* Copyright (C) StrawberryHacker */

#include "usb.h"
#include "print.h"
#include "list.h"
#include "usb_protocol.h"
#include "memory.h"
#include "bmalloc.h"
#include "thread.h"
#include "usb_debug.h"
#include "config.h"
#include "usb_strings.h"

/* Private valiables used for device enumeration */
static struct usb_setup_desc setup;
static u8 enum_buffer[USB_ENUM_BUFFER_SIZE];

/* Private USB core instance */
struct usb_core* usbc_private;

static void usbc_start_enum(struct usb_core* usbc);
static enum usb_enum_state usbc_get_enum_state(void);
static void usbc_enumerate_handler(struct urb* urb);
static u8 usbc_assign_driver(struct usb_dev* dev, struct usb_core* usbc);

static u8 usbc_new_address(struct usb_core* usbc);
static void usbc_delete_address(struct usb_core* usbc, u8 address);

/* Enumeration stages */
static void usbc_get_dev_desc(struct urb* urb, struct usb_core* usbc, u8 full);
static void usbc_set_dev_addr(struct urb* urb, struct usb_core* usbc);
static void usbc_get_cfg_desc(struct urb* urb, struct usb_core* usbc);
static void usbc_get_all_desc(struct urb* urb, struct usb_core* usbc);

/* Enumeration stages complete */
static void usbc_device_desc_done(struct urb* urb, struct usb_dev* dev);
static void usbc_ep0_size_done(struct urb* urb, struct usb_dev* dev);
static void usbc_address_done(struct urb* urb, struct usb_dev* dev);
static void usbc_desc_length_done(struct urb* urb, struct usb_dev* dev);
static void usbc_get_all_desc_done(struct urb* urb, struct usb_dev* dev);
static void usbc_get_product_name_done(struct urb* urb, struct usb_dev* dev);
static void usbc_get_manufacturer_name_done(struct urb* urb, struct usb_dev* dev);

static struct usb_dev* usbc_add_device(struct usb_core* usbc);
static u8 usbc_verify_descriptors(u8* data, u32 size, u32* configs, u32* ifaces, u32* eps);
static u32 usbc_get_desc_offset(u32 configs, u32 ifaces, u8 type, u32 index);
static u8 usbc_alloc_descriptors(struct usb_dev* dev, u32 configs, u32 ifaces, u32 eps);
static void usbc_delete_descriptors(struct usb_dev* dev);
static void usbc_init_descriptors(struct usb_dev* dev, u32 configs, u32 ifaces, u32 eps);
static u8 usbc_parse_descriptors(struct usb_dev* dev, u8* data, u32 size);
static u8 usbc_check_dev_match(const struct usb_dev_id* id, struct usb_dev* dev);
static u8 usbc_check_iface_match(const struct usb_dev_id* id, struct usb_iface* iface);
static u8 usbc_check_driver_match(struct usb_driver* driver, struct usb_iface* iface);
static struct usb_driver* usbc_find_driver(struct usb_iface* iface, struct usb_core* usbc);

/*
 * This will take in a URB an perform a get device descriptor request. Full 
 * specifies if the full descriptor is fetched or only the first 8 bytes. This
 * is due to unknown size of EP0 
 */
static void usbc_get_dev_desc(struct urb* urb, struct usb_core* usbc, u8 full)
{
    setup.bmRequestType    = USB_DEVICE_TO_HOST;
    setup.bRequest         = USB_REQ_GET_DESCRIPTOR;
    setup.bDescriptorType  = USB_DESC_DEVICE;
    setup.bDescriptorIndex = 0;
    setup.wIndex           = 0;

    if (full) {
        setup.wLength = 18;
    } else {
        setup.wLength = 8;
    }
    usbhc_fill_control_urb(urb, (u8 *)&setup, enum_buffer, &usbc_enumerate_handler);
    usbhc_submit_urb(urb, usbc->pipe0);
}

static void usb_get_string_desc(struct urb* urb, struct usb_core* usbc,
    u8 desc_index, u16 lang_id)
{
    setup.bmRequestType    = USB_DEVICE_TO_HOST;
    setup.bRequest         = USB_REQ_GET_DESCRIPTOR;
    setup.bDescriptorType  = USB_DESC_STRING;
    setup.bDescriptorIndex = desc_index;
    setup.wIndex           = lang_id;
    setup.wLength          = USB_ENUM_BUFFER_SIZE;

    usbhc_fill_control_urb(urb, (u8 *)&setup, enum_buffer, &usbc_enumerate_handler);
    usbhc_submit_urb(urb, usbc->pipe0);
}

static void usbc_set_dev_addr(struct urb* urb, struct usb_core* usbc)
{
    setup.bmRequestType = USB_HOST_TO_DEVICE;
    setup.bRequest      = USB_REQ_SET_ADDRESS;
    setup.wValue        = usbc_new_address(usbc);
    setup.wIndex        = 0;
    setup.wLength       = 0;

    usbhc_fill_control_urb(urb, (u8 *)&setup, enum_buffer, &usbc_enumerate_handler);
    usbhc_submit_urb(urb, usbc->pipe0);
}

static void usbc_get_cfg_desc(struct urb* urb, struct usb_core* usbc)
{
    setup.bmRequestType    = USB_DEVICE_TO_HOST;
    setup.bRequest         = USB_REQ_GET_DESCRIPTOR;
    setup.bDescriptorType  = USB_DESC_CONFIG;
    setup.bDescriptorIndex = 0;
    setup.wIndex           = 0;
    setup.wLength          = 9;

    usbhc_fill_control_urb(urb, (u8 *)&setup, enum_buffer, &usbc_enumerate_handler);
    usbhc_submit_urb(urb, usbc->pipe0);
}

static void usbc_get_all_desc(struct urb* urb, struct usb_core* usbc)
{
    setup.bmRequestType    = USB_DEVICE_TO_HOST;
    setup.bRequest         = USB_REQ_GET_DESCRIPTOR;
    setup.bDescriptorType  = USB_DESC_CONFIG;
    setup.bDescriptorIndex = 0;
    setup.wIndex           = 0;
    setup.wLength          = usbc->enum_dev->desc_total_size;

    usbhc_fill_control_urb(urb, (u8 *)&setup, enum_buffer, &usbc_enumerate_handler);
    usbhc_submit_urb(urb, usbc->pipe0);
}


/*
 * The URB will contain the first 8 bytes of the device decriptor. This should
 * be enough to read the default EP0 size
 */
static void usbc_ep0_size_done(struct urb* urb, struct usb_dev* dev)
{
    /* Update the endpoint zero size */
    printl("EP0 size done");
    struct usb_dev_desc* dev_desc = (struct usb_dev_desc *)urb->transfer_buffer;
    u32 packet_size = dev_desc->bMaxPacketSize;
    if ((packet_size < 8) || (packet_size > 1024)) {
        panic("Abort enumeration");
    }
    dev->ep0_size = packet_size;
    print("Max packet => %d\n", dev->ep0_size);
}

static void usbc_device_desc_done(struct urb* urb, struct usb_dev* dev)
{
    printl("Device descriptor done");
    struct usb_setup_desc* desc = (struct usb_setup_desc *)urb->setup_buffer;
    u32 size = desc->wLength;
    print("Size: %d\n", size);

    u8* src = urb->transfer_buffer;
    u8* dest = (u8 *)&dev->desc;

    memory_copy(src, dest, size);
}

static void usbc_address_done(struct urb* urb, struct usb_dev* dev)
{
    printl("Address done");
    struct usb_setup_desc* setup = (struct usb_setup_desc *)urb->setup_buffer;
    dev->address = setup->wValue;
    print("DEVICE ADDRESS => %d\n", dev->address);

    /* Update the pipe address */
    struct usb_core* usbc = (struct usb_core *)urb->context;
    usbhc_set_address(usbc->pipe0, dev->address);
}

static void usbc_desc_length_done(struct urb* urb, struct usb_dev* dev)
{
    printl("Descriptor length done");
    struct usb_config_desc* cfg_desc = (struct usb_config_desc *)urb->transfer_buffer;
    print("Bytes recieved => %d\n", urb->acctual_length);
    if (urb->acctual_length != 9) {
        panic("Error");
    }
    dev->desc_total_size = cfg_desc->wTotalLength;
    print("Total length => %d\n", dev->desc_total_size);
}

static void usbc_get_all_desc_done(struct urb* urb, struct usb_dev* dev)
{
    if (!usbc_parse_descriptors(dev, urb->transfer_buffer, urb->acctual_length)) {
        panic("Cannot parse descriptors");
    }

    print("CFGS => %d\n", dev->num_configs);
    print("IFACE => %d\n", dev->configs[0].num_ifaces);

    for (u32 c = 0; c < dev->num_configs; c++) {
        struct usb_config_desc* c_ptr = &dev->configs[c].desc;
        usb_print_config_desc(c_ptr);
        for (u32 i = 0; i < dev->configs[c].num_ifaces; i++) {
            struct usb_iface_desc* i_ptr = &dev->configs[c].ifaces[i].desc;
            usb_print_iface_desc(i_ptr);
            print("NUM ENDPOINTS => %d\n", dev->configs[c].ifaces[i].num_eps);
            for (u32 e = 0; e < dev->configs[c].ifaces[i].num_eps; e++) {
                struct usb_ep_desc* e_ptr = &dev->configs[c].ifaces[i].eps[e].desc;
                usb_print_ep_desc(e_ptr);
            }
        }
    }

}

/*
 * Copies a unicode string to an ASCII string. It adds a zero terminating
 * character at the end. It takes in the maximum length of both strings so no
 * overflow will ever happend
 */
static void usbc_uni_to_string(const char* uni, u32 uni_size, char* string,
    u32 string_size)
{
    u32 uni_pos = 0;
    u32 ascii_pos = 0;

    /* Save space for the ternimation character */
    if (string_size > 0) {
        string_size--;
    } else {
        return;
    }
    while ((uni_pos < uni_size) && (ascii_pos < string_size)) {
        string[ascii_pos] = uni[uni_pos];
        ascii_pos += 1;
        uni_pos += 2;
    }

    /* Termination character */
    string[ascii_pos] = 0;
}

static void usbc_get_product_name_done(struct urb* urb, struct usb_dev* dev)
{
    struct usb_setup_desc* desc = (struct usb_setup_desc *)urb->setup_buffer;
    if (desc->bDescriptorIndex == 0) {
        return;
    }
    char* src = (char *)(urb->transfer_buffer + USB_STRING_OFFSET);
    usbc_uni_to_string(src, urb->acctual_length - 2,
        dev->product, USB_DEV_NAME_MAX_SIZE);
}

static void usbc_get_manufacturer_name_done(struct urb* urb, struct usb_dev* dev)
{
    struct usb_setup_desc* desc = (struct usb_setup_desc *)urb->setup_buffer;
    if (desc->bDescriptorIndex == 0) {
        return;
    }
    char* src = (char *)(urb->transfer_buffer + USB_STRING_OFFSET);
    usbc_uni_to_string(src, urb->acctual_length - 2,
        dev->manufacturer, USB_DEV_NAME_MAX_SIZE);
}

/*
 * This is called when the URB does not return OK. This is most of the times
 * due to NAK or STALL from the device
 */
static void usb_handle_urb_fail(struct urb* urb)
{
    panic("Enumeration URB failed");
}

/*
 * Starts the enumeration process using the default control pipe
 */
static void usbc_start_enum(struct usb_core* usbc)
{
    /* For control pipe only - force claim the pipe */
    usbc->pipe0->state = PIPE_STATE_CLAIMED;

    struct pipe_config cfg = {
        .bank_switch = 0,
        .banks = 1,
        .dev_addr = 0,
        .ep_addr = 0,
        .frequency = 0,
        .size = 64,
        .type = PIPE_TYPE_CTRL
    };
    usbhc_pipe_configure(usbc->pipe0, &cfg);

    /* 
     * Add a new USB device for the enumeration. This will be in the device list
     * and can also be accessed by the enum_dev pointer in the USB core 
     */
    usbc_add_device(usbc);

    struct urb* urb = usbhc_alloc_urb();
    usbhc_set_urb_context(urb, usbc);

    /* Start the enumeration */
    usbc_private->enum_state = USB_ENUM_GET_EP0_SIZE;
    usbc_get_dev_desc(urb, usbc, 0);
    printl("Enumeration has started");
}

static enum usb_enum_state usbc_get_enum_state(void)
{
    return usbc_private->enum_state;
}

/*
 * This will perform the enumeration of a newly attatched devices. Everything
 * will happend asynchronously since this is a URB callback. The entire
 * enumeration will only use one URB. A new device will be added in this
 * process
 */
static void usbc_enumerate_handler(struct urb* urb)
{
    if (urb->status != URB_STATUS_OK) {
        usb_handle_urb_fail(urb);
        return;
    }
    struct usb_core* usbc = (struct usb_core *)urb->context;

    print("Enumerate handler => ");
    switch(usbc->enum_state) {
        case USB_ENUM_IDLE : {
            break;
        }
        case USB_ENUM_GET_EP0_SIZE : {
            usbc_ep0_size_done(urb, usbc->enum_dev);
            usbhc_set_ep_size(usbc->pipe0, usbc->enum_dev->ep0_size);
            usbc->enum_state = USB_ENUM_GET_DEV_DESC;
            usbc_get_dev_desc(urb, usbc, 1);
            break;
        }
        case USB_ENUM_GET_DEV_DESC : {
            usbc_device_desc_done(urb, usbc->enum_dev);
            usbc->enum_state = USB_ENUM_SET_ADDRESS;
            usbc_set_dev_addr(urb, usbc);
            break;
        }
        case USB_ENUM_SET_ADDRESS : {
            usbc_address_done(urb, usbc->enum_dev);
            usbc->enum_state = USB_ENUM_GET_DESC_LENGTH;
            usbc_get_cfg_desc(urb, usbc);
            break;
        }
        case USB_ENUM_GET_DESC_LENGTH : {
            usbc_desc_length_done(urb, usbc->enum_dev);
            usbc->enum_state = USB_ENUM_GET_DESCRIPTORS;
            usbc_get_all_desc(urb, usbc);
            break;
        }
        case USB_ENUM_GET_DESCRIPTORS : {
            usbc_get_all_desc_done(urb, usbc->enum_dev);
            usbc->enum_state = USB_ENUM_GET_PRODUCT_NAME;
            usb_get_string_desc(urb, usbc, usbc->enum_dev->desc.iProduct, 0);
            break;

        }
        case USB_ENUM_GET_PRODUCT_NAME : {
            usbc_get_product_name_done(urb, usbc->enum_dev);
            usbc->enum_state = USB_ENUM_GET_MANUFACTURER_NAME;
            usb_get_string_desc(urb, usbc, usbc->enum_dev->desc.iManufacturer, 0);
            break;
        }
        case USB_ENUM_GET_MANUFACTURER_NAME : {
            usbc_get_manufacturer_name_done(urb, usbc->enum_dev);

            /* Do not deallocate the pipe since pipe 1 will be lost */
            usbhw_pipe_disable(usbc->pipe0->num);

            print("Product name => %s\n", usbc->enum_dev->product);
            print("Manufacturer name => %s\n", usbc->enum_dev->manufacturer);

            usb_print_dev_desc(&usbc->enum_dev->desc);
            if (!usbc_assign_driver(usbc->enum_dev, usbc)) {

            }
            break;
        }
    }
}

static u8 usbc_assign_driver(struct usb_dev* dev, struct usb_core* usbc)
{
    struct list_node* node;
    list_iterate(node, &dev->iface_list) {
        struct usb_iface* iface = list_get_entry(node, struct usb_iface, node);
        usb_print_iface_desc(&iface->desc);
        usb_print_dev_desc(&usbc->enum_dev->desc);
        struct usb_driver* driver = usbc_find_driver(iface, usbc);

        if (driver) {
            printl("Found a suitable driver");
        } else {
            printl("No driver support");
        }
    }
}

/*
 * This returns a new free address. An address is dynamically assigned to a 
 * device during enumeration. If the SET_ADDRESS command fails the address should
 * still be set. Only after a disconnection should the address be deleted!
 */
static u8 usbc_new_address(struct usb_core* usbc)
{
    u16 bitmask = usbc->dev_addr_bm;
    for (u8 i = 1; i < MAX_PIPES; i++) {
        if ((bitmask & (1 << i)) == 0) {
            usbc->dev_addr_bm |= (1 << i);
            return i;
        }
    }
    return 0;
}

static void usbc_delete_address(struct usb_core* usbc, u8 address)
{
    usbc->dev_addr_bm &= ~(1 << address);
}

/*
 * This will add a new device to the USB core layer. This will be available in
 * the device list as well as from the enum device pointer in the USB host core
 */
static struct usb_dev* usbc_add_device(struct usb_core* usbc)
{
    struct usb_dev* dev = (struct usb_dev *)
        bmalloc(sizeof(struct usb_dev), BMALLOC_SRAM);

    list_add_first(&dev->node, &usbc_private->dev_list);
    usbc->enum_dev = dev;

    /* Make sure we can contain a list of all the interfaces */
    list_init(&dev->iface_list);

    string_copy("None", dev->product);
    string_copy("None", dev->manufacturer);

    /* Initialize the device pipes */
    for (u8 i = 0; i < MAX_PIPES; i++) {
        dev->pipes[i] = NULL;
    }
    dev->pipe_bm = 0;

    return dev;
}

static u8 usbc_verify_descriptors(u8* data, u32 size, u32* configs, u32* ifaces, 
    u32* eps)
{ 
    *configs = 0;
    *eps = 0;
    *ifaces = 0;

    u32 pos = 0;
    while (pos < size) {
        u32 type = data[pos + 1];
        u32 size = data[pos];

        if (type == USB_DESC_CONFIG) {
            usb_print_config_desc((struct usb_config_desc *)(data + pos));
            if (size != sizeof(struct usb_config_desc)) {
                return 0;
            }
            *configs += 1;
        } else if (type == USB_DESC_IFACE) {
            usb_print_iface_desc((struct usb_iface_desc *)(data + pos));
            if (size != sizeof(struct usb_iface_desc)) {
                return 0;
            }
            *ifaces += 1;
        } else if (type == USB_DESC_EP) {
            print("EP size => %d\n", size);
            usb_print_ep_desc((struct usb_ep_desc *)(data + pos));
            if (size != sizeof(struct usb_ep_desc)) {
                return 0;
            }
            *eps += 1;
        }
        pos += size;
    }
    
    /* If all descriptors and descriptor sized are right */
    if (pos == size) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * The descriptor structure on the host size is structured in the following way;
 * all configuration descriptors, all interface descriptors, and all endpoint
 * descriptors. This function calculates the offset of a given descriptor type,
 * with a given index. This function assumes the desriptor buffer layout is a
 * described above
 */
static u32 usbc_get_desc_offset(u32 configs, u32 ifaces, u8 type, u32 index)
{
    u32 offset = 0;
    
    if (type == USB_DESC_CONFIG) {
        offset += index * sizeof(struct usb_config);
    } else if (type == USB_DESC_IFACE) {
        offset += configs * sizeof(struct usb_config);
        offset += index * sizeof(struct usb_iface);
    } else if (type == USB_DESC_EP) {
        offset += configs * sizeof(struct usb_config);
        offset += ifaces * sizeof(struct usb_iface);
        offset += index * sizeof(struct usb_ep);
    }

    print("OFFSET ========>>> %d\n", offset);
    return offset;
}

/*
 * Allocates space for all configuration, interface and endpoint descriptors
 * whithin a device. These will all be contained inside a single buffer. 
 */
static u8 usbc_alloc_descriptors(struct usb_dev* dev, u32 configs, u32 ifaces, u32 eps)
{
    /* Allocate the memory */
    u32 desc_mem_size = configs * sizeof(struct usb_config) +
                        ifaces * sizeof(struct usb_iface) + 
                        eps * sizeof(struct usb_ep);

    dev->configs = (struct usb_config *)bmalloc(desc_mem_size, BMALLOC_SRAM);
    dev->desc_total_size = desc_mem_size;

    return (dev->configs) ? 1 : 0;
}

/*
 * Deletes all the devices; endpoint, interface and configuration descriptors. 
 * This must be called before the devices is deleted. 
 */
static void usbc_delete_descriptors(struct usb_dev* dev)
{
    bfree(dev->configs);

    dev->configs = NULL;
    dev->desc_total_size = 0;
    dev->num_configs = 0;
}

/*
 * This function will initialize all the descriptors except the pointers between
 * them. The pointer resolving happends in the parse_descriptors function. The
 * descriptor buffer needs to be allocated prior to this function
 */
static void usbc_init_descriptors(struct usb_dev* dev, u32 configs, u32 ifaces, u32 eps)
{
    u8* ptr = (u8 *)dev->configs;

    /* Initialize configuration structure */
    for (u32 i = 0; i < configs; i++) {
        struct usb_config* cfg = (struct usb_config *)ptr;
        cfg->curr_iface = NULL;
        ptr += sizeof(struct usb_config);
    }

    /* Initialize interface descriptors */
    for (u32 i = 0; i < ifaces; i++) {
        struct usb_iface* iface = (struct usb_iface *)ptr;
        iface->driver = NULL;
        iface->parent_dev = dev;
        iface->assigned = 0;
        list_add_first(&iface->node, &dev->iface_list);
        ptr += sizeof(struct usb_iface);
    }

    /* Initialize endpoint descriptors */
    for (u32 i = 0; i < eps; i++) {
        struct usb_ep* ep = (struct usb_ep *)ptr;
        ep->pipe = NULL;
        ptr += sizeof(struct usb_ep);
    }
}   

/*
 * Allocates and parses the entire descriptor tree. This consists of tree types
 * of descriptors; configuration, interface and endpoints. Everything is
 * contained in a single buffer pointed to by device->configs. The buffer layout
 * is as follows
 *                                device->desc_size
 *                  _____________________/\____________________
 * buffer start => | I x config | J x interface | K x endpoint |
 * 
 * The numbers I, J and K can be obtained by verifying the descriptor 
 * structure. This will also be stored together with the buffer pointers
 */
static u8 usbc_parse_descriptors(struct usb_dev* dev, u8* data, u32 size)
{
    u32 configs;
    u32 ifaces;
    u32 eps;

    if (!usbc_verify_descriptors(data, size, &configs, &ifaces, &eps)) {
        return 0;
    }

    if (!usbc_alloc_descriptors(dev, configs, ifaces, eps)) {
        return 0;
    }
    usbc_init_descriptors(dev, configs, ifaces, eps);

    u32 config_index = 0;
    u32 iface_index = 0;
    u32 ep_index = 0;

    struct usb_config* last_cfg = NULL;
    struct usb_iface* last_iface = NULL;

    u8* ptr = (u8 *)dev->configs;

    u32 pos = 0;
    while (pos < size) {
        u32 type = data[pos + 1];

        if (type == USB_DESC_CONFIG) {
            u32 offset = usbc_get_desc_offset(configs, ifaces, USB_DESC_CONFIG,
                config_index);

            struct usb_config* cfg = (struct usb_config *)(ptr + offset);
            memory_copy(data + pos, &cfg->desc, sizeof(struct usb_config_desc));

            dev->num_configs++;
            cfg->num_ifaces = 0;
            last_cfg = cfg;
            config_index++;

        } else if (type == USB_DESC_IFACE) {
            u32 offset = usbc_get_desc_offset(configs, ifaces, USB_DESC_IFACE,
                iface_index);

            struct usb_iface* iface = (struct usb_iface *)(ptr + offset);
            memory_copy(data + pos, &iface->desc, sizeof(struct usb_iface_desc));
            last_iface = iface;
            last_iface->num_eps = 0; 
            
            if (last_cfg == NULL) {
                return 0;
            }

            /* First interface in configuration */
            if (last_cfg->num_ifaces == 0) {
                last_cfg->ifaces = iface;
            }
            last_cfg->num_ifaces++;
            iface_index++;

        } else if (type == USB_DESC_EP) {
            u32 offset = usbc_get_desc_offset(configs, ifaces, USB_DESC_EP,
                ep_index);

            struct usb_ep* ep = (struct usb_ep *)(ptr + offset);
            memory_copy(data + pos, &ep->desc, sizeof(struct usb_ep));

            if (last_iface == NULL) {
                return 0;
            }
            
            /* First endpoint in interface */
            if (last_iface->num_eps == 0) {
                last_iface->eps = ep;
            }
            last_iface->num_eps++;
            ep_index++;
        }
        pos += data[pos];
    }

    /* No need to check pos == size bacause the descriptors are verified  */
    return 1;
}



/*
 * Checks if the given usb_dev_id matches the given USB device. It returns 1
 * if the match and 0 if they do not
 */
static u8 usbc_check_dev_match(const struct usb_dev_id* id, struct usb_dev* dev)
{
    u32 flags = id->flags;
    if (flags & USB_DEV_ID_VENDOR_MASK) {
        if (id->vendor_id != dev->desc.idVendor) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_PRODUCT_MASK) {
        if (id->product_id != dev->desc.idProduct) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_DEV_CLASS_MASK) {
        if (id->dev_class != dev->desc.bDeviceClass) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_DEV_SUBCLASS_MASK) {
        if (id->dev_sub_class != dev->desc.bDeviceSubClass) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_DEV_PROTOCOL_MASK) {
        if (id->dev_protocol != dev->desc.bDeviceProtocol) {
            return 0;
        }
    }
    return 1;
}

/*
 * Checks if the given usb_dev_id matches the given USB interface. It returns 1
 * if the match and 0 if they do not
 */
static u8 usbc_check_iface_match(const struct usb_dev_id* id, struct usb_iface* iface)
{
    u32 flags = id->flags;
    if (flags & USB_DEV_ID_IFACE_CLASS_MASK) {
        if (id->iface_class != iface->desc.bInterfaceClass) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_IFACE_SUBCLASS_MASK) {
        if (id->iface_sub_class != iface->desc.bInterfaceSubClass) {
            return 0;
        }
    }
    if (flags & USB_DEV_ID_IFACE_PROTOCOL_MASK) {
        if (id->iface_protocol != iface->desc.bInterfaceProtocol) {
            return 0;
        }
    }
    return 1;
}

/*
 * Checks is a device is supported by the given driver. Returns 1 if it thinks
 * it can support it and zero otherwise. 
 */
static u8 usbc_check_driver_match(struct usb_driver* driver, struct usb_iface* iface)
{
    u32 count = driver->num_dev_ids;
    const struct usb_dev_id* dev_ids = driver->dev_ids;

    /* Go through and check for a valid driver */
    for (u32 i = 0; i < count; i++) {
        /* Check device fields */
        if (usbc_check_dev_match(dev_ids + i, iface->parent_dev))
        {
            if (usbc_check_iface_match(dev_ids + i, iface)) {
                return 1;
            }
        }
    }
    return 0;
}

static struct usb_driver* usbc_find_driver(struct usb_iface* iface, struct usb_core* usbc)
{
    /* Check if the interface is free */
    if (iface->assigned == 1) {
        return NULL;
    }

    struct list_node* node;
    list_iterate(node, &usbc->driver_list) {
        struct usb_driver* driver = list_get_entry(node, struct usb_driver, node);

        u8 status = usbc_check_driver_match(driver, iface);

        if (status) {
            /* The driver can support the device */
            if (!driver->connect(iface)) {
                return NULL;
            }
            return driver;
        }
    }
    return NULL;
}

/*
 * Default USB core root hub event handler. This callback will be called by the
 * USB host controller upon a change in the root hub status. This will start
 * the tire 1 device enumeration process, since the reset send event will be
 * reported by the root hub.
 * 
 * All other enumation requests will happend via the USB HUB interface. This 
 * will handle the device discovery, reset and enumeration request
 */
void root_hub_event(struct usbhc* usbhc, enum root_hub_event event)
{
    if (event == RH_EVENT_CONNECTION) {
        printl("USB core => connection");
        usbhc_send_reset();

    } else if (event == RH_EVENT_DISCONNECTION) {
        printl("USB core => disconnection");

    } else if (event == RH_EVENT_RESET_SENT) {
        printl("USB core => reset sent");
        usbc_start_enum(usbc_private);
    }
}

/*
 * Start of frame / start of micro-frame callback for the USB core
 */
static void sof_event(struct usbhc* usbhc)
{

}

void usbc_init(struct usb_core* usbc, struct usbhc* usbhc)
{
    usbc_private = usbc;
    usbc_private->enum_state = USB_ENUM_IDLE;

    usbc->usbhc = usbhc;
    usbc->pipe0 = &usbhc->pipes[0];

    /* Initialize the device list */
    list_init(&usbc->dev_list);
    usbc->dev_addr_bm = 1;

    /* Initialize the driver list */
    list_init(&usbc->driver_list);

    usbhc_add_root_hub_callback(usbhc, &root_hub_event);
    usbhc_add_sof_callback(usbhc, &sof_event);
}

/*
 * Adds a driver to the system
 */
void usbc_add_driver(struct usb_driver* driver, struct usb_core* usbc)
{
    list_add_first(&driver->node, &usbc->driver_list);
}
