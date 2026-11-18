/*
 * Copyright The tarpm Project Authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/**
 * @file
 * @brief UTC time conversions that do not depend on the width of time_t.
 *
 * The v4 format and the v6 format both record times in 32 bit tags,
 * so values run through 2106.  time_t is 32 bits on some of the
 * systems we build on, which cannot hold anything past 2038.  We do
 * the civil date arithmetic ourselves so every system reads and
 * writes the same range.
 */

#ifndef _TARPM_EPOCHTIME_H
#define _TARPM_EPOCHTIME_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Seconds in a day */
#define TARPM_SECS_PER_DAY INT64_C(86400)

/* Days in the 400 year Gregorian era */
#define TARPM_DAYS_PER_ERA INT64_C(146097)

/* Days from 0000-03-01 to 1970-01-01 */
#define TARPM_EPOCH_SHIFT INT64_C(719468)

/**
 * @brief Days from 1970-01-01 to the given civil date.
 *
 * We shift the year to start in March so the leap day falls at the
 * end of it and the month length pattern repeats.
 *
 * @param year The year, such as 2026.
 * @param month The month from 1 to 12.
 * @param day The day of the month from 1 to 31.
 * @return Days since 1970-01-01, negative for earlier dates.
 */
static inline int64_t
tarpm_days_from_civil(int64_t year, unsigned int month, unsigned int day)
{
    int64_t era = 0;
    unsigned int yoe = 0;
    unsigned int doy = 0;
    unsigned int doe = 0;

    if (month <= 2) {
        year -= 1;
    }

    if (year >= 0) {
        era = year / 400;
    } else {
        era = (year - 399) / 400;
    }

    /* year of the era, which runs from 0 to 399 */
    yoe = (unsigned int) (year - era * 400);

    /* day of the year counting from March */
    if (month > 2) {
        doy = (153U * (month - 3U) + 2U) / 5U + day - 1U;
    } else {
        doy = (153U * (month + 9U) + 2U) / 5U + day - 1U;
    }

    /* day of the era, which runs from 0 to 146096 */
    doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;

    return era * TARPM_DAYS_PER_ERA + (int64_t) doe - TARPM_EPOCH_SHIFT;
}

/**
 * @brief Civil date for the given number of days since 1970-01-01.
 *
 * This is the inverse of tarpm_days_from_civil().
 *
 * @param days Days since 1970-01-01, negative for earlier dates.
 * @param year Where to write the year.
 * @param month Where to write the month from 1 to 12.
 * @param day Where to write the day of the month from 1 to 31.
 */
static inline void
tarpm_civil_from_days(int64_t days, int64_t *year, unsigned int *month, unsigned int *day)
{
    int64_t era = 0;
    unsigned int doe = 0;
    unsigned int yoe = 0;
    unsigned int doy = 0;
    unsigned int mp = 0;

    days += TARPM_EPOCH_SHIFT;

    if (days >= 0) {
        era = days / TARPM_DAYS_PER_ERA;
    } else {
        era = (days - (TARPM_DAYS_PER_ERA - 1)) / TARPM_DAYS_PER_ERA;
    }

    doe = (unsigned int) (days - era * TARPM_DAYS_PER_ERA);
    yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    mp = (5U * doy + 2U) / 153U;
    *day = doy - (153U * mp + 2U) / 5U + 1U;

    /* the shifted year starts in March, so move the month back */
    if (mp < 10U) {
        *month = mp + 3U;
    } else {
        *month = mp - 9U;
    }

    *year = (int64_t) yoe + era * 400;

    if (*month <= 2U) {
        *year += 1;
    }

    return;
}

/**
 * @brief Convert a broken down UTC time to seconds since the epoch.
 *
 * This works like timegm() but is not limited by the width of time_t.
 * The caller is expected to pass a normalized struct tm, which is
 * what strptime() writes.
 *
 * @param tm_info The broken down UTC time.
 * @return Seconds since 1970-01-01T00:00:00Z, negative for earlier times.
 */
static inline int64_t
tarpm_timegm(const struct tm *tm_info)
{
    int64_t days = 0;

    if (tm_info == NULL) {
        return 0;
    }

    days = tarpm_days_from_civil((int64_t) tm_info->tm_year + 1900,
                                 (unsigned int) (tm_info->tm_mon + 1),
                                 (unsigned int) tm_info->tm_mday);

    return (days * TARPM_SECS_PER_DAY)
           + ((int64_t) tm_info->tm_hour * 3600)
           + ((int64_t) tm_info->tm_min * 60)
           + (int64_t) tm_info->tm_sec;
}

/**
 * @brief Convert seconds since the epoch to a broken down UTC time.
 *
 * This works like gmtime_r() but is not limited by the width of
 * time_t.
 *
 * @param seconds Seconds since 1970-01-01T00:00:00Z.
 * @param result Where to write the broken down UTC time.
 * @return True on success, false if the year will not fit in struct tm.
 */
static inline bool
tarpm_gmtime(int64_t seconds, struct tm *result)
{
    int64_t days = 0;
    int64_t rem = 0;
    int64_t year = 0;
    int64_t wday = 0;
    unsigned int month = 0;
    unsigned int day = 0;

    if (result == NULL) {
        return false;
    }

    days = seconds / TARPM_SECS_PER_DAY;
    rem = seconds % TARPM_SECS_PER_DAY;

    /* C truncates toward zero, so borrow a day for negative times */
    if (rem < 0) {
        rem += TARPM_SECS_PER_DAY;
        days -= 1;
    }

    tarpm_civil_from_days(days, &year, &month, &day);

    if (year < ((int64_t) INT_MIN + 1900) || year > ((int64_t) INT_MAX + 1900)) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->tm_year = (int) (year - 1900);
    result->tm_mon = (int) month - 1;
    result->tm_mday = (int) day;
    result->tm_hour = (int) (rem / 3600);
    result->tm_min = (int) ((rem % 3600) / 60);
    result->tm_sec = (int) (rem % 60);

    /* 1970-01-01 was a Thursday */
    wday = (days + 4) % 7;

    if (wday < 0) {
        wday += 7;
    }

    result->tm_wday = (int) wday;
    result->tm_yday = (int) (days - tarpm_days_from_civil(year, 1, 1));
    result->tm_isdst = 0;

    return true;
}

#endif /* _TARPM_EPOCHTIME_H */
