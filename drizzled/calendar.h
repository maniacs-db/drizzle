/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file 
 *
 * Structures and functions for:
 *
 * Calculating day number in Gregorian and Julian proleptic calendars.
 * Converting between day numbers and dates in the calendars.
 * Converting between different calendars.
 * Calculating differences between dates.
 *
 * Works used in research:
 *
 * @cite "Calendrical Calculations", Dershowitz and Reingold
 * @cite ISO 8601 http://en.wikipedia.org/wiki/ISO_8601
 * @cite http://www.ddj.com/hpc-high-performance-computing/197006254 
 * @cite http://en.wikipedia.org/wiki/Julian_day#Calculation
 */

#ifndef DRIZZLED_CALENDAR_H
#define DRIZZLED_CALENDAR_H

#define JULIAN_DAY_NUMBER_AT_ABSOLUTE_DAY_ONE INT64_C(1721425)

#define DAYS_IN_NORMAL_YEAR INT32_C(365)
#define DAYS_IN_LEAP_YEAR INT32_C(366)

#define GREGORIAN_START_YEAR 1582
#define GREGORIAN_START_MONTH 10
#define GREGORIAN_START_DAY 15

#define UNIX_EPOCH_MAX_YEARS 2038
#define UNIX_EPOCH_MIN_YEARS 1970

/**
 * The following constants define the system of calculating the number
 * of days in various periods of time in the Gregorian calendar.
 *
 * Leap years (years containing 366 days) occur:
 *
 * - When the year is evenly divisible by 4
 * - If the year is evenly divisible by 100, it must also
 *   be evenly divisible by 400.
 */
#define GREGORIAN_DAYS_IN_400_YEARS UINT32_C(146097)
#define GREGORIAN_DAYS_IN_100_YEARS UINT32_C(36524)
#define GREGORIAN_DAYS_IN_4_YEARS   UINT32_C(1461)

/**
 * Simple macro returning whether the supplied year
 * is a leap year in the supplied calendar.
 *
 * @param Year to evaluate
 * @param Calendar to use
 */
#define IS_LEAP_YEAR(y, c) (days_in_year((y),(c)) == 366)

/**
 * Simple macro returning whether the supplied year
 * is a leap year in the Gregorian proleptic calendar.
 */
#define IS_GREGORIAN_LEAP_YEAR(y) (days_in_year_gregorian((y)) == 366)

/**
 * Simple macro returning whether the supplied year
 * is a leap year in the Julian proleptic calendar.
 */
#define IS_JULIAN_LEAP_YEAR(y) (days_in_year_julian((y)) == 366)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Different calendars supported by the temporal library
 */
enum calendar
{
  GREGORIAN= 1
, JULIAN= 2
, HEBREW= 3
, ISLAM= 4
};

/**
 * Calculates the Julian Day Number from the year, month 
 * and day supplied for a Gregorian Proleptic calendar date.
 *
 * @note
 *
 * Year month and day values are assumed to be valid.  This 
 * method does no bounds checking or validation.
 *
 * @param Year of date
 * @param Month of date
 * @param Day of date
 */
int64_t julian_day_number_from_gregorian_date(uint32_t year, uint32_t month, uint32_t day);

/**
 * Translates an absolute day number to a 
 * Julian day number.
 *
 * @param The absolute day number
 */
int64_t absolute_day_number_to_julian_day_number(int64_t absolute_day);

/**
 * Translates a Julian day number to an 
 * absolute day number.  
 *
 * @param The Julian day number
 */
int64_t julian_day_number_to_absolute_day_number(int64_t julian_day);

/**
 * Given a supplied Julian Day Number, populates a year, month, and day
 * with the date in the Gregorian Proleptic calendar which corresponds to
 * the given Julian Day Number.
 *
 * @param Julian Day Number
 * @param Pointer to year to populate
 * @param Pointer to month to populate
 * @param Pointer to the day to populate
 */
void gregorian_date_from_julian_day_number(int64_t julian_day
                                         , uint32_t *year_out
                                         , uint32_t *month_out
                                         , uint32_t *day_out);

/**
 * Given a supplied Absolute Day Number, populates a year, month, and day
 * with the date in the Gregorian Proleptic calendar which corresponds to
 * the given Absolute Day Number.
 *
 * @param Absolute Day Number
 * @param Pointer to year to populate
 * @param Pointer to month to populate
 * @param Pointer to the day to populate
 */
void gregorian_date_from_absolute_day_number(int64_t absolute_day
                                           , uint32_t *year_out
                                           , uint32_t *month_out
                                           , uint32_t *day_out);

/**
 * Returns the number of days in a particular year.
 *
 * @param year to evaluate
 * @param calendar to use
 */
uint32_t days_in_year(uint32_t year, enum calendar calendar);

/**
 * Returns the number of days in a particular Gregorian Proleptic calendar year.
 *
 * @param year to evaluate
 */
uint32_t days_in_year_gregorian(uint32_t year);

/**
 * Returns the number of days in a particular Julian Proleptic calendar year.
 *
 * @param year to evaluate
 */
uint32_t days_in_year_julian(uint32_t year);

#define NUM_LEAP_YEARS(y, c) ((c) == GREGORIAN \
    ? number_of_leap_years_gregorian((y)) \
    : number_of_leap_years_julian((y)))

/**
 * Returns the number of leap years that have
 * occurred in the Julian Proleptic calendar
 * up to the supplied year.
 *
 * @param year to evaluate (1 - 9999)
 */
int32_t number_of_leap_years_julian(uint32_t year);

/**
 * Returns the number of leap years that have
 * occurred in the Gregorian Proleptic calendar
 * up to the supplied year.
 *
 * @param year to evaluate (1 - 9999)
 */
int32_t number_of_leap_years_gregorian(uint32_t year);

/**
 * Returns the number of days in a month, given
 * a year and a month in the Gregorian calendar.
 *
 * @param Year in Gregorian Proleptic calendar
 * @param Month in date
 */
uint32_t days_in_gregorian_year_month(uint32_t year, uint32_t month);

/**
 * Returns the number of the day in a week.
 *
 * @see temporal_to_number_days()
 *
 * Return values:
 *
 * Day            Day Number  Sunday first day?
 * -------------- ----------- -----------------
 * Sunday         0           true
 * Monday         1           true
 * Tuesday        2           true
 * Wednesday      3           true
 * Thursday       4           true
 * Friday         5           true
 * Saturday       6           true
 * Sunday         6           false
 * Monday         0           false
 * Tuesday        1           false
 * Wednesday      2           false
 * Thursday       3           false
 * Friday         4           false
 * Saturday       5           false
 *
 * @param Number of days since start of Gregorian calendar.
 * @param Consider Sunday the first day of the week?
 */
uint32_t day_of_week(int64_t day_number, bool sunday_is_first_day_of_week);

/**
 * Given a year, month, and day, returns whether the date is 
 * valid for the Gregorian proleptic calendar.
 *
 * @param The year
 * @param The month
 * @param The day
 */
bool is_valid_gregorian_date(uint32_t year, uint32_t month, uint32_t day);

/**
 * Returns whether the supplied date components are within the 
 * range of the UNIX epoch.
 *
 * Times in the range of 1970-01-01T00:00:00 to 2038-01-19T03:14:07
 *
 * @param Year
 * @param Month
 * @param Day
 * @param Hour
 * @param Minute
 * @param Second
 */
bool in_unix_epoch_range(uint32_t year
                       , uint32_t month
                       , uint32_t day
                       , uint32_t hour
                       , uint32_t minute
                       , uint32_t second);

/* 
 * I don't like using these defines, but probably good to keep in sync
 * with MySQL's week mode stuff.. 
 */
#define DRIZZLE_WEEK_MODE_MONDAY_FIRST_DAY   1
#define DRIZZLE_WEEK_MODE_USE_ISO_8601_1988  2
#define DRIZZLE_WEEK_MODE_WEEK_RANGE_IS_ORDINAL 4

/**
 * Returns the number of the week from a supplied year, month, and
 * date in the Gregorian proleptic calendar.
 *
 * The week number returned will depend on the values of the
 * various boolean flags passed to the function.
 *
 * The flags influence returned values in the following ways:
 *
 * sunday_is_first_day_of_week
 *
 * If TRUE, Sunday is first day of week
 * If FALSE,	Monday is first day of week
 *
 * week_range_is_ordinal
 *
 * If FALSE, the week is in range 0-53
 *
 * Week 0 is returned for the the last week of the previous year (for
 * a date at start of january) In this case one can get 53 for the
 * first week of next year.  This flag ensures that the week is
 * relevant for the given year. 
 *
 * If TRUE, the week is in range 1-53.
 *
 * In this case one may get week 53 for a date in January (when
 * the week is that last week of previous year) and week 1 for a
 * date in December.
 *
 * use_iso_8601_1988
 *
 * If TRUE, the weeks are numbered according to ISO 8601:1988
 *
 * ISO 8601:1988 means that if the week containing January 1 has
 * four or more days in the new year, then it is week 1;
 * Otherwise it is the last week of the previous year, and the
 * next week is week 1.
 *
 * If FALSE, the week that contains the first 'first-day-of-week' is week 1.
 *
 * @param Subject year
 * @param Subject month
 * @param Subject day
 * @param Is sunday the first day of the week?
 * @param Is the week range ordinal?
 * @param Should we use ISO 8601:1988 rules?
 * @param Pointer to a uint32_t to hold the resulting year, which 
 *        may be incremented or decremented depending on flags
 */
int64_t week_number_from_gregorian_date(uint32_t year
                                        , uint32_t month
                                        , uint32_t day
                                        , bool sunday_is_first_day_of_week
                                        , bool week_range_is_ordinal
                                        , bool use_iso_8601_1988
                                        , uint32_t *year_out);

#ifdef __cplusplus
}
#endif

#endif /* DRIZZLED_CALENDAR_H */