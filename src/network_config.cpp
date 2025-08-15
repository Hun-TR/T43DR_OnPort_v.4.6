// network_config.cpp - Düzeltilmiş ve Tamamlanmış Versiyon
#include "network_config.h"
#include <ETH.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "log_system.h"
#include "settings.h"

// Global settings değişkenini kullan
extern Settings settings;

// Network config global değişkeni (header'da extern olarak tanımlı)
NetworkConfig netConfig;

// Network ayarlarını yükle - TAMAMLANMIŞ İMPLEMENTASYON
void loadNetworkConfig() {
    Preferences prefs;
    prefs.begin("network-config", true);
    
    // DHCP kullanımı (varsayılan true)
    netConfig.useDHCP = prefs.getBool("use_dhcp", true);
    
    // Statik IP ayarları
    String staticIPStr = prefs.getString("static_ip", "192.168.1.160");
    String gatewayStr = prefs.getString("gateway", "192.168.1.1");
    String subnetStr = prefs.getString("subnet", "255.255.255.0");
    String dns1Str = prefs.getString("dns1", "8.8.8.8");
    String dns2Str = prefs.getString("dns2", "8.8.4.4");
    
    // IP validasyonu ve atama
    if (!netConfig.staticIP.fromString(staticIPStr)) {
        netConfig.staticIP.fromString("192.168.1.160");
        addLog("⚠️ Geçersiz statik IP, varsayılan kullanılıyor", WARN, "NET");
    }
    
    if (!netConfig.gateway.fromString(gatewayStr)) {
        netConfig.gateway.fromString("192.168.1.1");
        addLog("⚠️ Geçersiz gateway, varsayılan kullanılıyor", WARN, "NET");
    }
    
    if (!netConfig.subnet.fromString(subnetStr)) {
        netConfig.subnet.fromString("255.255.255.0");
        addLog("⚠️ Geçersiz subnet, varsayılan kullanılıyor", WARN, "NET");
    }
    
    if (!netConfig.dns1.fromString(dns1Str)) {
        netConfig.dns1.fromString("8.8.8.8");
        addLog("⚠️ Geçersiz DNS1, varsayılan kullanılıyor", WARN, "NET");
    }
    
    if (!netConfig.dns2.fromString(dns2Str)) {
        netConfig.dns2.fromString("8.8.4.4");
        addLog("⚠️ Geçersiz DNS2, varsayılan kullanılıyor", WARN, "NET");
    }
    
    prefs.end();
    
    // Settings ile senkronize et (backward compatibility)
    if (!netConfig.useDHCP) {
        settings.local_IP = netConfig.staticIP;
        settings.gateway = netConfig.gateway;
        settings.subnet = netConfig.subnet;
        settings.primaryDNS = netConfig.dns1;
    }
    
    addLog("✅ Network konfigürasyonu yüklendi", SUCCESS, "NET");
    addLog("DHCP: " + String(netConfig.useDHCP ? "Aktif" : "Pasif"), INFO, "NET");
    if (!netConfig.useDHCP) {
        addLog("Statik IP: " + netConfig.staticIP.toString(), INFO, "NET");
    }
}

// Network ayarlarını kaydet - YENİ İMPLEMENTASYON
void saveNetworkConfig(bool useDHCP, String ip, String gw, String sn, String d1, String d2) {
    // Input validation
    IPAddress testIP;
    if (!useDHCP) {
        if (!testIP.fromString(ip)) {
            addLog("❌ Geçersiz IP adresi: " + ip, ERROR, "NET");
            return;
        }
        if (!testIP.fromString(gw)) {
            addLog("❌ Geçersiz Gateway adresi: " + gw, ERROR, "NET");
            return;
        }
        if (!testIP.fromString(sn)) {
            addLog("❌ Geçersiz Subnet adresi: " + sn, ERROR, "NET");
            return;
        }
        if (d1.length() > 0 && !testIP.fromString(d1)) {
            addLog("❌ Geçersiz DNS1 adresi: " + d1, ERROR, "NET");
            return;
        }
        if (d2.length() > 0 && !testIP.fromString(d2)) {
            addLog("❌ Geçersiz DNS2 adresi: " + d2, ERROR, "NET");
            return;
        }
    }
    
    Preferences prefs;
    prefs.begin("network-config", false);
    
    // Ayarları kaydet
    prefs.putBool("use_dhcp", useDHCP);
    prefs.putString("static_ip", ip);
    prefs.putString("gateway", gw);
    prefs.putString("subnet", sn);
    prefs.putString("dns1", d1);
    prefs.putString("dns2", d2);
    
    prefs.end();
    
    // Global config'i güncelle
    netConfig.useDHCP = useDHCP;
    if (!useDHCP) {
        netConfig.staticIP.fromString(ip);
        netConfig.gateway.fromString(gw);
        netConfig.subnet.fromString(sn);
        netConfig.dns1.fromString(d1);
        netConfig.dns2.fromString(d2);
        
        // Settings ile senkronize et
        settings.local_IP = netConfig.staticIP;
        settings.gateway = netConfig.gateway;
        settings.subnet = netConfig.subnet;
        settings.primaryDNS = netConfig.dns1;
    }
    
    addLog("✅ Network konfigürasyonu kaydedildi", SUCCESS, "NET");
}

// Network config JSON döndür 
String getNetworkConfigJSON() {
    JsonDocument doc;  // StaticJsonDocument<512> doc; yerine
    
    doc["useDHCP"] = netConfig.useDHCP;
    doc["staticIP"] = netConfig.staticIP.toString();
    doc["gateway"] = netConfig.gateway.toString();
    doc["subnet"] = netConfig.subnet.toString();
    doc["dns1"] = netConfig.dns1.toString();
    doc["dns2"] = netConfig.dns2.toString();
    
    // Mevcut ethernet durumu
    doc["currentIP"] = ETH.localIP().toString();
    doc["currentGateway"] = ETH.gatewayIP().toString();
    doc["currentSubnet"] = ETH.subnetMask().toString();
    doc["currentDNS"] = ETH.dnsIP().toString();
    doc["linkUp"] = ETH.linkUp();
    doc["linkSpeed"] = ETH.linkSpeed();
    doc["fullDuplex"] = ETH.fullDuplex();
    doc["macAddress"] = ETH.macAddress();
    
    String output;
    serializeJson(doc, output);
    return output;
}

// Ethernet başlatma - GELİŞTİRİLMİŞ VERSİYON
void initEthernetAdvanced() {
    addLog("🌐 Gelişmiş Ethernet başlatılıyor...", INFO, "ETH");
    
    // WT32-ETH01 için doğru pin konfigürasyonu
    // RMII interface kullanıyor
    ETH.begin(
        1,                      // PHY Address
        16,                     // Power Pin
        23,                     // MDC Pin
        18,                     // MDIO Pin
        ETH_PHY_LAN8720,       // PHY Type
        ETH_CLOCK_GPIO17_OUT   // Clock mode
    );
    
    // MAC address'i logla
    addLog("MAC Adresi: " + ETH.macAddress(), INFO, "ETH");
    
    // Network konfigürasyonuna göre IP ayarla
    if (!netConfig.useDHCP) {
        addLog("Statik IP konfigürasyonu uygulanıyor...", INFO, "ETH");
        
        if (!ETH.config(netConfig.staticIP, netConfig.gateway, netConfig.subnet, netConfig.dns1, netConfig.dns2)) {
            addLog("❌ Statik IP konfigürasyonu başarısız!", ERROR, "ETH");
            addLog("DHCP'ye geri dönülüyor...", WARN, "ETH");
            
            // DHCP'ye fallback
            netConfig.useDHCP = true;
            ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        } else {
            addLog("✅ Statik IP konfigürasyonu başarılı", SUCCESS, "ETH");
            addLog("IP: " + netConfig.staticIP.toString(), INFO, "ETH");
            addLog("Gateway: " + netConfig.gateway.toString(), INFO, "ETH");
            addLog("Subnet: " + netConfig.subnet.toString(), INFO, "ETH");
            addLog("DNS1: " + netConfig.dns1.toString(), INFO, "ETH");
        }
    } else {
        addLog("DHCP ile IP adresi alınıyor...", INFO, "ETH");
    }
    
    // Bağlantı bekleme (max 15 saniye)
    unsigned long startTime = millis();
    const unsigned long CONNECT_TIMEOUT = 15000;
    
    addLog("Ethernet bağlantısı bekleniyor...", INFO, "ETH");
    
    while (!ETH.linkUp() && millis() - startTime < CONNECT_TIMEOUT) {
        delay(100);
        
        // Her 2 saniyede bir durum güncelle
        if ((millis() - startTime) % 2000 == 0) {
            addLog("Bağlantı bekleniyor... (" + String((millis() - startTime) / 1000) + "s)", DEBUG, "ETH");
        }
    }
    
    if (ETH.linkUp()) {
        // Bağlantı başarılı
        addLog("🎉 Ethernet bağlantısı başarılı!", SUCCESS, "ETH");
        addLog("📍 IP Adresi: " + ETH.localIP().toString(), SUCCESS, "ETH");
        addLog("🚪 Gateway: " + ETH.gatewayIP().toString(), INFO, "ETH");
        addLog("🔍 Subnet Mask: " + ETH.subnetMask().toString(), INFO, "ETH");
        addLog("🌐 DNS: " + ETH.dnsIP().toString(), INFO, "ETH");
        addLog("⚡ Link Hızı: " + String(ETH.linkSpeed()) + " Mbps", INFO, "ETH");
        addLog("🔀 Duplex: " + String(ETH.fullDuplex() ? "Full" : "Half"), INFO, "ETH");
        
        // DHCP'den alınan IP'yi settings'e kaydet
        if (netConfig.useDHCP) {
            settings.local_IP = ETH.localIP();
            settings.gateway = ETH.gatewayIP();
            settings.subnet = ETH.subnetMask();
            settings.primaryDNS = ETH.dnsIP();
            
            addLog("DHCP bilgileri settings'e kaydedildi", DEBUG, "ETH");
        }
        
        // Network test
        addLog("🔗 Network erişilebilirlik testi yapılıyor...", INFO, "ETH");
        // Bu kısımda ping test'i eklenebilir
        
    } else {
        // Bağlantı başarısız
        addLog("❌ Ethernet bağlantısı başarısız!", ERROR, "ETH");
        addLog("🔌 Kablo bağlantısını kontrol edin", WARN, "ETH");
        addLog("⚙️ Network ayarlarını kontrol edin", WARN, "ETH");
        
        // Fallback IP ayarları (emergency access)
        settings.local_IP.fromString("192.168.1.160");
        settings.gateway.fromString("192.168.1.1");
        settings.subnet.fromString("255.255.255.0");
        settings.primaryDNS.fromString("8.8.8.8");
        
        addLog("🆘 Acil durum IP ayarları yüklendi", WARN, "ETH");
    }
    
    // Ethernet event handler'ları ayarla
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_ETH_START:
                addLog("🔄 Ethernet başlatıldı", INFO, "ETH");
                break;
            case ARDUINO_EVENT_ETH_CONNECTED:
                addLog("🔌 Ethernet kablosu bağlandı", SUCCESS, "ETH");
                break;
            case ARDUINO_EVENT_ETH_GOT_IP:
                addLog("📶 IP adresi alındı: " + ETH.localIP().toString(), SUCCESS, "ETH");
                break;
            case ARDUINO_EVENT_ETH_DISCONNECTED:
                addLog("🔌 Ethernet kablosu çıkarıldı", ERROR, "ETH");
                break;
            case ARDUINO_EVENT_ETH_STOP:
                addLog("🛑 Ethernet durduruldu", WARN, "ETH");
                break;
            default:
                break;
        }
    });
    
    addLog("✅ Ethernet Advanced Init tamamlandı", SUCCESS, "ETH");
}