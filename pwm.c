#include <asm/uaccess.h> 
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/hardware.h>
#include <float.h>

#define SUCCESS 			0
#define DEVICE_NAME 		"PWM"
#define MAX_DEVFS_BUFFER	50

#define PWM1_CTRL_ADDR 		0x4005C000
#define PWM2_CTRL_ADDR 		0x4005C004
#define PWM_FREQ_MASK 		0xFF00
#define PWM_FREQ_SHIFT		8
#define PWM_CYCLE_MASK 		0xFF
#define PWM_CYCLE_SHIFT 	0
#define PWM_ENABLE_MASK 	0x80000000
#define PWM_ENABLE_SHIFT	31 
#define PWM_LEVEL_MASK 		0x40000000
#define PWM_LEVEL_SHIFT		30
#define PWM_CLOCK_CTRL 		0x400040B8
#define PWM_CLOCK_MASK		0x115
#define	PWM2_INT_MASK		0x6FFFFFFF

#define LCD_CTRL  			0x31040018
#define PWM_DIVIDER 		256
#define PWM_CLK				32000
#define MAX_DUTY 			256
#define MAX_PERCENTAGE		100

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bart Kolvoort");
MODULE_DESCRIPTION("PWM");

/*
 * Global variables are declared as static, so are global within the file.
 */
static uint32_t major_number; /* major number assigned to our device driver */
static bool device_opened = false; /* prevents simultaneous access to device */

uint32_t freq_to_RLD(uint32_t freq)
{
	if(freq > 0)
	{
		return (PWM_CLK/PWM_DIVIDER) / freq;
	}
	else
	{
		printk(KERN_ALERT "Invalid frequency set: %d\n Frequency must be greater than 0\n", freq);
		return -EINVAL;
	}
}

uint32_t RLD_to_freq(uint32_t rld)
{
	if(rld > 0)
	{
		return (PWM_CLK/rld) / PWM_DIVIDER;
	}
	else
	{
		printk(KERN_ALERT "Invalid reloadvalue set: %d\n Reloadvalue must be greater than 0\n", rld);
		return -EINVAL;
	}
}

uint32_t duty_to_percentage(uint32_t duty)
{
	if(duty > 0)
	{
		return (duty * MAX_PERCENTAGE) / MAX_DUTY;
	}
	else
	{
		printk(KERN_ALERT "Invalid duty cycle set: %d\n duty cycle must be greater than 0\n", duty);
		return -EINVAL;
	}
}

uint32_t percentage_to_duty(uint32_t percentage)
{
	if(percentage > 0)
	{
		return (MAX_DUTY * percentage) / MAX_PERCENTAGE;	
	}
	else
	{
		printk(KERN_ALERT "Invalid percentage set: %d\n percentage must be greater than 0\n", percentage);
		return -EINVAL;
	}
}

/*
 * "cat /dev/chardev"
 */
static int device_open(struct inode *inode, struct file *file)
{
	uint32_t minor = iminor(inode); 
	if(minor >= 0)
	{
		file->private_data = (void*)minor;
	}

    if (device_opened) 
    {
        return -EBUSY;
    }
    device_opened = true;

    return SUCCESS;
}

/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
    device_opened = false;
    return 0;
}

/*
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t device_read(struct file *filp, /* see include/linux/fs.h  */
               			   char *buffer,      /* buffer to fill with data */
                           size_t length,     /* length of the buffer  */
                           loff_t *offset)    /* offset is zero on first call */
{
    const uint32_t MaxSize = length < MAX_DEVFS_BUFFER ? length : MAX_DEVFS_BUFFER;
    char msg[MaxSize];
    uint32_t msgLength = 0;
    uint32_t bytesRemaining = 0;

    static int* vAddressPwm1 = io_p2v(PWM1_CTRL_ADDR);
    static int* vAddressPwm2 = io_p2v(PWM2_CTRL_ADDR);
    volatile uint32_t result = 0;
    uint32_t minor = 0;

	if(vAddressPwm1 == NULL || vAddressPwm2 == NULL)
	{
		printk(KERN_ALERT "Invalid address: %d.\n", msgLength);
    	return -EADDRNOTAVAIL;
	}

    if (*offset != 0) {
        /* as we write all data in one go, we have no more data */
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
    	case 0: /* read enable pwm1 */
    		result = (*vAddressPwm1 & PWM_ENABLE_MASK); 
    		result >>= PWM_ENABLE_SHIFT;
    		printk(KERN_INFO "PWM1 enable: %d\n", result);
    		break;
    	case 1: /* read frequency pwm1 */
 			result = (*vAddressPwm1 & PWM_FREQ_MASK);
 			result >>= PWM_FREQ_SHIFT;
 			result = RLD_to_freq(result); 
 			printk(KERN_INFO "PWM1 frequency: %d\n", result);
    		break;
    	case 2: /* read duty cycle pwm1 */
    		result = (*vAddressPwm1 & PWM_CYCLE_MASK); 
    		result >>= PWM_CYCLE_SHIFT;
    		result = duty_to_percentage(result);
    		printk(KERN_INFO "PWM1 duty cycle: %d\n", result);
    		break;
    	case 3: /* read enable pwm2 */
			result = (*vAddressPwm2 & PWM_ENABLE_MASK);
			result >>= PWM_ENABLE_SHIFT;
    		printk(KERN_INFO "PWM2 enable: %d\n", result);
    		break;
    	case 4: /* read frequency pwm2 */
 			result = (*vAddressPwm2 & PWM_FREQ_MASK);
 			result >>= PWM_FREQ_SHIFT;
 			result = RLD_to_freq(result); 
 			printk(KERN_INFO "PWM2 frequency: %d\n", result);
    		break;
    	case 5: /* read duty cycle pwm2 */
    		result = (*vAddressPwm2 & PWM_CYCLE_MASK);
    		result >>= PWM_CYCLE_SHIFT;
    		result = duty_to_percentage(result);
    		printk(KERN_INFO "PWM2 duty cycle: %d\n", result);    		
    		break;

    	default:
    		printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    		return -EINVAL;
    		break;
	}

    bytesRemaining = copy_to_user(buffer, msg, msgLength);

    *offset += msgLength - bytesRemaining;
    return msgLength - bytesRemaining;
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/chardev
 */
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	uint32_t data = 0;
	int preValData = 0;
	static int* vAddressPwm1 = io_p2v(PWM1_CTRL_ADDR);
    static int* vAddressPwm2 = io_p2v(PWM2_CTRL_ADDR);
	uint32_t minor = (uint32_t)filp->private_data;

	if(len > MAX_DEVFS_BUFFER)
	{
		return -EMSGSIZE;
	}

	if(minor < 0)
	{
    	printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    	return -EINVAL;
	}

	if(sscanf(buff, "%d", &preValData) <= 0)
	{
		printk(KERN_ALERT "Invalid format. Data in not a number\n");
		return -EILSEQ;
	}
	else
	{
		if(preValData < 0)
		{
			printk(KERN_ALERT "Invalid format. Data in not a real number\n");
			return -EINVAL;
		}
		else
		{
			data = preValData;
		}
	}

	switch(minor)
    {
    	case 0: /* write enable pwm1 */
    		if(data < 0 || data > 1)
    		{
    			printk(KERN_ALERT "Enable [0-1] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}

    		*vAddressPwm1 = (data << PWM_ENABLE_SHIFT) ^ (~PWM_ENABLE_MASK & *vAddressPwm1); 
    		printk(KERN_INFO "PWM1 enable set to: 0x%x\n", *vAddressPwm1 & PWM_ENABLE_MASK);
    		break;

    	case 1: /* write frequency pwm1 */
    		if(data < 1 || data > 128)
    		{
    			printk(KERN_ALERT "Frequency [1-128] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}
    		*vAddressPwm1 = (freq_to_RLD(data) << PWM_FREQ_SHIFT) ^ (~PWM_FREQ_MASK & *vAddressPwm1);
    		printk(KERN_INFO "PWM1 FREQ set to: 0x%x\n", *vAddressPwm1 & PWM_FREQ_MASK); 
    		break;

    	case 2: /* write duty cycle pwm1 */
    		if(data < 1 || data > 100)
    		{
    			printk(KERN_ALERT "Duty cycle [1-100] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}
    		*vAddressPwm1 = (percentage_to_duty(data) << PWM_CYCLE_SHIFT) ^ (~PWM_CYCLE_MASK & *vAddressPwm1); 
    		printk(KERN_INFO "PWM1 DUTY set to: 0x%x\n", *vAddressPwm1 & PWM_CYCLE_MASK); 
    		break;

    	case 3: /* write enable pwm2 */
    		if(data < 0 || data > 1)
    		{
    			printk(KERN_ALERT "Enable [0-1] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}
    		*vAddressPwm2 = (data << PWM_ENABLE_SHIFT) ^ (~PWM_ENABLE_MASK & *vAddressPwm2);
    		printk(KERN_INFO "PWM2 enable set to: 0x%x\n", *vAddressPwm2 & PWM_ENABLE_MASK);    		 			
    		break;

    	case 4: /* write frequency pwm2 */
    		if(data < 1 || data > 128)
    		{
    			printk(KERN_ALERT "Frequency [1-128] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}
    		*vAddressPwm2 = (freq_to_RLD(data) << PWM_FREQ_SHIFT) ^ (~PWM_FREQ_MASK & *vAddressPwm2); 
    		printk(KERN_INFO "PWM2 FREQ set to: 0x%x\n", *vAddressPwm2 & PWM_FREQ_MASK);    		 			 			
    		break;

    	case 5: /* write duty cycle pwm2 */
    		if(data < 1 || data > 100)
    		{
    			printk(KERN_ALERT "Duty cycle [1-100] allowed\nGiven value is: %d\n", data);
    			return -EINVAL;
    		}
    		*vAddressPwm2 = (percentage_to_duty(data) << PWM_CYCLE_SHIFT) ^ (~PWM_CYCLE_MASK & *vAddressPwm2);  
    		printk(KERN_INFO "PWM2 DUTY set to: 0x%x\n", *vAddressPwm2 & PWM_CYCLE_SHIFT);    		  		
    		break;

    	default:
    		printk(KERN_ALERT "No minor assigned: %d.\n", minor);
    		return -EINVAL;
    		break;
	}
	printk(KERN_INFO "Register PWM1: 0x%x\n", *vAddressPwm1);
	printk(KERN_INFO "Register PWM2: 0x%x\n", *vAddressPwm2);
	return (int)len;
}

static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
	*(unsigned int*)(io_p2v(LCD_CTRL)) = 0;
	*(unsigned int*)(io_p2v(PWM2_CTRL_ADDR)) &= PWM2_INT_MASK;
    *(unsigned int*)(io_p2v(PWM_CLOCK_CTRL)) = PWM_CLOCK_MASK;

    major_number = register_chrdev(0, DEVICE_NAME, &fops);

    if (major_number < 0) {
        printk(KERN_ALERT "Registering char device failed with %d\n", major_number);
        return major_number;
    }

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major_number);
    printk(KERN_INFO "the driver, create a dev file with\n");
    printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, major_number);
    printk(KERN_INFO "Try various minor numbers. Try to cat and echo to\n");
    printk(KERN_INFO "the device file.\n");
    printk(KERN_INFO "Remove the device file and module when done.\n");

    return SUCCESS;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
    unregister_chrdev(major_number, DEVICE_NAME);
}