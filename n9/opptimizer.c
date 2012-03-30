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
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/cpufreq.h>
#include <plat/common.h>
#include <plat/opp.h>

#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Lance Colton <lance.colton@gmail.com>\n"
#define DRIVER_DESCRIPTION "opptimizer.ko - The OPP Management API\n\
https://gitorious.org/opptimizer-n9/opptimizer-n9 for source\n\
This module uses SYMSEARCH by Skrilax_CZ\n\
Made possible by Jeffrey Kawika Patricio and Tiago Sousa\n"
#define DRIVER_VERSION "0.1-alpha3"


MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

SYMSEARCH_DECLARE_FUNCTION_STATIC(int, opp_get_opp_count_fp, enum opp_t opp_type);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, 
			opp_find_freq_floor_fp, enum opp_t opp_type, unsigned long *freq);

static int maxdex;
static unsigned long def_max_rate;

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

static int proc_opptimizer_read(char *buffer, char **buffer_location,
							  off_t offset, int count, int *eof, void *data)
{
	int ret = 0;
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);

	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (IS_ERR(opp)) {
		ret = 0;
	}
	ret += scnprintf(buffer+ret, count-ret, "%lu\n", opp->rate);
	ret += scnprintf(buffer+ret, count+ret, "%s\n", DRIVER_VERSION);
	return ret;
};

static int proc_opptimizer_write(struct file *filp, const char __user *buffer,
						 unsigned long len, void *data)
{
	unsigned long rate, freq = ULONG_MAX;
	struct omap_opp *opp;
	
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
		
		freq_table[maxdex].frequency = policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = rate / 1000;
			
		opp->rate = rate;

	} else
		printk(KERN_INFO "opptimizer: incorrect parameters\n");
	return len;
};
							 
static int __init opptimizer_init(void)
{
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp;
	struct proc_dir_entry *proc_entry;
	
	printk(KERN_INFO " %s %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by %s\n", DRIVER_AUTHOR);

	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_find_freq_floor, opp_find_freq_floor_fp);
	
	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);
	
	maxdex = (opp_get_opp_count_fp(OPP_MPU)-1);
	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	def_max_rate = opp->rate;
	
	buf = (char *)vmalloc(BUF_SIZE);
	
	proc_entry = create_proc_read_entry("opptimizer", 0644, NULL, proc_opptimizer_read, NULL);
	proc_entry->write_proc = proc_opptimizer_write;
	
	return 0;
};

static void __exit opptimizer_exit(void)
{
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp;
	
	remove_proc_entry("opptimizer", NULL);
	
	vfree(buf);
	
	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	opp->rate = def_max_rate;
	freq_table[maxdex].frequency = policy->max = policy->cpuinfo.max_freq =
	  policy->user_policy.max = def_max_rate / 1000;
	
	printk(KERN_INFO " opptimizer: Reseting values to default... Goodbye!\n");
};
							 
module_init(opptimizer_init);
module_exit(opptimizer_exit);

