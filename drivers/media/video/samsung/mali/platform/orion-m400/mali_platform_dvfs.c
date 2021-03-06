/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform_dvfs.c
 * Platform specific Mali driver dvfs functions
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

#include <asm/io.h>

#include "mali_device_pause_resume.h"
#include <linux/workqueue.h>

#define MAX_MALI_DVFS_STEPS 5
#define MALI_DVFS_WATING 10 // msec

#ifdef CONFIG_CPU_FREQ
#include <mach/asv.h>
#define EXYNOS4_ASV_ENABLED
#endif

#include <plat/cpu.h>

static int bMaliDvfsRun = 0;

static _mali_osk_atomic_t bottomlock_status;
static int bottom_lock_step = 0;

typedef struct mali_dvfs_tableTag{
	unsigned int clock;
	unsigned int freq;
	unsigned int vol;
}mali_dvfs_table;

typedef struct mali_dvfs_statusTag{
	unsigned int currentStep;
	mali_dvfs_table * pCurrentDvfs;

}mali_dvfs_currentstatus;

typedef struct mali_dvfs_thresholdTag{
	unsigned int downthreshold;
	unsigned int upthreshold;
}mali_dvfs_threshold_table;

typedef struct mali_dvfs_staycount{
	unsigned int staycount;
}mali_dvfs_staycount_table;

typedef struct mali_dvfs_stepTag{
	int clk;
	int vol;
}mali_dvfs_step;

mali_dvfs_step step[MALI_DVFS_STEPS]={
	/*step 0 clk*/ {134,   950000},
#if (MALI_DVFS_STEPS > 1)
	/*step 1 clk*/ {160,   950000},
#if (MALI_DVFS_STEPS > 2)
	/*step 2 clk*/ {200,  1000000},
#if (MALI_DVFS_STEPS > 3)
	/*step 3 clk*/ {267,  1050000},
#if (MALI_DVFS_STEPS > 4)
	/*step 4 clk*/ {267,  1050000}
#endif
#endif
#endif
#endif
};

mali_dvfs_staycount_table mali_dvfs_staycount[MALI_DVFS_STEPS]={
	/*step 0*/{0},
#if (MALI_DVFS_STEPS > 1)
	/*step 1*/{0},
#if (MALI_DVFS_STEPS > 2)
	/*step 2*/{0},
#if (MALI_DVFS_STEPS > 3)
	/*step 3*/{0},
#if (MALI_DVFS_STEPS > 4)
	/*step 4*/{0}
#endif
#endif
#endif
#endif
};

int step0_clk = 134;
int step0_vol = 950000;
#if (MALI_DVFS_STEPS > 1)
int step1_clk = 160;
int step1_vol = 950000;
int step0_up = 60;
int step1_down = 50;
#if (MALI_DVFS_STEPS > 2)
int step2_clk = 200;
int step2_vol = 1000000;
int step1_up = 60;
int step2_down = 50;;
#if (MALI_DVFS_STEPS > 3)
int step3_clk = 267;
int step3_vol = 1050000;
int step2_up = 85;
int step3_down = 50;
#if (MALI_DVFS_STEPS > 4)
int step4_clk = 267;
int step4_vol = 1050000;
int step3_up = 85;
int step4_down = 70;
#endif
#endif
#endif
#endif

mali_dvfs_table mali_dvfs_all[MAX_MALI_DVFS_STEPS]={
	{134   ,1000000   ,  950000},
	{160   ,1000000   ,  950000},
	{200   ,1000000   , 1000000},
	{267   ,1000000   , 1000000},
	{267   ,1000000   , 1000000} };

mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS]={
	{134  ,1000000    , 950000},
#if (MALI_DVFS_STEPS > 1)
	{160  ,1000000    , 950000},
#if (MALI_DVFS_STEPS > 2)
	{200  ,1000000    ,1000000},
#if (MALI_DVFS_STEPS > 3)
	{267  ,1000000    ,1000000},
#if (MALI_DVFS_STEPS > 4)
	{267  ,1000000    ,1000000}
#endif
#endif
#endif
#endif
};

mali_dvfs_threshold_table mali_dvfs_threshold[MALI_DVFS_STEPS]={
	{0   , 55},
#if (MALI_DVFS_STEPS > 1)
	{40  , 65},
#if (MALI_DVFS_STEPS > 2)
	{50  , 75},
#if (MALI_DVFS_STEPS > 3)
	{60  , 85},
#if (MALI_DVFS_STEPS > 4)
	{75  ,100}
#endif
#endif
#endif
#endif
};

#ifdef EXYNOS4_ASV_ENABLED
#define ASV_8_LEVEL	8
#define ASV_5_LEVEL	5

static unsigned int asv_3d_volt_5_table[ASV_5_LEVEL][MALI_DVFS_STEPS] = {
	/* L5(134MHz), L4(160MHz), L3(200MHz), L2(267MHz), L1(267MHz) */
	{ 950000, 1000000, 1100000, 1100000, 1100000},	/* S */
	{ 950000, 1000000, 1100000, 1100000, 1100000},	/* A */
	{ 900000,  950000, 1000000, 1000000, 1000000},	/* B */
	{ 900000,  950000,  950000, 1000000, 1000000},	/* C */
	{ 900000,  950000,  950000,  950000,  950000},	/* D */
};

static unsigned int asv_3d_volt_8_table[ASV_8_LEVEL][MALI_DVFS_STEPS] = {
	/* L5(134MHz), L4(160MHz), L3(200MHz), L2(267MHz), L1(267MHz) */
	{ 950000, 1000000, 1100000, 1100000, 1100000},	/* SS */
	{ 950000, 1000000, 1100000, 1100000, 1100000},	/* A1 */
	{ 950000, 1000000, 1100000, 1100000, 1100000},	/* A2 */
	{ 900000,  950000, 1000000, 1000000, 1000000},	/* B1 */
	{ 900000,  950000, 1000000, 1000000, 1000000},	/* B2 */
	{ 900000,  950000, 1000000, 1000000, 1000000},	/* C1 */
	{ 900000,  950000,  950000, 1000000, 1000000},	/* C2 */
	{ 900000,  950000,  950000,  950000,  950000},	/* D1 */
};
#endif /* ASV_LEVEL */

/*dvfs status*/
mali_dvfs_currentstatus maliDvfsStatus;
int mali_dvfs_control = 0;

u32 mali_dvfs_utilization = 255;

static void mali_dvfs_work_handler(struct work_struct *w);

static struct workqueue_struct *mali_dvfs_wq = 0;
extern mali_io_address clk_register_map;
extern _mali_osk_lock_t *mali_dvfs_lock;

int mali_runtime_resumed = -1;

static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);

/* lock/unlock CPU freq by Mali */
#include <linux/types.h>
#include <mach/cpufreq.h>

atomic_t mali_cpufreq_lock;

int cpufreq_lock_by_mali(unsigned int freq)
{
#ifdef CONFIG_EXYNOS4_CPUFREQ
/* #if defined(CONFIG_CPU_FREQ) && defined(CONFIG_ARCH_EXYNOS4) */
	unsigned int level;

	if (atomic_read(&mali_cpufreq_lock) == 1)
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_G3D);

	if (exynos_cpufreq_get_level(freq * 1000, &level)) {
		printk(KERN_ERR
			"Mali: failed to get cpufreq level for %dMHz", freq);
		if (atomic_read(&mali_cpufreq_lock) == 1)
			atomic_set(&mali_cpufreq_lock, 0);
		return -EINVAL;
	}

	if (exynos_cpufreq_lock(DVFS_LOCK_ID_G3D, level)) {
		printk(KERN_ERR "Mali: failed to cpufreq lock for L%d", level);
		if (atomic_read(&mali_cpufreq_lock) == 1)
			atomic_set(&mali_cpufreq_lock, 0);
		return -EINVAL;
	}

	printk(KERN_DEBUG "Mali: cpufreq locked on <%d>%dMHz\n", level, freq);

	if (atomic_read(&mali_cpufreq_lock) == 0)
		atomic_set(&mali_cpufreq_lock, 1);
#endif
	return 0;
}

void cpufreq_unlock_by_mali(void)
{
#ifdef CONFIG_EXYNOS4_CPUFREQ
/* #if defined(CONFIG_CPU_FREQ) && defined(CONFIG_ARCH_EXYNOS4) */
	if (atomic_read(&mali_cpufreq_lock) == 1) {
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_G3D);
		atomic_set(&mali_cpufreq_lock, 0);
		printk(KERN_DEBUG "Mali: cpufreq locked off\n");
	}
#endif
}

static unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
int get_mali_dvfs_control_status(void)
{
	return mali_dvfs_control;
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	maliDvfsStatus.currentStep = step % MAX_MALI_DVFS_STEPS;
	if (step >= MAX_MALI_DVFS_STEPS)
		mali_runtime_resumed = maliDvfsStatus.currentStep;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	return MALI_TRUE;
}
#endif
static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;
#ifdef CPUFREQ_LOCK_DURING_440
	int err;
#endif

#ifdef CONFIG_REGULATOR
	if (mali_regulator_get_usecount() == 0) {
		MALI_DEBUG_PRINT(1, ("regulator use_count is 0 \n"));
		return MALI_FALSE;
	}
#endif

	if (boostup) {
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
	} else {
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
	}

	set_mali_dvfs_current_step(validatedStep);
	/*for future use*/
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

#ifdef CPUFREQ_LOCK_DURING_440
	/* lock/unlock CPU freq by Mali */
	if (mali_dvfs[step].clock >= 533)
		err = cpufreq_lock_by_mali(1200);
	else if (mali_dvfs[step].clock == 440)
		err = cpufreq_lock_by_mali(1000);
	else
		cpufreq_unlock_by_mali();
#endif

	return MALI_TRUE;
}

static void mali_platform_wating(u32 msec)
{
	/*sample wating
	change this in the future with proper check routine.
	*/
	unsigned int read_val;
	while(1) {
		read_val = _mali_osk_mem_ioread32(clk_register_map, 0x00);
		if ((read_val & 0x8000)==0x0000) break;
			_mali_osk_time_ubusydelay(100); // 1000 -> 100 : 20101218
		}
		/* _mali_osk_time_ubusydelay(msec*1000);*/
}

static mali_bool change_mali_dvfs_status(u32 step, mali_bool boostup )
{

	MALI_DEBUG_PRINT(1, ("> change_mali_dvfs_status: %d, %d \n",step, boostup));

	if (!set_mali_dvfs_status(step, boostup)) {
		MALI_DEBUG_PRINT(1, ("error on set_mali_dvfs_status: %d, %d \n",step, boostup));
		return MALI_FALSE;
	}

	/*wait until clock and voltage is stablized*/
	mali_platform_wating(MALI_DVFS_WATING); /*msec*/

	return MALI_TRUE;
}

#ifdef EXYNOS4_ASV_ENABLED
extern unsigned int exynos_result_of_asv;

static mali_bool mali_dvfs_table_update(void)
{
	unsigned int exynos_result_of_asv_group;
	unsigned int target_asv;
	unsigned int i;
	exynos_result_of_asv_group = exynos_result_of_asv & 0xf;
	target_asv = exynos_result_of_asv >> 28;
	MALI_PRINT(("exynos_result_of_asv_group = 0x%x, target_asv = 0x%x\n", exynos_result_of_asv_group, target_asv));

	if (target_asv == 0x8) { //SUPPORT_1400MHZ
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].vol = asv_3d_volt_5_table[exynos_result_of_asv_group][i];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
		}
	} else if (target_asv == 0x4){ //SUPPORT_1200MHZ
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].vol = asv_3d_volt_8_table[exynos_result_of_asv_group][i];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
		}
	}

	return MALI_TRUE;
}
#endif

static unsigned int decideNextStatus(unsigned int utilization)
{
	static unsigned int level = 0; // 0:stay, 1:up
	static int mali_dvfs_clk = 0;

	if (mali_runtime_resumed >= 0) {
		level = mali_runtime_resumed;
		mali_runtime_resumed = -1;
		return level;
	}

	if (mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold
			<= mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold) {
		MALI_PRINT(("upthreadshold is smaller than downthreshold: %d < %d\n",
				mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold,
				mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold));
		return level;
	}

	if (!mali_dvfs_control && level == maliDvfsStatus.currentStep) {
		if (utilization > (int)(255 * mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold / 100) &&
				level < MALI_DVFS_STEPS - 1) {
			level++;
		}
		if (utilization < (int)(255 * mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold / 100) &&
				level > 0) {
			level--;
		}
	} else if (mali_dvfs_control == 999) {
		int i = 0;
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			step[i].clk = mali_dvfs_all[i].clock;
		}
//#ifdef EXYNOS4_ASV_ENABLED
//		mali_dvfs_table_update();
//#endif
		i = 0;
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].clock = step[i].clk;
		}
		mali_dvfs_control = 0;
		level = 0;

		step0_clk = step[0].clk;
		change_dvfs_tableset(step0_clk, 0);
		step1_clk = step[1].clk;
		change_dvfs_tableset(step1_clk, 1);
		step2_clk = step[2].clk;
		change_dvfs_tableset(step2_clk, 2);
		step3_clk = step[3].clk;
		change_dvfs_tableset(step3_clk, 3);
		step4_clk = step[4].clk;
		change_dvfs_tableset(step4_clk, 4);
	} else if (mali_dvfs_control != mali_dvfs_clk && mali_dvfs_control != 999) {
		if (mali_dvfs_control < mali_dvfs_all[1].clock && mali_dvfs_control > 0) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[0].clock;
			}
			maliDvfsStatus.currentStep = 0;
		} else if (mali_dvfs_control < mali_dvfs_all[2].clock && mali_dvfs_control >= mali_dvfs_all[1].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[1].clock;
			}
			maliDvfsStatus.currentStep = 1;
		} else if (mali_dvfs_control < mali_dvfs_all[3].clock && mali_dvfs_control >= mali_dvfs_all[2].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[2].clock;
			}
			maliDvfsStatus.currentStep = 2;
		} else if (mali_dvfs_control < mali_dvfs_all[4].clock && mali_dvfs_control >= mali_dvfs_all[3].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk  = mali_dvfs_all[3].clock;
			}
			maliDvfsStatus.currentStep = 3;
		} else {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk  = mali_dvfs_all[4].clock;
			}
			maliDvfsStatus.currentStep = 4;
		}
		step0_clk = step[0].clk;
		change_dvfs_tableset(step0_clk, 0);
		step1_clk = step[1].clk;
		change_dvfs_tableset(step1_clk, 1);
		step2_clk = step[2].clk;
		change_dvfs_tableset(step2_clk, 2);
		step3_clk = step[3].clk;
		change_dvfs_tableset(step3_clk, 3);
		step4_clk = step[4].clk;
		change_dvfs_tableset(step4_clk, 4);
		level = maliDvfsStatus.currentStep;
	}

	mali_dvfs_clk = mali_dvfs_control;

	if (_mali_osk_atomic_read(&bottomlock_status) > 0) {
		if (level < bottom_lock_step)
			level = bottom_lock_step;
	}

	return level;
}

static mali_bool mali_dvfs_status(u32 utilization)
{
	unsigned int nextStatus = 0;
	unsigned int curStatus = 0;
	mali_bool boostup = MALI_FALSE;
	static int stay_count = 0; /* to prevent frequent switch */

	MALI_DEBUG_PRINT(1, ("> mali_dvfs_status: %d \n",utilization));

	/*decide next step*/
	curStatus = get_mali_dvfs_status();
	nextStatus = decideNextStatus(utilization);

	MALI_DEBUG_PRINT(1, ("= curStatus %d, nextStatus %d, maliDvfsStatus.currentStep %d \n", curStatus, nextStatus, maliDvfsStatus.currentStep));

	/*if next status is same with current status, don't change anything*/
	if ((curStatus != nextStatus && stay_count == 0)) {
		/*check if boost up or not*/
		if (nextStatus > maliDvfsStatus.currentStep) boostup = 1;

		/*change mali dvfs status*/
		if (!change_mali_dvfs_status(nextStatus,boostup)) {
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
			return MALI_FALSE;
		}
		stay_count = mali_dvfs_staycount[maliDvfsStatus.currentStep].staycount;
	} else {
		if (stay_count > 0)
			stay_count--;
	}

	return MALI_TRUE;
}

int mali_dvfs_is_running(void)
{
	return bMaliDvfsRun;
}

void mali_dvfs_late_resume(void)
{
	// set the init clock as low when resume
	set_mali_dvfs_status(0, 0);
}

static void mali_dvfs_work_handler(struct work_struct *w)
{
	int change_clk = 0;
	int change_step = 0;
	bMaliDvfsRun = 1;

	/* dvfs table change when clock was changed */
	if (step0_clk != mali_dvfs[0].clock) {
		MALI_PRINT(("::: step0_clk change to %d Mhz\n", step0_clk));
		change_clk = step0_clk;
		change_step = 0;
		step0_clk = change_dvfs_tableset(change_clk, change_step);
	}
#if (MALI_DVFS_STEPS > 1)
	if (step1_clk != mali_dvfs[1].clock) {
		MALI_PRINT(("::: step1_clk change to %d Mhz\n", step1_clk));
		change_clk = step1_clk;
		change_step = 1;
		step1_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step0_up != mali_dvfs_threshold[0].upthreshold) {
		MALI_PRINT(("::: step0_up change to %d %\n", step0_up));
		mali_dvfs_threshold[0].upthreshold = step0_up;
	}
	if (step1_down != mali_dvfs_threshold[1].downthreshold) {
		MALI_PRINT((":::step1_down change to %d %\n", step1_down));
		mali_dvfs_threshold[1].downthreshold = step1_down;
	}
#if (MALI_DVFS_STEPS > 2)
	if (step2_clk != mali_dvfs[2].clock) {
		MALI_PRINT(("::: step2_clk change to %d Mhz\n", step2_clk));
		change_clk = step2_clk;
		change_step = 2;
		step2_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step1_up != mali_dvfs_threshold[1].upthreshold) {
		MALI_PRINT((":::step1_up change to %d %\n", step1_up));
		mali_dvfs_threshold[1].upthreshold = step1_up;
	}
	if (step2_down != mali_dvfs_threshold[2].downthreshold) {
		MALI_PRINT((":::step2_down change to %d %\n", step2_down));
		mali_dvfs_threshold[2].downthreshold = step2_down;
	}
#if (MALI_DVFS_STEPS > 3)
	if (step3_clk != mali_dvfs[3].clock) {
		MALI_PRINT(("::: step3_clk change to %d Mhz\n", step3_clk));
		change_clk = step3_clk;
		change_step = 3;
		step3_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step2_up != mali_dvfs_threshold[2].upthreshold) {
		MALI_PRINT((":::step2_up change to %d %\n", step2_up));
		mali_dvfs_threshold[2].upthreshold = step2_up;
	}
	if (step3_down != mali_dvfs_threshold[3].downthreshold) {
		MALI_PRINT((":::step3_down change to %d %\n", step3_down));
		mali_dvfs_threshold[3].downthreshold = step3_down;
	}
#if (MALI_DVFS_STEPS > 4)
	if (step4_clk != mali_dvfs[4].clock) {
		MALI_PRINT(("::: step4_clk change to %d Mhz\n", step4_clk));
		change_clk = step4_clk;
		change_step = 4;
		step4_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step3_up != mali_dvfs_threshold[3].upthreshold) {
		MALI_PRINT((":::step3_up change to %d %\n", step3_up));
		mali_dvfs_threshold[3].upthreshold = step3_up;
	}
	if (step4_down != mali_dvfs_threshold[4].downthreshold) {
		MALI_PRINT((":::step4_down change to %d %\n", step4_down));
		mali_dvfs_threshold[4].downthreshold = step4_down;
	}
#endif
#endif
#endif
#endif

#ifdef DEBUG
	mali_dvfs[0].vol = step0_vol;
	mali_dvfs[1].vol = step1_vol;
	mali_dvfs[2].vol = step2_vol;
	mali_dvfs[3].vol = step3_vol;
	mali_dvfs[4].vol = step4_vol;
#endif
	MALI_DEBUG_PRINT(3, ("=== mali_dvfs_work_handler\n"));

	if (!mali_dvfs_status(mali_dvfs_utilization))
		MALI_DEBUG_PRINT(1,( "error on mali dvfs status in mali_dvfs_work_handler"));

	bMaliDvfsRun = 0;
}

mali_bool init_mali_dvfs_status(int step)
{
	/*default status
	add here with the right function to get initilization value.
	*/
	if (!mali_dvfs_wq)
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");

	_mali_osk_atomic_init(&bottomlock_status, 0);

	mali_dvfs_table_update();

	/*add a error handling here*/
	set_mali_dvfs_current_step(step);

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{

	_mali_osk_atomic_term(&bottomlock_status);


	if (mali_dvfs_wq)
		destroy_workqueue(mali_dvfs_wq);
	mali_dvfs_wq = NULL;
}

mali_bool mali_dvfs_handler(u32 utilization)
{
	mali_dvfs_utilization = utilization;
	queue_work_on(0, mali_dvfs_wq,&mali_dvfs_work);

	/*add error handle here*/
	return MALI_TRUE;
}

int change_dvfs_tableset(int change_clk, int change_step)
{
#ifdef CPUFREQ_LOCK_DURING_440
	int err;
#endif

	mali_dvfs[change_step].clock = change_clk;
	MALI_PRINT((":::mali dvfs step %d clock and voltage = %d Mhz, %d V\n",change_step, mali_dvfs[change_step].clock, mali_dvfs[change_step].vol));

	if (maliDvfsStatus.currentStep == change_step) {
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[change_step].vol, mali_dvfs[change_step].vol);
#endif
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[change_step].clock, mali_dvfs[change_step].freq);

#ifdef CPUFREQ_LOCK_DURING_440
		/* lock/unlock CPU freq by Mali */
		if (mali_dvfs[change_step].clock >= 533)
			err = cpufreq_lock_by_mali(1200);
		else if (mali_dvfs[change_step].clock == 440)
			err = cpufreq_lock_by_mali(1000);
		else
			cpufreq_unlock_by_mali();
#endif
	}

	return mali_dvfs[change_step].clock;
}

void mali_default_step_set(int step, mali_bool boostup)
{
	mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);

	if (maliDvfsStatus.currentStep == 1)
		set_mali_dvfs_status(step, boostup);
}

int mali_dvfs_bottom_lock_push(int lock_step)
{
	int prev_status = _mali_osk_atomic_read(&bottomlock_status);

	if (prev_status < 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for push\n"));
		return -1;
	}
	if (samsung_rev() < EXYNOS4412_REV_2_0)
		lock_step = min(lock_step, MALI_DVFS_STEPS - 2);
	else
		lock_step = min(lock_step, MALI_DVFS_STEPS - 1);

	if (bottom_lock_step < lock_step) {
		bottom_lock_step = lock_step;
		if (get_mali_dvfs_status() < lock_step) {
			mali_regulator_set_voltage(mali_dvfs[lock_step].vol,
						   mali_dvfs[lock_step].vol);
			mali_clk_set_rate(mali_dvfs[lock_step].clock,
					  mali_dvfs[lock_step].freq);
			set_mali_dvfs_current_step(lock_step);
		}
	}
	return _mali_osk_atomic_inc_return(&bottomlock_status);
}

int mali_dvfs_bottom_lock_pop(void)
{
	int prev_status = _mali_osk_atomic_read(&bottomlock_status);

	if (prev_status <= 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for pop\n"));
		return -1;
	} else if (prev_status == 1) {
		bottom_lock_step = 0;
		MALI_PRINT(("gpu bottom lock release\n"));
	}

	return _mali_osk_atomic_dec_return(&bottomlock_status);
}

int mali_dvfs_get_vol(int step)
{
	step = step % MAX_MALI_DVFS_STEPS;
	MALI_DEBUG_ASSERT(step<MAX_MALI_DVFS_STEPS);

	return mali_dvfs[step].vol;
}

#if MALI_VOLTAGE_LOCK
int mali_vol_get_from_table(int vol)
{
	int i;
	for (i = 0; i < MALI_DVFS_STEPS; i++) {
		if (mali_dvfs[i].vol >= vol)
			return mali_dvfs[i].vol;
	}
	MALI_PRINT(("Failed to get voltage from mali_dvfs table, maximum voltage is %d uV\n", mali_dvfs[MALI_DVFS_STEPS-1].vol));
	return 0;
}
#endif
