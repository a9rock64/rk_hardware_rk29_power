/*
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co. Ltd. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LOG_TAG "RKPowerHAL"
#define DEBUG_EN 1
#include <utils/Log.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define BUFFER_LENGTH 128
#define FREQ_LENGTH 10

#define CPU_CLUST0_GOV_PATH "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define CPU_CLUST0_AVAIL_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies"
#define CPU_CLUST0_SCAL_MAX_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
#define CPU_CLUST0_SCAL_MIN_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"
#define CPU_CLUST0_BOOSTPULSE_PATH "/sys/devices/system/cpu/cpufreq/interactive/boostpulse"

#define GPU_AVAIL_FREQ "/sys/devices/10091000.gpu/available_frequencies"
#define GPU_FREQ_CLK   "/sys/devices/10091000.gpu/clock"
#define GPU_GOV_ENABLE "/sys/devices/10091000.gpu/dvfs_enable"

#define DDR_GOV_ENABLE "/dev/video_state"

static char cpu_clust0_available_freqs[FREQ_LENGTH][FREQ_LENGTH];
static char gpu_available_freqs[FREQ_LENGTH][FREQ_LENGTH];

static void sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    ALOGE("sysfs writing to %s: %s\n", path, s);
    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

/*************** Modify cpu clust0 scaling max && min freq for interactive mode **********************/
static void cpu_clus0_boost(int max, int min )
{
    if(DEBUG_EN)ALOGI("RK cpu_clus0_boost Entered!");
    if(DEBUG_EN)ALOGI("cpu_clust0_available_freqs[%d]:%s",max,cpu_clust0_available_freqs[max]);
    if(DEBUG_EN)ALOGI("cpu_clust0_available_freqs[%d]:%s",min,cpu_clust0_available_freqs[min]);
    if( *cpu_clust0_available_freqs[max]>='0' && *cpu_clust0_available_freqs[max]<='9' )
        sysfs_write(CPU_CLUST0_SCAL_MAX_FREQ,cpu_clust0_available_freqs[max]);
    else
        ALOGE("Invalid max freq can not be set!");
    if( *cpu_clust0_available_freqs[min]>='0' && *cpu_clust0_available_freqs[min]<='9' )
        sysfs_write(CPU_CLUST0_SCAL_MIN_FREQ,cpu_clust0_available_freqs[min]);
    else
        ALOGE("Invalid min freq can not be set!");
}


/*************** Modify gpu max && min freq for simple_ondemand mode **********************/
static void gpu_boost(int max, int min)
{
    if(DEBUG_EN)ALOGI("RK gpu_boost Entered!");
    if(DEBUG_EN)ALOGI("gpu_available_freqs[%d]:%s",max,gpu_available_freqs[max]);
    if(DEBUG_EN)ALOGI("gpu_available_freqs[%d]:%s",min,gpu_available_freqs[min]);
    sysfs_write(GPU_GOV_ENABLE, "0");
    sysfs_write(GPU_FREQ_CLK, "480000000");
}


/******** touch bootst  *********/
static void touch_boost(int on)
{
    if(DEBUG_EN)ALOGI("RK touch_boost Entered!");

    // improve ddr freq
    sysfs_write(DDR_GOV_ENABLE, on ? "n" : "p");
}

/************** Modify cpu gpu to performance mode ************************/
static void performance_boost(int on)
{
    if(DEBUG_EN)ALOGI("RK performance_boost Entered!");

    sysfs_write(CPU_CLUST0_GOV_PATH, on ? "performance" : "interactive");
    sysfs_write(GPU_GOV_ENABLE, on ? "0" : "1");
    sysfs_write(GPU_FREQ_CLK, on ? "480000000" : "200000000");
    sysfs_write(DDR_GOV_ENABLE, on ? "p" : "n");
}

/************** Modify cpu gpu to powersave mode ************************/
static void low_power_boost(int on)
{
    if(DEBUG_EN)ALOGI("RK low_power_boost Entered!");
    sysfs_write(CPU_CLUST0_GOV_PATH, on ? "powersave" : "interactive");
}

static void rk_power_init(struct power_module *module)
{
    if(DEBUG_EN)ALOGD("version 4.0\n");

    int   fd, count, i=0;
    char  cpu_clus0_freqs[BUFFER_LENGTH];
    char  gpu_freqs[BUFFER_LENGTH] ;
    char* freq_split;

    /*********************** obtain cpu cluster0 available freqs **************************/
    if (fd = open(CPU_CLUST0_AVAIL_FREQ,O_RDONLY)) {
        count = read(fd,cpu_clus0_freqs,sizeof(cpu_clus0_freqs)-1);
        if(count < 0) ALOGE("Error reading from %s\n", CPU_CLUST0_AVAIL_FREQ);
        else
            cpu_clus0_freqs[count] = '\0';
    } else {
        ALOGE("Error to open %s\n", CPU_CLUST0_AVAIL_FREQ);
    }
    if(DEBUG_EN)ALOGI("cpu_clus0_freqs:%s\n",cpu_clus0_freqs);

    freq_split = strtok(cpu_clus0_freqs," ");
    strcpy(cpu_clust0_available_freqs[0], freq_split);

    if(DEBUG_EN)ALOGI("cpu_clust0 available freq[0]:%s\n", cpu_clust0_available_freqs[0]);

    for (i = 1; freq_split = strtok(NULL, " "); i++) {
        strcpy(cpu_clust0_available_freqs[i], freq_split);
        if(DEBUG_EN) ALOGI("cpu_clust0 available freq[%d]:%s\n", i, cpu_clust0_available_freqs[i]);
    }

    /*********************** obtain gpu available freqs **************************/
    if (fd = open(GPU_AVAIL_FREQ, O_RDONLY)) {
        count = read(fd, gpu_freqs, sizeof(gpu_freqs)-1);
        if(count < 0) ALOGE("Error reading from %s\n", GPU_AVAIL_FREQ);
        else
            gpu_freqs[count] = '\0';
    } else {
        ALOGE("Error to open %s\n", GPU_AVAIL_FREQ);
    }

    if(DEBUG_EN)ALOGI("gpu_freqs:%s\n", gpu_freqs);

    freq_split = strtok(gpu_freqs, " ");
    strcpy(gpu_available_freqs[0], freq_split);
    if(DEBUG_EN)ALOGI("gpu available freq[0]:%s\n", gpu_available_freqs[0]);

    for(i = 1; freq_split = strtok(NULL, " "); i++) {
        strcpy(gpu_available_freqs[i], freq_split);
        if(DEBUG_EN) ALOGI("gpu available freq[%d]:%s\n", i, gpu_available_freqs[i]);
    }
}

/*performs power management actions upon the
 * system entering interactive state (that is, the system is awake
 * and ready for interaction, often with UI devices such as
 * display and touchscreen enabled) or non-interactive state (the
 * system appears asleep, display usually turned off).
 */
static void rk_power_set_interactive(struct power_module *module, int on)
{
    /*************Add appropriate actions for specific platform && product type *****************/
    sysfs_write("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", on ? "1200000" : "816000");
    sysfs_write("/sys/devices/system/cpu/cpu1/online", on ? "1" : "0");
    sysfs_write("/sys/devices/system/cpu/cpu2/online", on ? "1" : "0");
    sysfs_write("/sys/devices/system/cpu/cpu3/online", on ? "1" : "0");
}

/*
 * (*powerHint) is called to pass hints on power requirements, which
 * may result in adjustment of power/performance parameters of the
 * cpufreq governor and other controls.
 */
static void rk_power_hint(struct power_module *module, power_hint_t hint, void *data)
{
    /*************Add appropriate actions for specific platform && product type *****************/
    int mode = 0;
    switch (hint) {
    case POWER_HINT_INTERACTION:
	 if(data != NULL) {
            mode = *(int*)data;
	    touch_boost(mode);
	 } else {
            mode = 0;
	    touch_boost(mode);
	 }
        break;

    case POWER_HINT_VSYNC:
        break;

    case POWER_HINT_VIDEO_DECODE:
        break;

    case POWER_HINT_LOW_POWER:
        /*if(data!=NULL) {
            mode = *(int*)data;
            low_power_boost(mode);
        }*/
        break;

    case POWER_HINT_SUSTAINED_PERFORMANCE:
        if(data != NULL) {
            mode = *(int*)data;
            performance_boost(mode);
        } else {
            mode = 0;
            performance_boost(mode);
        }
        break;
    case POWER_HINT_PERFORMANCE:
        if(data != NULL) {
            mode = *(int*)data;
            performance_boost(mode);
        } else {
            mode = 0;
            performance_boost(mode);
        }
        break;
    case POWER_HINT_VR_MODE:
        break;
    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct power_module HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        module_api_version: POWER_MODULE_API_VERSION_0_5,
        hal_api_version: HARDWARE_HAL_API_VERSION,
        id: POWER_HARDWARE_MODULE_ID,
        name: TARGET_BOARD_PLATFORM " Power HAL",
        author: "Rockchip",
        methods: &power_module_methods,
    },

    init: rk_power_init,
    setInteractive: rk_power_set_interactive,
    powerHint: rk_power_hint,
};
