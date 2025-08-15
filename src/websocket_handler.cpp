// websocket_handler.cpp - Build Error'ları Düzeltilmiş Versiyon

#include "websocket_handler.h"
#include "log_system.h"
#include "settings.h"
#include "auth_system.h"
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// External functions
extern String getCurrentDateTime();
extern String getUptime();
extern bool isTimeSynced();

// WebSocket server instance
WebSocketsServer webSocket(WEBSOCKET_PORT);

// Client authentication tracking
struct WSClient {
    bool authenticated;
    unsigned long lastPing;
    String sessionId;
    IPAddress clientIP;
    unsigned long connectTime;
    String userAgent;
};

WSClient wsClients[MAX_WS_CLIENTS];

// Forward declarations
void sendInitialDataToClient(uint8_t clientNum);
void sendStatusToClient(uint8_t clientNum);
void sendLogsToClient(uint8_t clientNum);
bool isValidClientIndex(uint8_t clientNum);

// Client index validation
bool isValidClientIndex(uint8_t clientNum) {
    return (clientNum < MAX_WS_CLIENTS);
}

// WebSocket başlatma
void initWebSocket() {
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    webSocket.enableHeartbeat(30000, 5000, 3);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        wsClients[i].authenticated = false;
        wsClients[i].lastPing = 0;
        wsClients[i].sessionId = "";
        wsClients[i].clientIP = IPAddress(0,0,0,0);
        wsClients[i].connectTime = 0;
        wsClients[i].userAgent = "";
    }
    
    addLog("✅ WebSocket server başlatıldı (Port " + String(WEBSOCKET_PORT) + 
           ", Max Clients: " + String(MAX_WS_CLIENTS) + ")", SUCCESS, "WS");
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (!isValidClientIndex(num)) {
        addLog("❌ WebSocket client ID geçersiz: " + String(num) + "/" + String(MAX_WS_CLIENTS), ERROR, "WS");
        return;
    }
    
    switch(type) {
        case WStype_DISCONNECTED: {
            wsClients[num].authenticated = false;
            wsClients[num].sessionId = "";
            wsClients[num].clientIP = IPAddress(0,0,0,0);
            wsClients[num].connectTime = 0;
            wsClients[num].userAgent = "";
            
            addLog("📤 WebSocket client #" + String(num) + " bağlantısı kesildi", INFO, "WS");
            break;
        }
        
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            wsClients[num].clientIP = ip;
            wsClients[num].lastPing = millis();
            wsClients[num].connectTime = millis();
            wsClients[num].authenticated = false;
            
            addLog("📥 WebSocket client #" + String(num) + " bağlandı: " + ip.toString(), INFO, "WS");
            
            // İlk bağlantıda authentication isteği gönder - YENİ JSON SYNTAX
            JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
            doc["type"] = "auth_required";
            doc["message"] = "Authentication required for WebSocket access";
            doc["timestamp"] = millis();
            doc["serverTime"] = getCurrentDateTime();
            doc["clientId"] = num;
            
            String output;
            serializeJson(doc, output);
            webSocket.sendTXT(num, output);
            
            break;
        }
        
        case WStype_TEXT: {
            if (length > 1024) {
                addLog("❌ WebSocket mesajı çok büyük: " + String(length) + " bytes", ERROR, "WS");
                webSocket.sendTXT(num, "{\"type\":\"error\",\"message\":\"Message too large\"}");
                return;
            }
            
            char* message = new char[length + 1];
            memcpy(message, payload, length);
            message[length] = '\0';
            
            // JSON parse et - YENİ SYNTAX
            JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
            DeserializationError error = deserializeJson(doc, message);
            
            delete[] message;
            
            if (error) {
                addLog("❌ WebSocket JSON parse hatası: " + String(error.c_str()), ERROR, "WS");
                JsonDocument errorDoc;  // StaticJsonDocument yerine JsonDocument
                errorDoc["type"] = "error";
                errorDoc["message"] = "Invalid JSON format";
                errorDoc["error"] = error.c_str();
                
                String errorOutput;
                serializeJson(errorDoc, errorOutput);
                webSocket.sendTXT(num, errorOutput);
                return;
            }
            
            String cmd = doc["cmd"] | "";
            
            if (cmd.length() > 50) {
                addLog("❌ WebSocket komut çok uzun: " + String(cmd.length()), ERROR, "WS");
                return;
            }
            
            // Authentication kontrolü ve komut işleme
            if (cmd == "auth") {
                String token = doc["token"] | "";
                String clientInfo = doc["userAgent"] | "Unknown";
                
                if (settings.isLoggedIn && (token.startsWith("session_") || token.length() > 10)) {
                    wsClients[num].authenticated = true;
                    wsClients[num].lastPing = millis();
                    wsClients[num].sessionId = token;
                    wsClients[num].userAgent = clientInfo.substring(0, 100);
                    
                    JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                    response["type"] = "auth_success";
                    response["message"] = "WebSocket authentication successful";
                    response["clientId"] = num;
                    response["serverTime"] = getCurrentDateTime();
                    response["sessionTimeout"] = settings.SESSION_TIMEOUT / 1000;
                    response["timestamp"] = millis();
                    
                    String output;
                    serializeJson(response, output);
                    webSocket.sendTXT(num, output);
                    
                    addLog("✅ WebSocket client #" + String(num) + " kimlik doğrulaması başarılı", SUCCESS, "WS");
                    
                    delay(500);
                    sendInitialDataToClient(num);
                    
                } else {
                    JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                    response["type"] = "auth_failed";
                    response["message"] = "Authentication failed - invalid session";
                    response["reason"] = settings.isLoggedIn ? "invalid_token" : "no_active_session";
                    response["timestamp"] = millis();
                    
                    String output;
                    serializeJson(response, output);
                    webSocket.sendTXT(num, output);
                    
                    addLog("❌ WebSocket client #" + String(num) + " kimlik doğrulaması başarısız", WARN, "WS");
                    
                    delay(2000);
                    webSocket.disconnect(num);
                }
            }
            // Authenticated user commands
            else if (wsClients[num].authenticated) {
                if (cmd == "ping") {
                    wsClients[num].lastPing = millis();
                    
                    JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                    response["type"] = "pong";
                    response["timestamp"] = millis();
                    response["clientId"] = num;
                    response["latency"] = doc["timestamp"] ? (millis() - doc["timestamp"].as<unsigned long>()) : 0;
                    
                    String output;
                    serializeJson(response, output);
                    webSocket.sendTXT(num, output);
                }
                else if (cmd == "get_status") {
                    sendStatusToClient(num);
                }
                else if (cmd == "get_logs") {
                    sendLogsToClient(num);
                }
                else if (cmd == "get_info") {
                    JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                    response["type"] = "system_info";
                    response["deviceName"] = settings.deviceName;
                    response["tmName"] = settings.transformerStation;
                    response["version"] = "3.0";
                    response["uptime"] = getUptime();
                    response["freeHeap"] = ESP.getFreeHeap();
                    response["chipModel"] = ESP.getChipModel();
                    response["cpuFreq"] = ESP.getCpuFreqMHz();
                    response["timestamp"] = millis();
                    
                    String output;
                    serializeJson(response, output);
                    webSocket.sendTXT(num, output);
                }
                else {
                    JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                    response["type"] = "error";
                    response["message"] = "Unknown command: " + cmd;
                    response["availableCommands"] = "ping, get_status, get_logs, get_info";
                    response["timestamp"] = millis();
                    
                    String output;
                    serializeJson(response, output);
                    webSocket.sendTXT(num, output);
                }
            }
            else {
                JsonDocument response;  // StaticJsonDocument yerine JsonDocument
                response["type"] = "error";
                response["message"] = "Authentication required";
                response["timestamp"] = millis();
                
                String output;
                serializeJson(response, output);
                webSocket.sendTXT(num, output);
            }
            
            break;
        }
        
        case WStype_BIN:
            addLog("⚠️ WebSocket binary veri alındı (desteklenmiyor) - Client #" + String(num), WARN, "WS");
            break;
            
        case WStype_ERROR:
            if (isValidClientIndex(num)) {
                wsClients[num].authenticated = false;
            }
            addLog("❌ WebSocket hatası - Client #" + String(num), ERROR, "WS");
            break;
            
        case WStype_PING:
            if (isValidClientIndex(num)) {
                wsClients[num].lastPing = millis();
            }
            break;
            
        case WStype_PONG:
            if (isValidClientIndex(num)) {
                wsClients[num].lastPing = millis();
            }
            break;
            
        default:
            addLog("🔍 WebSocket bilinmeyen event türü: " + String(type) + " - Client #" + String(num), DEBUG, "WS");
            break;
    }
}

// İlk veriyi cliente gönder
void sendInitialDataToClient(uint8_t clientNum) {
    if (!isValidClientIndex(clientNum) || !wsClients[clientNum].authenticated) {
        return;
    }
    
    addLog("📊 Client #" + String(clientNum) + " için initial data gönderiliyor", DEBUG, "WS");
    
    sendStatusToClient(clientNum);
    delay(100);
    sendLogsToClient(clientNum);
    
    addLog("✅ Client #" + String(clientNum) + " initial data gönderildi", DEBUG, "WS");
}

// Belirli cliente durum gönder
void sendStatusToClient(uint8_t clientNum) {
    if (!isValidClientIndex(clientNum) || !wsClients[clientNum].authenticated) {
        return;
    }
    
    JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
    doc["type"] = "status";
    doc["datetime"] = getCurrentDateTime();
    doc["uptime"] = getUptime();
    doc["deviceName"] = settings.deviceName;
    doc["tmName"] = settings.transformerStation;
    doc["deviceIP"] = settings.local_IP.toString();
    doc["baudRate"] = settings.currentBaudRate;
    doc["ethernetStatus"] = ETH.linkUp();
    doc["ethernetSpeed"] = ETH.linkSpeed();
    doc["timeSynced"] = isTimeSynced();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wsClients"] = getWebSocketClientCount();
    doc["totalLogs"] = totalLogs;
    doc["sessionActive"] = settings.isLoggedIn;
    doc["timestamp"] = millis();
    
    // Sistem load bilgisi
    static unsigned long lastCpuTime = 0;
    unsigned long currentTime = millis();
    if (lastCpuTime > 0) {
        doc["systemLoad"] = (currentTime - lastCpuTime) > 1100 ? "high" : "normal";
    }
    lastCpuTime = currentTime;
    
    String output;
    serializeJson(doc, output);
    webSocket.sendTXT(clientNum, output);
}

// Belirli cliente logları gönder
void sendLogsToClient(uint8_t clientNum) {
    if (!isValidClientIndex(clientNum) || !wsClients[clientNum].authenticated) {
        return;
    }
    
    // Son 15 logu gönder
    int logCount = min(15, totalLogs);
    
    for (int i = 0; i < logCount; i++) {
        int idx = (logIndex - 1 - i + 50) % 50;
        if (logs[idx].message.length() > 0) {
            JsonDocument logDoc;  // StaticJsonDocument yerine JsonDocument
            logDoc["type"] = "log";
            logDoc["timestamp"] = logs[idx].timestamp;
            logDoc["message"] = logs[idx].message;
            logDoc["level"] = logLevelToString(logs[idx].level);
            logDoc["source"] = logs[idx].source;
            logDoc["millis"] = logs[idx].millis_time;
            logDoc["sequence"] = logCount - i;
            
            String output;
            serializeJson(logDoc, output);
            webSocket.sendTXT(clientNum, output);
            
            delay(20);
        }
    }
    
    // Log gönderimi tamamlandı sinyali
    JsonDocument endDoc;  // StaticJsonDocument yerine JsonDocument
    endDoc["type"] = "logs_complete";
    endDoc["totalSent"] = logCount;
    endDoc["timestamp"] = millis();
    
    String endOutput;
    serializeJson(endDoc, endOutput);
    webSocket.sendTXT(clientNum, endOutput);
}

// WebSocket loop
void handleWebSocket() {
    webSocket.loop();
    
    // Client timeout kontrolü - 60 saniye
    static unsigned long lastTimeoutCheck = 0;
    unsigned long now = millis();
    
    if (now - lastTimeoutCheck > 60000) {
        lastTimeoutCheck = now;
        
        int timeoutCount = 0;
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (wsClients[i].authenticated && wsClients[i].lastPing > 0) {
                if (now - wsClients[i].lastPing > 120000) { // 2 dakika timeout
                    addLog("⏰ WebSocket client #" + String(i) + " timeout (" + 
                           wsClients[i].clientIP.toString() + ") - " + 
                           String((now - wsClients[i].lastPing) / 1000) + "s", WARN, "WS");
                    
                    webSocket.disconnect(i);
                    wsClients[i].authenticated = false;
                    wsClients[i].lastPing = 0;
                    timeoutCount++;
                }
            }
        }
        
        if (timeoutCount > 0) {
            addLog("🧹 " + String(timeoutCount) + " WebSocket client timeout ile temizlendi", INFO, "WS");
        }
    }
}

// Log mesajı broadcast - RATE LIMITED
void broadcastLog(const String& message, const String& level, const String& source) {
    static unsigned long lastBroadcast = 0;
    static int broadcastCount = 0;
    unsigned long now = millis();
    
    if (now - lastBroadcast > 1000) {
        broadcastCount = 0;
        lastBroadcast = now;
    }
    
    if (broadcastCount >= 5) {
        return;
    }
    
    JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
    doc["type"] = "log";
    doc["timestamp"] = getFormattedTimestamp();
    doc["message"] = message;
    doc["level"] = level;
    doc["source"] = source;
    doc["millis"] = millis();
    doc["broadcast"] = true;
    
    String output;
    serializeJson(doc, output);
    
    int sentCount = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            webSocket.sendTXT(i, output);
            sentCount++;
        }
    }
    
    if (sentCount > 0) {
        broadcastCount++;
    }
}

// Sistem durumu broadcast
void broadcastStatus() {
    static unsigned long lastStatusBroadcast = 0;
    if (millis() - lastStatusBroadcast < 5000) {
        return;
    }
    lastStatusBroadcast = millis();
    
    JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
    doc["type"] = "status_update";
    doc["datetime"] = getCurrentDateTime();
    doc["uptime"] = getUptime();
    doc["ethernetStatus"] = ETH.linkUp();
    doc["timeSynced"] = isTimeSynced();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wsClients"] = getWebSocketClientCount();
    doc["sessionActive"] = settings.isLoggedIn;
    doc["timestamp"] = millis();
    
    String output;
    serializeJson(doc, output);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            webSocket.sendTXT(i, output);
        }
    }
}

// Arıza verisi broadcast
void broadcastFault(const String& faultData) {
    if (faultData.length() == 0) {
        return;
    }
    
    JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
    doc["type"] = "fault";
    doc["timestamp"] = getFormattedTimestamp();
    doc["data"] = faultData.length() > 200 ? faultData.substring(0, 197) + "..." : faultData;
    doc["fullLength"] = faultData.length();
    doc["millis"] = millis();
    
    String output;
    serializeJson(doc, output);
    
    int sentCount = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            webSocket.sendTXT(i, output);
            sentCount++;
        }
    }
    
    if (sentCount > 0) {
        addLog("📡 Arıza verisi " + String(sentCount) + " client'a broadcast edildi", DEBUG, "WS");
    }
}

// Belirli bir cliente mesaj gönder - STRING REFERENCE SORUNU DÜZELTİLDİ
void sendToClient(uint8_t clientNum, const String& message) {
    if (!isValidClientIndex(clientNum) || !wsClients[clientNum].authenticated) {
        return;
    }
    
    if (message.length() > 1024) {
        addLog("⚠️ Client #" + String(clientNum) + " için mesaj çok büyük: " + String(message.length()), WARN, "WS");
        return;
    }
    
    // String kopyası oluştur (const referans sorunu çözümü)
    String messageCopy = message;
    webSocket.sendTXT(clientNum, messageCopy);
}

// Tüm clientlara mesaj gönder - STRING REFERENCE SORUNU DÜZELTİLDİ
void sendToAllClients(const String& message) {
    if (message.length() > 1024) {
        addLog("⚠️ Broadcast mesajı çok büyük: " + String(message.length()), WARN, "WS");
        return;
    }
    
    // String kopyası oluştur (const referans sorunu çözümü)
    String messageCopy = message;
    
    int sentCount = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            webSocket.sendTXT(i, messageCopy);
            sentCount++;
        }
    }
    
    if (sentCount > 0) {
        addLog("📢 Mesaj " + String(sentCount) + " client'a gönderildi", DEBUG, "WS");
    }
}

// WebSocket bağlantı durumu
bool isWebSocketConnected() {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            return true;
        }
    }
    return false;
}

// Bağlı client sayısı
int getWebSocketClientCount() {
    int authenticatedCount = 0;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].authenticated) {
            authenticatedCount++;
        }
    }
    
    return authenticatedCount;
}

// WebSocket durum bilgisi - YENİ JSON SYNTAX
String getWebSocketStatusJSON() {
    JsonDocument doc;  // StaticJsonDocument yerine JsonDocument
    
    doc["serverRunning"] = true;
    doc["port"] = WEBSOCKET_PORT;
    doc["maxClients"] = MAX_WS_CLIENTS;
    doc["authenticatedClients"] = getWebSocketClientCount();
    
    // YENİ JSON API - createNestedArray yerine to<JsonArray>()
    JsonArray clients = doc["clients"].to<JsonArray>();
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].clientIP != IPAddress(0,0,0,0)) {
            // YENİ JSON API - createNestedObject yerine add<JsonObject>()
            JsonObject client = clients.add<JsonObject>();
            client["id"] = i;
            client["ip"] = wsClients[i].clientIP.toString();
            client["authenticated"] = wsClients[i].authenticated;
            client["lastPing"] = wsClients[i].lastPing;
            client["connectTime"] = wsClients[i].connectTime;
            client["sessionId"] = wsClients[i].sessionId.substring(0, 10) + "...";
            client["userAgent"] = wsClients[i].userAgent.substring(0, 50);
            
            if (wsClients[i].lastPing > 0) {
                client["lastPingAgo"] = (millis() - wsClients[i].lastPing) / 1000;
            }
            
            if (wsClients[i].connectTime > 0) {
                client["connectedFor"] = (millis() - wsClients[i].connectTime) / 1000;
            }
        }
    }
    
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;
    
    String output;
    serializeJson(doc, output);
    return output;
}

// WebSocket temizleme
void cleanupWebSocketClients() {
    int cleanedCount = 0;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].clientIP != IPAddress(0,0,0,0)) {
            if (millis() - wsClients[i].lastPing > 300000) {
                webSocket.disconnect(i);
                wsClients[i].authenticated = false;
                wsClients[i].clientIP = IPAddress(0,0,0,0);
                wsClients[i].lastPing = 0;
                wsClients[i].connectTime = 0;
                wsClients[i].sessionId = "";
                wsClients[i].userAgent = "";
                cleanedCount++;
            }
        }
    }
    
    if (cleanedCount > 0) {
        addLog("🧹 " + String(cleanedCount) + " eski WebSocket client temizlendi", INFO, "WS");
    }
}

// Acil durum - Tüm clientları kes
void disconnectAllWebSocketClients() {
    addLog("🚨 Tüm WebSocket clientları kesiliyor", WARN, "WS");
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (wsClients[i].clientIP != IPAddress(0,0,0,0)) {
            webSocket.disconnect(i);
            wsClients[i].authenticated = false;
            wsClients[i].clientIP = IPAddress(0,0,0,0);
            wsClients[i].lastPing = 0;
            wsClients[i].connectTime = 0;
            wsClients[i].sessionId = "";
            wsClients[i].userAgent = "";
        }
    }
    
    addLog("✅ Tüm WebSocket clientları kesildi", INFO, "WS");
}