// timezones.h: label/definition pairs from the table at rodata 0x3c2292ac (pairs of char*).
// The /save handler matches the posted value against the definition and stores both.
#pragma once
struct TzEntry { const char *label; const char *def; };
static const TzEntry TIMEZONES[] = {
    { "UTC (Koordinierte Weltzeit)", "UTC0" },
    { "GMT (Greenwich Mean Time)", "GMT0" },
    { "BST - Großbritannien (Sommerzeit)", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "CET - Mitteleuropa", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "EET - Osteuropa", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "MSK - Moskau", "MSK-3" },
    { "GST - Dubai (Gulf Standard Time)", "GST-4" },
    { "WET - Westeuropa", "WET0WEST,M3.5.0/1,M10.5.0/2" },
    { "EST/EDT - US/Ostküste", "EST5EDT,M3.2.0/2,M11.1.0/2" },
    { "CST/CDT - US/Zentral", "CST6CDT,M3.2.0/2,M11.1.0/2" },
    { "MST/MDT - US/Berge", "MST7MDT,M3.2.0/2,M11.1.0/2" },
    { "PST/PDT - US/Westküste", "PST8PDT,M3.2.0/2,M11.1.0/2" },
    { "AKST/AKDT - Alaska", "AKST9AKDT,M3.2.0/2,M11.1.0/2" },
    { "HST - Hawaii", "HST10" },
    { "BRT - Brasília", "BRT3" },
    { "ART - Argentinien", "ART3" },
    { "WAT - Westafrika", "WAT-1" },
    { "CAT - Zentralafrika", "CAT-2" },
    { "EAT - Ostafrika", "EAT-3" },
    { "IST - Indien", "IST-5:30" },
    { "BST - Bangladesch", "BST-6" },
    { "ICT - Indochina (TH/VN)", "ICT-7" },
    { "CST - China", "CST-8" },
    { "JST - Japan", "JST-9" },
    { "KST - Korea", "KST-9" },
    { "AWST -  Austr. West", "AWST-8" },
    { "ACST/ACDT - Austr. Zentral", "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3" },
    { "AEST/AEDT - Austr. Ost", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "NZST/NZDT - Neuseeland", "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
};
static const int TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);
