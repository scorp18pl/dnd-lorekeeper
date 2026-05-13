#include "CalendarSystem.h"
#include <string>

// Floor division that handles negative numerators correctly.
static int floorDiv(int a, int b) {
    return a / b - (a % b != 0 && ((a ^ b) < 0) ? 1 : 0);
}
static int floorMod(int a, int b) {
    return a - b * floorDiv(a, b);
}

int CalendarSystem::daysPerYear() const {
    int total = 0;
    for (const auto& m : months) total += m.days;
    return total > 0 ? total : 365;
}

std::string CalendarSystem::formatDay(int day) const {
    if (!defined())
        return "Day " + std::to_string(day);

    int dpy  = daysPerYear();
    int year = floorDiv(day, dpy);
    int doy  = floorMod(day, dpy);

    // Find month and day-of-month
    int monthIdx   = (int)months.size() - 1;
    int dayOfMonth = doy;
    for (int i = 0; i < (int)months.size(); ++i) {
        if (dayOfMonth < months[i].days) { monthIdx = i; break; }
        dayOfMonth -= months[i].days;
    }

    // Find current era: eras are contiguous, sorted by start_day.
    // The active era is the one with the largest start_day <= day.
    std::string eraName;
    for (const auto& era : eras) {
        if (day >= era.start_day) eraName = era.name;
        else break;
    }

    // Weekday
    std::string weekdayName;
    if (!week_days.empty()) {
        int wd = floorMod(day, (int)week_days.size());
        weekdayName = week_days[wd];
    }

    // "12 Harvestmoon, Year 312, Age of Ash"
    std::string result =
        std::to_string(dayOfMonth + 1) + " " + months[monthIdx].name +
        ", " + epoch_name + " " + std::to_string(year + 1);
    if (!eraName.empty())    result += ", " + eraName;
    if (!weekdayName.empty()) result  = weekdayName + ", " + result;

    return result;
}
