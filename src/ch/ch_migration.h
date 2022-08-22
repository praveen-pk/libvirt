/*
 * ch_migration.h: Cloud Hypervisor migration support
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

#pragma once

#include "ch_conf.h"
#include "vircommand.h"
#include "virportallocator.h"
#include "virutil.h"


#define CH_MIGRATION_FLAGS \
    (VIR_MIGRATE_LIVE | \
     VIR_MIGRATE_PERSIST_DEST | \
     VIR_MIGRATE_UNDEFINE_SOURCE | \
     VIR_MIGRATE_PAUSED)

#define CH_MIGRATION_PARAMETERS \
    VIR_MIGRATE_PARAM_URI,              VIR_TYPED_PARAM_STRING, \
    VIR_MIGRATE_PARAM_DEST_NAME,        VIR_TYPED_PARAM_STRING, \
    VIR_MIGRATE_PARAM_DEST_XML,         VIR_TYPED_PARAM_STRING, \
    VIR_MIGRATE_PARAM_PERSIST_XML,      VIR_TYPED_PARAM_STRING, \
    NULL

char *
chDomainMigrationSrcBegin(virConnectPtr conn,
                          virDomainObj *vm,
                          const char *xmlin,
                          char **cookieout,
                          int *cookieoutlen);

virDomainDef *
chDomainMigrationAnyPrepareDef(virCHDriver *driver,
                               const char *dom_xml,
                               const char *dname,
                               char **origname);

int
chDomainMigrationDstPrepare(virConnectPtr dconn,
                            virDomainDef **def,
                            const char *cookiein,
                            int cookieinlen,
                            char **cookieout,
                            int *cookieoutlen,
                            const char *uri_in,
                            char **uri_out,
                            const char *origname);

int
chDomainMigrationSrcPerform(virCHDriver *driver,
                            virDomainObj *vm,
                            const char *dom_xml,
                            const char *dconnuri,
                            const char *uri_str,
                            const char *dname,
                            unsigned int flags);

virDomainPtr
chDomainMigrationDstFinish(virConnectPtr dconn,
                           virDomainObj *vm,
                           unsigned int flags,
                           int cancelled);

int
chDomainMigrationSrcConfirm(virCHDriver *driver,
                            virDomainObj *vm,
                            unsigned int flags,
                            int cancelled);
