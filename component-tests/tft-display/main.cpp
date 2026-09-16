#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>

// ==========================================================
// PIN CONFIGURATION FOR ESP32-S3 DEVKITC-1 (HEADER J3 SIDE)
// ==========================================================
#define TFT_CS     1  // Chip Select
#define TFT_DC     2  // Data / Command
#define TFT_RST   42  // Hardware Reset
#define TFT_MOSI  41  // SPI MOSI (SDI on display)
#define TFT_SCK   40  // SPI Clock (SCK on display)
#define TFT_MISO  39  // SPI MISO (SDO on display, required for readcommand8 diagnostics)
#define TFT_LED   47  // Backlight (Recommended: connect display LED directly to 3.3V!)

// Option A: Hardware SPI (Fast)
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

// Option B: Software SPI (Bit-banged - exactly like the site example, immune to SPI bus issues)
// Uncomment the line below and comment Option A if Hardware SPI gives a blank screen:
// Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST, TFT_MISO);

// 3D Wireframe Cube Model for Video Motion Benchmark
struct Point3D {
	float x, y, z;
};

struct Point2D {
	int16_t x, y;
};

Point3D cubeVertices[8] = {
	{-35, -35, -35},
	{ 35, -35, -35},
	{ 35,  35, -35},
	{-35,  35, -35},
	{-35, -35,  35},
	{ 35, -35,  35},
	{ 35,  35,  35},
	{-35,  35,  35}
};

const uint8_t cubeEdges[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0}, // Back face
	{4, 5}, {5, 6}, {6, 7}, {7, 4}, // Front face
	{0, 4}, {1, 5}, {2, 6}, {3, 7}  // Connecting edges
};

// Function prototypes
void printDiagnostics();
void testColorBars();
void testGradients();
void testGeometryAndText();
void runVideoAnimationTest(uint32_t durationMs);

void setup() {
	Serial.begin(115200);
	delay(1000);
	Serial.println("\n==================================================");
	Serial.println("   2.8\" SPI TFT (ILI9341) Startup & Diagnostic   ");
	Serial.println("==================================================");

	// Turn on Backlight LED pin if wired to GPIO
	pinMode(TFT_LED, OUTPUT);
	digitalWrite(TFT_LED, HIGH);

	// Manual hardware reset with generous delays
	pinMode(TFT_RST, OUTPUT);
	digitalWrite(TFT_RST, HIGH);
	delay(50);
	digitalWrite(TFT_RST, LOW);
	delay(100);
	digitalWrite(TFT_RST, HIGH);
	delay(200);

	// Initialize SPI bus with our custom ESP32-S3 pins
	SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);

	// Initialize ILI9341 at a safe breadboard-friendly clock (16 MHz)
	tft.begin(16000000);

	// Read hardware diagnostic registers
	printDiagnostics();

	// Set orientation and initial background
	tft.setRotation(1);
	tft.fillScreen(ILI9341_BLACK);

	Serial.println("Initialization sequence completed!");
}

void printDiagnostics() {
	Serial.println("\n--- ILI9341 Diagnostic Registers ---");
	uint8_t x = tft.readcommand8(ILI9341_RDMODE);
	Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
	x = tft.readcommand8(ILI9341_RDMADCTL);
	Serial.print("MADCTL Mode:        0x"); Serial.println(x, HEX);
	x = tft.readcommand8(ILI9341_RDPIXFMT);
	Serial.print("Pixel Format:       0x"); Serial.println(x, HEX);
	x = tft.readcommand8(ILI9341_RDIMGFMT);
	Serial.print("Image Format:       0x"); Serial.println(x, HEX);
	x = tft.readcommand8(ILI9341_RDSELFDIAG);
	Serial.print("Self Diagnostic:    0x"); Serial.println(x, HEX);
	Serial.println("------------------------------------");
	if (x == 0x00 || x == 0xFF) {
		Serial.println("[WARNING] Registers returned 0x00 or 0xFF!");
		Serial.println("Check: 1) VCC connected to 5V; 2) SCK, MOSI, MISO, CS pins correct; 3) LED to 3.3V.");
	} else {
		Serial.println("[SUCCESS] Display communication verified!");
	}
}

void loop() {
	// Test 1: Classic SMPTE-style Video Color Bars
	Serial.println("Running Test 1: Video Color Bars");
	testColorBars();
	delay(3500);
	
	// Test 2: Color Gradient Ramps (Bit-depth & Color Purity)
	Serial.println("Running Test 2: Color Gradients");
	testGradients();
	delay(3500);
	
	// Test 3: Screen Alignment, Primitives, and Typography
	Serial.println("Running Test 3: Geometry & Fonts");
	testGeometryAndText();
	delay(3500);
	
	// Test 4: Dynamic 3D Real-time Animation & FPS Test (10 seconds)
	Serial.println("Running Test 4: Dynamic Motion & FPS Video Benchmark");
	runVideoAnimationTest(10000);
}

// -------------------------------------------------------------
// 1. SMPTE-STYLE COLOR BARS TEST
// -------------------------------------------------------------
void testColorBars() {
	int w = tft.width();
	int h = tft.height();
	
	tft.fillScreen(ILI9341_BLACK);
	
	// Top 70% bars: 7 standard broadcast colors
	uint16_t topColors[] = {
		ILI9341_WHITE,
		ILI9341_YELLOW,
		ILI9341_CYAN,
		ILI9341_GREEN,
		ILI9341_MAGENTA,
		ILI9341_RED,
		ILI9341_BLUE
	};
	int barWidth = w / 7;
	int topHeight = (h * 7) / 10;
	
	for (int i = 0; i < 7; i++) {
		int x = i * barWidth;
		int bw = (i == 6) ? (w - x) : barWidth;
		tft.fillRect(x, 0, bw, topHeight, topColors[i]);
	}
	
	// Middle 15% bars
	uint16_t midColors[] = {
		ILI9341_BLUE,
		ILI9341_BLACK,
		ILI9341_MAGENTA,
		ILI9341_BLACK,
		ILI9341_CYAN,
		ILI9341_BLACK,
		ILI9341_WHITE
	};
	int midHeight = (h * 15) / 100;
	for (int i = 0; i < 7; i++) {
		int x = i * barWidth;
		int bw = (i == 6) ? (w - x) : barWidth;
		tft.fillRect(x, topHeight, bw, midHeight, midColors[i]);
	}
	
	// Bottom 15% bars (reference black, gray, white)
	int botY = topHeight + midHeight;
	int botHeight = h - botY;
	tft.fillRect(0, botY, w / 4, botHeight, 0x18E3);              // Dark Gray
	tft.fillRect(w / 4, botY, w / 4, botHeight, ILI9341_WHITE);   // White
	tft.fillRect(w / 2, botY, w / 4, botHeight, 0x0010);          // Deep Blue / Black
	tft.fillRect((3 * w) / 4, botY, w / 4, botHeight, ILI9341_BLACK);
	
	// Overlay text card
	tft.fillRect(20, 20, 280, 48, ILI9341_BLACK);
	tft.drawRect(20, 20, 280, 48, ILI9341_WHITE);
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(2);
	tft.setCursor(30, 26);
	tft.println("VIDEO COLOR BARS");
	tft.setTextSize(1);
	tft.setCursor(30, 50);
	tft.print("ILI9341 240x320 SPI  |  ESP32-S3");
}

// -------------------------------------------------------------
// 2. COLOR GRADIENT RAMPS
// -------------------------------------------------------------
void testGradients() {
	int w = tft.width();
	int h = tft.height();
	
	tft.fillScreen(ILI9341_BLACK);
	
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(2);
	tft.setCursor(15, 10);
	tft.print("COLOR GRADIENT TEST");
	
	int barH = 36;
	int startY = 40;
	int spacing = 45;
	
	// Red ramp
	for (int x = 0; x < w; x++) {
		uint8_t r = (x * 31) / w;
		uint16_t color = r << 11;
		tft.drawFastVLine(x, startY, barH, color);
	}
	tft.setCursor(15, startY + 12);
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(1);
	tft.print("RED (5-bit)");
	
	// Green ramp
	for (int x = 0; x < w; x++) {
		uint8_t g = (x * 63) / w;
		uint16_t color = g << 5;
		tft.drawFastVLine(x, startY + spacing, barH, color);
	}
	tft.setCursor(15, startY + spacing + 12);
	tft.print("GREEN (6-bit)");
	
	// Blue ramp
	for (int x = 0; x < w; x++) {
		uint8_t b = (x * 31) / w;
		uint16_t color = b;
		tft.drawFastVLine(x, startY + spacing * 2, barH, color);
	}
	tft.setCursor(15, startY + spacing * 2 + 12);
	tft.print("BLUE (5-bit)");
	
	// Grayscale ramp
	for (int x = 0; x < w; x++) {
		uint8_t gray = (x * 255) / w;
		uint16_t color = tft.color565(gray, gray, gray);
		tft.drawFastVLine(x, startY + spacing * 3, barH, color);
	}
	tft.setCursor(15, startY + spacing * 3 + 12);
	tft.print("GRAYSCALE (RGB565)");
}

// -------------------------------------------------------------
// 3. GEOMETRY, ALIGNMENT & FONTS TEST
// -------------------------------------------------------------
void testGeometryAndText() {
	int w = tft.width();
	int h = tft.height();
	
	tft.fillScreen(ILI9341_NAVY);
	
	// Outer border to verify zero edge-clipping
	tft.drawRect(0, 0, w, h, ILI9341_YELLOW);
	tft.drawRect(2, 2, w - 4, h - 4, ILI9341_WHITE);
	
	// Crosshair center lines
	tft.drawFastHLine(0, h / 2, w, ILI9341_DARKGREY);
	tft.drawFastVLine(w / 2, 0, h, ILI9341_DARKGREY);
	
	// Concentric Circles
	for (int r = 10; r <= 50; r += 10) {
		tft.drawCircle(w / 2, h / 2, r, ILI9341_CYAN);
	}
	
	// Geometric shapes
	tft.fillRect(15, 20, 50, 40, ILI9341_RED);
	tft.drawRect(15, 20, 50, 40, ILI9341_WHITE);
	
	tft.fillRoundRect(w - 65, 20, 50, 40, 8, ILI9341_GREEN);
	tft.drawRoundRect(w - 65, 20, 50, 40, 8, ILI9341_WHITE);
	
	tft.fillTriangle(40, h - 20, 15, h - 60, 65, h - 60, ILI9341_ORANGE);
	tft.drawTriangle(40, h - 20, 15, h - 60, 65, h - 60, ILI9341_WHITE);
	
	// Text sizes
	tft.setTextColor(ILI9341_YELLOW);
	tft.setTextSize(1);
	tft.setCursor(85, 25);
	tft.print("Text Size 1: 6x8 Font");
	
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(2);
	tft.setCursor(85, 42);
	tft.print("Text Size 2");
	
	tft.setTextColor(ILI9341_GREENYELLOW);
	tft.setTextSize(3);
	tft.setCursor(85, 175);
	tft.print("320 x 240");
}

// -------------------------------------------------------------
// 4. DYNAMIC 3D WIREFRAME CUBE & FPS VIDEO TEST
// -------------------------------------------------------------
void runVideoAnimationTest(uint32_t durationMs) {
	int cx = tft.width() / 2;
	int cy = (tft.height() / 2) + 10;
	
	tft.fillScreen(ILI9341_BLACK);
	
	// Header Banner
	tft.fillRect(0, 0, tft.width(), 24, 0x18C3);
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(1);
	tft.setCursor(8, 8);
	tft.print("VIDEO BENCHMARK: 3D ROTATION & FPS");
	
	float rotX = 0;
	float rotY = 0;
	float rotZ = 0;
	
	Point2D prevPoints[8];
	bool hasPrev = false;
	
	uint32_t startTime = millis();
	uint32_t frameCount = 0;
	float fps = 0.0;
	uint32_t lastFpsCalc = millis();
	
	while (millis() - startTime < durationMs) {
		// Project and rotate 3D points
		Point2D projected[8];
		float cosX = cos(rotX), sinX = sin(rotX);
		float cosY = cos(rotY), sinY = sin(rotY);
		float cosZ = cos(rotZ), sinZ = sin(rotZ);
		
		for (int i = 0; i < 8; i++) {
			// Rotation on X
			float y1 = cubeVertices[i].y * cosX - cubeVertices[i].z * sinX;
			float z1 = cubeVertices[i].y * sinX + cubeVertices[i].z * cosX;
			float x1 = cubeVertices[i].x;
			
			// Rotation on Y
			float x2 = x1 * cosY + z1 * sinY;
			float z2 = -x1 * sinY + z1 * cosY;
			float y2 = y1;
			
			// Rotation on Z
			float x3 = x2 * cosZ - y2 * sinZ;
			float y3 = x2 * sinZ + y2 * cosZ;
			
			// Perspective projection
			float distance = 140.0;
			float factor = distance / (distance + z2);
			
			projected[i].x = (int16_t)(cx + x3 * factor);
			projected[i].y = (int16_t)(cy + y3 * factor);
		}
		
		// Erase previous wireframe edges to avoid full-screen redraw flicker
		if (hasPrev) {
			for (int i = 0; i < 12; i++) {
				tft.drawLine(
					prevPoints[cubeEdges[i][0]].x, prevPoints[cubeEdges[i][0]].y,
					prevPoints[cubeEdges[i][1]].x, prevPoints[cubeEdges[i][1]].y,
					ILI9341_BLACK
				);
			}
		}
		
		// Draw new wireframe edges in vibrant cyan/yellow
		for (int i = 0; i < 12; i++) {
			uint16_t edgeColor = (i < 4) ? ILI9341_YELLOW : ((i < 8) ? ILI9341_CYAN : ILI9341_GREEN);
			tft.drawLine(
				projected[cubeEdges[i][0]].x, projected[cubeEdges[i][0]].y,
				projected[cubeEdges[i][1]].x, projected[cubeEdges[i][1]].y,
				edgeColor
			);
		}
		
		// Save previous coordinates
		for (int i = 0; i < 8; i++) {
			prevPoints[i] = projected[i];
		}
		hasPrev = true;
		
		// Advance rotation angles
		rotX += 0.045;
		rotY += 0.060;
		rotZ += 0.025;
		
		// Calculate and display FPS
		frameCount++;
		if (millis() - lastFpsCalc >= 500) {
			fps = (frameCount * 1000.0f) / (millis() - lastFpsCalc);
			frameCount = 0;
			lastFpsCalc = millis();
			
			// Display FPS counter in corner
			tft.fillRect(tft.width() - 80, 6, 75, 12, 0x18C3);
			tft.setTextColor(ILI9341_GREEN);
			tft.setTextSize(1);
			tft.setCursor(tft.width() - 75, 8);
			tft.printf("FPS: %.1f", fps);
		}
		
		// Small delay to stabilize frame pacing
		delay(10);
	}
}

