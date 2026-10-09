#pragma once
#include <Arduino.h>

// Initialize serial controller if needed
void initSerialCtrl();

// Non-blocking serial loop processor for ANSI arrow keys (encoder emulation) and text commands
void processSerialInput();

// Executes a serial/terminal command string
void handleSerialCommand(const String& cmd);

