/*
 opptimizer_n9.ko - The OPP Mannagement API
 version 0.1-alpha3 
 by Lance Colton <lance.colton@gmail.com>
 License: GNU GPLv3
 <http://www.gnu.org/licenses/gpl-3.0.html>
 
 Latest Source & changelog:
 https://gitorious.org/opptimizer-n9/opptimizer-n9
 
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/cpufreq.h>
#include <plat/common.h>
#include <plat/opp.h>
#include </usr/src/kernel-headers/arch/arm/mach-omap2/voltage.h>

#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Lance Colton <lance.colton@gmail.com>\n"
#define DRIVER_DESCRIPTION "opptimizer.ko - The OPP Management API\n\
https://gitorious.org/opptimizer-n9/opptimizer-n9 for source\n\
This module uses SYMSEARCH by Skrilax_CZ\n\
Made possible by Jeffrey Kawika Patricio and Tiago Sousa\n"
#define DRIVER_VERSION "1.0"


MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

// opp.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						opp_get_opp_count_fp, enum opp_t opp_type);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, 
						opp_find_freq_floor_fp, enum opp_t opp_type, unsigned long *freq);
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long, 
						opp_get_voltage_fp, const struct omap_opp *opp);
//SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, 
//						opp_find_freq_ceil_fp, enum opp_t opp_type, unsigned long *freq);						
//voltage.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_volt_data *, 
						omap_get_volt_data_fp, int vdd, unsigned long volt);		
						

static int opp_count, enabled_opp_count, main_index, cpufreq_index;

unsigned long default_max_rate;

static struct cpufreq_frequency_table *freq_table;
static struct cpufreq_policy *policy;

#define BUF_SIZE PAGE_SIZE
static char *buf;

/**
 * struct omap_opp - OMAP OPP description structure
 * @enabled:	true/false - marking this OPP as enabled/disabled
 * @rate:	Frequency in hertz
 * @u_volt:	Nominal voltage in microvolts corresponding to this OPP
 * @opp_id:	opp identifier (deprecated)
 *
 * This structure stores the OPP information for a given domain.
 */
struct omap_opp {
	bool enabled;
	unsigned long rate;
	unsigned long u_volt;
	u8 opp_id;	
};
/**
 * omap_volt_data - Omap voltage specific data.
 *
 * @u_volt_nominal	: The possible voltage value in uVolts
 * @u_volt_dyn_nominal	: The run time optimized nominal voltage for device.
 *			  this dynamic nominal is the nominal voltage
 *			  specialized for that device at that time.
 * @u_volt_dyn_margin	: margin to add on top of calib voltage for this opp
 * @u_volt_calib	: Calibrated voltage for this opp
 * @sr_nvalue		: Smartreflex N target value at voltage <voltage>
 * @sr_errminlimit	: Error min limit value for smartreflex. This value
 *			  differs at differnet opp and thus is linked
 *			  with voltage.
 * @vp_errorgain	: Error gain value for the voltage processor. This
 *			  field also differs according to the voltage/opp.
 */
//struct omap_volt_data {
//	unsigned long	u_volt_nominal;
//	unsigned long	u_volt_dyn_nominal;
//	unsigned long	u_volt_dyn_margin;
//	unsigned long	u_volt_calib;
//	u32		sr_nvalue;
//	u8		sr_errminlimit;
//	u8		vp_errorgain;
//	bool		abb;
//	u32		sr_val;
//	u32		sr_error;
//};


static int proc_opptimizer_read(char *buffer, char **buffer_location,
							  off_t offset, int count, int *eof, void *data)
{
	int ret = 0;
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct omap_volt_data *vdata;

	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (IS_ERR(opp)) {
		ret = 0;
	}
	
	vdata = omap_get_volt_data_fp(0, opp_get_voltage_fp(opp));
	
	ret += scnprintf(buffer+ret, count-ret, "opp rate: %lu\n", opp->rate);
	ret += scnprintf(buffer+ret, count-ret, "freq table [0]: %u\n", freq_table[0].frequency);
	ret += scnprintf(buffer+ret, count-ret, "policy->max: %u\n", policy->max);
	ret += scnprintf(buffer+ret, count-ret, "cpuinfo.max_freq: %u\n", policy->cpuinfo.max_freq);
	ret += scnprintf(buffer+ret, count-ret, "user_policy.max: %u\n", policy->user_policy.max);
	ret += scnprintf(buffer+ret, count-ret, "vdata->u_volt_nominal: %10ld\n", vdata->u_volt_nominal);
	ret += scnprintf(buffer+ret, count-ret, "vdata->u_volt_dyn_nominal: %10ld\n", vdata->u_volt_dyn_nominal);
	ret += scnprintf(buffer+ret, count-ret, "vdata->u_volt_dyn_margin: %10ld\n", vdata->u_volt_dyn_margin);
	ret += scnprintf(buffer+ret, count-ret, "vdata->u_volt_calib: %10ld\n", vdata->u_volt_calib);
	ret += scnprintf(buffer+ret, count-ret, "vdata->sr_nvalue: 0x%08x\n", vdata->sr_nvalue);
	ret += scnprintf(buffer+ret, count-ret, "vdata->sr_errminlimit: %u\n", vdata->sr_errminlimit);
	ret += scnprintf(buffer+ret, count-ret, "vdata->vp_errorgain: 0x%08x\n", vdata->vp_errorgain);
	ret += scnprintf(buffer+ret, count-ret, "vdata->sr_error: 0x%08x\n", vdata->sr_error);
	ret += scnprintf(buffer+ret, count-ret, "vdata->sr_val: 0x%08x\n", vdata->sr_val);
	ret += scnprintf(buffer+ret, count-ret, "vdata->abb: %2s\n", (vdata->abb) ? "yes" : "no");
	ret += scnprintf(buffer+ret, count+ret, "%s\n", DRIVER_VERSION);
	return ret;
};

static int proc_opptimizer_write(struct file *filp, const char __user *buffer,
						 unsigned long len, void *data)
{
	unsigned long temp_rate, rate, freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	if(!len || len >= BUF_SIZE)
		return -ENOSPC;
	if(copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = 0;
	if(sscanf(buf, "%lu", &rate) == 1) {
		opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
		if (IS_ERR(opp)) {
			return -ENODEV;
		}
		
		temp_rate = policy->user_policy.min;
		
		freq_table[0].frequency =
				policy->max = policy->cpuinfo.max_freq =
				policy->user_policy.max = rate / 1000;
		freq_table[3].frequency = policy->min =
				policy->cpuinfo.min_freq =
				policy->user_policy.min = rate / 1000;
			
		opp->rate = rate;
		
		freq_table[3].frequency = policy->min = policy->cpuinfo.min_freq =
				policy->user_policy.min = temp_rate;
	} else
		printk(KERN_INFO "opptimizer: incorrect parameters\n");
	return len;
};

							 
static int __init opptimizer_init(void)
{
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct proc_dir_entry *proc_entry;
	
	printk(KERN_INFO " %s %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by %s\n", DRIVER_AUTHOR);

	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_find_freq_floor, opp_find_freq_floor_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_get_voltage, opp_get_voltage_fp);	
	//SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_find_freq_ceil, opp_find_freq_ceil_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, omap_get_volt_data, omap_get_volt_data_fp);
	
	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);
	
	opp_count = enabled_opp_count = (opp_get_opp_count_fp(OPP_MPU));
	if (enabled_opp_count == opp_count) {
		main_index = cpufreq_index = (enabled_opp_count-1);
	} else {
		main_index = enabled_opp_count;
		cpufreq_index = (enabled_opp_count-1);
	}
	
	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (!opp || IS_ERR(opp)) {
		return -ENODEV;
	}
	
	default_max_rate = opp->rate;
	
	buf = (char *)vmalloc(BUF_SIZE);
	
	proc_entry = create_proc_read_entry("opptimizer", 0644, NULL, proc_opptimizer_read, NULL);
	proc_entry->write_proc = proc_opptimizer_write;

	return 0;
};

static void __exit opptimizer_exit(void)
{
	unsigned long temp_rate, freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	remove_proc_entry("opptimizer", NULL);
	
	vfree(buf);
	
	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (!opp || IS_ERR(opp)) {
		return;
	}

	opp->rate = default_max_rate;
	
	temp_rate = policy->user_policy.min;
	freq_table[0].frequency =
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = default_max_rate / 1000;
	freq_table[3].frequency = policy->min =
			policy->cpuinfo.min_freq =
			policy->user_policy.min = default_max_rate / 1000;
	
	freq_table[3].frequency = policy->min = policy->cpuinfo.min_freq =
			policy->user_policy.min = temp_rate;
	printk(KERN_INFO " opptimizer: Reseting values to default... Goodbye!\n");
};
							 
module_init(opptimizer_init);
module_exit(opptimizer_exit);

