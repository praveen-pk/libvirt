/*
 * Copyright Intel Corp. 2020-2021
 *
 * ch_domain.c: Domain manager functions for Cloud-Hypervisor driver
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
#include "virsystemd.h"
#include "virtime.h"

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
virCHDomainObjInitJob(virCHDomainObjPrivate *priv)
{
    memset(&priv->job, 0, sizeof(priv->job));

    if (virCondInit(&priv->job.cond) < 0)
        return -1;

    return 0;
}

static void
virCHDomainObjResetJob(virCHDomainObjPrivate *priv)
{
    struct virCHDomainJobObj *job = &priv->job;

    job->active = CH_JOB_NONE;
    job->owner = 0;
}

static void
virCHDomainObjFreeJob(virCHDomainObjPrivate *priv)
{
    ignore_value(virCondDestroy(&priv->job.cond));
}

/*
 * obj must be locked before calling, virCHDriver must NOT be locked
 *
 * This must be called by anything that will change the VM state
 * in any way
 *
 * Upon successful return, the object will have its ref count increased.
 * Successful calls must be followed by EndJob eventually.
 */
int
virCHDomainObjBeginJob(virDomainObj *obj, enum virCHDomainJob job)
{
    virCHDomainObjPrivate *priv = obj->privateData;
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
virCHDomainObjEndJob(virDomainObj *obj)
{
    virCHDomainObjPrivate *priv = obj->privateData;
    enum virCHDomainJob job = priv->job.active;

    VIR_DEBUG("Stopping job: %s",
              virCHDomainJobTypeToString(job));

    virCHDomainObjResetJob(priv);
    virCondSignal(&priv->job.cond);
}

static void *
virCHDomainObjPrivateAlloc(void *opaque G_GNUC_UNUSED)
{
    virCHDomainObjPrivate *priv;

    priv = g_new0(virCHDomainObjPrivate, 1);

    if (virCHDomainObjInitJob(priv) < 0) {
        g_free(priv);
        return NULL;
    }

    return priv;
}

static void
virCHDomainObjPrivateFree(void *data)
{
    virCHDomainObjPrivate *priv = data;

    virCHDomainObjFreeJob(priv);
    g_free(priv);
}

static int
virCHDomainDefPostParseBasic(virDomainDef *def,
                             void *opaque G_GNUC_UNUSED)
{
    /* check for emulator and create a default one if needed */
    if (!def->emulator) {
        if (!(def->emulator = g_find_program_in_path(CH_CMD))) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("No emulator found for cloud-hypervisor"));
            return 1;
        }
    }

    return 0;
}

static virClass *virCHDomainVcpuPrivateClass;
static void virCHDomainVcpuPrivateDispose(void *obj);

static int virCHDomainVcpuPrivateOnceInit(void) {
  if (!VIR_CLASS_NEW(virCHDomainVcpuPrivate, virClassForObject()))
    return -1;

  return 0;
}

VIR_ONCE_GLOBAL_INIT(virCHDomainVcpuPrivate);

static virObject *virCHDomainVcpuPrivateNew(void) {
  virCHDomainVcpuPrivate *priv;

  if (virCHDomainVcpuPrivateInitialize() < 0)
    return NULL;

  if (!(priv = virObjectNew(virCHDomainVcpuPrivateClass)))
    return NULL;

  return (virObject *)priv;
}

static void virCHDomainVcpuPrivateDispose(void *obj) {
  virCHDomainVcpuPrivate *priv = obj;

  priv->tid = 0;

  return;
}

virDomainXMLPrivateDataCallbacks virCHDriverPrivateDataCallbacks = {
    .alloc = virCHDomainObjPrivateAlloc,
    .free = virCHDomainObjPrivateFree,
    .vcpuNew = virCHDomainVcpuPrivateNew,
};

static int
virCHDomainDefPostParse(virDomainDef *def,
                        unsigned int parseFlags G_GNUC_UNUSED,
                        void *opaque,
                        void *parseOpaque G_GNUC_UNUSED)
{
    virCHDriver *driver = opaque;
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
    .domainPostParseBasicCallback = virCHDomainDefPostParseBasic,
    .domainPostParseCallback = virCHDomainDefPostParse,
};

virCHMonitor *virCHDomainGetMonitor(virDomainObj *vm) {
  return CH_DOMAIN_PRIVATE(vm)->monitor;
}

int virCHDomainRefreshThreadInfo(virDomainObj *vm) {
  size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
  virCHMonitorThreadInfo *info = NULL;
  size_t nthreads, ncpus = 0;
  size_t i;

  nthreads = virCHMonitorGetThreadInfo(virCHDomainGetMonitor(vm), true, &info);

  for (i = 0; i < nthreads; i++) {
    virCHDomainVcpuPrivate *vcpupriv;
    virDomainVcpuDef *vcpu;
    virCHMonitorCPUInfo *vcpuInfo;

    if (info[i].type != virCHThreadTypeVcpu)
      continue;

    // TODO: hotplug support
    vcpuInfo = &info[i].vcpuInfo;
    vcpu = virDomainDefGetVcpu(vm->def, vcpuInfo->cpuid);
    vcpupriv = CH_DOMAIN_VCPU_PRIVATE(vcpu);
    vcpupriv->tid = vcpuInfo->tid;
    ncpus++;
  }

  // TODO: Remove the warning when hotplug is implemented.
  if (ncpus != maxvcpus)
    VIR_WARN("Mismatch in the number of cpus, expected: %ld, actual: %ld",
             maxvcpus, ncpus);

  return 0;
}

pid_t virCHDomainGetVcpuPid(virDomainObj *vm, unsigned int vcpuid) {
  virDomainVcpuDef *vcpu = virDomainDefGetVcpu(vm->def, vcpuid);
  return CH_DOMAIN_VCPU_PRIVATE(vcpu)->tid;
}

bool virCHDomainHasVcpuPids(virDomainObj *vm) {
  size_t i;
  size_t maxvcpus = virDomainDefGetVcpusMax(vm->def);
  virDomainVcpuDef *vcpu;

  for (i = 0; i < maxvcpus; i++) {
    vcpu = virDomainDefGetVcpu(vm->def, i);

    if (CH_DOMAIN_VCPU_PRIVATE(vcpu)->tid > 0)
      return true;
  }

  return false;
}

char *virCHDomainGetMachineName(virDomainObj *vm) {
  virCHDomainObjPrivate *priv = CH_DOMAIN_PRIVATE(vm);
  virCHDriver *driver = priv->driver;
  char *ret = NULL;

  if (vm->pid > 0) {
    ret = virSystemdGetMachineNameByPID(vm->pid);
    if (!ret)
      virResetLastError();
  }

  if (!ret)
    ret = virDomainDriverGenerateMachineName("ch", driver->embeddedRoot,
                                             vm->def->id, vm->def->name,
                                             driver->privileged);

  return ret;
}

/**
 * virCHDomainObjFromDomain:
 * @domain: Domain pointer that has to be looked up
 *
 * This function looks up @domain and returns the appropriate virDomainObj
 * that has to be released by calling virDomainObjEndAPI().
 *
 * Returns the domain object with incremented reference counter which is locked
 * on success, NULL otherwise.
 */
virDomainObj *virCHDomainObjFromDomain(virDomain *domain) {
  virDomainObj *vm;
  virCHDriver *driver = domain->conn->privateData;
  char uuidstr[VIR_UUID_STRING_BUFLEN];

  vm = virDomainObjListFindByUUID(driver->domains, domain->uuid);
  if (!vm) {
    virUUIDFormat(domain->uuid, uuidstr);
    virReportError(VIR_ERR_NO_DOMAIN,
                   _("no domain with matching uuid '%s' (%s)"), uuidstr,
                   domain->name);
    return NULL;
  }

  return vm;
}
