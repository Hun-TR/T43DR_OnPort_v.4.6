// time_sync.cpp - Düzeltilmiş ve İyileştirilmiş Versiyon
#include "time_sync.h"
#include "uart_handler.h"
#include "log_system.h"
#include <time.h>

// Global zaman değişkeni (header'da extern olarak tanımlı)
TimeData timeData = {false, "", "", 0, 0};

// Static değişkenler
static bool timeSyncErrorLogged = false;
static unsigned long lastSyncAttempt = 0;

// Forward declarations - Fonksiyon prototipleri
void updateSystemTime();

// Tarih formatla: DDMMYY -> DD.MM.20YY - İYİLEŞTİRİLMİŞ
String formatDate(const String& dateStr) {
    if (dateStr.length() != 6) {
        addLog("❌ Geçersiz tarih formatı uzunluğu: " + String(dateStr.length()), ERROR, "TIME");
        return "Geçersiz";
    }
    
    // Numeric validation
    for (int i = 0; i < 6; i++) {
        if (!isDigit(dateStr.charAt(i))) {
            addLog("❌ Tarihte numeric olmayan karakter: " + String(dateStr.charAt(i)), ERROR, "TIME");
            return "Geçersiz";
        }
    }
    
    int day = dateStr.substring(0, 2).toInt();
    int month = dateStr.substring(2, 4).toInt();
    int year = 2000 + dateStr.substring(4, 6).toInt();
    
    // Date validation
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2020 || year > 2050) {
        addLog("❌ Geçersiz tarih değerleri: " + String(day) + "/" + String(month) + "/" + String(year), ERROR, "TIME");
        return "Geçersiz";
    }
    
    char buffer[12];
    sprintf(buffer, "%02d.%02d.%04d", day, month, year);
    return String(buffer);
}

// Saat formatla: HHMMSS -> HH:MM:SS - İYİLEŞTİRİLMİŞ
String formatTime(const String& timeStr) {
    if (timeStr.length() != 6) {
        addLog("❌ Geçersiz saat formatı uzunluğu: " + String(timeStr.length()), ERROR, "TIME");
        return "Geçersiz";
    }
    
    // Numeric validation
    for (int i = 0; i < 6; i++) {
        if (!isDigit(timeStr.charAt(i))) {
            addLog("❌ Saatte numeric olmayan karakter: " + String(timeStr.charAt(i)), ERROR, "TIME");
            return "Geçersiz";
        }
    }
    
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(2, 4).toInt();
    int second = timeStr.substring(4, 6).toInt();
    
    // Time validation
    if (hour > 23 || minute > 59 || second > 59) {
        addLog("❌ Geçersiz saat değerleri: " + String(hour) + ":" + String(minute) + ":" + String(second), ERROR, "TIME");
        return "Geçersiz";
    }
    
    char buffer[10];
    sprintf(buffer, "%02d:%02d:%02d", hour, minute, second);
    return String(buffer);
}

// ESP32 sistem saatini güncelle - İYİLEŞTİRİLMİŞ
void updateSystemTime() {
    if (!timeData.isValid || timeData.lastDate == "Geçersiz" || timeData.lastTime == "Geçersiz") {
        addLog("❌ Geçersiz zaman verisi, sistem saati güncellenemiyor", ERROR, "TIME");
        return;
    }
    
    // Tarih ve saati parse et
    int day, month, year, hour, minute, second;
    
    if (sscanf(timeData.lastDate.c_str(), "%d.%d.%d", &day, &month, &year) != 3) {
        addLog("❌ Tarih parse hatası: " + timeData.lastDate, ERROR, "TIME");
        return;
    }
    
    if (sscanf(timeData.lastTime.c_str(), "%d:%d:%d", &hour, &minute, &second) != 3) {
        addLog("❌ Saat parse hatası: " + timeData.lastTime, ERROR, "TIME");
        return;
    }
    
    // tm struct oluştur
    struct tm timeinfo;
    timeinfo.tm_year = year - 1900;  // tm_year is years since 1900
    timeinfo.tm_mon = month - 1;     // tm_mon is 0-11
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;           // Daylight saving time info not available
    
    // Validate the time structure
    time_t t = mktime(&timeinfo);
    if (t == -1) {
        addLog("❌ Sistem saati oluşturulamadı", ERROR, "TIME");
        return;
    }
    
    // Sistem saatini ayarla
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    if (settimeofday(&now, NULL) == 0) {
        addLog("✅ Sistem saati güncellendi: " + timeData.lastDate + " " + timeData.lastTime, SUCCESS, "TIME");
        
        // Timezone ayarla (Türkiye saati - UTC+3)
        setenv("TZ", "TRT-3", 1);
        tzset();
    } else {
        addLog("❌ Sistem saati ayarlanamadı", ERROR, "TIME");
    }
}

// dsPIC'ten gelen zaman verisini parse et - İYİLEŞTİRİLMİŞ
bool parseTimeResponse(const String& response) {
    if (response.length() < 6) {
        addLog("❌ Zaman yanıtı çok kısa: " + String(response.length()), ERROR, "TIME");
        return false;
    }
    
    addLog("🔍 Zaman yanıtı parse ediliyor: " + response, DEBUG, "TIME");
    
    // Format 1: "DATE:DDMMYY,TIME:HHMMSS"
    if (response.indexOf("DATE:") >= 0 && response.indexOf("TIME:") >= 0) {
        int dateStart = response.indexOf("DATE:") + 5;
        int dateEnd = response.indexOf(",");
        int timeStart = response.indexOf("TIME:") + 5;
        
        if (dateEnd > dateStart && timeStart > dateEnd) {
            String dateStr = response.substring(dateStart, dateEnd);
            String timeStr = response.substring(timeStart, timeStart + 6);
            
            dateStr.trim();
            timeStr.trim();
            
            if (dateStr.length() == 6 && timeStr.length() == 6) {
                String formattedDate = formatDate(dateStr);
                String formattedTime = formatTime(timeStr);
                
                if (formattedDate != "Geçersiz" && formattedTime != "Geçersiz") {
                    timeData.lastDate = formattedDate;
                    timeData.lastTime = formattedTime;
                    addLog("✅ Format 1 parse başarılı: " + formattedDate + " " + formattedTime, DEBUG, "TIME");
                    return true;
                }
            }
        }
    }
    
    // Format 2: "DDMMYYHHMMSS" (12 karakter)
    if (response.length() == 12) {
        String dateStr = response.substring(0, 6);
        String timeStr = response.substring(6, 12);
        
        String formattedDate = formatDate(dateStr);
        String formattedTime = formatTime(timeStr);
        
        if (formattedDate != "Geçersiz" && formattedTime != "Geçersiz") {
            timeData.lastDate = formattedDate;
            timeData.lastTime = formattedTime;
            addLog("✅ Format 2 parse başarılı: " + formattedDate + " " + formattedTime, DEBUG, "TIME");
            return true;
        }
    }
    
    // Format 3: Sadece tarih "DDMMYY" (6 karakter)
    if (response.length() == 6) {
        String formattedDate = formatDate(response);
        if (formattedDate != "Geçersiz") {
            timeData.lastDate = formattedDate;
            addLog("✅ Sadece tarih parse edildi: " + formattedDate, DEBUG, "TIME");
            return true;
        }
    }
    
    // Format 4: Checksum'lı veri "DDMMYYx" ve "HHMMSSy"
    if (response.length() == 7) {
        String dataOnly = response.substring(0, 6);
        char checksum = response.charAt(6);
        
        if (checksum >= 'A' && checksum <= 'Z') { // Tarih
            String formattedDate = formatDate(dataOnly);
            if (formattedDate != "Geçersiz") {
                timeData.lastDate = formattedDate;
                addLog("✅ Checksum'lı tarih parse edildi: " + formattedDate, DEBUG, "TIME");
                return true;
            }
        } else if (checksum >= 'a' && checksum <= 'z') { // Saat
            String formattedTime = formatTime(dataOnly);
            if (formattedTime != "Geçersiz") {
                timeData.lastTime = formattedTime;
                timeData.isValid = true;
                addLog("✅ Checksum'lı saat parse edildi: " + formattedTime, DEBUG, "TIME");
                return true;
            }
        }
    }
    
    addLog("❌ Hiçbir format eşleşmedi: " + response, WARN, "TIME");
    return false;
}

// dsPIC'ten zaman isteği gönder - İYİLEŞTİRİLMİŞ
bool requestTimeFromDsPIC() {
    // Rate limiting - 10 saniyede bir istekten fazla yapma
    unsigned long now = millis();
    if (now - lastSyncAttempt < 10000) {
        return timeData.isValid; // Son durum ne ise onu döndür
    }
    lastSyncAttempt = now;
    
    String response;
    
    // Zaman isteği komutu gönder - birden fazla komut dene
    String commands[] = {"GETTIME", "TIME", "DT", "DATETIME"};
    bool success = false;
    
    for (int i = 0; i < 4 && !success; i++) {
        addLog("🔄 Zaman komutu gönderiliyor: " + commands[i], DEBUG, "TIME");
        
        if (sendCustomCommand(commands[i], response, 3000)) {
            if (response.length() > 0) {
                addLog("📥 Yanıt alındı (" + String(response.length()) + " byte): " + response, DEBUG, "TIME");
                
                if (parseTimeResponse(response)) {
                    success = true;
                    break;
                }
            }
        }
        
        delay(500); // Komutlar arası kısa bekleme
    }
    
    if (success) {
        timeData.lastSync = millis();
        timeData.syncCount++;
        timeData.isValid = true;
        
        // Error flag'i sıfırla
        timeSyncErrorLogged = false;
        
        addLog("✅ Zaman senkronize edildi (#" + String(timeData.syncCount) + "): " + 
               timeData.lastDate + " " + timeData.lastTime, SUCCESS, "TIME");
        
        // Sistem saatini güncelle
        updateSystemTime();
        
        return true;
    } else {
        // Sadece hata daha önce loglanmadıysa logla
        if (!timeSyncErrorLogged) {
            addLog("❌ dsPIC'ten zaman bilgisi alınamadı (tüm komutlar denendi)", ERROR, "TIME");
            timeSyncErrorLogged = true;
        }
        
        // Uzun süre senkronizasyon yoksa geçerliliği kaldır
        if (timeData.isValid && (now - timeData.lastSync > 1800000)) { // 30 dakika
            timeData.isValid = false;
            addLog("⚠️ Zaman verisi eskidi, geçerlilik kaldırıldı", WARN, "TIME");
        }
        
        return false;
    }
}

// Periyodik senkronizasyon kontrolü - İYİLEŞTİRİLMİŞ
void checkTimeSync() {
    static unsigned long lastSyncRequest = 0;
    static bool firstSyncDone = false;
    
    unsigned long now = millis();
    
    // İlk senkronizasyon için daha sık dene
    unsigned long SYNC_INTERVAL = firstSyncDone ? 300000 : 30000; // 5 dakika veya 30 saniye
    
    // İlk senkronizasyon veya periyodik senkronizasyon
    if (timeData.syncCount == 0 || (now - lastSyncRequest > SYNC_INTERVAL)) {
        lastSyncRequest = now;
        
        if (requestTimeFromDsPIC()) {
            if (!firstSyncDone) {
                firstSyncDone = true;
                addLog("🎯 İlk zaman senkronizasyonu tamamlandı", SUCCESS, "TIME");
            }
        }
    }
    
    // Zaman geçerliliğini kontrol et (15 dakika timeout)
    if (timeData.isValid && (now - timeData.lastSync > 900000)) {
        addLog("⚠️ Zaman senkronizasyonu 15 dakikadır yok, geçerlilik sorgulanıyor...", WARN, "TIME");
        
        // Acil senkronizasyon denemesi
        if (!requestTimeFromDsPIC()) {
            timeData.isValid = false;
            addLog("❌ Zaman senkronizasyonu kayıp", ERROR, "TIME");
        }
    }
}

// API için zaman bilgilerini döndür - İYİLEŞTİRİLMİŞ
String getCurrentDateTime() {
    if (!timeData.isValid) {
        // Sistem saatini kullanmayı dene
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", &timeinfo);
            return String(buffer) + " (Sistem)";
        }
        return "Senkronizasyon bekleniyor...";
    }
    
    // Son senkronizasyon zamanını ekle
    unsigned long elapsed = (millis() - timeData.lastSync) / 1000;
    String ageInfo = "";
    if (elapsed > 60) {
        ageInfo = " (" + String(elapsed / 60) + "dk önce)";
    } else if (elapsed > 5) {
        ageInfo = " (" + String(elapsed) + "s önce)";
    }
    
    return timeData.lastDate + " " + timeData.lastTime + ageInfo;
}

String getCurrentDate() {
    if (!timeData.isValid) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char buffer[16];
            strftime(buffer, sizeof(buffer), "%d.%m.%Y", &timeinfo);
            return String(buffer);
        }
        return "---";
    }
    return timeData.lastDate;
}

String getCurrentTime() {
    if (!timeData.isValid) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char buffer[16];
            strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
            return String(buffer);
        }
        return "---";
    }
    return timeData.lastTime;
}

bool isTimeSynced() {
    // Hem dsPIC senkronizasyonu hem de sistem saati kontrolü
    if (timeData.isValid) {
        return true;
    }
    
    // Sistem saati ayarlanmış mı kontrol et
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // 2020'den sonra ise büyük ihtimalle doğru
        return (timeinfo.tm_year + 1900) > 2020;
    }
    
    return false;
}

// Zaman senkronizasyon istatistikleri - İYİLEŞTİRİLMİŞ
String getTimeSyncStats() {
    String stats = "=== ZAMAN SENKRONİZASYON DURUMU ===\n";
    
    stats += "Durum: " + String(timeData.isValid ? "✅ Aktif" : "❌ Pasif") + "\n";
    stats += "Toplam Senkronizasyon: " + String(timeData.syncCount) + "\n";
    
    if (timeData.lastSync > 0) {
        unsigned long elapsed = (millis() - timeData.lastSync) / 1000;
        stats += "Son Senkronizasyon: " + String(elapsed) + " saniye önce\n";
        
        if (elapsed > 300) { // 5 dakikadan fazla
            stats += "⚠️ UYARI: Son senkronizasyon çok eski!\n";
        }
    } else {
        stats += "Son Senkronizasyon: Hiç yapılmadı\n";
    }
    
    stats += "Son Tarih: " + (timeData.lastDate.length() > 0 ? timeData.lastDate : "Yok") + "\n";
    stats += "Son Saat: " + (timeData.lastTime.length() > 0 ? timeData.lastTime : "Yok") + "\n";
    
    // Sistem saati durumu
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char sysTime[32];
        strftime(sysTime, sizeof(sysTime), "%d.%m.%Y %H:%M:%S", &timeinfo);
        stats += "Sistem Saati: " + String(sysTime) + "\n";
    } else {
        stats += "Sistem Saati: Ayarlanmamış\n";
    }
    
    // Performans bilgisi
    stats += "Uptime: " + String(millis() / 1000) + " saniye\n";
    
    return stats;
}