#include "log_system.h"
#include <time.h>
#include "time_sync.h" // Zaman senkronizasyon durumunu kontrol etmek için ekleyin


// log_system.h'de 'extern' olarak bildirilen global değişkenlerin
// gerçek tanımlamaları burada yapılır.
LogEntry logs[50];
int logIndex = 0;
int totalLogs = 0;

// NTP'den geçerli zaman alınamazsa kullanılacak zaman formatı
String getFormattedTimestampFallback() {
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    char buffer[16];
    sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(buffer);
}

// NTP'den veya sistemden zamanı alıp formatlayan ana fonksiyon
String getFormattedTimestamp() {
    // Zamanın gerçekten senkronize olup olmadığını kontrol et
    if (isTimeSynced()) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", &timeinfo);
            return String(buffer);
        }
    }
    // Eğer senkronize değilse, çalışma süresini göster
    unsigned long seconds = millis() / 1000;
    char buffer[32];
    sprintf(buffer, "[NO_SYNC %02lu:%02lu:%02lu]", (seconds/3600)%24, (seconds/60)%60, seconds%60);
    return String(buffer);
}

// Log sistemini başlatan fonksiyon
void initLogSystem() {
    for (int i = 0; i < 50; i++) {
        logs[i].message = "";
    }
    logIndex = 0;
    totalLogs = 0;
    // Sistem başlatıldığında ilk logu ekle
    addLog("Log sistemi başlatıldı.", INFO, "SYSTEM");
}

// Yeni bir log ekleyen ana fonksiyon
void addLog(const String& msg, LogLevel level, const String& source) {
    logs[logIndex].timestamp = getFormattedTimestamp();
    logs[logIndex].message = msg;
    logs[logIndex].level = level;
    logs[logIndex].source = source;
    logs[logIndex].millis_time = millis();

    logIndex = (logIndex + 1) % 50; // Dairesel arabellek mantığı
    if (totalLogs < 50) {
        totalLogs++;
    }

    // Seri monitöre de logu bas
    Serial.println("[" + getFormattedTimestamp() + "] [" + logLevelToString(level) + "] [" + source + "] " + msg);
}

// Log seviyesini string'e çeviren yardımcı fonksiyon
String logLevelToString(LogLevel level) {
    switch (level) {
        case ERROR: return "ERROR";
        case WARN:  return "WARN";
        case INFO:  return "INFO";
        case DEBUG: return "DEBUG";
        case SUCCESS: return "SUCCESS";
        default: return "UNKNOWN";
    }
}

// Tüm logları temizleyen fonksiyon
void clearLogs() {
    for (int i = 0; i < 50; i++) {
        logs[i].message = "";
    }
    logIndex = 0;
    totalLogs = 0;
    addLog("Log kayıtları temizlendi.", WARN, "SYSTEM");
}