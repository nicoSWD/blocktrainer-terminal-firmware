// Timezone choices offered by the web UI: display label and POSIX TZ definition.
// /save matches the posted value against the definition and stores both.
#pragma once
struct TzEntry { const char *label; const char *def; };
static const TzEntry TIMEZONES[] = {
    { "UTC, Coordinated Universal Time", "UTC0" },
    { "Western Europe: Lisbon, Dublin (WET)", "WET0WEST,M3.5.0/1,M10.5.0/2" },
    { "United Kingdom: London (GMT/BST)", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "GMT, no daylight saving: Reykjavik, Accra", "GMT0" },
    { "Central Europe: Berlin, Paris, Rome (CET/CEST)", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Eastern Europe: Athens, Helsinki, Kyiv (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Moscow, Istanbul (UTC+3)", "MSK-3" },
    { "Gulf: Dubai, Abu Dhabi (UTC+4)", "GST-4" },
    { "West Africa: Lagos, Kinshasa (UTC+1)", "WAT-1" },
    { "Central Africa: Maputo, Harare (UTC+2)", "CAT-2" },
    { "East Africa: Nairobi, Addis Ababa (UTC+3)", "EAT-3" },
    { "India: Mumbai, Delhi (UTC+5:30)", "IST-5:30" },
    { "Bangladesh: Dhaka (UTC+6)", "BST-6" },
    { "Indochina: Bangkok, Hanoi (UTC+7)", "ICT-7" },
    { "China: Beijing, Singapore (UTC+8)", "CST-8" },
    { "Japan: Tokyo (UTC+9)", "JST-9" },
    { "Korea: Seoul (UTC+9)", "KST-9" },
    { "Australia West: Perth (UTC+8)", "AWST-8" },
    { "Australia Central: Adelaide, Darwin", "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3" },
    { "Australia East: Sydney, Melbourne", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "New Zealand: Auckland", "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
    { "Hawaii (UTC-10)", "HST10" },
    { "Alaska: Anchorage", "AKST9AKDT,M3.2.0/2,M11.1.0/2" },
    { "US Pacific: Los Angeles, Seattle", "PST8PDT,M3.2.0/2,M11.1.0/2" },
    { "US Mountain: Denver, Salt Lake City", "MST7MDT,M3.2.0/2,M11.1.0/2" },
    { "US Central: Chicago, Dallas", "CST6CDT,M3.2.0/2,M11.1.0/2" },
    { "US Eastern: New York, Miami", "EST5EDT,M3.2.0/2,M11.1.0/2" },
    { "Brazil: Sao Paulo, Brasilia (UTC-3)", "BRT3" },
    { "Argentina: Buenos Aires (UTC-3)", "ART3" },
};
static const int TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);
