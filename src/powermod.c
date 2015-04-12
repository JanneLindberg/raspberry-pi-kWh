/*
  Copyright (C) 2015 JanneLindberg

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
  MA 02110-1301 USA.
*/

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/moduleparam.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>

#include <linux/sched.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>

#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Janne.Lindberg");
MODULE_DESCRIPTION("Raspberry powermeter module");

//#define DEBUG


#define DEFAULT_GPIO_PIN 23

static int gpio_pin = DEFAULT_GPIO_PIN;

static int gpio_irq_number;

static int kwh = 0;
static int kwh_pulse_counter;

// Is this really used ??
static int irq_counter = 0;

static int pulses;
static int watt;

static char* device_id = "PWR";

/*
 * Keep a count when we are missing pulses when there was expected to "be some".
 * typically there should be at least some pulses from the sensor within each
 * intervall, if it's totally silent for the perdiod, this could indicate some
 * hardware and/or cabling problem.
 */
static int pulse_fail_cnt = 0;
#define MISSING_PULSES_THRESHOLD 10

/*
 * Default period to do a measurement is 30 seconds
 */
static int measurement_period = 30;

static const int watt_seconds = 3600000;

/*
 * The number of pulses for one KWh
 */
static int pulses_for_kwh = 10000;


module_param(measurement_period, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(measurement_period, "intervall in seconds, default 30 second");

module_param(pulses_for_kwh, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(pulses_for_kwh, "Pulses for one kilowatt hour, default value 10000");

module_param(gpio_pin, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(gpio_pin, "gpio pin number");

module_param(device_id, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(device_id, "Device id");


static struct timer_list kwh_mod_timer;

/*
 * IRQ service.
 * increments the pulse counter and when above the limit
 * the actual kWh counter is incremented.
 */
static irqreturn_t gpio_rising_interrupt(int irq, void* dev_id) {
    irq_counter++;

    kwh_pulse_counter++;
    if(kwh_pulse_counter > pulses_for_kwh) {
        kwh++;
        kwh_pulse_counter = 0;
    }

    return(IRQ_HANDLED);
}


static int module_init_irq(void) {
    irq_counter = 0;

    gpio_irq_number = gpio_to_irq(gpio_pin);

    if ( request_irq(gpio_irq_number, gpio_rising_interrupt, IRQF_TRIGGER_RISING|IRQF_ONESHOT, "gpio_rising", NULL) ) {
        printk(KERN_ERR "Failed to acquired IRQ %d", gpio_irq_number);
        return(-EIO);
    }
#ifdef DEBUG
    else {
        printk(KERN_ERR "Acquired IRQ %d for gpio_pin %d\n", gpio_irq_number, gpio_pin);
    }
#endif

    return 0;
}

static void module_remove_irq(void) {
    free_irq(gpio_irq_number, NULL);
#ifdef DEBUG
    printk ("gpio remove irq\n");
#endif
    return;
}


int time_intervall(void) {
    return measurement_period * 1000;
}


static int calculate_power_from_pulses(int pulses) {
    if(pulses > 0) {
        int watt = (watt_seconds / measurement_period) / (pulses_for_kwh / pulses);
        return watt;
    } else {
        return 0;
    }
}


static void kwh_mod_timer_callback( unsigned long data )
{
    mod_timer( &kwh_mod_timer, jiffies + msecs_to_jiffies(time_intervall()) );

    pulses = irq_counter;
    irq_counter = 0;

    if(pulses > 0) {
        watt = calculate_power_from_pulses(pulses);
        pulse_fail_cnt = 0;
    } else {
        pulse_fail_cnt++;
    }
}


static int mod_init_timer( void )
{
    int ret;

    printk("Timer module installing\n");
    setup_timer( &kwh_mod_timer, kwh_mod_timer_callback, 0 );

#ifdef DEBUG
    printk( "Starting timer to fire in %dms (%ld)\n", time_intervall(), jiffies );
#endif

    ret = mod_timer( &kwh_mod_timer, jiffies + msecs_to_jiffies(time_intervall()) );
    if (ret) {
        printk("Error in mod_timer\n");
    }

    return 0;
}


static void remove_timer(void)
{
    int ret = del_timer_sync( &kwh_mod_timer );
    if (ret) {
        printk("Could not delete the timer. status:%d\n", ret);
    } else {
        printk("Timer removed\n");
    }
    return;
}



static ssize_t kilo_watt_hour(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t c = PAGE_SIZE;

    c -= snprintf(buf + PAGE_SIZE - c, c, "%d.%02d", kwh, kwh_pulse_counter);
    return PAGE_SIZE - c;
}

static struct kobj_attribute kwh_pulse_attribute = __ATTR(kwh_pulse, 0444, kilo_watt_hour, NULL);



static ssize_t watt_second(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t c = PAGE_SIZE;
    c -= snprintf(buf + PAGE_SIZE - c, c, "%d", watt);
    return PAGE_SIZE - c;
}

static struct kobj_attribute watt_attribute = __ATTR(watt, 0444, watt_second, NULL);


static ssize_t period_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", measurement_period);
}

static ssize_t period_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    sscanf(buf, "%du", &measurement_period);
    printk( "set measurement_period = %d\n", measurement_period );

    return count;
}

static struct kobj_attribute period_attribute = __ATTR(measurement_period, 0666, period_show, period_store);


/*
 * Get and Set KWH value.
 * A typical use for this is to preset the internal kilo watt hour (KWh) counter with the value
 * from the actual power meter unit.
 */
static ssize_t get_kilo_watt_hour(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t size = PAGE_SIZE;
    size -= snprintf(buf + PAGE_SIZE - size, size, "%d", kwh);
    return PAGE_SIZE - size;
}

static ssize_t set_kilo_watt_hour(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    sscanf(buf, "%du", &kwh);
    printk( "set kwh  = %d\n", kwh );
    return count;
}

static struct kobj_attribute set_get_kilo_watt_hour = __ATTR(kwh, 0666, get_kilo_watt_hour, set_kilo_watt_hour);



static int pulse_fail(void) {
    return pulse_fail_cnt > MISSING_PULSES_THRESHOLD;
}


/*
 * Return essential data in json format
 */
static ssize_t status_json(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct timespec ts;
    ssize_t i = PAGE_SIZE;

    getnstimeofday(&ts);

    i -= snprintf(buf + PAGE_SIZE - i, i, "{\"ws\":\"%d\",\"kwh\":\"%d.%02d\",\"fail\":\"%d\"}",
                  watt, kwh, kwh_pulse_counter, pulse_fail());

    return PAGE_SIZE - i;
}

static struct kobj_attribute json_status_attribute = __ATTR(json, 0444, status_json, NULL);


/*
 * Return data in json format.
 * timestamp in milliseconds since epoc
 */
static ssize_t status_ts_json(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t i = PAGE_SIZE;
    struct timespec ts;

    getnstimeofday(&ts);

    i -= snprintf(buf + PAGE_SIZE - i, i, "[{\"timestamp\":\"%u%03u\",\"dev-id\":\"%s\",",
                  (unsigned int)ts.tv_sec,
                  ((unsigned int)ts.tv_nsec / 1000000L),
                  &device_id[0]);
    
    i -= snprintf(buf + PAGE_SIZE - i, i, "\"watt\":\"%d\",\"kwh\":\"%d.%02d\",", watt, kwh, kwh_pulse_counter);
    i -= snprintf(buf + PAGE_SIZE - i, i, "\"fail\":\"%d\"}]", pulse_fail());
          
    return PAGE_SIZE - i;
}

static struct kobj_attribute ts_json_status_attribute = __ATTR(json_ev, 0444, status_ts_json, NULL);


/*
 * Create a group of attributes so that we can create and destroy them all at once.
 */
static struct kobj_attribute *attrs[] = {

    &set_get_kilo_watt_hour,

    &kwh_pulse_attribute,
    &watt_attribute,

    &json_status_attribute,
    &ts_json_status_attribute,

    &period_attribute,

    NULL,   /* NULL terminates the attribute list */
};


static struct kobject *power_mod_kobj;

static struct attribute_group attr_group = {
    .attrs = attrs,
};


static int __init power_mod_init(void)
{
    int retval;

    power_mod_kobj = kobject_create_and_add("power-mod", kernel_kobj);
    if (!power_mod_kobj) {
        return -ENOMEM;
    }

    retval = sysfs_create_group(power_mod_kobj, &attr_group);
    if (retval) {
        kobject_put(power_mod_kobj);
    } else {
        mod_init_timer();
        module_init_irq();
        printk ("loaded: device-id=\"%s\", gpio_pin=%d\n", device_id, gpio_pin );
    }

    return retval;
}


static void __exit power_mod_exit(void)
{
    kobject_put(power_mod_kobj);

    module_remove_irq();
    remove_timer();
}

module_init(power_mod_init);
module_exit(power_mod_exit);
