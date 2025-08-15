#include <Arduino.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_log.h>
#include <esp_task_wdt.h>        // EKSIK INCLUDE EKLENDİ
#include "settings.h"
#include "log_system.h"
#include "uart_handler.h"
#include "web_routes.h"
#include "websocket_handler.h"
#include "password_policy.h"
#include "backup_restore.h"
#include "time_sync.h"
#include "network_config.h"
#include "ntp_handler.h"

// Task handle'ları
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t uartTaskHandle = NULL;
TaskHandle_t systemTaskHandle = NULL;

// Sistem değişkenleri
unsigned long lastHeapCheck = 0;
size_t minFreeHeap = SIZE_MAX;
bool systemStable = true;

// Forward declaration - EKSIK FONKSIYON DEKLARASYONU
void checkSystemHealth();

// Web server task - Core 0'da çalışacak
void webServerTask(void *parameter) {
    addLog("🌐 Web server task başlatıldı (Core 0)", INFO, "TASK");
    
    while(true) {
        server.handleClient();
        handleWebSocket();
        vTaskDelay(1);
    }
}

// UART ve zaman senkronizasyon task - Core 1'de
void uartTask(void *parameter) {
    addLog("📡 UART task başlatıldı (Core 1)", INFO, "TASK");
    
    unsigned long lastTimeSync = 0;
    unsigned long lastUartHealth = 0;
    
    while(true) {
        unsigned long now = millis();
        
        // Zaman senkronizasyonu kontrolü (5 dakikada bir)
        if (now - lastTimeSync > 300000) {
            checkTimeSync();
            lastTimeSync = now;
        }
        
        // UART sağlık kontrolü (30 saniyede bir)
        if (now - lastUartHealth > 30000) {
            checkUARTHealth();
            lastUartHealth = now;
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Sistem monitoring task
void systemTask(void *parameter) {
    addLog("🔧 System monitoring task başlatıldı", INFO, "TASK");
    
    unsigned long lastBackupCheck = 0;
    unsigned long lastEthCheck = 0;
    unsigned long lastMemCheck = 0;
    unsigned long lastWSCleanup = 0;
    
    while(true) {
        unsigned long now = millis();
        
        // Otomatik backup kontrolü - 1 saatte bir
        if (now - lastBackupCheck > 3600000) {
            createAutomaticBackup();
            lastBackupCheck = now;
        }
        
        // Ethernet durumu kontrolü - 1 dakikada bir
        if (now - lastEthCheck > 60000) {
            static bool lastEthStatus = false;
            bool currentEthStatus = ETH.linkUp();
            
            if (currentEthStatus != lastEthStatus) {
                if (currentEthStatus) {
                    addLog("✅ Ethernet yeniden bağlandı - IP: " + ETH.localIP().toString(), SUCCESS, "ETH");
                    addLog("Hız: " + String(ETH.linkSpeed()) + " Mbps, " + 
                           String(ETH.fullDuplex() ? "Full" : "Half") + " Duplex", INFO, "ETH");
                } else {
                    addLog("❌ Ethernet bağlantısı kesildi", ERROR, "ETH");
                }
                lastEthStatus = currentEthStatus;
                
                if (isWebSocketConnected()) {
                    broadcastStatus();
                }
            }
            lastEthCheck = now;
        }
        
        // Bellek kontrolü - 30 saniyede bir
        if (now - lastMemCheck > 30000) {
            checkSystemHealth(); // ARTIK TANIMLI
            lastMemCheck = now;
        }
        
        // WebSocket temizlik - 10 dakikada bir
        if (now - lastWSCleanup > 600000) {
            cleanupWebSocketClients();
            lastWSCleanup = now;
        }
        
        // Session timeout kontrolü
        if (settings.isLoggedIn) {
            if (now - settings.sessionStartTime > settings.SESSION_TIMEOUT) {
                settings.isLoggedIn = false;
                addLog("⏰ Oturum zaman aşımı", INFO, "AUTH");
                
                if (isWebSocketConnected()) {
                    broadcastLog("Oturum zaman aşımı nedeniyle sonlandırıldı", "WARNING", "AUTH");
                }
            }
        }
        
        // İlk giriş sonrası parola değiştirme kontrolü
        static bool passwordChangeChecked = false;
        if (settings.isLoggedIn && !passwordChangeChecked) {
            if (mustChangePassword()) {
                if (isWebSocketConnected()) {
                    broadcastLog("Parolanızı değiştirmeniz gerekmektedir", "WARNING", "AUTH");
                }
            }
            passwordChangeChecked = true;
        }
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

// mDNS başlatma
void initMDNS() {
    uint8_t mac[6];
    ETH.macAddress(mac);
    char hostname[32];
    sprintf(hostname, "teias-%02x%02x", mac[4], mac[5]);
    
    if (MDNS.begin(hostname)) {
        addLog("✅ mDNS başlatıldı: " + String(hostname) + ".local", SUCCESS, "mDNS");
        
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "device", "TEİAŞ EKLİM");
        MDNS.addServiceTxt("http", "tcp", "version", "3.0");
        MDNS.addServiceTxt("http", "tcp", "model", "WT32-ETH01");
        
        MDNS.addService("ws", "tcp", WEBSOCKET_PORT);
        MDNS.addServiceTxt("ws", "tcp", "path", "/ws");
        
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║         BAĞLANTI BİLGİLERİ             ║");
        Serial.println("╠════════════════════════════════════════╣");
        Serial.print("║ IP Adresi    : ");
        Serial.print(ETH.localIP().toString());
        for(int i = ETH.localIP().toString().length(); i < 24; i++) Serial.print(" ");
        Serial.println("║");
        Serial.print("║ mDNS Adresi  : http://");
        Serial.print(hostname);
        Serial.print(".local");
        for(int i = strlen(hostname) + 13; i < 24; i++) Serial.print(" ");
        Serial.println("║");
        Serial.print("║ WebSocket    : ws://");
        Serial.print(ETH.localIP().toString());
        Serial.print(":81");
        for(int i = ETH.localIP().toString().length() + 8; i < 24; i++) Serial.print(" ");
        Serial.println("║");
        Serial.print("║ MAC Adresi   : ");
        Serial.print(ETH.macAddress());
        for(int i = ETH.macAddress().length(); i < 24; i++) Serial.print(" ");
        Serial.println("║");
        Serial.println("╚════════════════════════════════════════╝\n");
        
    } else {
        addLog("❌ mDNS başlatılamadı", ERROR, "mDNS");
    }
}

// Watchdog timer kurulumu - DÜZELTİLMİŞ
void initWatchdog() {
    // ESP-IDF v4.4+ için eski API kullan (platform compatibility)
    if (esp_task_wdt_init(30, true) == ESP_OK) {  // 30 saniye timeout, panic enable
        esp_task_wdt_add(NULL); // Current task'ı ekle
        addLog("🐕 Watchdog timer etkinleştirildi (30s)", INFO, "WDT");
    } else {
        addLog("⚠️ Watchdog timer başlatılamadı", WARN, "WDT");
    }
}

void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(240);
    
    esp_log_level_set("*", ESP_LOG_NONE);
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║      TEİAŞ EKLİM SİSTEMİ v3.0          ║");
    Serial.println("║   Trafo Merkezi Arıza Kayıt Sistemi    ║");
    Serial.println("║        🔧 Düzeltilmiş Versiyon        ║");
    Serial.println("╚════════════════════════════════════════╝");
    
    Serial.print("\n► CPU Frekansı: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");
    Serial.print("► Toplam Heap: ");
    Serial.print(ESP.getHeapSize());
    Serial.println(" bytes");
    Serial.print("► Chip Model: ");
    Serial.println(ESP.getChipModel());
    
    // Watchdog timer kurulumu
    initWatchdog();
    
    // LittleFS başlat
    Serial.print("► Dosya Sistemi (LittleFS)... ");
    if(!LittleFS.begin(true)){
        Serial.println("❌ HATA!");
        addLog("❌ LittleFS başlatılamadı - RESTART", ERROR, "FS");
        ESP.restart();
        return;
    }
    Serial.println("✅");
    
    // Modülleri başlat
    Serial.println("\n═══ MODÜLLER BAŞLATILIYOR ═══");
    
    Serial.print("► Log Sistemi... ");
    initLogSystem();
    Serial.println("✅");
    
    Serial.print("► Ayarlar... ");
    loadSettings();
    Serial.println("✅");
    
    Serial.print("► Network Yapılandırması... ");
    loadNetworkConfig();
    Serial.println("✅");
    
    Serial.print("► Ethernet... ");
    initEthernetAdvanced();
    Serial.println("✅");
    
    Serial.print("► UART (TX2:IO17, RX2:IO5)... ");
    initUART();
    Serial.println("✅");
    
    Serial.print("► NTP Handler... ");
    initNTPHandler();
    Serial.println("✅");
    
    Serial.print("► Web Sunucu... ");
    setupWebRoutes();
    Serial.println("✅");
    
    Serial.print("► WebSocket Server... ");
    initWebSocket();
    Serial.println("✅");
    
    Serial.print("► Parola Politikası... ");
    loadPasswordPolicy();
    Serial.println("✅");
    
    Serial.print("► mDNS... ");
    initMDNS();
    
    // Multi-core task'ları başlat
    Serial.println("\n═══ MULTI-CORE TASK BAŞLATILIYOR ═══");
    
    // Web server task (Core 0)
    xTaskCreatePinnedToCore(
        webServerTask,
        "WebServer",
        8192,
        NULL,
        3,
        &webTaskHandle,
        0
    );
    Serial.println("► Web Server Task (Core 0) ✅");
    
    // UART task (Core 1)
    xTaskCreatePinnedToCore(
        uartTask,
        "UART",
        4096,
        NULL,
        2,
        &uartTaskHandle,
        1
    );
    Serial.println("► UART Task (Core 1) ✅");
    
    // System monitoring task (Core 1)
    xTaskCreatePinnedToCore(
        systemTask,
        "System",
        4096,
        NULL,
        1,
        &systemTaskHandle,
        1
    );
    Serial.println("► System Task (Core 1) ✅");
    
    minFreeHeap = ESP.getFreeHeap();
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║         SİSTEM HAZIR!                  ║");
    Serial.println("╠════════════════════════════════════════╣");
    Serial.println("║ Kullanıcı: admin                       ║");
    Serial.println("║ Şifre    : 1234                        ║");
    Serial.print("║ Bellek   : ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" bytes");
    for(int i = String(ESP.getFreeHeap()).length(); i < 24; i++) Serial.print(" ");
    Serial.println("║");
    Serial.print("║ Tasks    : ");
    Serial.print(uxTaskGetNumberOfTasks());
    Serial.print(" aktif");
    for(int i = String(uxTaskGetNumberOfTasks()).length() + 6; i < 24; i++) Serial.print(" ");
    Serial.println("║");
    Serial.println("╚════════════════════════════════════════╝\n");
    
    addLog("🚀 Sistem başlatıldı - Multi-core aktif", SUCCESS, "SYSTEM");
    addLog("📍 Trafo Merkezi: " + settings.transformerStation, INFO, "SYSTEM");
    addLog("🌐 IP Adresi: " + ETH.localIP().toString(), INFO, "SYSTEM");
}

// Sistem sağlık kontrolü - FONKSIYON TANIMLANDI
void checkSystemHealth() {
    size_t currentHeap = ESP.getFreeHeap();
    
    if (currentHeap < minFreeHeap) {
        minFreeHeap = currentHeap;
    }
    
    if (currentHeap < 20000) {
        addLog("🚨 KRİTİK: Düşük bellek: " + String(currentHeap) + " bytes", ERROR, "SYSTEM");
        systemStable = false;
        
        if (currentHeap < 10000) {
            addLog("💥 ACİL DURUM: Bellek tükendi, yeniden başlatılıyor...", ERROR, "SYSTEM");
            delay(1000);
            ESP.restart();
        }
    } else if (currentHeap < 40000) {
        addLog("⚠️ UYARI: Düşük bellek: " + String(currentHeap) + " bytes", WARN, "SYSTEM");
        systemStable = false;
    } else {
        if (!systemStable) {
            addLog("✅ Bellek durumu normale döndü: " + String(currentHeap) + " bytes", SUCCESS, "SYSTEM");
            systemStable = true;
        }
    }
    
    // Task monitoring
    static unsigned long lastTaskCheck = 0;
    if (millis() - lastTaskCheck > 60000) {
        lastTaskCheck = millis();
        
        UBaseType_t taskCount = uxTaskGetNumberOfTasks();
        addLog("📊 Aktif task sayısı: " + String(taskCount), DEBUG, "SYSTEM");
        
        // Task health check
        if (webTaskHandle && eTaskGetState(webTaskHandle) == eDeleted) {
            addLog("❌ Web task crashed! Yeniden başlatılıyor...", ERROR, "TASK");
            ESP.restart();
        }
        
        if (uartTaskHandle && eTaskGetState(uartTaskHandle) == eDeleted) {
            addLog("❌ UART task crashed! Yeniden başlatılıyor...", ERROR, "TASK");
            ESP.restart();
        }
    }
    
    // Watchdog reset - Güvenli versiyon
    #ifdef CONFIG_TASK_WDT
        esp_task_wdt_reset();
    #endif
}

void loop() {
    // Watchdog reset - Güvenli versiyon
    #ifdef CONFIG_TASK_WDT
        esp_task_wdt_reset();
    #endif
    
    // Sistem sağlık kontrolü - 30 saniyede bir
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > 30000) {
        checkSystemHealth();
        lastHealthCheck = millis();
    }
    
    // Durum broadcast'i - 10 saniyede bir
    static unsigned long lastBroadcast = 0;
    if (millis() - lastBroadcast > 10000) {
        if (isWebSocketConnected()) {
            broadcastStatus();
        }
        lastBroadcast = millis();
    }
    
    delay(1000);
}