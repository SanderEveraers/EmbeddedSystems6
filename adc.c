#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/irqs.h>
#include <asm/uaccess.h>
#include <linux/wait.h>

#define DEVICE_NAME 		"adc"
#define ADC_NUMCHANNELS		3

// adc registers
#define	ADCLK_CTRL			io_p2v(0x400040B4)
#define	ADCLK_CTRL1			io_p2v(0x40004060)
#define	ADC_SELECT			io_p2v(0x40048004)
#define	ADC_CTRL			io_p2v(0x40048008)
#define ADC_VALUE           io_p2v(0x40048048)
#define SIC2_ATR            io_p2v(0x40010010)
#define SIC1_ER             io_p2v(0x4000c000)

#define GPI01_BIT           _BIT(23)
#define ADC_BIT             0x80

#define READ_REG(reg)       (*(volatile unsigned int *)(reg))
#define WRITE_REG(data,reg) (*(volatile unsigned int *)(reg) = (data))

#define AD_STROBE           0x2
#define AD_PDN_CTRL         0x4
#define ADC_VALUE_MASK      0x3FF

//#define TIMING_DEBUG
#ifdef TIMING_DEBUG
#define OUTPUT_REG_SET      io_p2v(0x40028020)
#define OUTPUT_REG_CLR      io_p2v(0x40028024)
#define GPI_INT_OUT         0x400
#define ADC_OUT             0x1
#endif

static unsigned int         adc_channel = 0;
static int                  adc_values[ADC_NUMCHANNELS] = {0, 0, 0};


static irqreturn_t          adc_interrupt (int irq, void * dev_id);
static irqreturn_t          gp_interrupt (int irq, void * dev_id);
static wait_queue_head_t    adc_wait_queue;

bool   adcWaitFlag =        false;
bool   gpiWaitFlag =        false;
bool   devReadWaitFlag =    false;

/*----------------------------ADC region-------------------------------------*/

static void adc_init (void)
{
	unsigned long data;

	// set 32 KHz RTC clock
    data = READ_REG (ADCLK_CTRL);
    data |= 0x1;
    WRITE_REG (data, ADCLK_CTRL);

	// rtc clock ADC & Display = from PERIPH_CLK
    data = READ_REG (ADCLK_CTRL1);
    data &= ~0x01ff;
    WRITE_REG (data, ADCLK_CTRL1);

	// negative & positive reference
    data = READ_REG(ADC_SELECT);
    data &= ~0x03c0;
    data |=  0x0280;
    WRITE_REG (data, ADC_SELECT);

	// switch on adc and reset
    data = READ_REG (ADC_CTRL);
    data |= AD_PDN_CTRL;
    WRITE_REG (data, ADC_CTRL);

    //TODO: adc interrupt on (SIC1_ER)
    data = READ_REG(SIC1_ER);
    data |= ADC_BIT;
    WRITE_REG(data, SIC1_ER);

    //TODO: gpi01 to edge interrupt (SIC2_ATR)
    data = READ_REG(SIC2_ATR);
    data |= (GPI01_BIT);  //edge
    //data &= ~(1<<GPI01_BIT); //level
    WRITE_REG(data, SIC2_ATR);

	//IRQ init
    if (request_irq (IRQ_LPC32XX_TS_IRQ, adc_interrupt, IRQF_DISABLED, "", NULL) != 0)
    {
        printk(KERN_ALERT "ADC IRQ request failed\n");
    }
    if (request_irq (IRQ_LPC32XX_GPI_01, gp_interrupt, IRQF_DISABLED, "", NULL) != 0)
    {
        printk (KERN_ALERT "GP IRQ request failed\n");
    }


    init_waitqueue_head(&adc_wait_queue);
}


static void adc_start (unsigned int channel)
{
	unsigned long data;

	if (channel >= ADC_NUMCHANNELS)
    {
        printk(KERN_WARNING "selected channel exceeds out of range %d\n",channel);
        channel = 0;
    }
#ifdef TIMING_DEBUG
    WRITE_REG(ADC_OUT, OUTPUT_REG_SET);
#endif
	data = READ_REG (ADC_SELECT);
    // select the channel, first read it, ignore channel bits and change only the channel bits (0x0030)
	WRITE_REG((data & ~0x0030) | ((channel << 4) & 0x0030), ADC_SELECT);

    // remember the channel globally so we can recognise it later on
	adc_channel = channel;

	// start conversion
    data = READ_REG (ADC_CTRL);
    data |= AD_STROBE;
    WRITE_REG (data, ADC_CTRL);
}

/*----------------------------Interrupt region-------------------------------*/

static irqreturn_t adc_interrupt (int irq, void * dev_id)
{    
#ifdef TIMING_DEBUG    
    WRITE_REG(ADC_OUT, OUTPUT_REG_CLR);
#endif
    adc_values[adc_channel] = READ_REG(ADC_VALUE) & ADC_VALUE_MASK;
    printk(KERN_INFO "ADC(%d)=%d\n", adc_channel, adc_values[adc_channel]);

    if(gpiWaitFlag)
    {
        // start the next channel:
        adc_channel++;
        if (adc_channel < ADC_NUMCHANNELS)
        {
            adcWaitFlag = false;
            adc_start (adc_channel);
        }
        else
        {
            gpiWaitFlag = false;
        }
    }
    else
    {
        adcWaitFlag = true;
        wake_up_interruptible(&adc_wait_queue); //continue program
    }

    return (IRQ_HANDLED);
}

static irqreturn_t gp_interrupt(int irq, void * dev_id)
{
#ifdef TIMING_DEBUG
    WRITE_REG(GPI_INT_OUT, OUTPUT_REG_SET); //between press and interrupt
#endif
    if(!gpiWaitFlag && !devReadWaitFlag)
    {
        gpiWaitFlag = true;
        adc_start(0);
    }
#ifdef TIMING_DEBUG
    WRITE_REG(GPI_INT_OUT, OUTPUT_REG_CLR);
#endif
    return (IRQ_HANDLED);
}

/*----------------------------Module region----------------------------------*/

static void adc_exit (void)
{
    free_irq (IRQ_LPC32XX_TS_IRQ, NULL);
    free_irq (IRQ_LPC32XX_GPI_01, NULL);
}


static ssize_t device_read (struct file * file, char __user * buf, size_t length, loff_t * f_pos)
{
	int     channel = (int) file->private_data;
    char    return_buf[128];
    int     bytes_read = 0;
    int     bytes_remaining = 0;

    if (*f_pos != 0)
    {
        return 0;
    }

    if (channel < 0 || channel >= ADC_NUMCHANNELS)
    {
		return -EFAULT;
    }

    adcWaitFlag = false;
    if (!gpiWaitFlag && !devReadWaitFlag)
    {
        devReadWaitFlag = true;
        adc_start (channel);
        wait_event_interruptible(adc_wait_queue, adcWaitFlag);
        devReadWaitFlag = false;
    } else
    {
        return -EBUSY;
    }
    
    // read adc and copy it into 'buf'
    bytes_read = snprintf(return_buf, ADC_VALUE_MASK, "%d", adc_values[adc_channel]);
    bytes_remaining = copy_to_user(buf, return_buf, bytes_read);
    *f_pos += bytes_read - bytes_remaining;
    return (bytes_read - bytes_remaining);
}




static int device_open (struct inode * inode, struct file * file)
{
    uint32_t minor = iminor(inode);
    if (minor >= 0)
    {
        file->private_data = (void*)minor;
    }

    try_module_get(THIS_MODULE);
    return 0;
}


static int device_release (struct inode * inode, struct file * file)
{
    module_put(THIS_MODULE);
	return 0;
}


static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = device_read,
    .open = device_open,
    .release = device_release
};


static struct chardev
{
    dev_t       dev;
    struct cdev cdev;
} adcdev;


int adcdev_init (void)
{
    // try to get a dynamically allocated major number
	int error = alloc_chrdev_region(&adcdev.dev, 0, ADC_NUMCHANNELS, DEVICE_NAME);;

	if(error < 0)
	{
		// failed to get major number for our device.
		printk(KERN_WARNING DEVICE_NAME ": dynamic allocation of major number failed, error=%d\n", error);
		return error;
	}

	printk(KERN_INFO DEVICE_NAME ": major number=%d\n", MAJOR(adcdev.dev));

	cdev_init(&adcdev.cdev, &fops);
	adcdev.cdev.owner = THIS_MODULE;
	adcdev.cdev.ops = &fops;

	error = cdev_add(&adcdev.cdev, adcdev.dev, ADC_NUMCHANNELS);
	if(error < 0)
	{
		// failed to add our character device to the system
		printk(KERN_WARNING DEVICE_NAME ": unable to add device, error=%d\n", error);
		return error;
	}

	adc_init();

	return 0;
}


/*
 * Cleanup - unregister the appropriate file from /dev
 */
void cleanup_module()
{
	cdev_del(&adcdev.cdev);
	unregister_chrdev_region(adcdev.dev, ADC_NUMCHANNELS);

	adc_exit();
}


module_init(adcdev_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yamil Baptista, Sander Everaers, Bart Kolvoort");
MODULE_DESCRIPTION("ADC Driver");

