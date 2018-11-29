#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/uaccess.h>

#include "../drivers/hid/hid-ids.h"

static struct usb_device *device;
static struct usb_driver bloody_driver;
static struct usb_class_driver class;
static struct file_operations fops;

#define BLOODY_MAGIC 		0x07
#define BLOODY_BL_OPCODE	0x11
#define BLOODY_BL_WRITE 	0x80
#define BLOODY_BL_READ 		0x00
#define BLOODY_BL_INDEX  	8

// supported devices (incomplete, partially tested)
#define BLOODY_V5_PID 0x172A
#define BLOODY_V7_PID 0xF613
#define BLOODY_V8_PID 0x11F5
#define BLOODY_R7_PID 0x1485

// USB Bulk read/write
#define BULK_EP_OUT 0x01
#define BULK_EP_IN 0x82
#define MAX_PKT_SIZE 512
static unsigned char bulk_buf[MAX_PKT_SIZE];

static int bloody_backlight_level;
static int bloody_set_backlight_level(unsigned char level);

static int __init
bloody_init(void)
{
        int res;
        res = usb_register(&bloody_driver);
        if(res < 0)
                printk(KERN_INFO "usb_register failed for"__FILE__" with %d\n", res);
        
        return res;
}

static void __exit
bloody_exit(void)
{
        usb_deregister(&bloody_driver);
	return;
}

static int
bloody_probe(
                struct usb_interface *interface, 
                const struct usb_device_id *id)
{
        int ret;
        device = interface_to_usbdev(interface);
        class.name = "usb/bloody%d";
        class.fops = &fops;
        if((ret = usb_register_dev(interface, &class)) < 0)
                printk(KERN_ERR __FILE__": unable to get a minor for this device\n");
        else
                printk(KERN_INFO __FILE__": minor obtained %d\n", interface->minor);
        return ret;
}

static void
bloody_disconnect(struct usb_interface *interface)
{
        printk(KERN_INFO __FILE__" disconnected\n");
        usb_deregister_dev(interface, &class);
	return;
}

static int
bloody_ctrl_write_to_dev(unsigned char data[], size_t size)
{
        int ret = 0;

	ret = usb_control_msg(
		device,		// dev
		usb_rcvctrlpipe(device, 0), // pipe
		0x9,	// request
		0x21,		// requesttype
		0x0307,		// value
		2,		// index
		data,		// data
		size,		// size
		5000		// timeout
	);

        return ret;
}

static int
bloody_ctrl_read_from_dev(
	unsigned char *request, size_t request_size,
	unsigned char *response, size_t response_size)
{
	int ret = 0;

	// write the request
	if((ret = bloody_ctrl_write_to_dev(request, request_size)) < 0)
		goto fail;

	// receive response
	ret = usb_control_msg(
		device,		// dev
		usb_rcvctrlpipe(device, 0), // pipe
		0x1,		// request
		0xa1,		// requesttype
		0x0307,		// value
		2,		// index
		response,	// data
		response_size,	// size
		5000		// timeout
	);

fail:
	return ret;
}

unsigned char
bloody_get_backlight_level(void)
{
	unsigned char request[72] = 
	{
		BLOODY_MAGIC,
		BLOODY_BL_OPCODE,
		0x00,0x00,
		BLOODY_BL_READ,
		0x00
	};
	unsigned char response[72];
	
	bloody_ctrl_read_from_dev(request, sizeof(request), response, sizeof(response));

	return response[BLOODY_BL_INDEX];
}

static int
bloody_set_backlight_level(unsigned char level)
{
        int ret=0;
        static unsigned char bl_packet[72] =
        {
                BLOODY_MAGIC,
                BLOODY_BL_OPCODE,
                0x00,0x00,
                BLOODY_BL_WRITE,
                0x00,0x00,0x00,
                0xFF //level - magic byte
        };
	bl_packet[BLOODY_BL_INDEX] = level;

        ret = bloody_ctrl_write_to_dev(bl_packet, sizeof(bl_packet));
        if(ret < 0)
                printk(KERN_WARNING __FILE__
                        ":set_backlight_level(%d) failed with %d\n",
                        level, ret);

        return ret;
}

static int
bloody_bulk_open(struct inode *i, struct file *f)
{
        return 0;
}

static int
bloody_bulk_close(struct inode *i, struct file *f)
{
        return 0;
}

static ssize_t
bloody_bulk_read(struct file *f, char __user *buf, size_t cnt, loff_t *off)
{
        int ret;
	int read_cnt;

        ret = usb_bulk_msg(
                        device,
                        usb_rcvbulkpipe(device, BULK_EP_IN),
                        bulk_buf,
                        MAX_PKT_SIZE,
                        &read_cnt, 
                        5000
        );

        if(ret)
        {
                printk(KERN_ERR __FILE__": bulk message returned %d\n", ret);
                return ret;
        }

        if(copy_to_user(buf, bulk_buf, cnt < read_cnt ? cnt : read_cnt))
                return -EFAULT;

        return cnt < read_cnt ? cnt : read_cnt;
}

static ssize_t
bloody_bulk_write(
	struct file *f,
	const char __user *buf,
	size_t cnt,
	loff_t *off)
{
	int ret;
	int wrote_cnt = cnt < MAX_PKT_SIZE ? cnt : MAX_PKT_SIZE;

	if(copy_from_user(bulk_buf, buf, cnt < MAX_PKT_SIZE ? cnt : MAX_PKT_SIZE))
		return -EFAULT;
	
	ret = usb_bulk_msg(
		device,
		usb_sndbulkpipe(device, BULK_EP_OUT),
		bulk_buf,
		cnt < MAX_PKT_SIZE ? cnt : MAX_PKT_SIZE, 
		&wrote_cnt,
		5000
	);

	if(ret)
	{
		printk(KERN_ERR __FILE__": bulk message returned %d\n", ret);
		return ret;
	}

	return wrote_cnt;
}

static struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = bloody_bulk_open,
	.release = bloody_bulk_close,
	.read = bloody_bulk_read,
	.write = bloody_bulk_write,
};

static int
bloody_param_bl_set(
	const char *val_str,
	const struct kernel_param *kp)
{
	int val=0, ret=0;
	ret = kstrtoint(val_str, 10, &val);

	if(val < 0 || val > 3)
		return -EINVAL;

	if(bloody_set_backlight_level(val) < 0)
		return -EINVAL;

	return param_set_int(val_str, kp);
}

static int
bloody_param_bl_get(
		char *val_str,
		const struct kernel_param *kp)
{
	unsigned char val = bloody_get_backlight_level(); 
	sprintf(val_str, "%u", val);
	return strlen(val_str);
}

static const struct kernel_param_ops bloody_backlight_level_ops =
{
	.set = bloody_param_bl_set,
	.get = bloody_param_bl_get,
};

module_param_cb(
	bloody_backlight_level,
	&bloody_backlight_level_ops,
	&bloody_backlight_level,
	0644
);

MODULE_PARM_DESC(bloody_backlight_level,
	"Set backligt level on bloody mice");

static const struct usb_device_id bloody_devices[] =
{
	{ USB_DEVICE(USB_VENDOR_ID_A4TECH, BLOODY_V7_PID) },
	{ USB_DEVICE(USB_VENDOR_ID_A4TECH, BLOODY_R7_PID) },
	{ USB_DEVICE(USB_VENDOR_ID_A4TECH, BLOODY_V8_PID) },
	{ USB_DEVICE(USB_VENDOR_ID_A4TECH, BLOODY_V5_PID) },
	{}
};
MODULE_DEVICE_TABLE(usb, bloody_devices);

static struct usb_driver bloody_driver = {
        .name = "A4Tech bloody mouse driver",
	.id_table = bloody_devices,
        .probe = bloody_probe,
        .disconnect = bloody_disconnect,
};

module_init(bloody_init);
module_exit(bloody_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A4Tech Bloody driver");
MODULE_AUTHOR("Tomas Pruzina <pruzinat@gmail.com>");
