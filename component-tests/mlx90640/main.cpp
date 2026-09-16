#include <Wire.h>
#include <Adafruit_MLX90640.h>

constexpr uint8_t FRAME_WIDTH = 32;
constexpr uint8_t FRAME_HEIGHT = 24;
constexpr size_t  FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT;

// ESP32-S3 hardware I2C pins
constexpr int PIN_SDA = 16;
constexpr int PIN_SCL = 17;

Adafruit_MLX90640 mlx;
float frame[FRAME_SIZE];

void setup() {
    Serial.begin(921600);
    delay(1500);
    Serial.println("\n[MLX90640] Initializing...");

    Wire.begin(PIN_SDA, PIN_SCL);
    // MLX90640 EEPROM can only be read at <= 400 kHz during begin()
    Wire.setClock(400000);

    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
        Serial.println("[MLX90640] Error: sensor not detected. Verify wiring.");
        while (true) {
            delay(500);
        }
    }
    Serial.println("[MLX90640] Detected successfully!");

    // Now speed up I2C to 800 kHz for high-speed RAM frame streaming
    Wire.setClock(800000);

    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(MLX90640_32_HZ);
    Serial.print("[MLX90640] I2C Clock: ");
    Serial.println(Wire.getClock());
    Serial.println("[MLX90640] Ready and streaming.");
}

// 4-byte magic sync header: 0x5A, 0xA5, 0x5A, 0xA5
constexpr uint8_t HEADER[4] = {0x5A, 0xA5, 0x5A, 0xA5};

uint32_t frameCount = 0;
uint32_t totalI2cTime = 0;
uint32_t totalSerialTime = 0;

void loop() {
    uint32_t t0 = millis();
    int status = mlx.getFrame(frame);
    uint32_t t1 = millis();

    if (status != 0) {
        return;
    }

    uint32_t t2 = millis();
    // High-speed binary transmission: 4-byte sync header + 768 float32 values (3076 bytes total)
    Serial.write(HEADER, sizeof(HEADER));
    Serial.write(reinterpret_cast<const uint8_t*>(frame), sizeof(frame));
    uint32_t t3 = millis();

    totalI2cTime += (t1 - t0);
    totalSerialTime += (t3 - t2);
    frameCount++;

    if (frameCount >= 60) {
        // Optional debug over secondary channel or comment
        frameCount = 0;
        totalI2cTime = 0;
        totalSerialTime = 0;
    }
}
