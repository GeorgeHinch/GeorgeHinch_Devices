#pragma once

// Copy this file to include/config.h and fill in your values.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// Root CA used to validate GitHub's HTTPS certificate chain.
// Replace this placeholder with the current PEM root CA used by github.com.
// Do not use client.setInsecure() for deployed controllers.
static const char GITHUB_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE_WITH_CURRENT_GITHUB_ROOT_CA_CERTIFICATE
-----END CERTIFICATE-----
)EOF";
