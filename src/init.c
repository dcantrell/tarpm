/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: Apache-2.0
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

    rpmFreeMacros(NULL);
    rpmFreeRpmrc();
    initialized = true;

    return 0;
}
