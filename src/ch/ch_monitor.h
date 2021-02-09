/*
 * Copyright Intel Corp. 2020
 *
 * ch_driver.h: header file for Cloud-Hypervisor driver functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <curl/curl.h>

#include "virobject.h"
#include "domain_conf.h"

#define URL_ROOT "http://localhost/api/v1"
#define URL_VMM_SHUTDOWN "vmm.shutdown"
#define URL_VMM_PING "vmm.ping"
#define URL_VM_CREATE "vm.create"
#define URL_VM_DELETE "vm.delete"
#define URL_VM_BOOT "vm.boot"
#define URL_VM_SHUTDOWN "vm.shutdown"
#define URL_VM_REBOOT "vm.reboot"
#define URL_VM_Suspend "vm.pause"
#define URL_VM_RESUME "vm.resume"

#define VIRCH_THREAD_NAME_LEN   16

typedef enum {
    virCHThreadTypeEmulator,
    virCHThreadTypeVcpu,
    virCHThreadTypeIO,
    virCHThreadTypeUnknown,
    virCHThreadTypeMax
} virCHThreadType;

typedef struct _virCHMonitorCPUInfo virCHMonitorCPUInfo;
typedef virCHMonitorCPUInfo *virCHMonitorCPUInfoPtr;

struct _virCHMonitorCPUInfo {
    int cpuid;

    bool online;
};

typedef struct _virCHMonitorEmuThreadInfo virCHMonitorEmuThreadInfo;
typedef virCHMonitorEmuThreadInfo *virCHMonitorEmuThreadInfoPtr;

struct _virCHMonitorEmuThreadInfo {
    char    thrName[VIRCH_THREAD_NAME_LEN];
};

typedef struct _virCHMonitorIOThreadInfo virCHMonitorIOThreadInfo;
typedef virCHMonitorIOThreadInfo *virCHMonitorIOThreadInfoPtr;

struct _virCHMonitorIOThreadInfo {
    char    thrName[VIRCH_THREAD_NAME_LEN];
};

typedef struct _virCHMonitorThreadInfo virCHMonitorThreadInfo;
typedef virCHMonitorThreadInfo *virCHMonitorThreadInfoPtr;

struct _virCHMonitorThreadInfo {
    virCHThreadType type;
    pid_t   tid;

    union {
        virCHMonitorCPUInfo vcpuInfo;
        virCHMonitorEmuThreadInfo emuInfo;
        virCHMonitorIOThreadInfo ioInfo;
    };
};

typedef struct _virCHMonitor virCHMonitor;
typedef virCHMonitor *virCHMonitorPtr;

struct _virCHMonitor {
    virObjectLockable parent;

    CURL *handle;

    char *socketpath;

    pid_t pid;

    virDomainObjPtr vm;

    size_t nthreads;
    virCHMonitorThreadInfoPtr threads;
};

virCHMonitorPtr virCHMonitorNew(virDomainObjPtr vm, const char *socketdir);
void virCHMonitorClose(virCHMonitorPtr mon);

int virCHMonitorCreateVM(virCHMonitorPtr mon,
                         size_t *nnicindexes, int **nicindexes);
int virCHMonitorBootVM(virCHMonitorPtr mon);
int virCHMonitorShutdownVM(virCHMonitorPtr mon);
int virCHMonitorRebootVM(virCHMonitorPtr mon);
int virCHMonitorSuspendVM(virCHMonitorPtr mon);
int virCHMonitorResumeVM(virCHMonitorPtr mon);

ssize_t virCHMonitorRefreshThreadInfo(virCHMonitorPtr mon);
size_t virCHMonitorGetThreadInfo(virCHMonitorPtr mon, bool refresh,
                                 virCHMonitorThreadInfoPtr *threads);

int virCHMonitorGetIOThreads(virCHMonitorPtr mon,
                            virDomainIOThreadInfoPtr **iothreads);