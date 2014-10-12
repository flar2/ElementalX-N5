/*
 * Screen wake timeout
 * Copyright (C) 2014 flar2 <asegaert@gmail.com>
 * 
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/lcd_notify.h>
#include <linux/android_alarm.h>
#include <linux/qpnp/power-on.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/init.h>

#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

#define WAKE_TIMEOUT_MAJOR_VERSION	1
#define WAKE_TIMEOUT_MINOR_VERSION	0
#define WAKEFUNC "wakefunc"
#define PWRKEY_DUR		60

static struct input_dev * wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
struct notifier_block wfnotif;
static long long wake_timeout = 0;
static struct alarm wakefunc_rtc;
static bool wakefunc_triggered = false;


static void wake_presspwr(struct work_struct * wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
                return;
	input_event(wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(wake_pwrdev, EV_SYN, 0, 0);
	msleep(PWRKEY_DUR);
	input_event(wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(wake_pwrdev, EV_SYN, 0, 0);

	msleep(PWRKEY_DUR * 6);
	wakefunc_triggered = true;
	pwrkey_pressed = true;

	input_event(wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(wake_pwrdev, EV_SYN, 0, 0);
	msleep(PWRKEY_DUR);
	input_event(wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(wake_pwrdev, EV_SYN, 0, 0);
	msleep(PWRKEY_DUR);
        mutex_unlock(&pwrkeyworklock);
	
	return;
}
static DECLARE_WORK(wake_presspwr_work, wake_presspwr);

static void wake_pwrtrigger(void) {
	schedule_work(&wake_presspwr_work);
        return;
}

static void wakefunc_rtc_start(void)
{
	ktime_t wakeup_time;
	ktime_t curr_time;

	wakefunc_triggered = false;
	curr_time = alarm_get_elapsed_realtime();
	wakeup_time = ktime_add_us(curr_time,
			(wake_timeout * 1000LL * 60000LL));
	alarm_start_range(&wakefunc_rtc, wakeup_time,
			wakeup_time);
	pr_info("%s: Current Time: %ld, Alarm set to: %ld\n",
			WAKEFUNC,
			ktime_to_timeval(curr_time).tv_sec,
			ktime_to_timeval(wakeup_time).tv_sec);
		
	pr_info("%s: Timeout started for %llu minutes\n", WAKEFUNC,
			wake_timeout);
}

static void wakefunc_rtc_cancel(void)
{
	int ret;

	wakefunc_triggered = false;
	ret = alarm_cancel(&wakefunc_rtc);
	if (ret)
		pr_info("%s: Timeout canceled\n", WAKEFUNC);
	else
		pr_info("%s: Nothing to cancel\n",
				WAKEFUNC);
}


static void wakefunc_rtc_callback(struct alarm *al)
{
	struct timeval ts;
	ts = ktime_to_timeval(alarm_get_elapsed_realtime());

	wake_pwrtrigger();
	
	pr_debug("%s: Time of alarm expiry: %ld\n", WAKEFUNC,
			ts.tv_sec);
}


//sysfs
static ssize_t show_wake_timeout(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%lld\n", wake_timeout);
}

static ssize_t store_wake_timeout(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long long input;
	int ret;

	ret = sscanf(buf, "%llu", &input);

	if (ret != 1) {
		return -EINVAL;
	}

	wake_timeout = input;

	return count;
}

static DEVICE_ATTR(wake_timeout, (S_IWUSR|S_IRUGO),
	show_wake_timeout, store_wake_timeout);


#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif


static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_START:
		wakefunc_rtc_cancel();
		break;
	case LCD_EVENT_ON_END:
		break;
	case LCD_EVENT_OFF_START:
		if (pwrkey_pressed == false && wakefunc_triggered == false && wake_timeout > 0) {
			wakefunc_rtc_start();
		}
		break;
	case LCD_EVENT_OFF_END:
		break;
	default:
		break;
	}

	return 0;
}


static int __init wake_timeout_init(void)
{
	int rc;

	pr_info("wake_timeout version %d.%d\n",
		 WAKE_TIMEOUT_MAJOR_VERSION,
		 WAKE_TIMEOUT_MINOR_VERSION);

	alarm_init(&wakefunc_rtc, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			wakefunc_rtc_callback);

	wake_pwrdev = input_allocate_device();
	if (!wake_pwrdev) {
		pr_err("Can't allocate suspend autotest power button\n");
		goto err_alloc_dev;
	}

	input_set_capability(wake_pwrdev, EV_KEY, KEY_POWER);
	wake_pwrdev->name = "wakefunc_pwrkey";
	wake_pwrdev->phys = "wakefunc_pwrkey/input0";

	rc = input_register_device(wake_pwrdev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_wake_timeout.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for wake_timeout\n", __func__);
	}

	wfnotif.notifier_call = lcd_notifier_callback;
	rc = lcd_register_client(&wfnotif);
	if (rc)
		pr_warn("%s: error\n", __func__);

err_input_dev:
	input_free_device(wake_pwrdev);
err_alloc_dev:
	pr_info(WAKEFUNC"%s: done\n", __func__);

	return 0;
}


static void __exit wake_timeout_exit(void)
{

	alarm_cancel(&wakefunc_rtc);
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
	lcd_unregister_client(&wfnotif);
	input_unregister_device(wake_pwrdev);
	input_free_device(wake_pwrdev);

	return;
}

MODULE_AUTHOR("flar2 <asegaert@gmail.com>");
MODULE_DESCRIPTION("'wake_timeout' - Disable screen wake functions after timeout");
MODULE_LICENSE("GPL v2");

module_init(wake_timeout_init);
module_exit(wake_timeout_exit);
