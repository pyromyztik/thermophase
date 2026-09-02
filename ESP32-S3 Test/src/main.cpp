#include <util.hpp>

// Initialize the NeoPixel strip object
Adafruit_NeoPixel onboardLED(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

// define global task handle from the header
TaskHandle_t diagTaskHandle = NULL;

void setup() {
	Serial.begin(115200);
	while (!Serial && millis() < 3000) 
        { /* Wait briefly for native USB CDC attachment */	}

	Serial.setTimeout(50); // Fast timeout for serial reads

	// setup on-board LED for koolness
    onboardLED.begin();
	onboardLED.setBrightness(50); // config brightness (0-255)
	onboardLED.show();            // Initialize all pixels to "off"

    // Spawn the LED task on Core 0
    xTaskCreatePinnedToCore(
        ledAnimationTask,   // Task function
        "LED_Task",         // Name
        2048,               // Stack size (bytes)
        NULL,               // Parameter
        1,                  // Priority
        NULL,               // Task handle
        0                   // Pinned to Core 0 (Arduino loop runs on Core 1)
    );

	Serial.println("\nSystem Ready. Type 'diagnostics' in Serial Monitor to run hardware diagnostics.");
}

unsigned long lastTelemetryTime = 0;

void loop() {
    // Check for user command over Serial
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // Remove whitespace and newline chars

        if (input.equalsIgnoreCase("diagnostics")) {
            if (diagTaskHandle == NULL) {
                Serial.println("\n[CMD] Spawning Diagnostics Task...");
                xTaskCreatePinnedToCore(
                    diagnosticsMessageTask, // Task function
                    "Diag_Task",            // Task name
                    4096,                   // Stack size (bytes)
                    NULL,                   // Parameter
                    1,                      // Priority
                    &diagTaskHandle,        // Task handle
                    1                       // Pinned to Core 1
                );
            } else {
                Serial.println("\n[WARN] Diagnostics task is already running. Please wait.");
            }
        }
    }
    // Periodic telemetry print every 1000 ms (non-blocking)
    unsigned long now = millis();
    if (now - lastTelemetryTime >= 1000) {
        lastTelemetryTime = now;
        Serial.printf("Uptime: %lu ms | Free Heap: %d bytes\n", 
                      now, ESP.getFreeHeap());
    }
    delay(10); // Small yield
}
