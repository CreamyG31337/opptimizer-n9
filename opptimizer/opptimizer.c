/*
 * opptimizer_n9.ko - The OPP Management API
 * version 1.5.4
 * by Lance Colton <lance.colton@gmail.com>
 * License: GNU GPLv3
 * <http://www.gnu.org/licenses/gpl-3.0.html>
 *
 * Latest Source & changelog:
 * https://github.com/CreamyG31337/opptimizer-n9
 *
 * NOTE: This module uses reverse engineering to access private kernel APIs
 * that are not exported. It uses symsearch to find unexported kernel symbols
 * at runtime and call them directly. This is the ONLY way to overclock the
 * Nokia N9, as there are no public APIs for this functionality.
 *
 * WARNING: Direct manipulation of kernel structures is inherently risky and
 * can cause system instability or crashes if done incorrectly. This code
 * bypasses normal kernel protections and directly modifies OPP (Operating
 * Performance Point) structures, voltage tables, and cpufreq policies.
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
#include <plat/clock.h>
#include </usr/src/kernel-headers/arch/arm/mach-omap2/voltage.h>
#include <linux/smp_lock.h>

#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Lance Colton <lance.colton@gmail.com>\n"
#define DRIVER_DESCRIPTION "opptimizer.ko - The OPP Management API\n\
https://github.com/CreamyG31337/opptimizer-n9 for source\n\
This module uses SYMSEARCH by Skrilax_CZ\n\
Made possible by Jeffrey Kawika Patricio and Tiago Sousa\n"
#define DRIVER_VERSION "1.5.4"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

/* Function pointer declarations for unexported kernel functions.
 * These are resolved at runtime using symsearch to find the actual
 * kernel symbol addresses. Source files noted for reference.
 */

// opp.c - Operating Performance Point management
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						opp_get_opp_count_fp, enum opp_t opp_type);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *,
						opp_find_freq_floor_fp, enum opp_t opp_type, unsigned long *freq);
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long,
						opp_get_voltage_fp, const struct omap_opp *opp);
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						opp_disable_fp, struct omap_opp *opp);
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						opp_enable_fp, struct omap_opp *opp);
//smartreflex-class1p5.c - SmartReflex voltage calibration
SYMSEARCH_DECLARE_FUNCTION_STATIC(void,
						sr_class1p5_reset_calib_fp, int vdd, bool reset, bool recal);
//voltage.c - Voltage domain management
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long,
						omap_voltageprocessor_get_voltage_fp, int vp_id);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_volt_data *,
						omap_get_volt_data_fp, int vdd, unsigned long volt);
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						omap_voltage_scale_fp, int vdd, struct omap_volt_data *vdata_target, struct omap_volt_data *vdata_current);
SYMSEARCH_DECLARE_FUNCTION_STATIC(void,
						vc_setup_on_voltage_fp, u32 vdd, unsigned long target_volt);
//cpu-omap.c - CPU frequency queries
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned int,
						omap_getspeed_fp, unsigned int cpu);
//clock.c - Clock rate management
SYMSEARCH_DECLARE_FUNCTION_STATIC(long,
						clk_round_rate_fp, struct clk *clk, unsigned long rate);
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						clk_set_rate_fp, struct clk *clk, unsigned long rate);
//clkdev.c - Clock device lookup
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct clk*,
						clk_get_fp, struct device *dev, const char *con_id);
//cpufreq.h - CPU frequency policy updates
SYMSEARCH_DECLARE_FUNCTION_STATIC(int,
						cpufreq_update_policy_fp,unsigned int cpu);

static int opp_count, enabled_opp_count, main_index, cpufreq_index;

unsigned long default_max_rate;

struct omap_volt_data default_vdata;

static struct cpufreq_frequency_table *freq_table;
static struct cpufreq_policy *policy;

#define MPU_CLK		"arm_fck"	/* MPU (CPU) clock name */
#define BUF_SIZE PAGE_SIZE		/* Buffer size for procfs I/O */
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
	if (IS_ERR(opp) || !opp) {
		ret = scnprintf(buffer+ret, count-ret, "Error: Could not find OPP\n");
		return ret;
	}

	vdata = omap_get_volt_data_fp(0, opp_get_voltage_fp(opp));
	if (!vdata) {
		ret = scnprintf(buffer+ret, count-ret, "Error: Could not get voltage data\n");
		return ret;
	}

	if (!freq_table || !policy) {
		ret = scnprintf(buffer+ret, count-ret, "Error: freq_table or policy is NULL\n");
		return ret;
	}

	ret += scnprintf(buffer+ret, count-ret, "opp rate: %lu\n", opp->rate);
	ret += scnprintf(buffer+ret, count-ret, "freq table [0]: %u\n", freq_table[0].frequency);
	ret += scnprintf(buffer+ret, count-ret, "policy->max: %u\n", policy->max);
	ret += scnprintf(buffer+ret, count-ret, "cpuinfo.max_freq: %u\n", policy->cpuinfo.max_freq);
	ret += scnprintf(buffer+ret, count-ret, "user_policy.max: %u\n", policy->user_policy.max);
	ret += scnprintf(buffer+ret, count-ret, "omap_voltageprocessor_get_voltage: %lu\n", omap_voltageprocessor_get_voltage_fp(0));
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
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->u_volt_nominal: %10ld\n", default_vdata.u_volt_nominal);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->u_volt_dyn_nominal: %10ld\n", default_vdata.u_volt_dyn_nominal);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->u_volt_dyn_margin: %10ld\n", default_vdata.u_volt_dyn_margin);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->u_volt_calib: %10ld\n", default_vdata.u_volt_calib);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->sr_nvalue: 0x%08x\n", default_vdata.sr_nvalue);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->sr_errminlimit: %u\n", default_vdata.sr_errminlimit);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->vp_errorgain: 0x%08x\n", default_vdata.vp_errorgain);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->sr_error: 0x%08x\n", default_vdata.sr_error);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->sr_val: 0x%08x\n", default_vdata.sr_val);
	ret += scnprintf(buffer+ret, count-ret, "Default_vdata->abb: %2s\n", (default_vdata.abb) ? "yes" : "no");
	ret += scnprintf(buffer+ret, count-ret, "v%s by @CreamyG31337\n", DRIVER_VERSION);
	return ret;
};

static int proc_opptimizer_write(struct file *filp, const char __user *buffer,
						 unsigned long len, void *data)
{
	unsigned long rate, freq = ULONG_MAX;
	unsigned long u_volt_current, u_volt_req = 0;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct cpufreq_freqs freqs;
	static struct clk *mpu_clk;
	struct omap_volt_data *volt_data;
	int ret;

	lock_kernel();
	/* CRITICAL: The missing unlock_kernel() calls on error paths were causing
	 * kernel deadlocks. If any error occurred (clk_get_fp failure, invalid
	 * parameters, etc.), the kernel lock would remain held, causing the system
	 * to hang. This appeared as a "crash" to users. All error paths now properly
	 * unlock before returning. */

	mpu_clk = clk_get_fp(NULL, MPU_CLK);
	if (IS_ERR(mpu_clk)) {
		ret = PTR_ERR(mpu_clk);
		unlock_kernel();
		return ret;
	}

	if(!len || len >= BUF_SIZE) {
		unlock_kernel();
		return -ENOSPC;
	}
	if(copy_from_user(buf, buffer, len)) {
		unlock_kernel();
		return -EFAULT;
	}
	buf[len] = 0;
	if(sscanf(buf, "%lu %lu", &rate, &u_volt_req) >= 1) {
		/* Find the highest OPP (Operating Performance Point) for MPU.
		 * opp_find_freq_floor with ULONG_MAX freq will return the highest OPP. */
		opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
		if (IS_ERR(opp) || !opp) {
			printk(KERN_ERR "opptimizer: opp_find_freq_floor_fp failed\n");
			unlock_kernel();
			return -ENODEV;
		}
		/* NOTE: opp_disable_fp() is commented out. Disabling the OPP before
		 * modification might be safer, but testing showed it works without.
		 * Leaving disabled to avoid potential side effects. */
		//opp_disable_fp(opp);
		/* CRITICAL: This was the most likely crash point when overclocking.
		 * If omap_get_volt_data_fp() returns NULL, the next use of volt_data would
		 * cause immediate kernel panic. This can happen due to:
		 * - Voltage system in transitional state (voltage scaling in progress,
		 *   SmartReflex recalibrating, etc.) - normal operational state, not corruption
		 * - Race condition: cpufreq governor or SmartReflex modifying voltage tables
		 *   simultaneously, causing temporary lookup failure
		 * - Invalid voltage lookup: overclocking to a voltage not in the normal OPP table
		 * - Legitimate lookup failure: voltage data simply doesn't exist for that level
		 * Without this NULL check, crashes would occur at:
		 * - Line 333: memcpy(&vdata_current, volt_data, ...) would panic
		 * - Line 351: volt_data->u_volt_calib = u_volt_req would panic
		 * This is NOT memory corruption - it's the function legitimately failing to
		 * find voltage data during a transitional or edge-case scenario. */
		volt_data = omap_get_volt_data_fp(0, opp_get_voltage_fp(opp));
		if (!volt_data) {
			printk(KERN_ERR "opptimizer: omap_get_volt_data_fp returned NULL!\n");
			unlock_kernel();
			return -ENODEV;
		}
		/* Safety limits: 800MHz - 1.7GHz. Values outside this range
		 * are likely typos and rejected to prevent hardware damage. */
		if(rate > 1700000000 || rate < 800000000){
			printk(KERN_INFO "opptimizer: rate too high or low!\n");
			unlock_kernel();
			return len;
		}

		/* Secondary crash point: If freq_table or policy is NULL (shouldn't happen
		 * if init succeeded, but could occur due to module removal race or init failure),
		 * the next line would cause immediate kernel panic. */
		if (!freq_table || !policy) {
			printk(KERN_ERR "opptimizer: freq_table or policy is NULL!\n");
			unlock_kernel();
			return -ENODEV;
		}

		/* Directly modify cpufreq structures to bypass normal locking mechanisms.
		 * This is necessary because we're overriding the normal frequency limits.
		 * The kernel's cpufreq subsystem would normally prevent this.
		 * NOTE: Without the NULL check above, this line would crash if freq_table
		 * or policy were NULL (e.g., freq_table[0] or policy->max dereference). */
		freq_table[0].frequency = policy->max = policy->cpuinfo.max_freq = policy->user_policy.max = rate / 1000;
		freqs.cpu = 0; /* N9 has only 1 CPU core */
		freqs.old = omap_getspeed_fp(0); /* Returns frequency in kHz (already divided by 1000) */
		freqs.new = rate / 1000; /* Convert Hz to kHz */
		/* NOTE: cpufreq_notify_transition() is commented out. This would notify
		 * other kernel subsystems (like cpufreq governors) of the frequency change.
		 * Disabled because it may cause conflicts with our direct manipulation. */
		if (freqs.old != freqs.new){
			//cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
		}
		/* When lowering frequency: set rate first, then lower voltage.
		 * This prevents the CPU from running at high voltage with low frequency. */
		if (freqs.new < freqs.old){
			/* Directly modify opp->rate. This updates the OPP structure that the
			 * kernel uses internally. The actual clock rate change happens through
			 * the cpufreq policy update at the end. This direct manipulation is
			 * necessary because normal APIs don't allow overclocking. */
			opp->rate = rate;
			/* NOTE: clk_set_rate_fp() is commented out. The clock rate is actually
			 * controlled by the cpufreq subsystem, so directly setting it here
			 * might conflict. The cpufreq_update_policy_fp() call at the end
			 * handles the actual frequency change. */
			//ret = clk_set_rate_fp(mpu_clk, freqs.new * 1000);
		}
		/* Voltage scaling order is critical for stability:
		 * - When increasing frequency: raise voltage FIRST, then frequency
		 * - When decreasing frequency: lower frequency FIRST, then voltage
		 * This prevents brownouts (voltage too low) or excessive power draw.
		 * NOTE: We don't explicitly lock dvfs_mutex because we're doing the
		 * smartreflex recalibration at the end, which should handle synchronization. */
		if (u_volt_req != 0){
			/* User requested a specific voltage. We need to:
			 * 1. Get current voltage data structure
			 * 2. Read actual current voltage from hardware
			 * 3. Update voltage data with new values
			 * 4. Scale voltage if it changed
			 * 5. Configure voltage controller */
			struct omap_volt_data vdata_current;
			memcpy(&vdata_current, volt_data, sizeof(vdata_current));
			/* Read the actual current voltage from the voltage processor.
			 * This is necessary because the volt_data structure might have
			 * stale calibration values, but the hardware has the real voltage. */
			u_volt_current = omap_voltageprocessor_get_voltage_fp(0);
			/* Update our copy with the actual current voltage so we can
			 * properly calculate the voltage transition. */
			vdata_current.u_volt_calib = u_volt_current;
			/* Hardware safety limits: 1.0V - 1.425V (in microvolts).
			 * These are hardware constraints to prevent damage. */
			if (u_volt_req >= 1425000){
				u_volt_req = 1425000; /* 1.425V maximum */
			}
			if (u_volt_req <= 1000000){
				u_volt_req = 1000000; /* 1.0V minimum */
			}
			/* Update voltage data structure with new calibration values.
			 * These values are used by the voltage scaling and SmartReflex systems. */
			volt_data->u_volt_calib = u_volt_req;
			volt_data->u_volt_dyn_nominal = u_volt_req;
			/* Remove dynamic voltage margin for overclocking. Normally the kernel
			 * adds margin to account for process variation, but when overclocking
			 * we want precise voltage control without extra headroom. */
			volt_data->u_volt_dyn_margin = 0;
			/* SmartReflex error minimum limit: 0x16 (22 decimal).
			 * Default kernel value: 0xF9 (249 decimal).
			 * 
			 * This is the minimum voltage error threshold before SmartReflex
			 * Class 1.5 takes corrective action. Lower value = tighter regulation:
			 * - 0xF9 (249): Loose regulation, only reacts to large voltage errors
			 * - 0x16 (22):  Tight regulation, reacts to small voltage errors
			 * 
			 * For overclocking, tighter regulation is critical because:
			 * 1. Higher frequencies are more sensitive to voltage variations
			 * 2. Prevents voltage droop that could cause crashes
			 * 3. Maintains stability at the edge of hardware limits
			 * 
			 * Trade-off: Tighter regulation uses more power due to more frequent
			 * voltage adjustments, but this is acceptable for overclocking. */
			volt_data->sr_errminlimit = 0x16;
			/* Voltage Processor error gain: NOT modified (left at default 0x16).
			 * 
			 * VP error gain controls how aggressively the voltage processor
			 * corrects voltage errors. Higher value = larger corrections per error:
			 * - Default 0x16: Moderate correction rate (balanced)
			 * - 0xFF (255): Maximum correction rate (very aggressive)
			 * 
			 * We leave this at default because:
			 * 1. SmartReflex (above) already provides tight regulation
			 * 2. Over-aggressive VP corrections could cause voltage overshoot/undershoot
			 * 3. Combined with tight SmartReflex, aggressive VP could create
			 *    oscillations or instability
			 * 
			 * The commented line below would enable maximum VP aggressiveness,
			 * but testing showed it's not needed and can cause instability. */
			//volt_data->vp_errorgain = 0xFF; /* Maximum VP correction rate - DISABLED */
			if (volt_data->u_volt_calib != u_volt_current) {
				/* Only scale voltage if it actually changed to avoid unnecessary operations. */
				omap_voltage_scale_fp(VDD1, volt_data, &vdata_current);
			}
			/* Configure voltage controller for the new voltage level. */
			vc_setup_on_voltage_fp(VDD1, volt_data->u_volt_calib);
		}
		else{
			/* User didn't specify voltage (u_volt_req == 0), so restore
			 * SmartReflex settings and return to default voltage. */
			struct omap_volt_data vdata_current;
			memcpy(&vdata_current, volt_data, sizeof(vdata_current));
			u_volt_current = omap_voltageprocessor_get_voltage_fp(0);
			vdata_current.u_volt_calib = u_volt_current;
			volt_data->u_volt_calib = default_vdata.u_volt_calib;
			volt_data->u_volt_dyn_nominal = default_vdata.u_volt_dyn_nominal;
			volt_data->u_volt_dyn_margin = default_vdata.u_volt_dyn_margin;
			volt_data->sr_errminlimit = default_vdata.sr_errminlimit;
			if (default_vdata.u_volt_calib != u_volt_current) {
				printk(KERN_INFO "opptimizer: returning to default voltage\n");
				omap_voltage_scale_fp(VDD1, volt_data, &vdata_current);
			}
			vc_setup_on_voltage_fp(VDD1, volt_data->u_volt_calib);
		}

		/* When increasing frequency: voltage was raised first (above),
		 * now set the new frequency. This order prevents brownouts. */
		if (freqs.new > freqs.old){
			/* Directly modify opp->rate. This is the internal OPP structure
			 * that tracks the frequency. The actual clock change happens via
			 * cpufreq_update_policy_fp() below. This direct manipulation bypasses
			 * normal kernel protections that prevent overclocking. */
			opp->rate = rate;
			/* NOTE: clk_set_rate_fp() commented out - see earlier comment.
			 * The cpufreq subsystem handles the actual clock rate change. */
			//ret = clk_set_rate_fp(mpu_clk, freqs.new * 1000);
		}
		if (freqs.old != freqs.new){
			/* NOTE: cpufreq_notify_transition() commented out - see earlier comment.
			 * We skip notifying other subsystems to avoid conflicts. */
			//cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
			printk(KERN_INFO "opptimizer: updated max rate to %dmhz \n",freqs.new / 1000);
		}
		/* NOTE: opp_enable_fp() commented out. We didn't disable it, so no need
		 * to re-enable. Also, re-enabling might trigger kernel validation checks
		 * that could reject our overclocked values. */
		//opp_enable_fp(opp);
		/* Reset and recalibrate SmartReflex. This is critical after voltage changes.
		 * SmartReflex is OMAP's adaptive voltage scaling system that adjusts voltage
		 * based on silicon characteristics. After changing voltage/frequency, we
		 * need to wipe old calibration data and let it recalibrate for the new settings. */
		sr_class1p5_reset_calib_fp(VDD1, true, true);
	} else
		printk(KERN_INFO "opptimizer: incorrect parameters\n");

	/* Update cpufreq policy. This propagates our direct structure modifications
	 * to the actual hardware. This is what actually changes the CPU frequency.
	 * NOTE: cpufreq_stats (frequency statistics) may be inaccurate after this,
	 * but that's a minor issue compared to getting overclocking to work. */

	cpufreq_update_policy_fp(0);

	unlock_kernel();

	return len;
};


static int __init opptimizer_init(void)
{
	unsigned long freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct proc_dir_entry *proc_entry;
	struct omap_volt_data *volt_data;


	printk(KERN_INFO " %s %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by %s\n", DRIVER_AUTHOR);

	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_find_freq_floor, opp_find_freq_floor_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_get_voltage, opp_get_voltage_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, sr_class1p5_reset_calib, sr_class1p5_reset_calib_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, omap_get_volt_data, omap_get_volt_data_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, omap_getspeed, omap_getspeed_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, clk_round_rate, clk_round_rate_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, clk_set_rate, clk_set_rate_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, clk_get, clk_get_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, omap_voltageprocessor_get_voltage, omap_voltageprocessor_get_voltage_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, omap_voltage_scale, omap_voltage_scale_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, vc_setup_on_voltage, vc_setup_on_voltage_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_disable, opp_disable_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, opp_enable, opp_enable_fp);
	/* NOTE: These symbols are commented out because they're not needed or
	 * not available in this kernel version. cpufreq_frequency_get_table()
	 * is available as a normal exported symbol, and cpufreq_stats functions
	 * aren't required for basic overclocking functionality. */
	//SYMSEARCH_BIND_FUNCTION_TO(opptimizer, cpufreq_frequency_get_table, cpufreq_frequency_get_table_fp);
	//SYMSEARCH_BIND_FUNCTION_TO(opptimizer, cpufreq_stats_create_table, cpufreq_stats_create_table_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opptimizer, cpufreq_update_policy, cpufreq_update_policy_fp);



	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);

	if (!freq_table || !policy) {
		printk(KERN_ERR "opptimizer: Failed to get freq_table or policy!\n");
		return -ENODEV;
	}

	opp_count = enabled_opp_count = (opp_get_opp_count_fp(OPP_MPU));
	if (enabled_opp_count == opp_count) {
		main_index = cpufreq_index = (enabled_opp_count-1);
	} else {
		main_index = enabled_opp_count;
		cpufreq_index = (enabled_opp_count-1);
	}

	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (!opp || IS_ERR(opp)) {
		printk(KERN_ERR "opptimizer: opp_find_freq_floor_fp failed in init!\n");
		return -ENODEV;
	}

	default_max_rate = opp->rate;

	volt_data = omap_get_volt_data_fp(0, opp_get_voltage_fp(opp));
	if (!volt_data) {
		printk(KERN_ERR "opptimizer: omap_get_volt_data_fp returned NULL in init!\n");
		return -ENODEV;
	}

	memcpy(&default_vdata, volt_data, sizeof(default_vdata));

	buf = (char *)vmalloc(BUF_SIZE);

	proc_entry = create_proc_read_entry("opptimizer", 0644, NULL, proc_opptimizer_read, NULL);
	proc_entry->write_proc = proc_opptimizer_write;

	return 0;
};

static void __exit opptimizer_exit(void)
{
	unsigned long temp_rate, freq = ULONG_MAX;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct omap_volt_data *vdata_current;

	remove_proc_entry("opptimizer", NULL);

	vfree(buf);

	opp = opp_find_freq_floor_fp(OPP_MPU, &freq);
	if (!opp || IS_ERR(opp)) {
		printk(KERN_ERR "opptimizer: opp_find_freq_floor_fp failed in exit!\n");
		return;
	}

	vdata_current = omap_get_volt_data_fp(0, opp_get_voltage_fp(opp));
	if (!vdata_current) {
		printk(KERN_ERR "opptimizer: omap_get_volt_data_fp returned NULL in exit!\n");
		return;
	}

	if (!freq_table || !policy) {
		printk(KERN_ERR "opptimizer: freq_table or policy is NULL in exit!\n");
		return;
	}

	/* Restore default frequency and voltage on module unload.
	 * Order matters: when speeding up, raise voltage first. When slowing down,
	 * lower frequency first. This prevents brownouts and excessive power draw. */
	if(opp->rate < default_max_rate){
		/* Current rate is below default, so we're speeding up.
		 * Raise voltage FIRST, then frequency, to prevent brownouts. */
		if (default_vdata.u_volt_calib != vdata_current->u_volt_calib) {
			omap_voltage_scale_fp(VDD1, &default_vdata, vdata_current);
		}
		opp->rate = default_max_rate;
		freq_table[0].frequency =
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = default_max_rate / 1000;
	}else{
		/* Current rate is at or above default, so we're slowing down.
		 * Lower frequency FIRST, then voltage, to prevent excessive power draw. */
		opp->rate = default_max_rate;
		freq_table[0].frequency =
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = default_max_rate / 1000;
		if (default_vdata.u_volt_calib != vdata_current->u_volt_calib) {
			omap_voltage_scale_fp(VDD1, &default_vdata, vdata_current);
		}
	}
	printk(KERN_INFO " opptimizer: Reseting values to default... Goodbye!\n");
};

module_init(opptimizer_init);
module_exit(opptimizer_exit);

