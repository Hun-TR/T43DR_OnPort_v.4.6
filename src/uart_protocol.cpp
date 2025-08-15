#include "uart_protocol.h"
#include "uart_handler.h"  // initUART() için eklendi
#include "log_system.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// Global değişkenler (header'da extern olarak tanımlı)
String lastResponse = "";
bool uartHealthy = true;
UARTStatistics uartStats = {0, 0, 0, 0, 0, 100.0};

// Frame durumları
enum FrameState {
    WAIT_START,
    READ_COMMAND,
    READ_LENGTH_HIGH,
    READ_LENGTH_LOW,
    READ_DATA,
    READ_CHECKSUM,
    WAIT_END
};

// CRC8 checksum hesaplama - İYİLEŞTİRİLMİŞ
uint8_t calculateCRC8(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return 0;
    
    uint8_t crc = 0x00;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07; // CRC-8 polynomial
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

// XOR checksum hesaplama (basit ve hızlı) - İYİLEŞTİRİLMİŞ
uint8_t calculateXORChecksum(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return 0;
    
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// Frame oluşturma - İYİLEŞTİRİLMİŞ
bool createFrame(UARTFrame& frame, uint8_t command, const uint8_t* data, uint16_t dataLength) {
    if (dataLength > MAX_FRAME_SIZE) {
        addLog("❌ Frame verisi çok büyük: " + String(dataLength) + "/" + String(MAX_FRAME_SIZE), ERROR, "UART");
        return false;
    }
    
    frame.command = command;
    frame.dataLength = dataLength;
    
    // Data kopyalama
    if (data != nullptr && dataLength > 0) {
        memcpy(frame.data, data, dataLength);
    }
    
    // Checksum hesapla (command + length + data)
    uint8_t checksumData[MAX_FRAME_SIZE + 3];
    checksumData[0] = command;
    checksumData[1] = (dataLength >> 8) & 0xFF;  // High byte
    checksumData[2] = dataLength & 0xFF;         // Low byte
    
    if (dataLength > 0 && data != nullptr) {
        memcpy(&checksumData[3], data, dataLength);
    }
    
    frame.checksum = calculateXORChecksum(checksumData, dataLength + 3);
    
    addLog("📦 Frame oluşturuldu - Cmd: 0x" + String(command, HEX) + 
           ", Len: " + String(dataLength) + 
           ", Checksum: 0x" + String(frame.checksum, HEX), DEBUG, "UART");
    
    return true;
}

// Frame gönderme (escape karakterleri ile) - İYİLEŞTİRİLMİŞ
bool sendFrame(const UARTFrame& frame) {
    if (!Serial2) {
        addLog("❌ UART portu açık değil", ERROR, "UART");
        return false;
    }
    
    // Buffer temizle
    while (Serial2.available()) {
        Serial2.read();
    }
    
    // Frame başlangıcı
    Serial2.write(FRAME_START_CHAR);
    
    // Command gönder (escape kontrolü ile)
    if (frame.command == FRAME_START_CHAR || frame.command == FRAME_END_CHAR || frame.command == FRAME_ESCAPE_CHAR) {
        Serial2.write(FRAME_ESCAPE_CHAR);
    }
    Serial2.write(frame.command);
    
    // Length gönder (2 byte, big-endian)
    uint8_t lengthHigh = (frame.dataLength >> 8) & 0xFF;
    uint8_t lengthLow = frame.dataLength & 0xFF;
    
    if (lengthHigh == FRAME_START_CHAR || lengthHigh == FRAME_END_CHAR || lengthHigh == FRAME_ESCAPE_CHAR) {
        Serial2.write(FRAME_ESCAPE_CHAR);
    }
    Serial2.write(lengthHigh);
    
    if (lengthLow == FRAME_START_CHAR || lengthLow == FRAME_END_CHAR || lengthLow == FRAME_ESCAPE_CHAR) {
        Serial2.write(FRAME_ESCAPE_CHAR);
    }
    Serial2.write(lengthLow);
    
    // Data gönder (escape kontrolü ile)
    for (uint16_t i = 0; i < frame.dataLength; i++) {
        if (frame.data[i] == FRAME_START_CHAR || frame.data[i] == FRAME_END_CHAR || frame.data[i] == FRAME_ESCAPE_CHAR) {
            Serial2.write(FRAME_ESCAPE_CHAR);
        }
        Serial2.write(frame.data[i]);
    }
    
    // Checksum gönder (escape kontrolü ile)
    if (frame.checksum == FRAME_START_CHAR || frame.checksum == FRAME_END_CHAR || frame.checksum == FRAME_ESCAPE_CHAR) {
        Serial2.write(FRAME_ESCAPE_CHAR);
    }
    Serial2.write(frame.checksum);
    
    // Frame sonu
    Serial2.write(FRAME_END_CHAR);
    
    // Flush ile gönderimi garanti et
    Serial2.flush();
    
    // İstatistik güncelle
    uartStats.totalFramesSent++;
    
    addLog("📤 Frame gönderildi - Cmd: 0x" + String(frame.command, HEX) + 
           ", Len: " + String(frame.dataLength) + 
           ", Total: " + String(uartStats.totalFramesSent), DEBUG, "UART");
    
    return true;
}

// Frame okuma (state machine ile) - İYİLEŞTİRİLMİŞ
bool receiveFrame(UARTFrame& frame, unsigned long timeout) {
    if (!Serial2) {
        addLog("❌ UART portu açık değil", ERROR, "UART");
        updateUARTStatistics(false, false, true);
        return false;
    }
    
    FrameState state = WAIT_START;
    unsigned long startTime = millis();
    uint16_t dataIndex = 0;
    bool escapeNext = false;
    uint8_t checksumData[MAX_FRAME_SIZE + 3];
    uint16_t checksumIndex = 0;
    
    // Frame değişkenlerini temizle
    memset(&frame, 0, sizeof(UARTFrame));
    
    while (millis() - startTime < timeout) {
        if (Serial2.available()) {
            uint8_t byte = Serial2.read();
            
            // Escape karakteri kontrolü
            if (byte == FRAME_ESCAPE_CHAR && !escapeNext) {
                escapeNext = true;
                continue;
            }
            
            // Escape sonrası karakter
            if (escapeNext) {
                escapeNext = false;
                // Byte'ı normal olarak işle
            } else {
                // Start/End karakterlerini kontrol et
                if (byte == FRAME_START_CHAR) {
                    state = READ_COMMAND;
                    dataIndex = 0;
                    checksumIndex = 0;
                    memset(&frame, 0, sizeof(UARTFrame));
                    continue;
                } else if (byte == FRAME_END_CHAR && state == READ_CHECKSUM) {
                    // Frame tamamlandı, checksum kontrolü yap
                    uint8_t calculatedChecksum = calculateXORChecksum(checksumData, checksumIndex);
                    
                    if (calculatedChecksum == frame.checksum) {
                        uartStats.totalFramesReceived++;
                        updateUARTStatistics(true, false, false);
                        
                        addLog("✅ Frame alındı - Cmd: 0x" + String(frame.command, HEX) + 
                               ", Len: " + String(frame.dataLength) + 
                               ", Checksum: OK", DEBUG, "UART");
                        return true;
                    } else {
                        uartStats.checksumErrors++;
                        updateUARTStatistics(false, true, false);
                        
                        addLog("❌ Checksum hatası! Beklenen: 0x" + String(calculatedChecksum, HEX) + 
                               ", Alınan: 0x" + String(frame.checksum, HEX), ERROR, "UART");
                        return false;
                    }
                }
            }
            
            // State machine
            switch (state) {
                case WAIT_START:
                    // Start karakteri bekleniyor
                    break;
                    
                case READ_COMMAND:
                    frame.command = byte;
                    if (checksumIndex < sizeof(checksumData)) {
                        checksumData[checksumIndex++] = byte;
                    }
                    state = READ_LENGTH_HIGH;
                    break;
                    
                case READ_LENGTH_HIGH:
                    frame.dataLength = (byte << 8);
                    if (checksumIndex < sizeof(checksumData)) {
                        checksumData[checksumIndex++] = byte;
                    }
                    state = READ_LENGTH_LOW;
                    break;
                    
                case READ_LENGTH_LOW:
                    frame.dataLength |= byte;
                    if (checksumIndex < sizeof(checksumData)) {
                        checksumData[checksumIndex++] = byte;
                    }
                    
                    if (frame.dataLength > MAX_FRAME_SIZE) {
                        addLog("❌ Frame verisi çok büyük: " + String(frame.dataLength), ERROR, "UART");
                        updateUARTStatistics(false, false, false);
                        return false;
                    }
                    
                    if (frame.dataLength > 0) {
                        state = READ_DATA;
                        dataIndex = 0;
                    } else {
                        state = READ_CHECKSUM;
                    }
                    break;
                    
                case READ_DATA:
                    frame.data[dataIndex] = byte;
                    if (checksumIndex < sizeof(checksumData)) {
                        checksumData[checksumIndex++] = byte;
                    }
                    dataIndex++;
                    
                    if (dataIndex >= frame.dataLength) {
                        state = READ_CHECKSUM;
                    }
                    break;
                    
                case READ_CHECKSUM:
                    frame.checksum = byte;
                    state = WAIT_END;
                    break;
                    
                case WAIT_END:
                    // End karakteri bekleniyor (yukarıda kontrol ediliyor)
                    break;
            }
        }
        
        delay(1);
    }
    
    // Timeout
    uartStats.timeoutErrors++;
    updateUARTStatistics(false, false, true);
    
    addLog("⏱️ Frame okuma timeout (" + String(timeout) + "ms)", WARN, "UART");
    return false;
}

// Komut gönder ve yanıt al (yeni protokol ile) - İYİLEŞTİRİLMİŞ
bool sendCommandWithProtocol(uint8_t command, const String& data, String& response, unsigned long timeout) {
    UARTFrame txFrame, rxFrame;
    
    // Timeout varsayılan değer kontrolü
    if (timeout == 0) {
        timeout = FRAME_TIMEOUT;
    }
    
    // TX frame oluştur
    if (!createFrame(txFrame, command, (uint8_t*)data.c_str(), data.length())) {
        addLog("❌ TX frame oluşturulamadı", ERROR, "UART");
        return false;
    }
    
    // Frame gönder
    if (!sendFrame(txFrame)) {
        addLog("❌ Frame gönderilemedi", ERROR, "UART");
        return false;
    }
    
    // Yanıt bekle
    if (!receiveFrame(rxFrame, timeout)) {
        addLog("❌ Yanıt frame'i alınamadı", ERROR, "UART");
        return false;
    }
    
    // Yanıt komutunu kontrol et
    if (rxFrame.command == CMD_NACK) {
        addLog("❌ Backend NACK yanıtı gönderdi", ERROR, "UART");
        response = "NACK";
        return false;
    }
    
    // Yanıtı string'e çevir
    response = "";
    for (uint16_t i = 0; i < rxFrame.dataLength; i++) {
        response += (char)rxFrame.data[i];
    }
    
    addLog("✅ Komut başarılı - Yanıt: " + response.substring(0, 20) + 
           (response.length() > 20 ? "..." : ""), DEBUG, "UART");
    
    return true;
}

// Gelişmiş komut gönderme fonksiyonları - İYİLEŞTİRİLMİŞ

bool requestTimeWithProtocol() {
    String response;
    if (sendCommandWithProtocol(CMD_GET_TIME, "", response, 3000)) {
        // Response formatları: "DDMMYYHHMMSS" veya "DATE:DDMMYY,TIME:HHMMSS"
        if (response.length() >= 12) {
            addLog("✅ Zaman bilgisi alındı: " + response, SUCCESS, "UART");
            lastResponse = response;
            return true;
        } else {
            addLog("❌ Geçersiz zaman formatı: " + response, ERROR, "UART");
        }
    }
    return false;
}

bool sendNTPConfigWithProtocol(const String& server1, const String& server2) {
    String data = server1 + "," + server2;
    String response;
    
    if (sendCommandWithProtocol(CMD_SET_NTP, data, response, 3000)) {
        if (response == "ACK" || response.indexOf("OK") >= 0) {
            addLog("✅ NTP config başarıyla gönderildi", SUCCESS, "UART");
            return true;
        } else {
            addLog("⚠️ NTP config yanıtı: " + response, WARN, "UART");
            return true; // Yanıt varsa başarılı say
        }
    }
    addLog("❌ NTP config gönderilemedi", ERROR, "UART");
    return false;
}

bool requestFirstFaultWithProtocol() {
    String response;
    if (sendCommandWithProtocol(CMD_GET_FIRST_FAULT, "", response, 5000)) {
        if (response.length() > 0) {
            addLog("✅ İlk arıza kaydı alındı (" + String(response.length()) + " byte)", SUCCESS, "UART");
            lastResponse = response;
            return true;
        }
    }
    addLog("❌ İlk arıza kaydı alınamadı", ERROR, "UART");
    return false;
}

bool requestNextFaultWithProtocol() {
    String response;
    if (sendCommandWithProtocol(CMD_GET_NEXT_FAULT, "", response, 5000)) {
        if (response.length() > 0) {
            addLog("✅ Sonraki arıza kaydı alındı (" + String(response.length()) + " byte)", SUCCESS, "UART");
            lastResponse = response;
            return true;
        } else {
            addLog("ℹ️ Daha fazla arıza kaydı yok", INFO, "UART");
            lastResponse = "EOL"; // End of List
            return true;
        }
    }
    addLog("❌ Sonraki arıza kaydı alınamadı", ERROR, "UART");
    return false;
}

// Ping komutu - bağlantı testi - İYİLEŞTİRİLMİŞ
bool pingBackend() {
    String response;
    if (sendCommandWithProtocol(CMD_PING, "PING", response, 2000)) {
        if (response == "PONG" || response == "ACK" || response.indexOf("OK") >= 0) {
            return true;
        } else {
            addLog("🏓 Ping yanıtı: " + response, DEBUG, "UART");
            return true; // Herhangi bir yanıt varsa backend canlı
        }
    }
    return false;
}

// UART sağlık kontrolü - DÜZELTİLMİŞ VERSİYON
void checkUARTHealthWithProtocol() {
    static unsigned long lastPing = 0;
    static int consecutiveFailures = 0;
    const unsigned long PING_INTERVAL = 30000; // 30 saniye
    
    if (millis() - lastPing > PING_INTERVAL) {
        lastPing = millis();
        
        if (pingBackend()) {
            consecutiveFailures = 0;
            if (!uartHealthy) {
                uartHealthy = true;
                addLog("✅ UART bağlantısı düzeldi", SUCCESS, "UART");
            }
        } else {
            consecutiveFailures++;
            addLog("⚠️ UART ping başarısız (#" + String(consecutiveFailures) + ")", WARN, "UART");
            
            if (consecutiveFailures >= 3) {
                uartHealthy = false;
                addLog("❌ UART bağlantısı kayıp", ERROR, "UART");
                
                // UART'ı yeniden başlat - DÜZELTİLMİŞ
                if (consecutiveFailures >= 5) {
                    addLog("🔄 UART yeniden başlatılıyor...", WARN, "UART");
                    initUART(); // uart_handler.h'den import edildi
                    consecutiveFailures = 0;
                    
                    // İstatistikleri sıfırla
                    uartStats.frameErrors++;
                }
            }
        }
    }
}

// İstatistikleri güncelle - İYİLEŞTİRİLMİŞ
void updateUARTStatistics(bool success, bool checksumError, bool timeoutError) {
    if (success) {
        // Başarılı işlem
        // totalFramesReceived zaten receiveFrame'de artırılıyor
    } else {
        // Başarısız işlem
        if (checksumError) {
            uartStats.checksumErrors++;
        } else if (timeoutError) {
            uartStats.timeoutErrors++;
        } else {
            uartStats.frameErrors++;
        }
    }
    
    // Başarı oranını hesapla
    unsigned long totalOperations = uartStats.totalFramesSent;
    unsigned long totalErrors = uartStats.checksumErrors + uartStats.timeoutErrors + uartStats.frameErrors;
    
    if (totalOperations > 0) {
        uartStats.successRate = ((float)(totalOperations - totalErrors) / (float)totalOperations) * 100.0;
        
        // Negatif değerleri önle
        if (uartStats.successRate < 0) {
            uartStats.successRate = 0.0;
        }
    }
    
    // Sağlık durumunu güncelle
    if (uartStats.successRate < 50.0) {
        uartHealthy = false;
    } else if (uartStats.successRate > 80.0) {
        uartHealthy = true;
    }
}

// İstatistikleri JSON olarak döndür - İYİLEŞTİRİLMİŞ
String getUARTStatisticsJSON() {
    JsonDocument doc;  // StaticJsonDocument<512> doc; yerine
    
    doc["totalSent"] = uartStats.totalFramesSent;
    doc["totalReceived"] = uartStats.totalFramesReceived;
    doc["checksumErrors"] = uartStats.checksumErrors;
    doc["timeoutErrors"] = uartStats.timeoutErrors;
    doc["frameErrors"] = uartStats.frameErrors;
    doc["successRate"] = round(uartStats.successRate * 100) / 100.0;
    doc["healthy"] = uartHealthy;
    doc["lastResponse"] = lastResponse.length() > 50 ? lastResponse.substring(0, 47) + "..." : lastResponse;
    doc["timestamp"] = millis();
    
    // Ek bilgiler
    unsigned long totalErrors = uartStats.checksumErrors + uartStats.timeoutErrors + uartStats.frameErrors;
    doc["totalErrors"] = totalErrors;
    doc["uptime"] = millis() / 1000;
    
    String output;
    serializeJson(doc, output);
    return output;
}

// Gelişmiş komutlar - EK FONKSİYONLAR

bool setBaudRateWithProtocol(long baudRate) {
    String data = String(baudRate);
    String response;
    
    if (sendCommandWithProtocol(CMD_SET_BAUDRATE, data, response, 3000)) {
        if (response == "ACK" || response.indexOf("OK") >= 0) {
            addLog("✅ BaudRate ayarı gönderildi: " + String(baudRate), SUCCESS, "UART");
            return true;
        } else {
            addLog("⚠️ BaudRate yanıtı: " + response, WARN, "UART");
        }
    }
    return false;
}

bool getStatusWithProtocol(String& statusData) {
    if (sendCommandWithProtocol(CMD_GET_STATUS, "", statusData, 3000)) {
        if (statusData.length() > 0) {
            addLog("✅ Backend status alındı", SUCCESS, "UART");
            return true;
        }
    }
    return false;
}

bool resetBackendWithProtocol() {
    String response;
    if (sendCommandWithProtocol(CMD_RESET, "RESET", response, 5000)) {
        if (response == "ACK" || response.indexOf("OK") >= 0) {
            addLog("✅ Backend reset komutu gönderildi", SUCCESS, "UART");
            return true;
        }
    }
    return false;
}

bool clearFaultsWithProtocol() {
    String response;
    if (sendCommandWithProtocol(CMD_CLEAR_FAULTS, "", response, 3000)) {
        if (response == "ACK" || response.indexOf("OK") >= 0) {
            addLog("✅ Arıza kayıtları temizlendi", SUCCESS, "UART");
            return true;
        }
    }
    return false;
}