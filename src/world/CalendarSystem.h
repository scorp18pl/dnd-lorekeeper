#pragma once
#include <optional>
#include <string>
#include <vector>

struct CalendarMonth {
    std::string name;
    int         days = 30;
};

struct CalendarEra {
    std::string name;
    int         start_day = 0;
    // end is implicit: next era's start_day - 1 (last era is ongoing)
};

struct LeapRule {
    int every_n_years = 4;
    int extra_days    = 1;
};

struct CalendarSystem {
    std::string                epoch_name = "Year";
    std::vector<CalendarEra>   eras;
    std::vector<CalendarMonth> months;
    std::vector<std::string>   week_days;
    std::optional<LeapRule>    leap_rule;

    bool defined() const { return !months.empty(); }
    int  daysPerYear() const;

    // Integer day since epoch 0 → human-readable calendar string.
    std::string formatDay(int day) const;
};
