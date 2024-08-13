/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TARPM_I18N_H
#define _TARPM_I18N_H

#ifdef GETTEXT_DOMAIN
#include <libintl.h>
#include <locale.h>

#define _(MSGID) gettext((MSGID))
#define N_(MSGID, MSGID_PLURAL, N) ngettext((MSGID), (MSGID_PLURAL), (N))
#else
#define _(MSGID) (MSGID)
#define N_(MSGID, MSGID_PLURAL, N) ((MSGID_PLURAL))
#endif

#endif /* _TARPM_I18N_H */
