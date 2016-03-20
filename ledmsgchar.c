/**
 * @file   ledmsgchar.c
 * @author David Good
 * @date   18 March 2016
 * @version 0.1
 * @brief   A character driver for a multiplexed LED message board.
 * This module maps to /dev/ledmsgchar.
 * Code originally based on examples by Derek Molloy.
 * @see http://www.derekmolloy.ie/ for great LKM examples.
 */

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/moduleparam.h>
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/gpio.h>           // Required for the GPIO functions
#include <linux/kobject.h>        // Using kobjects for the sysfs bindings
#include <asm/uaccess.h>          // Required for the copy to user function
#include <linux/kthread.h>        // Using kthreads for row scanning
#include <linux/delay.h>          // Needed for msleep() function
#include <linux/types.h>          // Required for u8 type

#define  DEVICE_NAME "ledmsgchar" ///< The device will appear at /dev/ledmsgchar using this value
#define  CLASS_NAME  "ledmsg"     ///< The device class -- this is a character device driver

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("David Good");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Multiplexed LED display driver");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users

#define LOG_ALERT(M, ...) printk(KERN_ALERT "LEDMSGCHAR: " M "\n", ##__VA_ARGS__)
#define LOG_INFO(M, ...)  printk(KERN_INFO  "LEDMSGCHAR: " M "\n", ##__VA_ARGS__)
#define CHECK(A, M, ...) if (!(A)) { LOG_ALERT(M, ##__VA_ARGS__); goto error; }

/* Character device related variables */
static int    majorNumber;                  ///< Stores the device number -- determined automatically
static char   message[256] = {0};           ///< Memory for the string that is passed from userspace
static short  size_of_message;              ///< Used to remember the size of the string stored
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class*  ledmsgcharClass  = NULL; ///< The device-driver class struct pointer
static struct device* ledmsgcharDevice = NULL; ///< The device-driver device struct pointer

// The prototype functions for the character driver -- must come before the struct definition
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

/* GPIO related vars */
/* BeagleBone GPIO numbers are calculated by (portNum * 32) + portPosition
 *   Example: GPIO1_30 = (1 * 32) + 30 = 62 */
static unsigned int gpioA0 = 62;        ///< Row select bus A0 (LSB) (GPIO1_30)
static unsigned int gpioA1 = 36;        ///< Row select bus A1       (GPIO1_4)
static unsigned int gpioA2 = 32;        ///< Row select bus A2 (MSB) (GPIO1_0)
static unsigned int gpioCLK = 48;       ///< Clock signal            (GPIO1_16)
static unsigned int gpioD0  = 49;       ///< Data signal             (GPIO1_17)
static unsigned int gpioSTB = 115;      ///< Data latch signal       (GPIO3_19)
static unsigned int gpioBLK = 117;      ///< Pin to blank the sign, active high, use PWM for dimming
static bool blank = 0;                  ///< Blank status of the sign, 1 for blank, 0 for not blank
/* module_param(blank, bool, S_IRUGO);     ///< Param desc. S_IRUGO can be read/not changed */
/* MODULE_PARAM_DESC(blank, " Blanks the sign if 1, un-blanks if 0"); */
static unsigned int row = 0;            ///< Current row being scanned
static unsigned int rowTimeMs = 2000;   ///< Display time for each row in ms
static struct task_struct *task;        /// The pointer to the thread task
#define NUM_ROWS      8
#define NUM_ROW_BYTES 18

#define INIT_BUFFER_PATTERN {                                           \
        {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  \
        {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          \
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}   \
    }

static u8 buf[NUM_ROWS][NUM_ROW_BYTES] = INIT_BUFFER_PATTERN;
static u8 userBuf[NUM_ROWS][NUM_ROW_BYTES];
static bool sUserBufReady = 0;

#define INIT_GPIO(A) if (!gpio_is_valid((A))) {                 \
        printk(KERN_INFO "LEDMSGCHAR: invalid GPIO " #A "\n");  \
        result = -ENODEV;                                       \
        goto error;                                             \
    } else {                                                    \
        gpio_request((A), "sysfs");                             \
        gpio_direction_output((A), 0);                          \
        gpio_export((A), false);                                \
    }

#define CLOSE_GPIO(A) {                                         \
        gpio_unexport((A));                                     \
        gpio_free(gpioBLK);                                     \
    }

/** @brief Internal: Writes row data to data chips
 *  Data is written Lowest byte first, Highest bit first so that the
 *  buffer in memory reads left to right just like the sign.
 *
 *  @param rowData A pointer to a byte buffer to be written out
 *  @param numBytes Number of bytes to write out
 */
static void write_row_data(u8 *rowData, unsigned int numBytes) {
    unsigned char mask;
    unsigned char b;
    while (numBytes > 0) {
        b = *rowData;
        ++rowData;
        for (mask = 0x80; mask != 0; mask >>= 1) {
            gpio_set_value(gpioD0, (b & mask) ? 1 : 0);
            gpio_set_value(gpioCLK, 1);
            // delay some time to respect minimum clock pulse width
            gpio_set_value(gpioCLK, 0);
        }
        --numBytes;
    }
}

/** @brief Periodic row update kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int update_row(void *arg) {
    LOG_INFO("Update row thread has started running");
    // printk(KERN_INFO "LEDMSGCHAR: Row update thread has started running \n");
    while (!kthread_should_stop()) {          // Returns true when kthread_stop() is called
        set_current_state(TASK_RUNNING);

        if (sUserBufReady) {
            memcpy(buf, userBuf, sizeof buf);
            sUserBufReady = 0;
        }

        // Increment to next row number and write out row data
        (row < 7) ? ++row : (row = 0);
        write_row_data(buf[row], NUM_ROW_BYTES);

        // Execute row change and latch data
        gpio_set_value(gpioBLK, 1); // Blank the display while we change rows
        // usleep(10);
        (row & 1) ? gpio_set_value(gpioA0, 1) : gpio_set_value(gpioA0, 0);
        (row & 2) ? gpio_set_value(gpioA1, 1) : gpio_set_value(gpioA1, 0);
        (row & 4) ? gpio_set_value(gpioA2, 1) : gpio_set_value(gpioA2, 0);
        // Do we need to wait here for output drivers to fully turn off?
        gpio_set_value(gpioSTB, 1);
        // udelay(??); don't violate minimum pulse width time
        gpio_set_value(gpioSTB, 0);
        // Do we need to wait here for driver outputs to settle?
        gpio_set_value(gpioBLK, 0); // Un-blank the display

        set_current_state(TASK_INTERRUPTIBLE);
        usleep_range(rowTimeMs, rowTimeMs);
    }
    // printk(KERN_INFO "EBB LED: Thread has run to completion \n");
    LOG_INFO("Thread has run to completion");
    return 0;
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point.
 *  @return returns 0 if successful
 */
static int __init ledmsgchar_init(void) {
    int result = 0;

    printk(KERN_INFO "LEDMSGCHAR: Initializing the LEDMSGCHAR LKM\n");

    // Try to dynamically allocate a major number for the device -- more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber<0){
        printk(KERN_ALERT "LEDMSGCHAR: failed to register a major number\n");
        return majorNumber;
    }
    printk(KERN_INFO "LEDMSGCHAR: registered correctly with major number %d\n", majorNumber);

    // Register the device class
    ledmsgcharClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(ledmsgcharClass)){               // Check for error and clean up if there is
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "LEDMSGCHAR: Failed to register device class\n");
        return PTR_ERR(ledmsgcharClass);        // Correct way to return an error on a pointer
    }
    printk(KERN_INFO "LEDMSGCHAR: device class registered correctly\n");

    // Register the device driver
    ledmsgcharDevice = device_create(ledmsgcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(ledmsgcharDevice)){              // Clean up if there is an error
        class_destroy(ledmsgcharClass);         // Repeated code but the alternative is goto statements
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(ledmsgcharDevice);
    }
    printk(KERN_INFO "LEDMSGCHAR: device class created correctly\n"); // Made it! device was initialized

    /* Get a hold of the GPIOs */
    // Is the GPIO a valid GPIO number (e.g., not all gpio are available)
    INIT_GPIO(gpioA0);
    INIT_GPIO(gpioA1);
    INIT_GPIO(gpioA2);
    INIT_GPIO(gpioCLK);
    INIT_GPIO(gpioD0);
    INIT_GPIO(gpioSTB);
    INIT_GPIO(gpioBLK);

    blank = 0;
    printk(KERN_INFO "LEDMSGCHAR: Blank state is %d\n", gpio_get_value(gpioBLK));

    sUserBufReady = 0;

    task = kthread_run(update_row, NULL, "ledmsgchar_update_row_thread");
    if (IS_ERR(task)) {
        printk(KERN_ALERT "LEDMSGCHAR: failed to create row update task");
        result = PTR_ERR(task);
        goto error;
    }

    return 0;

error:
    class_destroy(ledmsgcharClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit ledmsgchar_exit(void) {
    kthread_stop(task);

    CLOSE_GPIO(gpioA0);
    CLOSE_GPIO(gpioA1);
    CLOSE_GPIO(gpioA2);
    CLOSE_GPIO(gpioCLK);
    CLOSE_GPIO(gpioD0);
    CLOSE_GPIO(gpioSTB);
    CLOSE_GPIO(gpioBLK);

    device_destroy(ledmsgcharClass, MKDEV(majorNumber, 0)); // remove the device
    class_unregister(ledmsgcharClass);      // unregister the device class
    class_destroy(ledmsgcharClass);         // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    printk(KERN_INFO "LEDMSGCHAR: Goodbye from the LKM!\n");
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep){
   numberOpens++;
   printk(KERN_INFO "LEDMSGCHAR: Device has been opened %d time(s)\n", numberOpens);
   return 0;
}

/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user. In this case is uses the copy_to_user() function to
 *  send the buffer string to the user and captures any errors.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the b
 *  @param offset The offset if required
 */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int error_count = 0;
    // copy_to_user has the format ( *to, *from, size) and returns 0 on success
    error_count = copy_to_user(buffer, message, size_of_message);

    if (error_count == 0) {          // if true then have success
        printk(KERN_INFO "LEDMSGCHAR: Sent %d characters to the user\n", size_of_message);
        return (size_of_message=0);  // clear the position to the start and return 0
    }
    else {
        printk(KERN_INFO "LEDMSGCHAR: Failed to send %d characters to the user\n", error_count);
        return -EFAULT;              // Failed -- return a bad address message (i.e. -14)
    }
}

/** @brief Converts two ASCII characters representing a hex byte into a byte value
 *  Speed was chosen over correctness, so characters are not checked if they are
 *  valid hex.  If this is important, do the check before calling this function.
 *  @param val A pointer to two ASCII bytes. Does not need to be null terminated.
 *  @return The converted byte value
 */
static u8 ascii2byte(const char *val) {
    u8 result = 0;
    u8 index;

    // mapping of ASCII characters to hex values
    const u8 hexLookup[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // 01234567
        0x08, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 89:;<=>?
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, // @ABCDEFG
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // HIJKLMNO
    };
    // This bit manipulation takes advantage of the layout of the ascii table.
    // It works for both lower and upper case letters.  It does have the quirk
    // where some invalid hex chars will produce a result.
    index = (u8) ((*val & 0x1F) ^ 0x10);
    result = hexLookup[index] << 4;
    index = (u8) ((*(val + 1) & 0x1F) ^ 0x10);
    result |= hexLookup[index];

    return result;
}

/** @brief This function is called whenever the device is being written to from
 *  user space i.e. data is sent to the device from the user. The data is copied
 *  to the userBuf[] and the update_row task is notified that a new buffer is
 *  ready.
 *
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 *  @return The number of characters consumed by the write operation.
 */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    const char *pchar;
    unsigned int row, index;

    if (len < (NUM_ROWS * NUM_ROW_BYTES * 2)) {
        LOG_INFO("Did not receive enough bytes to fill buffer (%d of %d) ", len, NUM_ROWS * NUM_ROW_BYTES * 2);
        return -EINVAL;
    }

    // Wait for task to finish with userBuf
    while (sUserBufReady) {
        LOG_ALERT("Write request came before last write was consumed. Waiting for task...");
        msleep(2);              // This value was arbitrarily chosen.
    }

    pchar = buffer;
    for (row = 0; row < NUM_ROWS; ++row) {
        for (index = 0; index < NUM_ROW_BYTES; ++index) {
            userBuf[row][index] = ascii2byte(pchar);
            LOG_INFO("row %d, index %d = %x", row, index, userBuf[row][index]);
            pchar += 2;
        }
    }

    sUserBufReady = 1;
    LOG_INFO("Consumed %d bytes from user", len);
    return len;
}


/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "LEDMSGCHAR: Device successfully closed\n");
   return 0;
}

/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(ledmsgchar_init);
module_exit(ledmsgchar_exit);
