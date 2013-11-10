/*
 * drivers/input/touchscreen/sweep2wake.c
 *
 *
 * Copyright (c) 2012, Dennis Rassmann <showp1984@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/sweep2wake.h>

/* Tuneables */
#define DEBUG                   1
#define S2W_PWRKEY_DUR          60

#ifdef CONFIG_MACH_MSM8974_HAMMERHEAD
/* Hammerhead aka Nexus 5 */
#define S2W_Y_MAX               1920
#define S2W_X_MAX               1080
#define S2W_Y_LIMIT             S2W_Y_MAX-130
#define S2W_X_B1                400
#define S2W_X_B2                700
#define S2W_X_FINAL             250
#else
/* defaults */
#define S2W_Y_LIMIT             2350
#define S2W_X_MAX               1540
#define S2W_X_B1                500
#define S2W_X_B2                1000
#define S2W_X_FINAL             300
#endif


/* Resources */
int s2w_switch = 1;
bool scr_suspended = false, exec_count = true, irq_wake = false;
bool scr_on_touch = false, barrier[2] = {false, false};
static struct input_dev * sweep2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);

/* Read cmdline for s2w */
static int __init read_s2w_cmdline(char *s2w)
{
	if (strcmp(s2w, "1") == 0) {
		printk(KERN_INFO "[cmdline_s2w]: Sweep2Wake enabled. | s2w='%s'", s2w);
		s2w_switch = 1;
	} else if (strcmp(s2w, "0") == 0) {
		printk(KERN_INFO "[cmdline_s2w]: Sweep2Wake disabled. | s2w='%s'", s2w);
		s2w_switch = 0;
	} else {
		printk(KERN_INFO "[cmdline_s2w]: No valid input found. Going with default: | s2w='%u'", s2w_switch);
	}
	return 1;
}
__setup("s2w=", read_s2w_cmdline);

/* PowerKey setter */
void sweep2wake_setdev(struct input_dev * input_device) {
	sweep2wake_pwrdev = input_device;
	return;
}
EXPORT_SYMBOL(sweep2wake_setdev);

/* PowerKey work func */
static void sweep2wake_presspwr(struct work_struct * sweep2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
                return;
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	msleep(S2W_PWRKEY_DUR);
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	msleep(S2W_PWRKEY_DUR);
        mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(sweep2wake_presspwr_work, sweep2wake_presspwr);

/* PowerKey trigger */
void sweep2wake_pwrtrigger(void) {
	schedule_work(&sweep2wake_presspwr_work);
        return;
}

/* reset on finger release */
void sweep2wake_reset(void) {
	exec_count = true;
	barrier[0] = false;
	barrier[1] = false;
	scr_on_touch = false;
}

/* Sweep2wake main function */
void detect_sweep2wake(int x, int y, bool st)
{
        int prevx = 0, nextx = 0;
        bool single_touch = st;
#if DEBUG
        pr_info("[sweep2wake]: x,y(%4d,%4d) single:%s\n",
                x, y, (single_touch) ? "true" : "false");
#endif
	//left->right
	if ((single_touch) && (scr_suspended == true) && (s2w_switch > 0)) {
		prevx = 0;
		nextx = S2W_X_B1;
		if ((barrier[0] == true) ||
		   ((x > prevx) &&
		    (x < nextx) &&
		    (y > 0))) {
			prevx = nextx;
			nextx = S2W_X_B2;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x > prevx) &&
			    (x < nextx) &&
			    (y > 0))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x > prevx) &&
				    (y > 0)) {
					if (x > (S2W_X_MAX - S2W_X_FINAL)) {
						if (exec_count) {
							printk(KERN_INFO "[sweep2wake]: ON");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	//right->left
	} else if ((single_touch) && (scr_suspended == false) && (s2w_switch > 0)) {
		scr_on_touch=true;
		prevx = (S2W_X_MAX - S2W_X_FINAL);
		nextx = S2W_X_B2;
		if ((barrier[0] == true) ||
		   ((x < prevx) &&
		    (x > nextx) &&
		    (y > S2W_Y_LIMIT))) {
			prevx = nextx;
			nextx = S2W_X_B1;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x < prevx) &&
			    (x > nextx) &&
			    (y > S2W_Y_LIMIT))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x < prevx) &&
				    (y > S2W_Y_LIMIT)) {
					if (x < S2W_X_FINAL) {
						if (exec_count) {
							printk(KERN_INFO "[sweep2wake]: OFF");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	}
}

/*
 * INIT / EXIT stuff below here
 */

static int __init sweep2wake_init(void)
{
	pr_info("[sweep2wake]: %s done\n", __func__);
	return 0;
}

static void __exit sweep2wake_exit(void)
{
	return;
}

module_init(sweep2wake_init);
module_exit(sweep2wake_exit);

MODULE_DESCRIPTION("Sweep2wake");
MODULE_LICENSE("GPLv2");

