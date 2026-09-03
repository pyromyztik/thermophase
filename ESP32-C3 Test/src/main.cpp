#include <Arduino.h>

#define LEDPIN 8

// declaring methods
void blinkSequence(const char* sequence);

// Morse code timing standards (based on 1 unit = 150 ms)
const int UNIT_LENGTH   = 150;
const int DOT_DURATION   = UNIT_LENGTH;
const int DASH_DURATION  = UNIT_LENGTH * 3;
const int ELEMENT_PAUSE  = UNIT_LENGTH;
const int LETTER_PAUSE   = UNIT_LENGTH * 3;
const int WORD_PAUSE     = UNIT_LENGTH * 7;

// ITU Morse lookup tables
const char* morseAlphabet[] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", // A - H
    "..",   ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", // I - P
    "--.-", ".-.",  "...",  "-",    "..-",  "...-", ".--",  "-..-", // Q - X
    "-.--", "--.."                                                  // Y - Z
};

const char* morseNumbers[] = {
    "-----", ".----", "..---", "...--", "....-", // 0 - 4
    ".....", "-....", "--...", "---..", "----."  // 5 - 9
};

void setup() {
    pinMode(LEDPIN, OUTPUT);
    digitalWrite(LEDPIN, LOW);

    Serial.begin(115200);

    // Give the USB-CDC serial connection up to 3 seconds to establish DTR
    // Avoids an indefinite lockup if the serial monitor isn't opened immediately
    unsigned long start = millis();
    while (!Serial && (millis() - start < 3000)) delay(10);

    Serial.println("ESP32-C3 System ready. Enter text to broadcast:");
}

void loop() {
    if (Serial.available() > 0) {
        char c = Serial.read();

        // Convert lowercase letters to uppercase
        if (c >= 'a' && c <= 'z') c -= 32;

        // Process alphabet (A-Z)
        if (c >= 'A' && c <= 'Z') {
            const char* sequence = morseAlphabet[c - 'A'];

            Serial.print("Broadcasting [");
            Serial.print(c);
            Serial.print("] : ");
            Serial.println(sequence);

            blinkSequence(sequence);
            delay(LETTER_PAUSE);
        }
        // Process digits (0-9)
        else if (c >= '0' && c <= '9') {
            const char* sequence = morseNumbers[c - '0'];

            Serial.print("Broadcasting [");
            Serial.print(c);
            Serial.print("] : ");
            Serial.println(sequence);

            blinkSequence(sequence);
            delay(LETTER_PAUSE);
        }
        // Process spaces as word boundaries
        else if (c == ' ') {
            Serial.println("[ WORD SPACE ]");
            delay(WORD_PAUSE);
        }
        // Carriage returns and newlines are silently ignored
    }
}

void blinkSequence(const char* sequence) {
    int i = 0;

    while (sequence[i] != '\0') {
        digitalWrite(LEDPIN, HIGH);

        // Hold LED on for the duration of a dot or dash
        if (sequence[i] == '.') delay(DOT_DURATION);
        else if (sequence[i] == '-') delay(DASH_DURATION);

        digitalWrite(LEDPIN, LOW);

        // Pause between elements within the same character
        if (sequence[i + 1] != '\0') delay(ELEMENT_PAUSE);

        i++;
    }
}