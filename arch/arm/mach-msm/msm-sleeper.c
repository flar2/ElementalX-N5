/*
 * ElementalX msm-sleeper by flar2 <asegaert@gmail.com>
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

#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <mach/cpufreq.h>
#include <linux/lcd_notify.h>

#define MSM_SLEEPER_MAJOR_VERSION	3
#define MSM_SLEEPER_MINOR_VERSION	0

extern uint32_t maxscroff;
extern uint32_t maxscroff_freq;
static int limit_set = 0;

struct notifier_block notif;

static void msm_sleeper_suspend(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, maxscroff_freq);
		pr_info("Limit max frequency to: %d\n", maxscroff_freq);
	}
	limit_set = 1;

	return; 
}

static void msm_sleeper_resume(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, MSM_CPUFREQ_NO_LIMIT);
		pr_info("Restore max frequency to %d\n", MSM_CPUFREQ_NO_LIMIT);
	}
	limit_set = 0;

	return; 
}

static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_START:
		if (limit_set)
			msm_sleeper_resume();
		break;
	case LCD_EVENT_ON_END:
		break;
	case LCD_EVENT_OFF_START:
		if (maxscroff)
			msm_sleeper_suspend();
		break;
	case LCD_EVENT_OFF_END:
		break;
	default:
		break;
	}

	return 0;
}


static int __init msm_sleeper_init(void)
{
	pr_info("msm-sleeper version %d.%d\n",
		 MSM_SLEEPER_MAJOR_VERSION,
		 MSM_SLEEPER_MINOR_VERSION);

	notif.notifier_call = lcd_notifier_callback;

	if (lcd_register_client(&notif))
		printk("[msm-sleeper] error\n");

	return 0;
}

MODULE_AUTHOR("flar2 <asegaert@gmail.com>");
MODULE_DESCRIPTION("'msm-sleeper' - Limit max frequency while screen is off");
MODULE_LICENSE("GPL v2");

late_initcall(msm_sleeper_init);

