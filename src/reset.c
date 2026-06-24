/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdbool.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>

void
reset_librpm(void)
{
    static bool reset = false;

    if (reset) {
        return;
    }

    rpmFreeMacros(rpmGlobalMacroContext);
    rpmFreeRpmrc();
    reset = true;

    return;
}
