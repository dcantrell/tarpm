/*
 * Copyright The rpminspect Project Authors
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file
 * @brief Translation macros.
 *
 * These wrap gettext when the build turned translations on and fall
 * back to the untranslated string when it did not.
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
#define N_(MSGID, MSGID_PLURAL, N) ((MSGID))
#endif

#endif /* _TARPM_I18N_H */
