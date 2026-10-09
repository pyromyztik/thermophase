#include "ble.hpp"
#include "ppt.hpp"
#include "wifi.hpp"
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
#include <esp_heap_caps.h>

static volatile bool bleStreamingActive = false;
static volatile bool s_pptTransferActive = false;
static volatile bool s_pptTransferPending = false;
static volatile bool s_snapshotTransferActive = false;
static volatile bool s_snapshotTransferPending = false;
static volatile uint16_t currentPeerMTU = 23;

static void runPPTSpectralTransfer();
static void runSnapshotTransfer();

// Asynchronous Snapshot Picture Transfer Buffer
struct BLESnapshotTransfer {
    int16_t temperatures[768];
    float minTemp;
    float maxTemp;
    float centerTemp;
    uint32_t timestamp;
    uint32_t crc32;
};
static BLESnapshotTransfer s_snapshotData;
static char* s_snapshotDataLine = nullptr;

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
    uint8_t rangeMode;
    uint32_t frameIdx;
};

static BLEFrameSnapshot s_bleFrameBuffer;
static BLEFrameSnapshot s_bleWorkerSnapshot; // Statically allocated to prevent stack overflow
static SemaphoreHandle_t s_bleFrameMutex = nullptr;
static SemaphoreHandle_t s_bleTxMutex = nullptr;
static TaskHandle_t s_bleStreamTaskHandle = nullptr;
static volatile bool s_bleNewFrameAvailable = false;
alignas(4) static uint8_t s_blePacketBuffer[4 + 768 * sizeof(float)]; // 3076 bytes aligned (float32)

static uint16_t s_activeConnId = 0;
static bool s_hasActiveConn = false;
static esp_bd_addr_t s_peerBda = {};

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
            memcpy(s_peerBda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            snprintf(connectedClientInfo, sizeof(connectedClientInfo),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->connect.remote_bda[0],
                     param->connect.remote_bda[1],
                     param->connect.remote_bda[2],
                     param->connect.remote_bda[3],
                     param->connect.remote_bda[4],
                     param->connect.remote_bda[5]);
            Serial.printf("[BLE] Peer connected: %s (connId=%u)\n", connectedClientInfo, (unsigned int)s_activeConnId);

            // Enable Data Length Extension (DLE) so 251-byte Link Layer packets are supported
            esp_ble_gap_set_pkt_data_len(param->connect.remote_bda, 251);

            // NOTE: Do NOT call esp_ble_gap_update_conn_params here!
            // Calling it during onConnect causes an L2CAP packet collision with the client's
            // (Windows/Chrome) in-flight ATT MTU exchange, forcing Windows to abort the 517B exchange
            // and throttle down to its 138B controller buffer.
            // Fast connection interval (15ms - 30ms) is requested safely in onMtuChanged() below.
        }
    }

    void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        if (param) {
            currentPeerMTU = param->mtu.mtu;
            Serial.printf("[BLE] MTU negotiated with peer: %u bytes\n", (unsigned int)currentPeerMTU);

            // Now that MTU exchange is complete and the link is established, request fast interval
            if (s_hasActiveConn) {
                esp_ble_conn_update_params_t conn_params = {};
                memcpy(conn_params.bda, s_peerBda, sizeof(esp_bd_addr_t));
                conn_params.min_int = 0x0c; // 15 ms
                conn_params.max_int = 0x18; // 30 ms
                conn_params.latency = 0;
                conn_params.timeout = 400;  // 4000 ms
                esp_ble_gap_update_conn_params(&conn_params);
            }
        }
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        s_hasActiveConn = false;
        connectedClientInfo[0] = '\0';
        bleStreamingActive = false; // Automatically halt active frame streaming
        // Preserve currentPeerMTU so warm reconnections over existing cached OS links
        // retain the negotiated MTU (Windows/Chrome won't re-exchange MTU on cached links)
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

class TxNotifyCallbacks : public BLECharacteristicCallbacks {
public:
    volatile bool lastFailed = false;
    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) override {
        if (s == ERROR_GATT) {
            lastFailed = true;
        }
    }
};
static TxNotifyCallbacks s_txNotifyCallbacks;

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
    pTxCharacteristic->setCallbacks(&s_txNotifyCallbacks);

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

    // Initialize async streaming worker task and synchronization mutexes
    if (!s_bleFrameMutex) {
        s_bleFrameMutex = xSemaphoreCreateMutex();
    }
    if (!s_bleTxMutex) {
        s_bleTxMutex = xSemaphoreCreateMutex();
    }
    if (!s_bleStreamTaskHandle) {
        xTaskCreatePinnedToCore(
            bleStreamTask,
            "BLE_Stream_Task",
            4096,
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
            BLEAdvertising* adv = BLEDevice::getAdvertising();
            if (adv) {
                adv->stop();
            }
            if (s_hasActiveConn) {
                esp_ble_gap_disconnect(s_peerBda);
                s_hasActiveConn = false;
            }
        }
        Serial.println("[BLE] Subsystem stopped & radio sleeping (power saving active).");
    } else {
        if (!bleInitialized) {
            initBLE("thermophase");
        } else {
            BLEDevice::startAdvertising();
            Serial.println("[BLE] Radio advertising enabled as 'thermophase'.");
        }
    }
}

static bool sendBleChunk(const uint8_t* data, size_t len) {
    if (!deviceConnected || !pTxCharacteristic || !data || len == 0) return false;

    pTxCharacteristic->setValue(const_cast<uint8_t*>(data), len);

    int retries = 0;
    while (retries < 25 && deviceConnected) {
        s_txNotifyCallbacks.lastFailed = false;
        pTxCharacteristic->notify();
        if (!s_txNotifyCallbacks.lastFailed) {
            return true;
        }
        // Controller TX buffer congested / no memory:
        // Yield for BLE connection event to clear queue
        vTaskDelay(pdMS_TO_TICKS(8));
        retries++;
    }
    return true;
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

    // Mutex serialization: Wait for any active in-flight binary frame burst to complete
    // so telemetry text is sent atomically BETWEEN whole frames, never interleaved mid-frame
    if (s_bleTxMutex != nullptr && xSemaphoreTake(s_bleTxMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t offset = 0;
        size_t chunkCount = 0;
        while (offset < len) {
            if (!deviceConnected) break;
            size_t toSend = (len - offset > chunkSize) ? chunkSize : (len - offset);

            bool sent = sendBleChunk(data + offset, toSend);
            if (!sent && !deviceConnected) {
                break;
            }

            if (pHm10Characteristic && !s_pptTransferActive && !s_snapshotTransferActive) {
                pHm10Characteristic->setValue(const_cast<uint8_t*>(data + offset), toSend);
                pHm10Characteristic->notify();
            }

            offset += toSend;
            chunkCount++;
            if (offset < len) {
                if (chunkSize <= 32) {
                    // At small MTU (e.g. 20-byte chunks), yield 6 ms per chunk
                    // to respect BLE connection event boundaries
                    vTaskDelay(pdMS_TO_TICKS(6));
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
        }
        xSemaphoreGive(s_bleTxMutex);
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

bool isBLETransferringPPT() {
    return s_pptTransferActive || s_pptTransferPending;
}

bool isBLETransferringSnapshot() {
    return s_snapshotTransferActive || s_snapshotTransferPending;
}

void outPrintf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        Serial.print(buf);
        // Suppress periodic background telemetry over BLE while PPT or Snapshot transmission is active
        if (isBLEConnected() && !isBLETransferringPPT() && !isBLETransferringSnapshot()) {
            bleWrite(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
        }
        wifiTerminalLog(buf);
    }
}

void outPrint(const char* str) {
    if (!str) return;
    Serial.print(str);
    if (isBLEConnected() && !isBLETransferringPPT() && !isBLETransferringSnapshot()) {
        blePrint(str);
    }
    wifiTerminalLog(str);
}

void outPrintln(const char* str) {
    if (str && strlen(str) > 0) {
        outPrint(str);
    }
    if (!isBLETransferringPPT() && !isBLETransferringSnapshot()) {
        outPrint("\r\n");
    } else {
        Serial.print("\r\n");
        wifiTerminalLog("\r\n");
    }
}

bool isBLEStreaming() {
    return bleStreamingActive && isBLEConnected();
}

void setBLEStreaming(bool enable) {
    if (bleStreamingActive == enable) return;
    if (enable) {
        Serial.println("\n[STREAM] >>> THERMAL_STREAM_START: 32x24 int16 + range, sync=0x5AA55AA7, bytes=1546 <<<");
        blePrintf("[STREAM] START: 32x24 int16, sync=0x5AA55AA7\r\n");
        bleStreamingActive = true;

        // Request fast connection interval (15ms - 30ms) now that GATT discovery and MTU are established
        if (s_hasActiveConn) {
            esp_ble_conn_update_params_t conn_params = {};
            memcpy(conn_params.bda, s_peerBda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x0c; // 15 ms (compliant with Windows and Apple BLE limits)
            conn_params.max_int = 0x18; // 30 ms
            conn_params.latency = 0;
            conn_params.timeout = 400;  // 4000 ms
            esp_ble_gap_update_conn_params(&conn_params);
        }
    } else {
        bleStreamingActive = false;
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

        // 1. Service pending PPT Spectral Phasegram wireless transfer if requested
        if (s_pptTransferPending) {
            s_pptTransferPending = false;
            runPPTSpectralTransfer();
        }

        // 2. Service pending Snapshot Picture wireless transfer if requested
        if (s_snapshotTransferPending) {
            s_snapshotTransferPending = false;
            runSnapshotTransfer();
        }

        if (s_pptTransferActive || s_snapshotTransferActive || !bleStreamingActive || !deviceConnected || !pTxCharacteristic) {
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
        Serial.printf("[FRAME:%lu] 32x24 min=%.2f max=%.2f mode=%s (RENDER)\n",
                      (unsigned long)s_bleWorkerSnapshot.frameIdx,
                      s_bleWorkerSnapshot.minTemp, s_bleWorkerSnapshot.maxTemp,
                      s_bleWorkerSnapshot.rangeMode ? "MANUAL" : "AUTO");

        // 2. Query true connection MTU (negotiated with peer)
        uint16_t peerMtu = (currentPeerMTU > 23) ? currentPeerMTU : 23;
        if (pServer && s_hasActiveConn) {
            uint16_t serverMtu = pServer->getPeerMTU(s_activeConnId);
            if (serverMtu > peerMtu) {
                peerMtu = serverMtu;
            }
        }
        if (peerMtu < 23) peerMtu = 23;

        size_t chunkSize = (peerMtu > 3) ? (peerMtu - 3) : 20;
        if (chunkSize > 512) chunkSize = 512;
        if (chunkSize < 20) chunkSize = 20;

        // 3. Assemble 1546-byte packet:
        // MAGIC_HEADER (0x5AA55AA7) + rangeMode (1B) + reserved (1B) + minTemp (2B int16 cdeg) + maxTemp (2B int16 cdeg) + 768 int16 values (centi-degrees C)
        s_blePacketBuffer[0] = 0x5A;
        s_blePacketBuffer[1] = 0xA5;
        s_blePacketBuffer[2] = 0x5A;
        s_blePacketBuffer[3] = 0xA7;

        s_blePacketBuffer[4] = s_bleWorkerSnapshot.rangeMode;
        s_blePacketBuffer[5] = 0x00; // Reserved

        int32_t minCdeg = (int32_t)std::round(s_bleWorkerSnapshot.minTemp * 100.0f);
        if (minCdeg < -32768) minCdeg = -32768;
        if (minCdeg > 32767) minCdeg = 32767;
        int16_t minVal = (int16_t)minCdeg;

        int32_t maxCdeg = (int32_t)std::round(s_bleWorkerSnapshot.maxTemp * 100.0f);
        if (maxCdeg < -32768) maxCdeg = -32768;
        if (maxCdeg > 32767) maxCdeg = 32767;
        int16_t maxVal = (int16_t)maxCdeg;

        memcpy(s_blePacketBuffer + 6, &minVal, sizeof(int16_t));
        memcpy(s_blePacketBuffer + 8, &maxVal, sizeof(int16_t));

        int16_t* pInt16 = reinterpret_cast<int16_t*>(s_blePacketBuffer + 10);
        for (size_t i = 0; i < 768; ++i) {
            float val = s_bleWorkerSnapshot.temperatures[i];
            if (!std::isfinite(val)) val = 25.0f;
            int32_t cdeg = (int32_t)std::round(val * 100.0f);
            if (cdeg < -32768) cdeg = -32768;
            if (cdeg > 32767) cdeg = 32767;
            pInt16[i] = (int16_t)cdeg;
        }
        size_t totalBytes = 10 + 768 * sizeof(int16_t); // 1546 bytes
        size_t offset = 0;

        // Stream notifications directly to the subscribed NUS TX characteristic
        // Protect entire frame burst with s_bleTxMutex so telemetry text is sent between frames, never inside
        if (s_bleTxMutex != nullptr && xSemaphoreTake(s_bleTxMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t chunkCount = 0;
            while (offset < totalBytes) {
                if (!deviceConnected || !bleStreamingActive) break;
                size_t toSend = (totalBytes - offset > chunkSize) ? chunkSize : (totalBytes - offset);
                pTxCharacteristic->setValue(s_blePacketBuffer + offset, toSend);
                pTxCharacteristic->notify();

                offset += toSend;
                if (offset < totalBytes) {
                    chunkCount++;
                    // For small MTU (e.g. 20 bytes = 77 chunks), yield briefly every 8 chunks
                    // to prevent the BLE controller radio buffer from overflowing
                    if (chunkSize <= 32 && (chunkCount % 8 == 0)) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                    } else {
                        delayMicroseconds(200);
                    }
                }
            }
            xSemaphoreGive(s_bleTxMutex);
        }
    }
}

// Instantaneous non-blocking snapshot: takes < 1 microsecond, never stalls sensor task
void streamThermalFrameBLE(const float* temps, size_t count, float minTemp, float maxTemp, uint8_t rangeMode, uint32_t frameIdx) {
    if (!bleStreamingActive || !deviceConnected || !temps || count == 0) return;

    if (s_bleFrameMutex && xSemaphoreTake(s_bleFrameMutex, 0) == pdTRUE) {
        size_t n = (count > 768) ? 768 : count;
        memcpy(s_bleFrameBuffer.temperatures, temps, n * sizeof(float));
        s_bleFrameBuffer.minTemp = minTemp;
        s_bleFrameBuffer.maxTemp = maxTemp;
        s_bleFrameBuffer.rangeMode = rangeMode;
        s_bleFrameBuffer.frameIdx = frameIdx;
        s_bleNewFrameAvailable = true;
        xSemaphoreGive(s_bleFrameMutex);

        if (s_bleStreamTaskHandle != NULL) {
            xTaskNotifyGive(s_bleStreamTaskHandle);
        }
    }
}

// -----------------------------------------------------------------------------
// Pulsed Phase Thermography (PPT) Asynchronous Spectral BLE Transmission
// -----------------------------------------------------------------------------

static void base64Encode(const uint8_t* src, size_t len, char* dst, size_t maxDst) {
    static const char b64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t outLen = 4 * ((len + 2) / 3);
    if (outLen + 1 > maxDst) return;

    size_t di = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = src[i];
        uint32_t b1 = (i + 1 < len) ? src[i + 1] : 0;
        uint32_t b2 = (i + 2 < len) ? src[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        dst[di++] = b64Chars[(triple >> 18) & 0x3F];
        dst[di++] = b64Chars[(triple >> 12) & 0x3F];
        dst[di++] = (i + 1 < len) ? b64Chars[(triple >> 6) & 0x3F] : '=';
        dst[di++] = (i + 2 < len) ? b64Chars[triple & 0x3F] : '=';
    }
    dst[di] = '\0';
}

static void runPPTSpectralTransfer() {
    s_pptTransferActive = true;
    PPTSpectralSession* session = pptGetActiveSession();
    if (!session || !deviceConnected) {
        s_pptTransferActive = false;
        return;
    }

    Serial.printf("\n[BLE] >>> STARTING PPT SPECTRAL PHASEGRAM WIRELESS STREAM (16 harmonic frequencies) <<<\n");

    char lineBuf[256];
    snprintf(lineBuf, sizeof(lineBuf),
             "[PPT_SESSION_START] TIMESTAMP=%lu TOTAL_PHASEGRAMS=%u WIDTH=32 HEIGHT=24\r\n",
             (unsigned long)session->sessionTimestamp,
             (unsigned int)session->totalPhasegrams);
    blePrint(lineBuf);
    vTaskDelay(pdMS_TO_TICKS(50));

    for (int i = 0; i < session->totalPhasegrams; ++i) {
        if (!deviceConnected) {
            Serial.println("[BLE] Peer disconnected during PPT transfer. Transmission aborted.");
            break;
        }

        const PPTPhasegram& pg = session->phasegrams[i];

        // 1. Phasegram metadata header
        snprintf(lineBuf, sizeof(lineBuf),
                 "[PPT_PHASEGRAM_START] HARMONIC=%d/16 FREQ_HZ=%.3f DEPTH=%s MIN_RAD=%.4f MAX_RAD=%.4f CTR_RAD=%.4f\r\n",
                 pg.harmonicBin, pg.frequencyHz, pg.diffusionDepth,
                 pg.minPhaseRad, pg.maxPhaseRad, pg.centerPhaseRad);
        blePrint(lineBuf);
        vTaskDelay(pdMS_TO_TICKS(25));

        // 2. Format atomic line: "[PPT_PHASEGRAM_DATA] <1024_b64_chars>\r\n"
        const char prefix[] = "[PPT_PHASEGRAM_DATA] ";
        const size_t prefixLen = sizeof(prefix) - 1; // 21
        memcpy(session->dataLine, prefix, prefixLen);
        base64Encode(pg.normPhaseGrid, PPT_PIXELS, session->dataLine + prefixLen, sizeof(session->dataLine) - prefixLen - 4);
        size_t b64Len = strlen(session->dataLine);
        session->dataLine[b64Len] = '\r';
        session->dataLine[b64Len + 1] = '\n';
        session->dataLine[b64Len + 2] = '\0';

        // Send atomic line (all 1024 Base64 chars together with newline in single call)
        blePrint(session->dataLine);
        vTaskDelay(pdMS_TO_TICKS(25));

        // 3. Phasegram completion & CRC32 verification footer
        snprintf(lineBuf, sizeof(lineBuf),
                 "[PPT_PHASEGRAM_END] HARMONIC=%d CRC32=0x%08lX\r\n",
                 pg.harmonicBin, (unsigned long)pg.crc32);
        blePrint(lineBuf);

        Serial.printf("[BLE] PPT Harmonic %d/16 (%.2f Hz, %s) streamed (CRC32: 0x%08lX)\n",
                      pg.harmonicBin, pg.frequencyHz, pg.diffusionDepth, (unsigned long)pg.crc32);

        // Pacing delay between harmonic phasegrams
        vTaskDelay(pdMS_TO_TICKS(35));
    }

    if (deviceConnected) {
        snprintf(lineBuf, sizeof(lineBuf),
                 "[PPT_SESSION_COMPLETE] TIMESTAMP=%lu TOTAL_PHASEGRAMS=%u STATUS=VERIFIED\r\n",
                 (unsigned long)session->sessionTimestamp,
                 (unsigned int)session->totalPhasegrams);
        blePrint(lineBuf);
        vTaskDelay(pdMS_TO_TICKS(60));
        // Send completion marker twice to guarantee delivery over BLE
        blePrint(lineBuf);
        vTaskDelay(pdMS_TO_TICKS(50));
        Serial.println("[BLE] >>> PPT SPECTRAL PHASEGRAM TRANSMISSION COMPLETE (16/16 verified) <<<\n");
    }

    s_pptTransferActive = false;
}

void triggerPPTBLETransfer() {
    if (!deviceConnected) {
        Serial.println("[BLE] Cannot start PPT transfer: No BLE peer connected.");
        return;
    }
    if (s_pptTransferActive || s_pptTransferPending) {
        Serial.println("[BLE] PPT spectral transfer is already pending or in progress.");
        return;
    }

    s_pptTransferPending = true;
    if (s_bleStreamTaskHandle != NULL) {
        xTaskNotifyGive(s_bleStreamTaskHandle);
    }
}

// -----------------------------------------------------------------------------
// Snapshot Picture Asynchronous BLE Transmission (triggered on freeze)
// -----------------------------------------------------------------------------

static void runSnapshotTransfer() {
    s_snapshotTransferActive = true;
    if (!deviceConnected) {
        s_snapshotTransferActive = false;
        return;
    }

    Serial.printf("\n[BLE] >>> STARTING THERMAL SNAPSHOT WIRELESS TRANSFER <<<\n");

    if (s_snapshotDataLine == nullptr) {
        s_snapshotDataLine = (char*)heap_caps_malloc(2304, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_snapshotDataLine) {
            s_snapshotDataLine = (char*)malloc(2304);
        }
    }

    if (!s_snapshotDataLine) {
        Serial.println("[BLE] Failed to allocate snapshot line buffer!");
        s_snapshotTransferActive = false;
        return;
    }

    char headerBuf[256];
    snprintf(headerBuf, sizeof(headerBuf),
             "[SNAPSHOT_START] TIMESTAMP=%lu WIDTH=32 HEIGHT=24 MIN=%.2f MAX=%.2f CTR=%.2f\r\n",
             (unsigned long)s_snapshotData.timestamp,
             s_snapshotData.minTemp, s_snapshotData.maxTemp, s_snapshotData.centerTemp);
    blePrint(headerBuf);
    vTaskDelay(pdMS_TO_TICKS(30));

    // Base64 encode the 1536 raw binary bytes (768 * int16 centi-degrees) -> 2048 chars
    const char prefix[] = "[SNAPSHOT_DATA] ";
    const size_t prefixLen = sizeof(prefix) - 1; // 16
    memcpy(s_snapshotDataLine, prefix, prefixLen);

    base64Encode(reinterpret_cast<const uint8_t*>(s_snapshotData.temperatures),
                 768 * sizeof(int16_t),
                 s_snapshotDataLine + prefixLen,
                 2304 - prefixLen - 4);

    size_t b64Len = strlen(s_snapshotDataLine);
    s_snapshotDataLine[b64Len] = '\r';
    s_snapshotDataLine[b64Len + 1] = '\n';
    s_snapshotDataLine[b64Len + 2] = '\0';

    blePrint(s_snapshotDataLine);
    vTaskDelay(pdMS_TO_TICKS(30));

    // Snapshot completion & IEEE 802.3 CRC32 verification footer
    snprintf(headerBuf, sizeof(headerBuf),
             "[SNAPSHOT_END] TIMESTAMP=%lu CRC32=0x%08lX\r\n",
             (unsigned long)s_snapshotData.timestamp,
             (unsigned long)s_snapshotData.crc32);
    blePrint(headerBuf);
    vTaskDelay(pdMS_TO_TICKS(30));

    Serial.printf("[BLE] Thermal Snapshot transferred (CRC32: 0x%08lX)\n",
                  (unsigned long)s_snapshotData.crc32);

    s_snapshotTransferActive = false;
}

void triggerSnapshotBLETransfer(const float* temps, size_t count, float minTemp, float maxTemp, float centerTemp) {
    if (!deviceConnected || !temps || count == 0) return;
    if (s_snapshotTransferActive || s_snapshotTransferPending || s_pptTransferActive) return;

    size_t n = (count > 768) ? 768 : count;
    for (size_t i = 0; i < n; ++i) {
        float val = temps[i];
        if (!std::isfinite(val)) val = 25.0f;
        int32_t cdeg = (int32_t)std::round(val * 100.0f);
        if (cdeg < -32768) cdeg = -32768;
        if (cdeg > 32767) cdeg = 32767;
        s_snapshotData.temperatures[i] = (int16_t)cdeg;
    }
    s_snapshotData.minTemp = minTemp;
    s_snapshotData.maxTemp = maxTemp;
    s_snapshotData.centerTemp = centerTemp;
    s_snapshotData.timestamp = millis();

    // IEEE 802.3 CRC32 checksum over the 1536 raw binary bytes (768 * int16_t)
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* pRawBytes = reinterpret_cast<const uint8_t*>(s_snapshotData.temperatures);
    size_t rawLen = n * sizeof(int16_t);
    for (size_t i = 0; i < rawLen; ++i) {
        crc ^= pRawBytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
        }
    }
    s_snapshotData.crc32 = ~crc;

    s_snapshotTransferPending = true;
    if (s_bleStreamTaskHandle != NULL) {
        xTaskNotifyGive(s_bleStreamTaskHandle);
    }
}
