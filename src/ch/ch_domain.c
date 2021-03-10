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

#include <config.h>

#include "ch_domain.h"
#include "datatypes.h"
#include "domain_driver.h"
#include "viralloc.h"
#include "virlog.h"
#include "virtime.h"
#include "virsystemd.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_ENUM_IMPL(virCHDomainJob,
              CH_JOB_LAST,
              "none",
              "query",
              "destroy",
              "modify",
);

VIR_LOG_INIT("ch.ch_domain");

static int
virCHDomainObjInitJob(virCHDomainObjPrivatePtr priv)
{
    memset(&priv->job, 0, sizeof(priv->job));

    if (virCondInit(&priv->job.cond) < 0)
        return -1;

    return 0;
}

static void
virCHDomainObjResetJob(virCHDomainObjPrivatePtr priv)
{
    virCHDomainJobObjPtr job = &priv->job;

    job->active = CH_JOB_NONE;
    job->owner = 0;
}

int
virCHDomainObjRestoreJob(virDomainObjPtr obj,
                         virCHDomainJobObjPtr job)
{
    virCHDomainObjPrivatePtr priv = obj->privateData;

    memset(job, 0, sizeof(*job));
    job->active = priv->job.active;
    job->owner = priv->job.owner;

    virCHDomainObjResetJob(priv);
    return 0;
}

static void
virCHDomainObjFreeJob(virCHDomainObjPrivatePtr priv)
{
    ignore_value(virCondDestroy(&priv->job.cond));
}

/*
 * obj must be locked before calling, virCHDriverPtr must NOT be locked
 *
 * This must be called by anything that will change the VM state
 * in any way
 *
 * Upon successful return, the object will have its ref count increased.
 * Successful calls must be followed by EndJob eventually.
 */
int
virCHDomainObjBeginJob(virDomainObjPtr obj, enum virCHDomainJob job)
{
    virCHDomainObjPrivatePtr priv = obj->privateData;
    unsigned long long now;
    unsigned long long then;

    if (virTimeMillisNow(&now) < 0)
        return -1;
    then = now + CH_JOB_WAIT_TIME;

    while (priv->job.active) {
        VIR_DEBUG("Wait normal job condition for starting job: %s",
                  virCHDomainJobTypeToString(job));
        if (virCondWaitUntil(&priv->job.cond, &obj->parent.lock, then) < 0)
            goto error;
    }

    virCHDomainObjResetJob(priv);

    VIR_DEBUG("Starting job: %s", virCHDomainJobTypeToString(job));
    priv->job.active = job;
    priv->job.owner = virThreadSelfID();

    return 0;

 error:
    VIR_WARN("Cannot start job (%s) for domain %s;"
             " current job is (%s) owned by (%d)",
             virCHDomainJobTypeToString(job),
             obj->def->name,
             virCHDomainJobTypeToString(priv->job.active),
             priv->job.owner);

    if (errno == ETIMEDOUT)
        virReportError(VIR_ERR_OPERATION_TIMEOUT,
                       "%s", _("cannot acquire state change lock"));
    else
        virReportSystemError(errno,
                             "%s", _("cannot acquire job mutex"));
    return -1;
}

/*
 * obj must be locked and have a reference before calling
 *
 * To be called after completing the work associated with the
 * earlier virCHDomainBeginJob() call
 */
void
virCHDomainObjEndJob(virDomainObjPtr obj)
{
    virCHDomainObjPrivatePtr priv = obj->privateData;
    enum virCHDomainJob job = priv->job.active;

    VIR_DEBUG("Stopping job: %s",
              virCHDomainJobTypeToString(job));

    virCHDomainObjResetJob(priv);
    virCondSignal(&priv->job.cond);
}

/**
 * virCHDomainRemoveInactive:
 *
 * The caller must hold a lock to the vm.
 */
void
virCHDomainRemoveInactive(virCHDriverPtr driver,
                          virDomainObjPtr vm)
{
    if (vm->persistent) {
        /* Short-circuit, we don't want to remove a persistent domain */
        return;
    }

    virDomainObjListRemove(driver->domains, vm);
}

/**
 * virCHDomainRemoveInactiveLocked:
 *
 * The caller must hold a lock to the vm and must hold the
 * lock on driver->domains in order to call the remove obj
 * from locked list method.
 */
static void
virCHDomainRemoveInactiveLocked(virCHDriverPtr driver,
                                virDomainObjPtr vm)
{
    if (vm->persistent) {
        /* Short-circuit, we don't want to remove a persistent domain */
        return;
    }

    virDomainObjListRemoveLocked(driver->domains, vm);
}

/**
 * virCHDomainRemoveInactiveJob:
 *
 * Just like virCHDomainRemoveInactive but it tries to grab a
 * CH_JOB_MODIFY first. Even though it doesn't succeed in
 * grabbing the job the control carries with
 * virCHDomainRemoveInactive call.
 */
void
virCHDomainRemoveInactiveJob(virCHDriverPtr driver,
                             virDomainObjPtr vm)
{
    bool haveJob;

    haveJob = virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) >= 0;

    virCHDomainRemoveInactive(driver, vm);

    if (haveJob)
        virCHDomainObjEndJob(vm);
}

/**
 * virCHDomainRemoveInactiveJobLocked:
 *
 * Similar to virCHomainRemoveInactiveJob,
 * except that the caller must also hold the lock @driver->domains
 */
void
virCHDomainRemoveInactiveJobLocked(virCHDriverPtr driver,
                                   virDomainObjPtr vm)
{
    bool haveJob;

    haveJob = virCHDomainObjBeginJob(vm, CH_JOB_MODIFY) >= 0;

    virCHDomainRemoveInactiveLocked(driver, vm);

    if (haveJob)
        virCHDomainObjEndJob(vm);
}

static void *
virCHDomainObjPrivateAlloc(void *opaque)
{
    virCHDomainObjPrivatePtr priv;

    priv = g_new0(virCHDomainObjPrivate, 1);

    if (virCHDomainObjInitJob(priv) < 0) {
        g_free(priv);
        return NULL;
    }

    if (!(priv->devs = virChrdevAlloc())) {
        g_free(priv);
        return NULL;
    }

    priv->driver = opaque;

    return priv;
}

static void
virCHDomainObjPrivateFree(void *data)
{
    virCHDomainObjPrivatePtr priv = data;

    virChrdevFree(priv->devs);
    virCHDomainObjFreeJob(priv);
    if (priv->pidfile)
        g_free(priv->pidfile);
    g_free(priv);
}

static int
virCHDomainObjPrivateXMLFormat(virBufferPtr buf,
                               virDomainObjPtr vm)
{
    virCHDomainObjPrivatePtr priv = vm->privateData;
    virBufferAsprintf(buf, "<init pid='%lld'/>\n",
                      (long long)priv->initpid);

    return 0;
}

static int
virCHDomainObjPrivateXMLParse(xmlXPathContextPtr ctxt,
                              virDomainObjPtr vm,
                              virDomainDefParserConfigPtr config G_GNUC_UNUSED)
{
    virCHDomainObjPrivatePtr priv = vm->privateData;
    long long thepid;

    if (virXPathLongLong("string(./init[1]/@pid)", ctxt, &thepid) < 0) {
        VIR_WARN("Failed to load init pid from state %s",
                 virGetLastErrorMessage());
        priv->initpid = 0;
    } else {
        priv->initpid = thepid;
    }

    return 0;
}

static virClassPtr virCHDomainVcpuPrivateClass;
static void virCHDomainVcpuPrivateDispose(void *obj);

static int
virCHDomainVcpuPrivateOnceInit(void)
{
    if (!VIR_CLASS_NEW(virCHDomainVcpuPrivate, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virCHDomainVcpuPrivate);

static virObjectPtr
virCHDomainVcpuPrivateNew(void)
{
    virCHDomainVcpuPrivatePtr priv;

    if (virCHDomainVcpuPrivateInitialize() < 0)
        return NULL;

    if (!(priv = virObjectNew(virCHDomainVcpuPrivateClass)))
        return NULL;

    return (virObjectPtr) priv;
}


static void
virCHDomainVcpuPrivateDispose(void *obj)
{
    virCHDomainVcpuPrivatePtr priv = obj;

    priv->tid = 0;

    return;
}



virDomainXMLPrivateDataCallbacks virCHDriverPrivateDataCallbacks = {
    .alloc = virCHDomainObjPrivateAlloc,
    .free = virCHDomainObjPrivateFree,
    .format = virCHDomainObjPrivateXMLFormat,
    .parse  = virCHDomainObjPrivateXMLParse,
    .vcpuNew = virCHDomainVcpuPrivateNew,
};

static int
virCHDomainDefPostParse(virDomainDefPtr def,
                        unsigned int parseFlags G_GNUC_UNUSED,
                        void *opaque,
                        void *parseOpaque G_GNUC_UNUSED)
{
    virCHDriverPtr driver = opaque;
    g_autoptr(virCaps) caps = virCHDriverGetCapabilities(driver, false);
    if (!caps)
        return -1;
    if (!virCapabilitiesDomainSupported(caps, def->os.type,
                                        def->os.arch,
                                        def->virtType))
        return -1;

    return 0;
}

virDomainDefParserConfig virCHDriverDomainDefParserConfig = {
    .domainPostParseCallback = virCHDomainDefPostParse,
};

virCHMonitorPtr
virCHDomainGetMonitor(virDomainObjPtr vm)
{
    return CH_DOMAIN_PRIVATE(vm)->monitor;
}

pid_t
virCHDomainGetVcpuPid(virDomainObjPtr vm,
                     unsigned int vcpuid)
{
    virDomainVcpuDefPtr vcpu = virDomainDefGetVcpu(vm->def, vcpuid);
    return CH_DOMAIN_VCPU_PRIVATE(vcpu)->tid;
}

bool
virCHDomainHasVcpuPids(virDomainObjPtr vm)
{
    size_t i;
    size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
    virDomainVcpuDefPtr vcpu;

    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vm->def, i);

        if (CH_DOMAIN_VCPU_PRIVATE(vcpu)->tid > 0)
            return true;
    }

    return false;
}

char *virCHDomainGetMachineName(virDomainObjPtr vm)
{
    virCHDomainObjPrivatePtr priv = CH_DOMAIN_PRIVATE(vm);
    virCHDriverPtr driver = priv->driver;
    char *ret = NULL;

    if (vm->pid > 0) {
        ret = virSystemdGetMachineNameByPID(vm->pid);
        if (!ret)
            virResetLastError();
    }

    if (!ret)
        ret = virDomainDriverGenerateMachineName("ch",
                                                 driver->embeddedRoot,
                                                 vm->def->id, vm->def->name,
                                                 driver->privileged);

    return ret;
}

/**
 * virCHDomainObjFromDomain:
 * @domain: Domain pointer that has to be looked up
 *
 * This function looks up @domain and returns the appropriate virDomainObjPtr
 * that has to be released by calling virDomainObjEndAPI().
 *
 * Returns the domain object with incremented reference counter which is locked
 * on success, NULL otherwise.
 */
virDomainObjPtr
virCHDomainObjFromDomain(virDomainPtr domain)
{
    virDomainObjPtr vm;
    virCHDriverPtr driver = domain->conn->privateData;
    char uuidstr[VIR_UUID_STRING_BUFLEN];

    vm = virDomainObjListFindByUUID(driver->domains, domain->uuid);
    if (!vm) {
        virUUIDFormat(domain->uuid, uuidstr);
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching uuid '%s' (%s)"),
                       uuidstr, domain->name);
        return NULL;
    }

    return vm;
}