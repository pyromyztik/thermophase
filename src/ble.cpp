#include "ble.hpp"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstdarg>
#include <cmath>

// Standard Nordic UART Service (NUS) UUIDs
#define SERVICE_UUID_NUS       "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// HM-10 Compatible Service UUIDs (default for Web-Bluetooth-Terminal)
#define SERVICE_UUID_HM10      "0000FFE0-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_UUID_HM10 "0000FFE1-0000-1000-8000-00805F9B34FB"

static BLEServer* pServer = nullptr;
static BLECharacteristic* pTxCharacteristic = nullptr;
static BLECharacteristic* pRxCharacteristic = nullptr;
static BLECharacteristic* pHm10Characteristic = nullptr;
static volatile bool deviceConnected = false;
static bool bleEnabled = true;
static bool bleInitialized = false;
static char connectedClientInfo[32] = "";
static volatile bool bleStreamingActive = false;
static volatile uint16_t currentPeerMTU = 23;

// Ring buffer for incoming RX bytes
static constexpr size_t RX_BUFFER_SIZE = 256;
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

// Asynchronous BLE Frame Snapshot Buffer & Worker Task
struct BLEFrameSnapshot {
    float temperatures[768];
    float minTemp;
    float maxTemp;
    uint32_t frameIdx;
};

static BLEFrameSnapshot s_bleFrameBuffer;
static BLEFrameSnapshot s_bleWorkerSnapshot; // Statically allocated to prevent stack overflow
static SemaphoreHandle_t s_bleFrameMutex = nullptr;
static TaskHandle_t s_bleStreamTaskHandle = nullptr;
static volatile bool s_bleNewFrameAvailable = false;
alignas(4) static uint8_t s_blePacketBuffer[4 + 768 * sizeof(float)]; // 3076 bytes aligned (float32)

static uint16_t s_activeConnId = 0;
static bool s_hasActiveConn = false;

static void bleStreamTask(void* pvParameters);

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
    }

    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override {
        deviceConnected = true;
        if (param) {
            s_activeConnId = param->connect.conn_id;
            s_hasActiveConn = true;
            snprintf(connectedClientInfo, sizeof(connectedClientInfo),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->connect.remote_bda[0],
                     param->connect.remote_bda[1],
                     param->connect.remote_bda[2],
                     param->connect.remote_bda[3],
                     param->connect.remote_bda[4],
                     param->connect.remote_bda[5]);
            Serial.printf("[BLE] Peer connected: %s (connId=%u)\n", connectedClientInfo, (unsigned int)s_activeConnId);
        }
    }

    void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        if (param) {
            currentPeerMTU = param->mtu.mtu;
            Serial.printf("[BLE] MTU negotiated with peer: %u bytes\n", (unsigned int)currentPeerMTU);
        }
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        s_hasActiveConn = false;
        connectedClientInfo[0] = '\0';
        bleStreamingActive = false; // Automatically halt active frame streaming
        Serial.println("[BLE] Peer disconnected.");
        if (bleEnabled) {
            pServer->startAdvertising();
        }
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            for (size_t i = 0; i < rxValue.length(); ++i) {
                size_t nextHead = (rxHead + 1) % RX_BUFFER_SIZE;
                if (nextHead != rxTail) {
                    rxBuffer[rxHead] = (uint8_t)rxValue[i];
                    rxHead = nextHead;
                }
            }
        }
    }
};

class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t pass_key) override {}
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {}
    bool onConfirmPIN(uint32_t pin) override { return true; }
};

bool initBLE(const char* deviceName) {
    if (bleInitialized) return true;

    BLEDevice::init(deviceName ? deviceName : "thermophase");
    BLEDevice::setMTU(517);

    // Enable "Just Works" pairing for Windows & phone OS bonding
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    // Create the BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // 1. Create Nordic UART Service (NUS)
    BLEService* pNusService = pServer->createService(SERVICE_UUID_NUS);
    pTxCharacteristic = pNusService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pRxCharacteristic = pNusService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());
    pNusService->start();

    // 2. Create HM-10 Compatible Service (for Web-Bluetooth-Terminal)
    BLEService* pHm10Service = pServer->createService(SERVICE_UUID_HM10);
    pHm10Characteristic = pHm10Service->createCharacteristic(
        CHARACTERISTIC_UUID_HM10,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pHm10Characteristic->addDescriptor(new BLE2902());
    pHm10Characteristic->setCallbacks(new RxCallbacks());
    pHm10Service->start();

    // Configure primary advertisement data (Name + Flags + 16-bit 0xFFE0 service for Web Bluetooth filters)
    BLEAdvertisementData advData;
    advData.setName(deviceName ? deviceName : "thermophase");
    advData.setFlags(0x06);
    advData.setCompleteServices(BLEUUID((uint16_t)0xFFE0));

    // Configure scan response data with 128-bit Nordic UART Service UUID
    BLEAdvertisementData scanResponseData;
    scanResponseData.setCompleteServices(BLEUUID(SERVICE_UUID_NUS));

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setScanResponseData(scanResponseData);

    // Initialize async streaming worker task and synchronization mutex
    if (!s_bleFrameMutex) {
        s_bleFrameMutex = xSemaphoreCreateMutex();
    }
    if (!s_bleStreamTaskHandle) {
        xTaskCreatePinnedToCore(
            bleStreamTask,
            "BLE_Stream_Task",
            8192,
            NULL,
            1, // Low priority: never starves sensor read or display rendering
            &s_bleStreamTaskHandle,
            0  // Core 0 (alongside BT controller/network stack)
        );
    }

    bleInitialized = true;
    if (bleEnabled) {
        BLEDevice::startAdvertising();
        Serial.printf("[BLE] Services started (NUS + HM-10 0xFFE0). Advertising as '%s'\n", deviceName ? deviceName : "thermophase");
    } else {
        Serial.println("[BLE] Initialized in STANDBY (RF advertising disabled).");
    }
    return true;
}

bool isBLEConnected() {
    return deviceConnected;
}

bool isBLEEnabled() {
    return bleEnabled;
}

const char* getBLEConnectedClientInfo() {
    if (deviceConnected && connectedClientInfo[0] != '\0') {
        return connectedClientInfo;
    }
    return deviceConnected ? "Device" : nullptr;
}

void setBLEEnabled(bool enable) {
    if (bleEnabled == enable) return;
    bleEnabled = enable;

    if (!enable) {
        if (deviceConnected) {
            deviceConnected = false;
            connectedClientInfo[0] = '\0';
        }
        if (bleInitialized) {
            BLEDevice::getAdvertising()->stop();
        }
        Serial.println("[BLE] Radio advertising disabled (power saving active).");
    } else {
        if (bleInitialized) {
            BLEDevice::startAdvertising();
            Serial.println("[BLE] Radio advertising enabled as 'thermophase'.");
        }
    }
}

void bleWrite(const uint8_t* data, size_t len) {
    if (!deviceConnected || !data || len == 0 || !pTxCharacteristic) {
        return;
    }

    uint16_t peerMtu = currentPeerMTU;
    if (peerMtu < 23) peerMtu = 23;

    size_t chunkSize = (peerMtu > 3) ? (peerMtu - 3) : 20;
    if (chunkSize > 512) chunkSize = 512;
    if (chunkSize < 20) chunkSize = 20;

    size_t offset = 0;
    while (offset < len) {
        if (!deviceConnected) break;
        size_t toSend = (len - offset > chunkSize) ? chunkSize : (len - offset);
        pTxCharacteristic->setValue(const_cast<uint8_t*>(data + offset), toSend);
        pTxCharacteristic->notify();

        if (pHm10Characteristic) {
            pHm10Characteristic->setValue(const_cast<uint8_t*>(data + offset), toSend);
            pHm10Characteristic->notify();
        }

        offset += toSend;
        if (offset < len) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

void blePrint(const char* str) {
    if (!str) return;
    bleWrite(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

void blePrintf(const char* fmt, ...) {
    if (!isBLEConnected()) return;

    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        bleWrite(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
}

int bleAvailable() {
    return (rxHead - rxTail + RX_BUFFER_SIZE) % RX_BUFFER_SIZE;
}

int bleRead() {
    if (rxHead == rxTail) return -1;
    uint8_t byte = rxBuffer[rxTail];
    rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
    return byte;
}

void outPrintf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        Serial.print(buf);
        if (isBLEConnected()) {
            bleWrite(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
        }
    }
}

void outPrint(const char* str) {
    if (!str) return;
    Serial.print(str);
    if (isBLEConnected()) {
        blePrint(str);
    }
}

void outPrintln(const char* str) {
    if (str && strlen(str) > 0) {
        outPrint(str);
    }
    outPrint("\r\n");
}

bool isBLEStreaming() {
    return bleStreamingActive && isBLEConnected();
}

void setBLEStreaming(bool enable) {
    if (bleStreamingActive == enable) return;
    bleStreamingActive = enable;
    if (enable) {
        Serial.println("\n[STREAM] >>> THERMAL_STREAM_START: 32x24 int16, sync=0x5AA55AA6, bytes=1540 <<<");
        blePrintf("[STREAM] START: 32x24 int16, sync=0x5AA55AA6\r\n");
    } else {
        Serial.println("\n[STREAM] >>> THERMAL_STREAM_STOPPED <<<");
        blePrintf("[STREAM] STOPPED\r\n");
    }
}

// -----------------------------------------------------------------------------
// Asynchronous Dedicated BLE Thermal Streaming Worker Task
// -----------------------------------------------------------------------------
static void bleStreamTask(void* pvParameters) {
    while (true) {
        // Wait for notification when a new frame is posted (or timeout 50ms)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

        if (!bleStreamingActive || !deviceConnected || !pTxCharacteristic) {
            continue;
        }

        bool hasFrame = false;
        if (s_bleFrameMutex && xSemaphoreTake(s_bleFrameMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (s_bleNewFrameAvailable) {
                memcpy(&s_bleWorkerSnapshot, &s_bleFrameBuffer, sizeof(BLEFrameSnapshot));
                s_bleNewFrameAvailable = false;
                hasFrame = true;
            }
            xSemaphoreGive(s_bleFrameMutex);
        }

        if (!hasFrame) {
            continue;
        }

        // 1. Output serial indicator for the receiver (suggests receiver render sent data)
        Serial.printf("[FRAME:%lu] 32x24 min=%.2f max=%.2f (RENDER)\n",
                      (unsigned long)s_bleWorkerSnapshot.frameIdx,
                      s_bleWorkerSnapshot.minTemp, s_bleWorkerSnapshot.maxTemp);

        // 2. Query true connection MTU (negotiated with peer)
        uint16_t peerMtu = currentPeerMTU;
        if (pServer && s_hasActiveConn) {
            uint16_t serverMtu = pServer->getPeerMTU(s_activeConnId);
            if (serverMtu >= 23) {
                peerMtu = serverMtu;
            }
        }
        if (peerMtu < 23) peerMtu = 23;

        size_t chunkSize = (peerMtu > 3) ? (peerMtu - 3) : 20;
        if (chunkSize > 512) chunkSize = 512;
        if (chunkSize < 20) chunkSize = 20;

        // 3. Assemble 3076-byte packet: MAGIC_HEADER (0x5AA55AA5) + 768 float32 values
        s_blePacketBuffer[0] = 0x5A;
        s_blePacketBuffer[1] = 0xA5;
        s_blePacketBuffer[2] = 0x5A;
        s_blePacketBuffer[3] = 0xA5;

        memcpy(s_blePacketBuffer + 4, s_bleWorkerSnapshot.temperatures, 768 * sizeof(float));
        size_t totalBytes = 4 + 768 * sizeof(float); // 3076 bytes
        size_t offset = 0;

        // Stream notifications directly to the subscribed NUS TX characteristic
        while (offset < totalBytes) {
            if (!deviceConnected || !bleStreamingActive) break;
            size_t toSend = (totalBytes - offset > chunkSize) ? chunkSize : (totalBytes - offset);
            pTxCharacteristic->setValue(s_blePacketBuffer + offset, toSend);
            pTxCharacteristic->notify();

            offset += toSend;
            if (offset < totalBytes) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }
}

// Instantaneous non-blocking snapshot: takes < 1 microsecond, never stalls sensor task
void streamThermalFrameBLE(const float* temps, size_t count, float minTemp, float maxTemp, uint32_t frameIdx) {
    if (!bleStreamingActive || !deviceConnected || !temps || count == 0) return;

    if (s_bleFrameMutex && xSemaphoreTake(s_bleFrameMutex, 0) == pdTRUE) {
        size_t n = (count > 768) ? 768 : count;
        memcpy(s_bleFrameBuffer.temperatures, temps, n * sizeof(float));
        s_bleFrameBuffer.minTemp = minTemp;
        s_bleFrameBuffer.maxTemp = maxTemp;
        s_bleFrameBuffer.frameIdx = frameIdx;
        s_bleNewFrameAvailable = true;
        xSemaphoreGive(s_bleFrameMutex);

        if (s_bleStreamTaskHandle != NULL) {
            xTaskNotifyGive(s_bleStreamTaskHandle);
        }
    }
}
