#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>

// Time data structure
struct TimeData {
    bool isValid;
    String lastDate;
    String lastTime;
    unsigned long lastSync;
    int syncCount;
};

// Function declarations
bool requestTimeFromDsPIC();
bool parseTimeResponse(const String& response);
String formatDate(const String& dateStr);
String formatTime(const String& timeStr);
void updateSystemTime();
void checkTimeSync();
String getCurrentDateTime();
String getCurrentDate();
String getCurrentTime();
bool isTimeSynced();
String getTimeSyncStats();

// Global time data - extern declaration
extern TimeData timeData;

#endif // TIME_SYNC_H