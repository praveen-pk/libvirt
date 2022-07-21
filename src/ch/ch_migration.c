/*
 * ch_migration.c: Cloud Hypervisor migration support
 *
 * Copyright (C) 2021 Wei Liu <liuwe@microsoft.com>
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

#include "ch_conf.h"
#include "ch_domain.h"
#include "datatypes.h"
#include "virlog.h"
#include "virerror.h"
#include "viralloc.h"
#include "ch_migration.h"
#include "ch_process.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_migration");

typedef struct _chMigrationCookie chMigrationCookie;
typedef chMigrationCookie *chMigrationCookiePtr;
struct _chMigrationCookie {
	/* Nothing for now */
    int unused;
};

static void
chMigrationCookieFree(chMigrationCookiePtr mig)
{
    if (!mig)
        return;

    VIR_FREE(mig);
}


static chMigrationCookiePtr
chMigrationCookieNew(virDomainObj *dom)
{
    chMigrationCookie *mig = NULL;

    mig = g_new0(chMigrationCookie, 1);


    /* Nothing to do */
    (void)dom;

    return mig;

}

static int
chMigrationBakeCookie(chMigrationCookiePtr mig, char **cookieout,
                      int *cookieoutlen)
{
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    if (!cookieout || !cookieoutlen)
        return 0;

    /* Nothing to do */
    (void)mig;

    *cookieout = NULL;
    *cookieoutlen = 0;

    VIR_DEBUG("cookielen=%d cookie=%s", *cookieoutlen, *cookieout);

    return 0;
}

static int
chMigrationEatCookie(const char *cookiein, int cookieinlen,
                     chMigrationCookiePtr *migout)
{
    chMigrationCookie *mig = NULL;

    mig = g_new0(chMigrationCookie, 1);

    /* Nothing to do */
    (void)cookiein;
    (void)cookieinlen;
    *migout = mig;

    return 0;
}

static bool
chDomainMigrationIsAllowed(virDomainDef *def)
{
    if (def->nhostdevs > 0) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                _("domain has assigned host devices"));
        return false;
    }

    return true;
}

char *
chDomainMigrationSrcBegin(virConnectPtr conn,
                          virDomainObj *vm,
                          const char *xmlin,
                          char **cookieout,
                          int *cookieoutlen)
{
    virCHDriver *driver = conn->privateData;
    char *xml = NULL;
    chMigrationCookiePtr mig = NULL;
    virDomainDef *tmpdef = NULL, *def;

    //if (virCHDomainObjBeginJob(vm, VIR_JOB_MODIFY) < 0)
    //    goto cleanup;

    if (!(mig = chMigrationCookieNew(vm)))
        goto endjob;

    if (chMigrationBakeCookie(mig, cookieout, cookieoutlen) < 0)
        goto endjob;

    if (xmlin) {
        if (!(tmpdef = virDomainDefParseString(xmlin,
                        driver->xmlopt,
                        NULL,
                        VIR_DOMAIN_DEF_PARSE_INACTIVE |
                        VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE)))
            goto endjob;

        def = tmpdef;
    } else {
        def = vm->def;
    }

    if (!chDomainMigrationIsAllowed(def))
        goto endjob;

    xml = virDomainDefFormat(def, driver->xmlopt, VIR_DOMAIN_DEF_FORMAT_SECURE);

endjob:
    virDomainObjEndJob(vm);

    chMigrationCookieFree(mig);
    virDomainDefFree(tmpdef);
    return xml;
}

virDomainDef *
chDomainMigrationAnyPrepareDef(virCHDriver *driver,
                               const char *dom_xml,
                               const char *dname,
                               char **origname)
{
    virDomainDef *def;
    char *name = NULL;

    if (!dom_xml) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("no domain XML passed"));
        return NULL;
    }

    if (!(def = virDomainDefParseString(dom_xml, driver->xmlopt,
                                        NULL,
                                        VIR_DOMAIN_DEF_PARSE_INACTIVE |
                                        VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE)))
        goto cleanup;

    if (dname) {
        name = def->name;
        def->name = g_strdup(dname);
    }

 cleanup:
    if (def && origname)
        *origname = name;
    else
        VIR_FREE(name);
    return def;
}

int
chDomainMigrationDstPrepare(virConnectPtr dconn,
                            virDomainDef **def,
                            const char *cookiein,
                            int cookieinlen,
                            char **cookieout,
                            int *cookieoutlen,
                            const char *uri_in,
                            char **uri_out,
                            const char *origname)
{
    virCHDriver *driver = dconn->privateData;
    chMigrationCookiePtr mig = NULL;
    virDomainObj *vm = NULL;

    if (chMigrationEatCookie(cookiein, cookieinlen, &mig) < 0)
        goto cleanup;

    if (!(vm = virDomainObjListAdd(driver->domains, def,
                    driver->xmlopt,
                    VIR_DOMAIN_OBJ_LIST_ADD_LIVE |
                    VIR_DOMAIN_OBJ_LIST_ADD_CHECK_LIVE,
                    NULL)))
        goto cleanup;

    //if (virCHDomainObjBeginJob(vm, VIR_JOB_MODIFY) < 0)
    //    goto cleanup;

    (void) def;
    (void) uri_in;
    (void) uri_out;
    (void) cookieout;
    (void) cookieoutlen;
    (void) origname;

    if (virCHProcessStart(driver, vm, 0, VIR_DOMAIN_START_PAUSED) < 0)
        goto cleanup;

   // virCHDomainObjEndJob(vm);

    chMigrationCookieFree(mig);

    return 0;

cleanup:
    /* Remove virDomainObj from domain list */
    if (vm)
        virDomainObjListRemove(driver->domains, vm);
    chMigrationCookieFree(mig);

    return -1;
}


int
chDomainMigrationSrcPerform(virCHDriver *driver,
                            virDomainObj *vm,
                            const char *dom_xml,
                            const char *dconnuri,
                            const char *uri_str,
                            const char *dname,
                            unsigned int flags)
{
    (void) driver;
    (void) vm;
    (void) dom_xml;
    (void) dconnuri;
    (void) uri_str;
    (void) dname;
    (void) flags;

    return -1;
}

int
chDomainMigrationSrcConfirm(virCHDriver *driver,
                            virDomainObj *vm,
                            unsigned int flags,
                            int cancelled)
{
    (void) driver;
    (void) vm;
    (void) flags;
    (void) cancelled;

    return -1;
}
