/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdbool.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>

int
init_librpm(void)
{
    static bool initialized = false;

    if (initialized) {
        return RPMRC_OK;
    }

    rpmFreeMacros(rpmGlobalMacroContext);
    rpmFreeRpmrc();
    initialized = true;

    return 0;
}
