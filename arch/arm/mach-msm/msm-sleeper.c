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

#include <linux/earlysuspend2.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <mach/cpufreq.h>

#define MSM_SLEEPER_MAJOR_VERSION	2
#define MSM_SLEEPER_MINOR_VERSION	0

extern uint32_t maxscroff;
extern uint32_t maxscroff_freq;


#ifdef CONFIG_HAS_EARLYSUSPEND2
static void __cpuinit msm_sleeper_early_suspend(struct early_suspend2 *h)
{
	int cpu;
	int i;
	int num_cores = 4;

	for_each_possible_cpu(cpu) {
		msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, maxscroff_freq);
		pr_info("Limit max frequency to: %d\n", maxscroff_freq);
	}

	for (i = 1; i < num_cores; i++) {
		if (cpu_online(i))
			cpu_down(i);
	}


	return; 
}

static void __cpuinit msm_sleeper_late_resume(struct early_suspend2 *h)
{
	int cpu;
	int i;
	int num_cores = 4;

	for_each_possible_cpu(cpu) {
		msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, MSM_CPUFREQ_NO_LIMIT);
		pr_info("Restore max frequency to %d\n", MSM_CPUFREQ_NO_LIMIT);
	}

	for (i = 1; i < num_cores; i++) {
		if (!cpu_online(i))
			cpu_up(i);
	}

	return; 
}

static struct early_suspend2 msm_sleeper_early_suspend_driver = {
	.level = EARLY_SUSPEND2_LEVEL_BLANK_SCREEN + 10,
	.suspend = msm_sleeper_early_suspend,
	.resume = msm_sleeper_late_resume,
};
#endif

static int __init msm_sleeper_init(void)
{
	pr_info("msm-sleeper version %d.%d\n",
		 MSM_SLEEPER_MAJOR_VERSION,
		 MSM_SLEEPER_MINOR_VERSION);

#ifdef CONFIG_HAS_EARLYSUSPEND2
		register_early_suspend2(&msm_sleeper_early_suspend_driver);
#endif
	return 0;
}

MODULE_AUTHOR("flar2 <asegaert@gmail.com>");
MODULE_DESCRIPTION("'msm-sleeper' - Limit max frequency and shut down cores while screen is off");
MODULE_LICENSE("GPL v2");

late_initcall(msm_sleeper_init);

