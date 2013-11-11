/* kernel/power/earlysuspend2.c
 *
 * Copyright (C) 2005-2008 Google, Inc.
 *
 * Modified by flar2 <asegaert@gmail.com>
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

#include <linux/earlysuspend2.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>

#include "power.h"

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_VERBOSE = 1U << 3,
};
static int debug_mask = DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

static DEFINE_MUTEX(early_suspend2_lock);
static LIST_HEAD(early_suspend2_handlers);
static void early_suspend2(struct work_struct *work);
static void late_resume2(struct work_struct *work);
static DECLARE_WORK(early_suspend2_work, early_suspend2);
static DECLARE_WORK(late_resume2_work, late_resume2);
static DEFINE_SPINLOCK(state_lock);
enum {
	SUSPEND_REQUESTED = 0x1,
	SUSPENDED = 0x2,
	SUSPEND_REQUESTED_AND_SUSPENDED = SUSPEND_REQUESTED | SUSPENDED,
};
static int state;
suspend_state_t requested_suspend_state;
struct workqueue_struct *suspend_work_queue;

void register_early_suspend2(struct early_suspend2 *handler)
{
	struct list_head *pos;

	mutex_lock(&early_suspend2_lock);
	list_for_each(pos, &early_suspend2_handlers) {
		struct early_suspend2 *e;
		e = list_entry(pos, struct early_suspend2, link);
		if (e->level > handler->level)
			break;
	}
	list_add_tail(&handler->link, pos);
	if ((state & SUSPENDED) && handler->suspend)
		handler->suspend(handler);
	mutex_unlock(&early_suspend2_lock);
}
EXPORT_SYMBOL(register_early_suspend2);

void unregister_early_suspend2(struct early_suspend2 *handler)
{
	mutex_lock(&early_suspend2_lock);
	list_del(&handler->link);
	mutex_unlock(&early_suspend2_lock);
}
EXPORT_SYMBOL(unregister_early_suspend2);

static void early_suspend2(struct work_struct *work)
{
	struct early_suspend2 *pos;
	unsigned long irqflags;
	int abort = 0;

	mutex_lock(&early_suspend2_lock);
	spin_lock_irqsave(&state_lock, irqflags);

	if (state == SUSPEND_REQUESTED)
		state |= SUSPENDED;
	else
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);

	if (abort) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("early_suspend2: abort, state %d\n", state);
		mutex_unlock(&early_suspend2_lock);
		return;
	}

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("early_suspend2: call handlers\n");
	list_for_each_entry(pos, &early_suspend2_handlers, link) {
		if (pos->suspend != NULL) {
			if (debug_mask & DEBUG_VERBOSE)
				pr_info("early_suspend2: calling %pf\n", pos->suspend);
			pos->suspend(pos);
		}
	}
	mutex_unlock(&early_suspend2_lock);
}

static void late_resume2(struct work_struct *work)
{
	struct early_suspend2 *pos;
	unsigned long irqflags;
	int abort = 0;

	mutex_lock(&early_suspend2_lock);
	spin_lock_irqsave(&state_lock, irqflags);

	if (state == SUSPENDED)
		state &= ~SUSPENDED;
	else
		abort = 1;
	spin_unlock_irqrestore(&state_lock, irqflags);

	if (abort) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("late_resume2: abort, state %d\n", state);
		goto abort;
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume2: call handlers\n");
	list_for_each_entry_reverse(pos, &early_suspend2_handlers, link) {
		if (pos->resume != NULL) {
			if (debug_mask & DEBUG_VERBOSE)
				pr_info("late_resume2: calling %pf\n", pos->resume);

			pos->resume(pos);
		}
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume2: done\n");
abort:
	mutex_unlock(&early_suspend2_lock);
}

void request_suspend_state(suspend_state_t new_state)
{
	unsigned long irqflags;
	int old_sleep;

	spin_lock_irqsave(&state_lock, irqflags);
	old_sleep = state & SUSPEND_REQUESTED;

	if (debug_mask & DEBUG_SUSPEND) {
		struct timespec ts;
		struct rtc_time tm;
		getnstimeofday(&ts);
		rtc_time_to_tm(ts.tv_sec, &tm);
		pr_info("request_suspend_state: %s (%d->%d) at %lld "
			"(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
			new_state != PM_SUSPEND_ON ? "sleep" : "wakeup",
			requested_suspend_state, new_state,
			ktime_to_ns(ktime_get()),
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
	}

	if (!old_sleep && new_state != PM_SUSPEND_ON) {
		state |= SUSPEND_REQUESTED;
		queue_work(suspend_work_queue, &early_suspend2_work);
	} else if (old_sleep && new_state == PM_SUSPEND_ON) {
		state &= ~SUSPEND_REQUESTED;
		queue_work(suspend_work_queue, &late_resume2_work);
	}

	requested_suspend_state = new_state;
	spin_unlock_irqrestore(&state_lock, irqflags);
}

static int __init early_suspend2_init(void)
{
	suspend_work_queue = create_singlethread_workqueue("e-suspend");

	if (suspend_work_queue == NULL) {
		return -ENOMEM;
	}

	return 0;
}


static void __exit early_suspend2_exit(void)
{
	destroy_workqueue(suspend_work_queue);
}


core_initcall(early_suspend2_init);
module_exit(early_suspend2_exit);

MODULE_LICENSE("GPL v2");
