#include <asm/uaccess.h> 
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>  
#include <linux/kobject.h> 
#include <linux/module.h> 
#include <mach/hardware.h>


/*
/======================lookup table ===========================================
/
*/
#define PORT_0   0
#define PORT_2   2
#define PORT_3   3

#define JUMPER_1 1
#define JUMPER_2 2
#define JUMPER_3 3

typedef struct portlink
{
	uint32_t port;
	uint32_t bit;
	uint32_t jumper;
	uint32_t pin;
} portCombination;

//typedef struct portlink portCombination;

const portCombination lookup[] =
{	
	/* port, bit, jumper, pin */
	{PORT_0, 0, JUMPER_3, 40},
	{PORT_0, 1, JUMPER_2, 24},
	{PORT_0, 2, JUMPER_2, 11},
	{PORT_0, 3, JUMPER_2, 12},
	{PORT_0, 4, JUMPER_2, 13},
	{PORT_0, 5, JUMPER_2, 14},
	{PORT_0, 6, JUMPER_3, 33},
	{PORT_0, 7, JUMPER_1, 27},

	{PORT_2, 0, JUMPER_3, 47},
	{PORT_2, 2, JUMPER_3, 48},
	{PORT_2, 1, JUMPER_3, 56},
	{PORT_2, 3, JUMPER_3, 57},
	{PORT_2, 4, JUMPER_3, 49},
	{PORT_2, 5, JUMPER_3, 58},
	{PORT_2, 6, JUMPER_3, 50},
	{PORT_2, 7, JUMPER_3, 45},
	{PORT_2, 8, JUMPER_1, 49},
	{PORT_2, 9, JUMPER_1, 50},
	{PORT_2, 10, JUMPER_1, 51},
	{PORT_2, 11, JUMPER_1, 52},
	{PORT_2, 12, JUMPER_1, 53},

	{PORT_3, 0, JUMPER_3, 54},
	{PORT_3, 1, JUMPER_3, 46},
	{PORT_3, 4, JUMPER_3, 36},
	{PORT_3, 5, JUMPER_1, 24}
};

portCombination getPort(uint32_t jumper, uint32_t pin);portCombination getPort(uint32_t jumper, uint32_t pin)
{
	int i = 0;
	portCombination result = {0, 0, 0, 0};
	for(i = 0; i < (sizeof(lookup) / sizeof(portCombination)); i++)
	{
		if(lookup[i].jumper == jumper && lookup[i].pin == pin)
		{
			result = lookup[i];
		}
	}
	return result;
}
/*
/======================end lookup table =======================================
/======================registers ==============================================
*/

#define P0_INP_STATE    0x40028040
#define P0_OUTP_SET     0x40028044
#define P0_OUTP_CLR     0x40028048
#define P0_OUTP_STATE   0x4002804C
#define P0_DIR_SET      0x40028050
#define P0_DIR_CLR      0x40028054
#define P0_DIR_STATE    0x40028058
#define P0_MUX_SET      0x40028120
#define P0_MUX_CLR      0x40028124
#define	P0_MUX_STATE    0x40028128

#define P2_INP_STATE    0x4002801C
#define P2_OUTP_SET     0x40028020
#define P2_OUTP_CLR     0x40028024
#define P2_DIR_SET      0x40028010
#define P2_DIR_CLR      0x40028014
#define P2_DIR_STATE    0x40028018
#define	P2_MUX_SET      0x40028028
#define	P2_MUX_CLR      0x4002802C
#define	P2_MUX_STATE    0x40028030

#define P3_OUTP_SET     0x40028004	
#define P3_OUTP_STATE   0x4002800C
#define P3_INP_STATE    0x40028000
#define P3_OUTP_CLR     0x40028008
#define P3_DIR_SET      0x40028010
#define P3_DIR_CLR      0x40028014
#define P3_MUX_SET      0x40028110
#define	P3_MUX_CLR      0x40028114
#define P3_MUX_STATE    0x40028118
#define	P3_DIR_STATE    0x40028018

/*
/======================end registers ==========================================
/======================gpio module ============================================
*/

#define DEF_CMD_INPUT       'i'
#define DEF_CMD_OUTPUT      'o'
#define SUCCES              1
#define FAILURE             0
#define NO_MASK             0xFFFF

#define P2_MUX_MASK         0x8
#define LCD_CTRL            0x31040018
#define DEVICE_NAME         "GPIO"
#define sysfs_dir           "gpio"
#define sysfs_file          "config"
#define MAX_DEVFS_BUFFER    50

#define INPUT               1
#define OUTPUT              0
#define PORT3_DIR_OFFSET    25
#define PT3_GPIO04_OFFSET   10
#define PT3_GPIO5_OFFSET    24

#define sysfs_max_data_size 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bart Kolvoort, Yamil Baptista, Sander Everaers");
MODULE_DESCRIPTION("GPIO");

static uint32_t major_number;
static bool device_opened = false;
static ssize_t used_buffer_size = 0;

int32_t inputValue  = -1;
int32_t inputJumper = -1;
int32_t inputPin    = -1;


/*
/======================Region util functions ==================================
*/
uint32_t get_register_addr( uint32_t port, 
							uint32_t** dir_set, uint32_t** dir_clr, 
							uint32_t** mux_set, uint32_t** mux_clr)
{
	switch(port)
	{
		case PORT_0:
			*dir_set = (uint32_t*)P0_DIR_SET;
			*dir_clr = (uint32_t*)P0_DIR_CLR;
			*mux_set = (uint32_t*)P0_MUX_SET;
			*mux_clr = (uint32_t*)P0_MUX_CLR;
			return SUCCES;
			break;
		case PORT_2:
			*dir_set = (uint32_t*)P2_DIR_SET;
			*dir_clr = (uint32_t*)P2_DIR_CLR;
			*mux_set = (uint32_t*)P2_MUX_SET;
			*mux_clr = (uint32_t*)P2_MUX_CLR;
			return SUCCES;
			break;
		case PORT_3:
			*dir_set = (uint32_t*)P3_DIR_SET;
			*dir_clr = (uint32_t*)P3_DIR_CLR;
			*mux_set = (uint32_t*)P3_MUX_SET;
			*mux_clr = (uint32_t*)P3_MUX_CLR;
			return SUCCES;
			break;
		default:
			printk(KERN_WARNING "port not found: %d", port);
			return -EINVAL;
			break;
	}
}

uint32_t get_mux_mask(portCombination port)
{
	switch (port.port)
	{
		case PORT_0:
		{
			return (0 << port.bit);
			break;
		}
		case PORT_2:
		{
			return P2_MUX_MASK;
			break;
		}
		case PORT_3:
		{
			*(unsigned int*)(io_p2v(LCD_CTRL)) = 0;
			if(port.bit == 4 || port.bit == 5)
			{
				return (1 << port.bit);
			}
			else
			{
				printk(KERN_ERR "bit %d doesn't have a mutliplexer", port.bit);
				return NO_MASK;
			}
			break;
		}
		default:
			return FAILURE;
	}
}

void mux_config(uint32_t* registerMux, uint32_t mask)
{
	uint32_t* regMux = io_p2v((uint32_t)registerMux);
	*regMux |= mask;
}

void dir_config(uint32_t* registerDir, uint32_t bit)
{
	uint32_t* regDir = (io_p2v((uint32_t)registerDir));
	*regDir |= (1 << bit);
}


bool is_mux_gpio (portCombination port)
{
	uint32_t* portMux = 0;
	switch(port.port)
	{
		case PORT_0:
		{
			portMux = io_p2v(P0_MUX_STATE);
			break;
		}
		case PORT_2:
		{
			portMux = io_p2v(P2_MUX_STATE);
			break;
		}
		case PORT_3:
		{
			portMux = io_p2v(P3_MUX_STATE);
			if(get_mux_mask(port) == NO_MASK)
			{
				return true;
			}
			else if(get_mux_mask(port) == FAILURE)
			{
				return false;
			}
			break;
		}
		default:
		{
			printk(KERN_ALERT "port not known: %d\n", port.port);
			return -EINVAL;
		}
	}

	return (*portMux | get_mux_mask(port)) == *portMux; 
}

bool is_dir_set(portCombination port, int direction)
{
	volatile uint32_t* portDir = 0;
	uint32_t reg = 0;

	switch(port.port)
	{
		case PORT_0:
		{
			portDir = io_p2v(P0_DIR_STATE);
			reg = *portDir & (1 << port.bit);
			break;
		}
		case PORT_2:
		{			
			portDir = io_p2v(P2_DIR_STATE);
			reg = *portDir & (1 << port.bit);
			break;
		}
		case PORT_3:
		{
			portDir = io_p2v(P3_DIR_STATE);
			
			reg = *portDir & (1 << (port.bit + PORT3_DIR_OFFSET));
			break;
		}
		default:
		{
			printk(KERN_ALERT "port not known: %d\n", port.port);
			return -EINVAL;
		}
	}
	if (port.port == PORT_3)
	{
		return reg ==  direction << (port.bit + PORT3_DIR_OFFSET);
	}
	else
	{
		return reg ==  direction << port.bit;
	}
}

int port3_gpio(uint32_t portBit, uint32_t* reg)
{
	int result = -1;
	int toWrite = (1 << (portBit + PORT3_DIR_OFFSET));
	result = *reg |= toWrite;
	return *reg == result;
}

void set_register_output(portCombination port, bool state)
{
	uint32_t* reg = 0;
	switch(port.port)
	{
		case PORT_0:
		{
			if(state)
			{
				reg = io_p2v(P0_OUTP_SET);
				*reg |= (1 << port.bit);
			}	
			else
			{
				reg = io_p2v(P0_OUTP_CLR);
				*reg |= (1 << port.bit);
			}	
			break;
		}
		case PORT_2:
		{			
			if(state)
			{
				reg = io_p2v(P2_OUTP_SET);
				*reg |= (1 << port.bit);
			}	
			else
			{
				reg = io_p2v(P2_OUTP_CLR);
				*reg |= (1 << port.bit);
			}
			break;
		}
		case PORT_3:
		{
			if(state)
			{
				reg = io_p2v(P3_OUTP_SET);
				port3_gpio(port.bit, reg);
			}	
			else
			{
				reg = io_p2v(P3_OUTP_CLR);
				port3_gpio(port.bit, reg);
			}
			break;
		}
		default:
		{
			printk(KERN_ALERT "port not known: %d\n", port.port);
		}
	}
}

int get_register_input(portCombination port)
{
	uint32_t* reg;
	int result = -1;
	switch(port.port)
	{
		case PORT_0:
		{
			reg = io_p2v(P0_INP_STATE);
			result = *reg & (1 << port.bit);
			return (result == 0);
			break;
		}
		case PORT_2:
		{			
			reg = io_p2v(P2_INP_STATE);
			result = *reg & (1 << port.bit);
			return (result == 0);	
			break;
		}
		case PORT_3:
		{
			reg = io_p2v(P3_INP_STATE);
			return port3_gpio(port.bit, reg);		
			break;
		}
		default:
		{
			printk(KERN_ALERT "port not known: %d\n", port.port);
			return -EINVAL;
		}
	}
}


/*
/======================End utility functions ==================================
/======================Region sysfs functions =================================
*/

static ssize_t sysfs_write(struct device *dev, struct device_attribute *attr, const char *buffer, size_t count)
{
	uint32_t pin = 0;
	uint32_t jumper = 0;
	char command = '\0';
	portCombination port = {0,0,0,0};

	uint32_t* dir_set = 0;
	uint32_t* dir_clr = 0;
	uint32_t* mux_set = 0;
	uint32_t* mux_clr = 0; 
	uint32_t mux_mask = 0;

	used_buffer_size = count > sysfs_max_data_size ? sysfs_max_data_size : count;

	printk(KERN_INFO "sysfile_write (/sys/kernel/%s/%s) called, buffer: "
	   "%s, count: %u\n", sysfs_dir, sysfs_file, buffer, count);

	sscanf(buffer, "%c %d %d", &command, &jumper, &pin);
	port = getPort(jumper, pin);

	mux_mask = get_mux_mask(port);

	get_register_addr(port.port, &dir_set, &dir_clr, &mux_set, &mux_clr);

	switch(command)
	{
		case DEF_CMD_INPUT:
			mux_config(port.port != PORT_2 ? mux_clr : mux_set, mux_mask);
			if (port.port == PORT_3) 
			{
				dir_config(dir_clr, port.bit + PORT3_DIR_OFFSET);
			} 
			else 
			{
				dir_config(dir_clr, port.bit);
			}
			printk(KERN_INFO "Pin correctly configured to input.\n");
			break;
		case DEF_CMD_OUTPUT:
			mux_config(port.port != PORT_2 ? mux_clr : mux_set, mux_mask);
			if (port.port == PORT_3) 
			{
				dir_config(dir_set, port.bit + PORT3_DIR_OFFSET);
			} 
			else 
			{
				dir_config(dir_set, port.bit);
			}
			printk(KERN_INFO "Pin correctly configured to output.\n");
			break;
		default:
			printk(KERN_WARNING "command not found: %c\n", command);
			break;
	}

	return used_buffer_size;
}

/*
/======================End sysfs functions ====================================
/======================Region devfs functions =================================
*/

static int device_open(struct inode *inode, struct file *file)
{
	uint32_t minor = iminor(inode); 

	if(minor >= 0)
	{
		file->private_data = (void*)minor;
	}

    device_opened = true;

    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    device_opened = false;
    return 0;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)    
{

	const uint32_t MaxSize = length < MAX_DEVFS_BUFFER ? length : MAX_DEVFS_BUFFER;
    char msg[MaxSize];
    int32_t msgLength = 0;
    uint32_t bytesRemaining = 0;
    uint32_t minor = 0;

    if (*offset != 0) 
    {
        return 0;
    }

    if(filp != NULL)
    {
    	minor = (uint32_t)filp->private_data;
    	printk(KERN_INFO "Received minor numer: %d.\n", minor);
	}
	else
	{
		printk(KERN_ALERT "Recieved no file");
	}

    if(minor < 0)
	{
		printk(KERN_ALERT "No minor assigned: %d.\n", minor);
		return -EINVAL;
	}

	switch(minor)
    {
    	case INPUT:
    		msgLength = snprintf(msg, MaxSize, "%d %d {%d}\n", inputJumper, inputPin, inputValue);
    		break;
    	case OUTPUT:
    		//ignore case
    		break;
    	default:
    		printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    		break;
    }

	bytesRemaining = copy_to_user(buffer, msg, msgLength);
    *offset += msgLength - bytesRemaining;
    return msgLength - bytesRemaining;
}


static ssize_t device_write(struct file *filp, const char *buffer, size_t len, loff_t *off)
{

	uint32_t minor = 0;
	portCombination port = {0,0,0,0};

    uint32_t jumper = 0;
    uint32_t pin = 0;
    uint32_t state = 0;
    int result = -1;

    minor = (uint32_t)filp->private_data;

	if(len > MAX_DEVFS_BUFFER)
	{
		return -EMSGSIZE;
	}

	if(minor < 0)
	{
    	printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    	return -EINVAL;
	}

	sscanf(buffer, "%d %d %d", &jumper, &pin, &state);

	port = getPort(jumper, pin);
	inputJumper = jumper;
	inputPin = pin;


	switch(minor)
    {
    	case INPUT:
    	{
    		if(is_mux_gpio(port))
    		{
    			if(is_dir_set(port, 0))
    			{
    				result = get_register_input(port);
    				if(result < 0)
    				{
    					printk(KERN_ALERT "fatal error: cannot read register (%d)\n", result);
    				}
    				inputValue = result;
    			}
    			else
    			{
    				printk(KERN_ALERT "direction is not set\n");
    			}
    		}
    		else
    		{
    			printk(KERN_ALERT "port mux is not set\n");
    			return -EINVAL;
    		}
    		break;
    	}
    	case OUTPUT:
    	{
    		if(is_mux_gpio(port))
    		{
    			if(is_dir_set(port, 1))
    			{
    				set_register_output(port, state);
    			}
    			else
    			{
    				printk(KERN_ALERT "direction is not set\n");
    			}
    		}
    		else
    		{
    			printk(KERN_ALERT "port mux is not set\n");
    			return -EINVAL;
    		}
    		break;
    	}
    	default:
    	{
    		printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    		return -EINVAL;
    		break;
    	}
    }
    return (int)len;
}

/*
/======================End devfs functions ===================================
*/

static DEVICE_ATTR(config, S_IWUGO | S_IRUGO, NULL, sysfs_write);
static struct attribute *attrs[] = {&dev_attr_config.attr, NULL};
static struct attribute_group attr_group = {.attrs = attrs, };
static struct kobject *gpio_obj = NULL;

static struct file_operations fops =
{	
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};

int __init sysfs_init(void)
{
	int result = 0;

	//devfs
	major_number = register_chrdev(0, DEVICE_NAME, &fops);

    if (major_number < 0) {
        printk(KERN_ALERT "Registering char device failed with %d\n", major_number);
        return major_number;
    }

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major_number);
    printk(KERN_INFO "the driver, create the following dev files:\n");
    printk(KERN_INFO "'mknod /dev/gpo c %d 0'.\n", major_number);
    printk(KERN_INFO "'mknod /dev/gpi c %d 1'.\n", major_number);
    printk(KERN_INFO "Remove the device files and module when done.\n");

    //sysfs
	gpio_obj = kobject_create_and_add(sysfs_dir, kernel_kobj);
	if (gpio_obj == NULL) 
	{
		printk(KERN_WARNING "%s module failed to load: kobject_create_and_add failed\n", sysfs_file);
		return -ENOMEM;
	}

	result = sysfs_create_group(gpio_obj, &attr_group);
	if (result != 0) 
	{
		/* creating files failed, thus we must remove the created directory! */
		printk(KERN_WARNING "%s module failed to load: sysfs_create_group " "failed with result %d\n", sysfs_file, result);
		kobject_put(gpio_obj);
		return -ENOMEM;
	}

	printk(KERN_INFO "/sys/kernel/%s/%s created\n", sysfs_dir, sysfs_file);
	return result;
}

void __exit sysfs_exit(void)
{
	kobject_put(gpio_obj);
	unregister_chrdev(major_number, DEVICE_NAME);
	printk(KERN_INFO "/sys/kernel/%s/%s removed\n", sysfs_dir, sysfs_file);
}

module_init(sysfs_init);
module_exit(sysfs_exit);

/*
/======================End gpio module=========================================
*/