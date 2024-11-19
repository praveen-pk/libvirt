/*
 * ch_logcontext.h: CH log context
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

#include <glib-object.h>
#include "ch_conf.h"
#include "logging/log_manager.h"

#define CH_TYPE_LOG_CONTEXT ch_log_context_get_type()
G_DECLARE_FINAL_TYPE(chLogContext, ch_log_context, CH, LOG_CONTEXT, GObject);

chLogContext *chLogContextNew(virCHDriver *driver,
                                  virDomainObj *vm,
                                  const char *basename);
int chLogContextWrite(chLogContext *ctxt,
                        const char *fmt, ...) G_GNUC_PRINTF(2, 3);
ssize_t chLogContextRead(chLogContext *ctxt,
                           char **msg);
int chLogContextReadFiltered(chLogContext *ctxt,
                               char **msg,
                               size_t max);
int chLogContextGetWriteFD(chLogContext *ctxt);
void chLogContextMarkPosition(chLogContext *ctxt);

virLogManager *chLogContextGetManager(chLogContext *ctxt);
