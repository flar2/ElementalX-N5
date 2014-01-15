/* Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <mach/socinfo.h>
#include <mach/scm.h>

#include "kgsl.h"
#include "kgsl_pwrscale.h"
#include "kgsl_device.h"

#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
#include <linux/module.h>
#endif

#define TZ_GOVERNOR_PERFORMANCE 0
#define TZ_GOVERNOR_ONDEMAND    1
#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
#define TZ_GOVERNOR_SIMPLE	2
#endif

struct tz_priv {
	int governor;
	struct kgsl_power_stats bin;
	unsigned int idle_dcvs;
};
spinlock_t tz_lock;

/* FLOOR is 5msec to capture up to 3 re-draws
 * per frame for 60fps content.
 */
#define FLOOR			5000
/* CEILING is 50msec, larger than any standard
 * frame length, but less than the idle timer.
 */
#define CEILING			50000
#define TZ_RESET_ID		0x3
#define TZ_UPDATE_ID		0x4
#define TZ_INIT_ID		0x6

/* Trap into the TrustZone, and call funcs there. */
static int __secure_tz_entry2(u32 cmd, u32 val1, u32 val2)
{
	int ret;
	spin_lock(&tz_lock);
	/* sync memory before sending the commands to tz*/
	__iowmb();
	ret = scm_call_atomic2(SCM_SVC_IO, cmd, val1, val2);
	spin_unlock(&tz_lock);
	return ret;
}

static int __secure_tz_entry3(u32 cmd, u32 val1, u32 val2,
				u32 val3)
{
	int ret;
	spin_lock(&tz_lock);
	/* sync memory before sending the commands to tz*/
	__iowmb();
	ret = scm_call_atomic3(SCM_SVC_IO, cmd, val1, val2,
				val3);
	spin_unlock(&tz_lock);
	return ret;
}

static ssize_t tz_governor_show(struct kgsl_device *device,
				struct kgsl_pwrscale *pwrscale,
				char *buf)
{
	struct tz_priv *priv = pwrscale->priv;
	int ret;

	if (priv->governor == TZ_GOVERNOR_ONDEMAND)
		ret = snprintf(buf, 10, "ondemand\n");
#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
	else if (priv->governor == TZ_GOVERNOR_SIMPLE)
		ret = snprintf(buf, 8, "simple\n");
#endif
	else
		ret = snprintf(buf, 13, "performance\n");

	return ret;
}

static ssize_t tz_governor_store(struct kgsl_device *device,
				struct kgsl_pwrscale *pwrscale,
				 const char *buf, size_t count)
{
	char str[20];
	struct tz_priv *priv = pwrscale->priv;
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	int ret;

	ret = sscanf(buf, "%20s", str);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&device->mutex);

	if (!strncmp(str, "ondemand", 8))
		priv->governor = TZ_GOVERNOR_ONDEMAND;
#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
	else if (!strncmp(str, "simple", 6))
		priv->governor = TZ_GOVERNOR_SIMPLE;
#endif
	else if (!strncmp(str, "performance", 11))
		priv->governor = TZ_GOVERNOR_PERFORMANCE;

	if (priv->governor == TZ_GOVERNOR_PERFORMANCE) {
		kgsl_pwrctrl_pwrlevel_change(device, pwr->max_pwrlevel);
		pwr->default_pwrlevel = pwr->max_pwrlevel;
	} else {
		pwr->default_pwrlevel = pwr->init_pwrlevel;
	}

	mutex_unlock(&device->mutex);
	return count;
}

PWRSCALE_POLICY_ATTR(governor, 0644, tz_governor_show, tz_governor_store);

static struct attribute *tz_attrs[] = {
	&policy_attr_governor.attr,
	NULL
};

static struct attribute_group tz_attr_group = {
	.attrs = tz_attrs,
};

static void tz_wake(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	struct tz_priv *priv = pwrscale->priv;
	if (device->state != KGSL_STATE_NAP &&
#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
		(priv->governor == TZ_GOVERNOR_ONDEMAND ||
		 priv->governor == TZ_GOVERNOR_SIMPLE))
#else
		priv->governor == TZ_GOVERNOR_ONDEMAND)
#endif
		kgsl_pwrctrl_pwrlevel_change(device,
					device->pwrctrl.default_pwrlevel);
}

#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
/* KGSL Simple GPU Governor */
/* Copyright (c) 2011-2013, Paul Reioux (Faux123). All rights reserved. */
static int default_laziness = 5;
module_param_named(simple_laziness, default_laziness, int, 0664);

static int ramp_up_threshold = 6000;
module_param_named(simple_ramp_threshold, ramp_up_threshold, int, 0664);

static int laziness;

static int simple_governor(struct kgsl_device *device, int idle_stat)
{
	int val = 0;
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;

	/* it's currently busy */
	if (idle_stat < ramp_up_threshold) {
		if (pwr->active_pwrlevel == 0)
			val = 0; /* already maxed, so do nothing */
		else if ((pwr->active_pwrlevel > 0) &&
			(pwr->active_pwrlevel <= (pwr->num_pwrlevels - 1)))
			val = -1; /* bump up to next pwrlevel */
	/* idle case */
	} else {
		if ((pwr->active_pwrlevel >= 0) &&
			(pwr->active_pwrlevel < (pwr->num_pwrlevels - 1)))
			if (laziness > 0) {
				/* hold off for a while */
				laziness--;
				val = 0; /* don't change anything yet */
			} else {
				val = 1; /* above min, lower it */
				/* reset laziness count */
				laziness = default_laziness;
		} else if (pwr->active_pwrlevel == (pwr->num_pwrlevels - 1))
			val = 0; /* already @ min, so do nothing */
	}
	return val;
}
#endif

static void tz_idle(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct tz_priv *priv = pwrscale->priv;
	struct kgsl_power_stats stats;
	int val, idle;

	/* In "performance" mode the clock speed always stays
	   the same */
	if (priv->governor == TZ_GOVERNOR_PERFORMANCE)
		return;

	device->ftbl->power_stats(device, &stats);
	priv->bin.total_time += stats.total_time;
	priv->bin.busy_time += stats.busy_time;
	/* Do not waste CPU cycles running this algorithm if
	 * the GPU just started, or if less than FLOOR time
	 * has passed since the last run.
	 */
	if ((stats.total_time == 0) ||
		(priv->bin.total_time < FLOOR))
		return;

	/* If there is an extended block of busy processing, set
	 * frequency to turbo.  Otherwise run the normal algorithm.
	 */
	if (priv->bin.busy_time > CEILING) {
		val = 0;
		kgsl_pwrctrl_pwrlevel_change(device,
				KGSL_PWRLEVEL_TURBO);
	} else if (priv->idle_dcvs) {
		idle = priv->bin.total_time - priv->bin.busy_time;
		idle = (idle > 0) ? idle : 0;
		val = __secure_tz_entry2(TZ_UPDATE_ID, idle, device->id);
	} else {
		if (pwr->step_mul > 1)
#ifdef CONFIG_MSM_KGSL_SIMPLE_GOV
			{
			idle = priv->bin.total_time - priv->bin.busy_time;
			idle = (idle > 0) ? idle : 0;
			val = simple_governor(device, idle);
			}
#else
			val = __secure_tz_entry3(TZ_UPDATE_ID,
				(pwr->active_pwrlevel + 1)/2,
				priv->bin.total_time, priv->bin.busy_time);
#endif
		else
			val = __secure_tz_entry3(TZ_UPDATE_ID,
				pwr->active_pwrlevel,
				priv->bin.total_time, priv->bin.busy_time);
	}

	priv->bin.total_time = 0;
	priv->bin.busy_time = 0;

	/* If the decision is to move to a lower level, make sure the GPU
	 * frequency drops.
	 */
	if (val > 0)
		val *= pwr->step_mul;
	if (val)
		kgsl_pwrctrl_pwrlevel_change(device,
					     pwr->active_pwrlevel + val);
}

static void tz_busy(struct kgsl_device *device,
	struct kgsl_pwrscale *pwrscale)
{
	device->on_time = ktime_to_us(ktime_get());
}

static void tz_sleep(struct kgsl_device *device,
	struct kgsl_pwrscale *pwrscale)
{
	struct tz_priv *priv = pwrscale->priv;

	__secure_tz_entry2(TZ_RESET_ID, 0, 0);
	priv->bin.total_time = 0;
	priv->bin.busy_time = 0;
}

#ifdef CONFIG_MSM_SCM
static int tz_init(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	int i = 0, j = 1, ret = 0;
	struct tz_priv *priv;
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	unsigned int tz_pwrlevels[KGSL_MAX_PWRLEVELS + 1];

	priv = pwrscale->priv = kzalloc(sizeof(struct tz_priv), GFP_KERNEL);
	if (pwrscale->priv == NULL)
		return -ENOMEM;
	priv->idle_dcvs = 0;
	priv->governor = TZ_GOVERNOR_ONDEMAND;
	spin_lock_init(&tz_lock);
	kgsl_pwrscale_policy_add_files(device, pwrscale, &tz_attr_group);
	for (i = 0; i < pwr->num_pwrlevels - 1; i++) {
		if (i == 0)
			tz_pwrlevels[j] = pwr->pwrlevels[i].gpu_freq;
		else if (pwr->pwrlevels[i].gpu_freq !=
				pwr->pwrlevels[i - 1].gpu_freq) {
			j++;
			tz_pwrlevels[j] = pwr->pwrlevels[i].gpu_freq;
		}
	}
	tz_pwrlevels[0] = j;
	ret = scm_call(SCM_SVC_DCVS, TZ_INIT_ID, tz_pwrlevels,
				sizeof(tz_pwrlevels), NULL, 0);
	if (ret)
		priv->idle_dcvs = 1;
	return 0;
}
#else
static int tz_init(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	return -EINVAL;
}
#endif /* CONFIG_MSM_SCM */

static void tz_close(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	kgsl_pwrscale_policy_remove_files(device, pwrscale, &tz_attr_group);
	kfree(pwrscale->priv);
	pwrscale->priv = NULL;
}

struct kgsl_pwrscale_policy kgsl_pwrscale_policy_tz = {
	.name = "trustzone",
	.init = tz_init,
	.busy = tz_busy,
	.idle = tz_idle,
	.sleep = tz_sleep,
	.wake = tz_wake,
	.close = tz_close
};
EXPORT_SYMBOL(kgsl_pwrscale_policy_tz);
