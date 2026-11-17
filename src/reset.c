/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file
 * @brief Dropping the librpm state we picked up.
 *
 * librpm reads the system macro files the first time we use it.  We
 * only need its tag tables, so let the rest go.
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
