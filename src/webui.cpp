// Weboberflaeche, REST-API, Captive Portal und OTA
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "config.h"
#include "debuglog.h"
#include "time-service.h"
#include "wifi-ble.h"
#include "vesc.h"
#include "webui.h"


// ── Static assets (Font + CSS, ausgelagert fuer Mehrfachnutzung) ──────────────

// ── Ndot-47 Schriftart (WOFF2, ausgeliefert ueber /font.woff2) ──────────────
// Wird vom geteilten /style.css per @font-face geladen, von beiden Seiten genutzt.
// Ndot-47 Schriftart als WOFF2 (subsetted), 2848 bytes
// Wird ueber /font.woff2 ausgeliefert, von beiden Seiten per @font-face geladen.
static const uint8_t NDOT_FONT_WOFF2[] PROGMEM = {
  0x77,0x4f,0x46,0x32,0x4f,0x54,0x54,0x4f,0x00,0x00,0x0b,0x20,0x00,0x0a,0x00,0x00,
  0x00,0x00,0x94,0x38,0x00,0x00,0x0a,0xd8,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0d,0x82,0x9c,0x1d,0x1b,0x85,0x3e,0x06,0x60,0x00,0x3c,0x01,0x36,0x02,0x24,0x03,
  0x83,0x04,0x04,0x06,0x05,0x06,0x07,0x20,0x1b,0x80,0x93,0x51,0x54,0x51,0x5a,0xa3,
  0x28,0x97,0x93,0x2d,0xc1,0x57,0x05,0xd9,0x90,0x21,0xce,0x57,0x15,0x9d,0x26,0xaa,
  0x28,0x54,0x36,0x97,0xbb,0xdd,0xfb,0x74,0xca,0xef,0x86,0xa1,0x68,0x62,0x5c,0x79,
  0x9b,0x0c,0xec,0x2f,0x7f,0x92,0xcd,0x16,0xe0,0x2b,0x89,0xad,0x88,0xe3,0x07,0x78,
  0xa4,0xdc,0x9a,0xb0,0x6d,0x37,0x41,0xab,0x21,0x7d,0x4d,0x96,0x56,0x85,0x95,0x48,
  0xa4,0x89,0x17,0xca,0xb5,0x3f,0xaa,0xb5,0x14,0xe4,0x19,0x0f,0xbc,0x2c,0x7b,0xa4,
  0xdc,0x25,0x69,0xad,0xba,0x51,0xf5,0x5e,0x51,0x89,0x36,0xde,0xf8,0x9d,0xef,0xa2,
  0xd5,0x11,0x17,0xd0,0x9e,0x36,0xf7,0x86,0x59,0xff,0x97,0x5e,0xd1,0xfb,0xa4,0x7f,
  0x49,0x67,0xec,0x02,0x1e,0x9a,0x4d,0x69,0xc4,0x67,0x79,0x5e,0x2a,0x25,0x1e,0xe0,
  0x8e,0xd9,0x03,0xcb,0xff,0xad,0xb5,0x7a,0x1f,0xb7,0x92,0x8e,0xd0,0x4d,0x1a,0x99,
  0x94,0x66,0x76,0xff,0xce,0x0d,0xa2,0x12,0x49,0x3c,0xda,0xee,0xcd,0x3f,0x97,0x86,
  0x27,0x52,0x51,0x29,0x81,0x48,0x23,0xd4,0x4b,0x91,0x90,0x12,0x56,0x4e,0xff,0xa5,
  0x3f,0xad,0x51,0xd4,0x0a,0x04,0xe7,0xc9,0x8d,0x28,0xf6,0x97,0xd6,0x9b,0x56,0xac,
  0xb9,0xa4,0x23,0x92,0x00,0xcc,0x7d,0x3a,0xa5,0x74,0xfb,0xa5,0xfe,0x8e,0xe9,0x97,
  0x87,0x7f,0x8e,0x6b,0x4a,0xd3,0x83,0xd6,0xcd,0x16,0x04,0xac,0x16,0xc7,0xcd,0x5c,
  0x11,0x30,0x0a,0x42,0x88,0x4a,0xa4,0x7f,0x54,0xbc,0xbd,0x53,0x4a,0xc1,0x4c,0x4e,
  0xf3,0xff,0x33,0x98,0x1f,0xf2,0x53,0x6c,0x67,0xd4,0x31,0xa3,0x41,0x66,0x34,0x25,
  0x30,0xa3,0x45,0x67,0xb4,0x79,0x33,0x2a,0xec,0x7f,0xba,0x04,0xef,0xbb,0x04,0x1b,
  0xfd,0xd1,0x1a,0xc2,0x37,0x34,0x84,0x7e,0xf1,0x67,0xe5,0xa4,0x47,0x07,0x98,0x18,
  0xb9,0x59,0x03,0x59,0xf0,0x29,0x08,0x18,0x59,0x91,0x7b,0x64,0x9e,0xaa,0xd8,0x58,
  0x5a,0xbf,0xea,0x5a,0x7d,0x22,0x07,0x77,0x1b,0xcd,0x19,0x9b,0x29,0xa4,0xe7,0x30,
  0x39,0x6c,0x6e,0x0d,0xf7,0xfb,0xe6,0xf0,0xc9,0x5b,0x43,0x71,0xd7,0x91,0xcc,0xd6,
  0x89,0x45,0x92,0xcc,0x7f,0xdd,0xcc,0x69,0xce,0xbd,0xca,0x33,0xe2,0x15,0x58,0x50,
  0x50,0x63,0xd6,0x78,0xfc,0xd5,0xde,0xb5,0xa6,0xca,0x71,0x95,0x95,0x5f,0x64,0x50,
  0x40,0x53,0x65,0x8a,0x7f,0x58,0x54,0xa9,0x55,0x9d,0x38,0xad,0x36,0x6c,0x22,0xfe,
  0x6b,0x60,0x5b,0xfe,0x22,0x44,0x24,0xeb,0x87,0x9b,0x90,0x8f,0xa5,0xe1,0x73,0x76,
  0x8f,0x8b,0xb9,0x7f,0xe1,0xa1,0x1d,0xb4,0x0a,0x67,0x05,0x35,0x4b,0x4b,0x71,0x65,
  0x6e,0x6e,0x30,0xa1,0xae,0xf5,0xed,0x26,0xa7,0x8b,0x0b,0x38,0x17,0x47,0x94,0x98,
  0xf4,0x3b,0x4e,0xf8,0x38,0x5a,0xd9,0x58,0x51,0xcb,0x09,0xb8,0xb1,0x1e,0xa2,0x2b,
  0xe5,0x8b,0x5e,0x6a,0xe7,0x24,0x2a,0xbf,0x03,0x0e,0x26,0x46,0xb0,0x6e,0x91,0x6c,
  0x2c,0x1f,0xef,0x10,0xfc,0x80,0x5f,0x3c,0x75,0xed,0xa7,0xd0,0x9b,0x15,0xff,0x0c,
  0x66,0x3a,0xf6,0xbf,0x09,0x20,0x20,0x42,0x28,0x02,0x51,0x27,0xb4,0x81,0xef,0x60,
  0x03,0x08,0x16,0x08,0x92,0xa5,0x45,0x95,0x3c,0x61,0x29,0x42,0x0b,0xa2,0x0f,0xc4,
  0x67,0x24,0xd2,0x25,0x5b,0xa5,0x56,0x5e,0x4f,0x99,0x2c,0x99,0xcd,0xb2,0x42,0xb2,
  0x43,0x72,0xd6,0x0a,0x36,0x8a,0x95,0xca,0xb6,0x2a,0xbf,0xd5,0x0e,0x6a,0x5c,0xd1,
  0xba,0xca,0x59,0xe9,0x46,0xe9,0xdb,0xe8,0x1f,0x30,0x88,0x33,0x98,0x37,0x72,0x31,
  0x7a,0x69,0xc2,0x37,0xcb,0xb1,0xc8,0xb5,0x7a,0x61,0xd3,0x61,0x77,0xd4,0xe1,0x8f,
  0xd3,0x1e,0x97,0xcb,0x6e,0x9f,0x3d,0x82,0x3d,0xb7,0x7a,0x17,0xfb,0x9c,0xf5,0x67,
  0x81,0xfa,0xc1,0x06,0xa1,0x24,0x5c,0x2f,0x32,0x27,0xda,0x3b,0x66,0x75,0x5c,0x47,
  0x82,0x6e,0x92,0x6f,0xf2,0xbb,0x54,0xa3,0x74,0xd1,0x8c,0xbc,0x4c,0xc9,0xac,0xa2,
  0x6c,0xc9,0xec,0x3d,0xd9,0xbf,0x73,0xaa,0xf3,0x6c,0xf2,0xa3,0x0b,0xbe,0x17,0x95,
  0x17,0x5f,0x29,0xbd,0x5a,0x5e,0x51,0x51,0x51,0x39,0x51,0x2d,0x53,0xfd,0xb5,0x4e,
  0xb2,0xbe,0xa2,0x61,0x7f,0x53,0x65,0x8b,0x6c,0xab,0x74,0x5b,0x4c,0xfb,0x74,0xc7,
  0xaf,0x2e,0xfb,0x1e,0xb9,0x5e,0xc7,0xbe,0xd9,0x81,0xe6,0xc1,0xd5,0x43,0xcd,0xc3,
  0xab,0xa7,0xdc,0xe7,0xfd,0xed,0x6d,0x8b,0xff,0xd1,0x59,0x6b,0x0d,0xda,0x17,0xdb,
  0xd9,0xfe,0x75,0x2b,0x7f,0x78,0x83,0xda,0x66,0x31,0x7d,0xe2,0xb1,0xac,0x5d,0xd3,
  0xa3,0xed,0x0c,0xf2,0xc0,0xed,0xba,0x68,0x78,0xbf,0x7c,0x8f,0x7d,0xcf,0xdf,0xfd,
  0x3b,0x9b,0x69,0x7d,0x79,0x76,0x3c,0xa3,0xa9,0x7f,0x46,0xac,0x86,0xb2,0x7f,0xf6,
  0xfa,0xdd,0xc8,0xa8,0xb6,0x7b,0xaa,0xa2,0xbd,0x5b,0x08,0x6f,0xdb,0x09,0x8b,0xe6,
  0x02,0x5e,0x13,0xc3,0xca,0xc8,0x1a,0x45,0xcf,0xae,0x4c,0x22,0xfd,0x8d,0x44,0x70,
  0x9d,0x8a,0x1a,0x04,0x08,0xe0,0xa5,0xb8,0x4c,0x1d,0x31,0x7f,0x56,0x1d,0xe0,0x41,
  0xaf,0xe7,0xd3,0x29,0xd7,0x5b,0x41,0xb6,0x01,0x35,0x9e,0x00,0x70,0x75,0x45,0xcc,
  0x82,0x40,0xac,0xa7,0x2b,0x0d,0x6b,0x47,0x4b,0xd8,0x91,0xcb,0x36,0x6b,0xa4,0xbe,
  0xf8,0xdf,0x89,0x87,0x39,0xbd,0xb8,0xe8,0xaa,0x0a,0x32,0x19,0x73,0x4d,0xcd,0x1e,
  0x03,0x55,0x51,0x8a,0xd1,0x6e,0x02,0x4e,0xb9,0x2f,0x7b,0xd1,0xe9,0x58,0x81,0xe1,
  0x15,0xb0,0x58,0x44,0x2a,0x02,0x8c,0xb3,0x04,0xa7,0x55,0x24,0x16,0x32,0xa0,0xe8,
  0xf8,0x12,0xda,0xac,0x62,0x27,0xe0,0x59,0xa1,0xcd,0x6a,0xed,0xad,0x8a,0x4a,0x45,
  0x20,0x44,0x9e,0x68,0x41,0x35,0x07,0x08,0xa0,0x40,0x41,0x16,0x43,0x63,0xc2,0xeb,
  0x79,0x4b,0xcd,0xa2,0x62,0x4d,0xc6,0x49,0x3f,0x5b,0x72,0x55,0x31,0x3c,0x32,0xf0,
  0xb2,0xa8,0xea,0xa2,0xfd,0x1a,0x31,0x19,0x30,0x70,0x4e,0xc9,0x02,0xbe,0x90,0x74,
  0xe7,0x95,0x05,0xa0,0x32,0x2a,0x23,0xe3,0x39,0x4e,0xc4,0x7a,0xc0,0x4c,0x81,0x32,
  0xe7,0x02,0x96,0x29,0x91,0xcb,0x5a,0x40,0x90,0xf1,0x54,0x2d,0xa6,0x72,0x8c,0xd5,
  0xac,0x5c,0x60,0x01,0x15,0x28,0xf0,0x34,0x35,0x4a,0xae,0xc5,0x2b,0x0d,0xfb,0xbc,
  0x7a,0x32,0x6e,0x84,0xaa,0x17,0x80,0x39,0x25,0x33,0xe2,0xf8,0x8d,0x8c,0xce,0x67,
  0xc9,0x2b,0x77,0x24,0xb3,0x32,0x3a,0xaa,0x8a,0x44,0x20,0x2b,0xc2,0xa4,0x09,0x2d,
  0x8b,0x88,0xc6,0xa5,0x27,0x67,0xea,0x6f,0xf1,0x3c,0x8a,0xe5,0x12,0x5e,0xf1,0x8b,
  0x54,0xc8,0x4c,0x7e,0xad,0xe4,0x12,0x79,0x9e,0x2d,0x53,0xa4,0x82,0xca,0xb5,0xf4,
  0xcc,0x1b,0x28,0xd1,0x49,0x00,0x37,0x71,0xc2,0xd3,0x8c,0x27,0x04,0x35,0x11,0x6a,
  0xb8,0xa2,0xcf,0x3c,0xce,0x51,0xb2,0x79,0x21,0x9f,0xb8,0x1a,0xe7,0x47,0x52,0x12,
  0x35,0x37,0x39,0x10,0xd2,0x0c,0x24,0x11,0x10,0x3a,0x44,0x16,0x37,0x28,0x32,0x21,
  0x6a,0xa6,0xc7,0x64,0x18,0x80,0xb0,0xd1,0x24,0x76,0x2e,0x76,0xaa,0xbb,0x87,0x1b,
  0x6a,0x15,0x0d,0x2b,0xe2,0x86,0xe8,0x25,0x5d,0xe0,0x95,0xe2,0x33,0x95,0xc6,0x2c,
  0xed,0x49,0x24,0x2a,0x40,0x1e,0x88,0x4d,0x31,0x54,0xd3,0x39,0xd5,0x49,0x14,0x81,
  0x5a,0x44,0xcc,0x8c,0x64,0xc5,0x4e,0x8a,0x40,0x31,0xaf,0x0b,0x00,0x6e,0xea,0x1e,
  0x12,0xe4,0x38,0x95,0xe6,0x05,0x29,0x17,0xcd,0x8a,0x24,0x6c,0xc9,0xd3,0xe2,0x78,
  0x6a,0x79,0x36,0x1c,0x67,0xd7,0x91,0xae,0xd6,0x2e,0xb1,0x31,0x0c,0x8a,0xde,0x1e,
  0xd5,0x56,0xcc,0x53,0xd9,0x5f,0xdd,0x1d,0x00,0x87,0x19,0x54,0xdd,0x9c,0x49,0x15,
  0x95,0x13,0xe8,0x0d,0xdc,0x10,0x98,0xb5,0xca,0x2a,0x4b,0xce,0x97,0x24,0x92,0x7c,
  0x8e,0x1c,0x38,0x0a,0x3c,0x97,0x1a,0xff,0x28,0x9e,0xb9,0xfe,0x75,0x7c,0x95,0xd0,
  0xfe,0xe6,0x05,0xa0,0x95,0xe9,0xdf,0x2d,0xd8,0xbd,0x58,0xf3,0x31,0x0b,0x42,0xc9,
  0xe4,0x58,0x5f,0x01,0x90,0x4b,0x85,0xb3,0xea,0x30,0x0b,0xf8,0x3e,0x57,0x8e,0x31,
  0x4a,0x80,0xd5,0x83,0x2f,0xef,0x8a,0xdf,0x79,0xe1,0x6c,0xa2,0xca,0x17,0x09,0xe0,
  0x01,0xa1,0x23,0x4a,0xf0,0x90,0xdf,0xb1,0x54,0x09,0x86,0x83,0x50,0x8d,0x5c,0x29,
  0xcb,0x53,0x99,0x46,0x08,0xc7,0xb6,0x07,0x10,0x9a,0x13,0xef,0x1b,0xe8,0x78,0x84,
  0x56,0x2b,0x05,0xa1,0xa9,0x9a,0x30,0x23,0x28,0xfb,0x5a,0x1e,0x07,0x40,0xb2,0x1d,
  0xda,0xdf,0x90,0xc7,0xd5,0x01,0xe2,0xf2,0x05,0x46,0x78,0x1b,0x36,0x64,0x33,0x64,
  0x80,0xf6,0x71,0x35,0x02,0x5c,0x0e,0x75,0x29,0xcb,0xb8,0x1c,0xa4,0x0d,0xda,0x2e,
  0x87,0xbc,0x76,0x12,0xf0,0x18,0x1b,0xc6,0xbd,0xdf,0x07,0x80,0xde,0x1d,0x5b,0x82,
  0x05,0x2f,0xa8,0x2e,0xaa,0x7f,0x16,0x82,0x9d,0x08,0xcf,0x8c,0x60,0x27,0x58,0x99,
  0x58,0x17,0x2a,0x25,0x2d,0xd4,0x92,0x8a,0x89,0xf1,0x4a,0xa7,0x11,0x35,0xa7,0x43,
  0x73,0xcd,0xf7,0x12,0xf9,0xaa,0x58,0x24,0x09,0x70,0x13,0x04,0xbc,0x36,0xa5,0x40,
  0xd8,0xe2,0x00,0xd7,0x49,0xdc,0x03,0x06,0x8f,0x09,0xc1,0x79,0x42,0x64,0xd1,0xd7,
  0xf4,0xa3,0x21,0x52,0x82,0xc5,0x86,0x67,0xa6,0x6b,0xab,0x49,0xa6,0x00,0x15,0xb2,
  0x1b,0x3e,0x48,0xa0,0x31,0x89,0x5d,0x45,0xf4,0x68,0x76,0x11,0x39,0xcd,0x46,0xe3,
  0x61,0x58,0xcc,0x48,0x30,0x59,0x76,0x85,0xf2,0x0b,0x2d,0x6e,0xa5,0x43,0x15,0x77,
  0xd8,0x37,0x78,0x30,0x5f,0xf1,0x7e,0x75,0xc8,0xd0,0x4e,0x52,0x2e,0xc5,0xc6,0x43,
  0xb2,0x62,0x10,0x00,0xba,0x7f,0x92,0xf2,0x7c,0x70,0xab,0x6f,0x4d,0xec,0x9f,0x48,
  0x2e,0x80,0xc9,0x4e,0x44,0xef,0xc5,0x80,0x1c,0x4f,0x42,0x37,0xbc,0x78,0xec,0x93,
  0xd4,0x06,0x68,0xe8,0x37,0x2f,0x06,0x7c,0x31,0x1a,0x91,0x71,0xab,0xed,0x9b,0x74,
  0xd9,0x85,0x3f,0x62,0xb2,0x22,0xe5,0xdb,0x4e,0x12,0xfb,0xef,0x33,0xd1,0x0b,0xb6,
  0x5b,0xde,0x22,0xbf,0xbe,0xfd,0xb1,0x31,0xfe,0x68,0x1e,0xff,0x16,0x48,0xa8,0x55,
  0x04,0x6e,0x04,0x02,0x27,0xe4,0xed,0xba,0xa8,0x2c,0x2f,0x0b,0x1e,0xc7,0x17,0xf5,
  0xcb,0x25,0x68,0x2c,0xa7,0x47,0x35,0xe0,0x81,0x4f,0xd0,0xba,0x85,0x2d,0x92,0x21,
  0xdc,0xf6,0x5b,0x15,0xe4,0x1e,0x67,0x58,0x2e,0x73,0xee,0x70,0x97,0xcb,0x71,0x79,
  0xa4,0x48,0x4d,0xe0,0x13,0x3c,0xea,0x55,0xb9,0x1c,0x19,0xe5,0xab,0x68,0xbb,0xe2,
  0xb5,0x21,0x43,0x45,0x94,0x87,0x63,0x2a,0x1c,0x42,0x70,0x0f,0x88,0x8a,0xdf,0x1b,
  0x82,0x21,0x4e,0x1d,0x17,0x5c,0xc5,0x11,0x6b,0xd9,0xe6,0x58,0xac,0x94,0x3f,0xde,
  0xdf,0x34,0x95,0xd2,0x49,0xad,0x07,0x82,0x16,0x8a,0xa1,0xcf,0xf2,0x49,0xa1,0x57,
  0x11,0x23,0x07,0x80,0x7f,0xa9,0x80,0x28,0x45,0x86,0x84,0x89,0x17,0xc4,0x0a,0x35,
  0xaf,0xd0,0x65,0x21,0xf8,0x67,0x23,0x7e,0xd2,0xe7,0xa6,0x5d,0x5c,0x46,0x1b,0xb0,
  0xde,0xb4,0xcc,0x82,0x8c,0x64,0xff,0xd3,0x7f,0x38,0xd2,0xbf,0x87,0xd9,0xfe,0xc0,
  0x51,0xcf,0x18,0x28,0xb2,0x25,0x1c,0x70,0xc4,0xc7,0x38,0xe7,0x7a,0xb0,0x71,0xbf,
  0xe5,0xd4,0x5a,0xa0,0x79,0xbd,0xb0,0x4e,0xa7,0x68,0x3c,0xa4,0xed,0xb8,0x9c,0x18,
  0xde,0xdc,0xe0,0x33,0x7d,0x9c,0xa1,0xff,0xd9,0x2a,0xfa,0xcd,0x55,0x17,0x8d,0x76,
  0xbf,0xf9,0x1e,0x16,0x2e,0x45,0xa6,0xe9,0x73,0x31,0x86,0xbd,0xad,0xf8,0x8b,0x69,
  0x9a,0x1e,0x5b,0x4e,0x2f,0x8a,0xe3,0x78,0xf9,0x7c,0xd2,0xfe,0x28,0x1a,0x2e,0xea,
  0xca,0xa7,0xe5,0xc8,0x32,0x75,0x3d,0x82,0xbc,0x22,0x8b,0x9e,0x79,0x6c,0x2b,0xff,
  0xce,0x8a,0xff,0x8a,0x8c,0xdd,0x45,0x1c,0x81,0x68,0x26,0xf8,0xf2,0x27,0x09,0x34,
  0x7f,0x49,0xbf,0x4f,0x18,0x2f,0x35,0xb1,0x62,0x92,0x13,0x29,0x92,0xb2,0xe6,0xa7,
  0x8c,0xf1,0x79,0x21,0x9e,0x1e,0xa1,0x2a,0xd2,0x5f,0xd2,0x48,0x09,0x2b,0xb8,0xb6,
  0x7f,0xfb,0x83,0x57,0x97,0x0d,0xc7,0x5c,0x3c,0xff,0x7c,0xea,0xff,0x78,0x44,0xc4,
  0xa5,0xf8,0x4b,0xbc,0x2e,0x35,0x2d,0x6b,0xab,0x8e,0xc5,0xed,0xa5,0x15,0xbc,0x76,
  0x37,0x78,0xcc,0x99,0x37,0x24,0x2d,0x64,0x10,0x24,0xe0,0xb3,0x1d,0xb4,0x9a,0x28,
  0x26,0xd1,0xb5,0x14,0xf1,0xdd,0x05,0xa1,0xb3,0xd0,0xe4,0x71,0xad,0xa6,0x98,0xf1,
  0xb1,0x96,0x63,0xb1,0xd7,0xe7,0x3f,0x28,0xcc,0x22,0xe6,0x9f,0x8b,0xbd,0x25,0xe4,
  0xfa,0x6f,0x62,0xbc,0x15,0x56,0x2a,0x4c,0x24,0xde,0x6c,0x6a,0xe5,0x96,0xf5,0x16,
  0x17,0xc9,0x52,0x13,0x6b,0x5b,0x18,0xf0,0xc4,0x7d,0x61,0x80,0x53,0x1c,0x48,0x59,
  0x20,0x75,0x24,0xee,0x62,0xed,0x5a,0x59,0x45,0xe3,0x7c,0xcc,0xdf,0xb8,0x69,0xcd,
  0xa4,0x15,0x75,0xb8,0x21,0xa6,0x5a,0x34,0x4a,0x53,0x56,0x11,0xab,0xab,0xa1,0x9a,
  0x2a,0x9d,0x0b,0x90,0x0e,0xd2,0xcc,0x8e,0x04,0xaf,0x27,0x9e,0x20,0xf3,0x87,0x9c,
  0x94,0xd3,0x59,0x96,0x8a,0x55,0x6a,0xd0,0x88,0xbf,0xbd,0x0a,0x53,0x52,0xd7,0x4a,
  0x4c,0x78,0xf4,0x9d,0x2e,0x60,0xd9,0x7f,0x3e,0x9b,0xaf,0xbc,0x2d,0xd6,0xff,0x6b,
  0x71,0xdf,0x3c,0x49,0x4e,0x43,0x34,0x34,0x0a,0xd9,0xad,0xa7,0x14,0x96,0x6b,0x88,
  0xcc,0x5c,0x1b,0x63,0x49,0x2e,0x89,0xdd,0xd8,0x5c,0x65,0x2d,0xd5,0x3b,0xdc,0x88,
  0xa0,0x47,0x09,0x20,0x5f,0x07,0xd7,0xa5,0x09,0xaf,0xcf,0x44,0x21,0xdf,0xc9,0x88,
  0x50,0x57,0x98,0x09,0xa9,0xbd,0xa6,0xaf,0x29,0x82,0xe6,0xc0,0x12,0x72,0x76,0x3d,
  0x2c,0xdf,0x93,0x1e,0x08,0x51,0x17,0x39,0x66,0x0e,0x02,0xa1,0x63,0xde,0xfd,0x06,
  0xc2,0xc1,0x09,0x37,0xbc,0xad,0xa7,0xe3,0x12,0x0c,0x54,0x5b,0x21,0xa8,0xb1,0x34,
  0x21,0xc3,0xf2,0x14,0xf9,0xa2,0x6e,0x9a,0xf2,0xb5,0xa9,0x49,0x2d,0x0e,0x6b,0x72,
  0xca,0xf1,0x02,0x7d,0x66,0xe4,0x2c,0x2d,0xa5,0x9d,0x3f,0xad,0x95,0x3f,0x62,0xa5,
  0x58,0x12,0x4c,0xd3,0x37,0xf0,0xa7,0x4d,0x3d,0x56,0x65,0x2d,0xde,0x48,0xaf,0x19,
  0x52,0x71,0x60,0x0e,0xfc,0x0e,0x18,0x53,0x27,0x03,0x49,0x46,0x1d,0x71,0x12,0x14,
  0x77,0x91,0xbf,0xd6,0xc2,0xba,0x84,0xcb,0xba,0x31,0x8a,0x49,0x6f,0x35,0x62,0x13,
  0x6c,0x5c,0x50,0x4e,0xa4,0x9c,0x8e,0x2d,0xd5,0x15,0x0b,0xdf,0x00,0x31,0xed,0x6a,
  0x52,0x92,0x0e,0xae,0x2c,0xc6,0xeb,0x75,0x1f,0x08,0xd6,0x12,0x6c,0x92,0xd7,0x22,
  0x79,0xc1,0xcd,0x3e,0xfe,0xf5,0xe1,0x0c,0x04,0x80,0x18,0xb4,0x98,0x23,0x65,0x82,
  0xba,0x0a,0x88,0x80,0x01,0xc0,0x2f,0x8e,0xb4,0x58,0x9a,0x5f,0xb3,0x6f,0x18,0x3b,
  0xb7,0xc0,0x40,0xef,0x40,0xa4,0x36,0x14,0x3b,0xcc,0x99,0x70,0x02,0xeb,0x3f,0x2f,
  0x9d,0xc0,0xa1,0xfb,0xc5,0x5b,0x44,0xff,0xa5,0x19,0x5e,0x10,0x25,0xa2,0x7f,0xdc,
  0x92,0x14,0x91,0x1f,0x64,0x8e,0x6c,0x44,0x4b,0xbb,0xf3,0x6a,0xd7,0xef,0x98,0xa3,
  0x8c,0xca,0xd1,0x70,0x10,0x70,0xa4,0x20,0x4b,0x8e,0x3c,0x45,0xca,0x54,0xa9,0xd1,
  0xa5,0x47,0x9f,0x01,0x23,0x16,0xac,0xd9,0xb0,0x65,0xc7,0x81,0x2b,0x5f,0x7e,0xfc,
  0x05,0x08,0x12,0x21,0x5a,0x8c,0x58,0x71,0x12,0xa4,0x62,0xd0,0x05,0x58,0x37,0x5f,
  0x40,0x6a,0x13,0x82,0x43,0x71,0xbe,0x93,0xae,0x48,0x26,0x20,0x48,0x86,0xd4,0x21,
  0xb2,0x67,0x8d,0xc1,0xe1,0x10,0x7d,0xff,0x30,0xef,0xc8,0x52,0x38,0x40,0x12,0xb2,
  0x00,0x72,0x52,0x3e,0x93,0x8a,0x77,0x52,0xf9,0xbb,0x54,0xfd,0x0c,0x6a,0x52,0x77,
  0x37,0xe8,0x83,0xb1,0x34,0xdb,0x05,0xd6,0x5e,0xf6,0xa4,0xd8,0x0b,0x87,0x31,0x7a,
  0xaa,0xf0,0x4a,0x11,0x05,0xdb,0x40,0xf6,0x04,0x3a,0xda,0xb6,0x1a,0x86,0x69,0x09,
  0x53,0x81,0x32,0x81,0x2f,0xf8,0x43,0x30,0x84,0x41,0x74,0x7e,0xfc,0x72,0x02,0xd4,
  0xde,0x16,0x80,0xee,0xc4,0x1d,0xa2,0xe6,0x64,0x20,0x18,0xfb,0x43,0x90,0x3a,0x59,
  0x1f,0xb0,0x0d,0x88,0x3d,0xa1,0xcb,0x04,0x2a,0xa1,0xc5,0x1b,0x00,0x94,0x55,0x93,
  0x29,0x7c,0x30,0xde,0x87,0x4f,0xe3,0x00,0xe8,0xdf,0xd9,0x67,0x99,0xb7,0x9f,0xbd,
  0x55,0x33,0xf4,0x24,0xf8,0x52,0xe0,0x8b,0x8f,0x85,0x37,0xce,0xfe,0x23,0xbf,0xe4,
  0x0f,0x88,0xec,0xcd,0x91,0x43,0xbf,0x89,0x25,0x09,0xc9,0x03,0xe0,0x00,0x00,0x05,
  0xc0,0x03,0xa0,0x80,0x07,0x02,0x40,0x01,0x83,0x31,0x40,0x08,0x0c,0x04,0x14,0x1d,
  0xf6,0xb7,0x00,0xee,0xfb,0xf6,0xf2,0xcb,0xee,0x27,0x25,0x24,0xe7,0x7d,0x78,0x35,
  0x9b,0xfc,0x30,0x4b,0xda,0xfd,0xaf,0x08,0x84,0x6f,0x00,0xc0,0x3b,0x9d,0x2f,0x22,
  0xe8,0x69,0xab,0xf4,0x9d,0xc0,0x92,0xbf,0xf0,0x0d,0xa1,0x5f,0x00,0x11,0xd0,0x23,
  0xa7,0x45,0xf6,0xe6,0x6c,0x03,0x50,0xf1,0x1a,0x87,0xe0,0xc3,0x92,0x60,0xab,0x97,
  0xf4,0x00,0x74,0xa5,0x67,0x9f,0xeb,0xc0,0x77,0xd0,0x8f,0xe4,0x25,0xe0,0xfb,0x66,
  0x79,0x4d,0x99,0x76,0x88,0x40,0x96,0xe3,0x74,0xe4,0xc2,0xe7,0x59,0x72,0x84,0x84,
  0x5b,0x8e,0x55,0x9c,0xc6,0x93,0x99,0x3a,0x0f,0x25,0xd4,0x2c,0x05,0x96,0x04,0x58,
  0x6e,0x6d,0x97,0x1d,0x39,0x02,0x42,0x3f,0x5d,0x42,0x78,0x2a,0x41,0x0e,0xc9,0x70,
  0x10,0xf9,0x79,0x08,0x88,0x06,0x01,0x00,0x10,0x02,0x0f,0x1f,0x06,0xba,0x80,0x3c,
};
static const size_t NDOT_FONT_WOFF2_LEN = 2848;

static const char STYLE_CSS[] PROGMEM = R"csslit(
    @font-face{font-family:'Ndot47';src:url('/font.woff2') format('woff2');font-display:swap}
    :root{--bg:#000000;--bg2:#111111;--bg3:#1a1a1a;--border:#222222;--border2:#333333;--text:#e0e0e0;--text2:#aaa;--text3:#666;--accent:#00bcd4;--accent2:#00acc1;--ok:#4caf50;--err:#f44336;--ok-bg:#0a1f0d;--err-bg:#1f0a0a}
    [data-theme=light]{--bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;--border:#dddddd;--border2:#cccccc;--text:#111111;--text2:#555555;--text3:#999999;--accent:#0288d1;--accent2:#0277bd;--ok:#388e3c;--err:#c62828;--ok-bg:#e8f5e9;--err-bg:#ffebee}
    @media(prefers-color-scheme:light){:root:not([data-theme=dark]){--bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;--border:#dddddd;--border2:#cccccc;--text:#111111;--text2:#555555;--text3:#999999;--accent:#0288d1;--accent2:#0277bd;--ok:#388e3c;--err:#c62828;--ok-bg:#e8f5e9;--err-bg:#ffebee}}
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding:16px}
    .wrap{max-width:600px;margin:0 auto;padding:0 16px}
    h1{color:var(--accent);font-size:18px;margin-bottom:4px}
    .sub{color:var(--text3);font-size:12px;margin-bottom:24px}
    .tabs{display:flex;gap:4px;margin-bottom:16px;flex-wrap:wrap}
    .tab{padding:8px 16px;background:var(--bg2);border:1px solid var(--border);border-radius:6px;cursor:pointer;font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:13px;color:var(--text2)}
    .tab.active{background:var(--accent);color:#111;border-color:var(--accent)}
    .panel{display:none}.panel.active{display:block}
    .section{background:var(--bg2);border:1px solid var(--border2);border-radius:8px;padding:20px;margin-bottom:12px}
    .section h3{color:var(--accent);font-size:13px;margin-bottom:14px;text-transform:uppercase;letter-spacing:1px}
    label{display:block;font-size:12px;color:var(--text2);margin-bottom:4px;margin-top:10px}
    label:first-of-type{margin-top:0}
    input[type=text],input[type=password]{width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:13px}
    input:focus{outline:none;border-color:var(--accent)}
    .checkbox-row{display:flex;align-items:center;gap:10px;margin-top:12px;font-size:13px;color:var(--text2);cursor:pointer}
    .checkbox-row input[type=checkbox]{width:16px;height:16px;accent-color:var(--accent);cursor:pointer}
    .btn{width:100%;padding:11px;background:var(--accent);color:#111;border:none;border-radius:6px;font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:14px;font-weight:bold;cursor:pointer;margin-top:14px}
    .btn:hover{background:var(--accent2)}.btn.sm{padding:6px 12px;font-size:12px;width:auto;margin-top:0}
    .btn.red{background:var(--err);color:#fff}.btn.red:hover{opacity:0.85}
    .btn.green{background:var(--ok);color:#111}.btn.green:hover{opacity:0.85}
    .btn:disabled{background:var(--border);color:var(--text3);cursor:not-allowed}
    .msg{margin-top:10px;padding:8px 12px;border-radius:4px;font-size:13px;display:none}
    .msg.ok{background:var(--ok-bg);color:var(--ok);display:block}
    .msg.err{background:var(--err-bg);color:var(--err);display:block}
    .wifi-entry{background:var(--bg3);border:1px solid var(--border2);border-radius:6px;padding:12px;margin-bottom:8px}
    .wifi-entry-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
    .wifi-entry-num{font-size:11px;color:var(--text3)}
    .drop-zone{border:2px dashed var(--border);border-radius:6px;padding:28px;text-align:center;cursor:pointer;margin-bottom:12px}
    .drop-zone:hover,.drop-zone.dragover{border-color:var(--accent);background:var(--bg2)}
    .drop-zone .icon{font-size:32px;margin-bottom:6px}
    .drop-zone .label{color:var(--text2);font-size:13px}
    .drop-zone .filename{color:var(--accent);font-size:12px;margin-top:6px}
    input[type=file]{display:none}
    .progress-wrap{display:none;margin-top:12px}
    .progress-bar-bg{background:var(--border);border-radius:4px;height:8px;overflow:hidden}
    .progress-bar{background:var(--accent);height:8px;width:0%;transition:width .2s}
    .status{margin-top:8px;font-size:12px;color:var(--text2);text-align:center}
    .status.ok{color:var(--ok)}.status.err{color:var(--err)}
    .info-row{display:flex;justify-content:space-between;font-size:12px;padding:4px 0;border-bottom:1px solid var(--border2)}
    .info-row:last-child{border:none}
    .info-val{color:var(--accent)}
    .lang-btn{position:fixed;top:12px;right:12px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:12px;cursor:pointer}
    .lang-btn:hover{border-color:var(--accent);color:var(--accent)}
    .theme-btn{position:fixed;top:12px;right:56px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:12px;cursor:pointer}
    .theme-btn:hover{border-color:var(--accent);color:var(--accent)}
    .ep{margin-bottom:10px;padding:10px;background:var(--bg3);border-radius:6px;font-size:12px}
    .method{display:inline-block;padding:2px 6px;border-radius:3px;font-weight:bold;margin-right:6px;font-size:11px}
    .method.get{background:#1a3a1a;color:#81c784}.method.post{background:#3a1a1a;color:#e57373}
    .ep .path{color:var(--accent);font-weight:bold}
    .ep a.path{color:var(--accent);font-weight:bold;text-decoration:none}
    .ep a.path:hover{text-decoration:underline}
    .ep .desc{color:var(--text2);margin-top:4px}
    .ep .fields{margin-top:4px;color:var(--text3);font-size:11px}
    .api-h2{color:var(--text2);font-size:12px;margin:14px 0 8px;text-transform:uppercase;letter-spacing:1px}
)csslit";

// ── HTML PAGE ─────────────────────────────────────────────────────────────────
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>🛴 VESC BLE/WiFi</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
<button class="theme-btn" onclick="toggleTheme()" id="themeBtn">☀️</button>
<button class="lang-btn" onclick="toggleLang()" id="langBtn">DE</button>
<div class="wrap">
  <h1>&#x1F6F4; VESC BLE/WiFi</h1>
  <div class="sub" id="statusBar">Loading...</div>
  <div class="tabs">
    <div class="tab active" onclick="showTab('info')">Info</div>
    <div class="tab" onclick="showTab('config')">Config</div>
    <div class="tab" onclick="showTab('ota')">OTA Flash</div>
    <div class="tab" onclick="showTab('api')">API</div>
    <div class="tab" id="tab-leds-link" style="display:none" onclick="location.href='/leds'">LED</div>
  </div>

  <!-- INFO -->
  <div class="panel active" id="tab-info">
    <div class="section">
      <h3 id="lbl-status">Status</h3>
      <div id="infoContent"><div style="color:#666;font-size:13px" id="lbl-loading">Loading...</div></div>
    </div>
  </div>

  <!-- CONFIG -->
  <div class="panel" id="tab-config">
    <div class="section">
      <h3>BLE</h3>
      <label id="lbl-ble-name">BLE Name (visible in VESC Tool)</label>
      <input type="text" id="ble_name" maxlength="32" placeholder="VESC-BLE-WiFi">
      <label class="checkbox-row" style="margin-top:12px">
        <input type="checkbox" id="ble_pin_enabled" onchange="document.getElementById('blepin_wrap').style.display=this.checked?'':'none'">
        <span id="lbl-ble-pin-en">Require PIN for BLE pairing</span>
      </label>
      <div id="blepin_wrap" style="display:none;margin-top:8px">
        <label id="lbl-ble-pin">BLE pairing PIN (6 digits)</label>
        <input type="text" id="ble_pin" maxlength="6" inputmode="numeric" placeholder="123456">
        <div style="font-size:11px;color:var(--text3);margin-top:6px" id="lbl-ble-pin-hint">
          Exactly 6 digits required (leading zeros allowed, e.g. 001234). With PIN enabled, unpaired devices cannot communicate — pairing with PIN is enforced (takes effect after restart). Already paired devices stay paired. Without the checkbox, pairing is accepted automatically (Just Works).
        </div>
      </div>
      <label class="checkbox-row" style="margin-top:12px">
        <input type="checkbox" id="ble_full_power">
        <span id="lbl-ble-fullpwr">Disable BLE power saving (full performance)</span>
      </label>
      <div style="font-size:11px;color:var(--text3);margin-top:6px" id="lbl-ble-fullpwr-hint">
        Without the checkbox, advertising slows down after 15s idle to give WiFi more airtime (BLE and WiFi share one radio). With the checkbox, advertising stays permanently at the fast interval (20-40ms) — the device is always instantly discoverable and connects fastest, but WiFi throughput may suffer. Takes effect immediately, no restart needed.
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-ap-title">Access Point (Fallback)</h3>
      <label id="lbl-ap-name">AP Name (SSID)</label>
      <input type="text" id="ap_ssid" maxlength="32" placeholder="VESC-BLE-WiFi">
      <label id="lbl-ap-pass">AP Password (leave empty for open network)</label>
      <input type="password" id="ap_pass" maxlength="64" placeholder="leave empty for open network">
      <label id="lbl-ap-mode-sel">AP Mode</label>
      <select id="ap_mode" onchange="document.getElementById('apmode_auto').style.display=this.value==2?'':'none';updateErpmVisibility()" style="width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:13px">
        <option value="1" id="opt-apmode-on">On (always on)</option>
        <option value="2" id="opt-apmode-auto">Auto (on when riding, off when idle)</option>
      </select>
      <div id="apmode_auto" style="display:none;margin-top:10px">
        <label id="lbl-ap-timeout">Idle timeout (seconds, AP off after no movement and no AP client)</label>
        <input type="text" id="ap_timeout" maxlength="6" placeholder="120">
        <div style="font-size:11px;color:var(--text3);margin-top:6px" id="lbl-apmode-hint">
          Like BLE Auto: riding above the ERPM threshold keeps the AP awake and brings it back after timeout. A connected AP client pauses the timer.
        </div>
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-blemode-title">BLE Mode</h3>
      <label id="lbl-blemode-sel">Mode</label>
      <select id="ble_mode" onchange="document.getElementById('blemode_auto').style.display=this.value==2?'':'none';updateErpmVisibility()" style="width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:13px">
        <option value="1" id="opt-mode-on">On (always advertise)</option>
        <option value="0" id="opt-mode-off">Off (no BLE)</option>
        <option value="2" id="opt-mode-auto">Auto (on when moving)</option>
      </select>
      <div id="blemode_auto" style="display:none;margin-top:10px">
        <label id="lbl-blemode-off">Idle timeout (seconds, BLE off after no movement and no client)</label>
        <input type="text" id="ble_auto_off_sec" maxlength="5" placeholder="120">
        <div style="font-size:11px;color:var(--text3);margin-top:6px" id="lbl-blemode-hint">
          Boot default: BLE on. Movement above threshold resets the idle timer. Active connection (BLE/TCP/Web-UI) pauses the timer.
        </div>
      </div>
    </div>
    <div class="section" id="erpm_section" style="display:none">
      <h3 id="lbl-erpm-title">Movement Detection</h3>
      <label id="lbl-erpm-power">ERPM threshold to wake BLE/WiFi when riding</label>
      <input type="text" id="ble_auto_erpm_on" maxlength="6" placeholder="200">
      <div style="font-size:11px;color:var(--text3);margin-top:6px" id="lbl-erpm-hint">
        When BLE Auto mode or AP Auto mode is active and has switched off after the idle timeout, riding above this ERPM value switches BLE/AP back on. Higher value = needs faster riding to wake.
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-conn-title">Connection</h3>
      <label id="lbl-port">TCP Port (default: 65101)</label>
      <input type="text" id="vesc_port" maxlength="5" placeholder="65101">
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px">
        <div><label>UART RX Pin</label><input type="text" id="rx_pin" maxlength="3" placeholder="6"></div>
        <div><label>UART TX Pin</label><input type="text" id="tx_pin" maxlength="3" placeholder="5"></div>
      </div>
      <label class="checkbox-row" style="margin-top:12px">
        <input type="checkbox" id="vesc_poll">
        <span id="lbl-vesc-poll">Read VESC data (voltage, temp, fault)</span>
      </label>
      <label class="checkbox-row" style="margin-top:8px">
        <input type="checkbox" id="autoreboot" onchange="document.getElementById('autoreboot_opts').style.display=this.checked?'':'none'">
        <span id="lbl-autoreboot">Auto reboot if no client connected</span>
      </label>
      <div id="autoreboot_opts" style="display:none;margin-top:8px">
        <label id="lbl-autoreboot-time">Reboot after (seconds, min 60)</label>
        <input type="text" id="autoreboot_time" maxlength="6" placeholder="300">
        <label class="checkbox-row" style="margin-top:8px">
          <input type="checkbox" id="autoreboot_no_wifi">
          <span id="lbl-autoreboot-nowifi">Reboot even when connected to WiFi (no active VESC client needed)</span>
        </label>
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-roam-title">WiFi Roaming</h3>
      <label class="checkbox-row" style="margin-top:0">
        <input type="checkbox" id="roam_enabled" onchange="document.getElementById('roam_opts').style.display=this.checked?'':'none'">
        <span id="lbl-roam-enabled">Switch to stronger AP with same SSID</span>
      </label>
      <div id="roam_opts" style="display:none;margin-top:8px">
        <label id="lbl-roam-thr">Search threshold (dBm, e.g. -75 — weaker = roam search starts)</label>
        <input type="text" id="roam_threshold" maxlength="4" placeholder="-75">
        <label id="lbl-roam-hyst">Min. improvement (dB) to actually switch</label>
        <input type="text" id="roam_hysteresis" maxlength="3" placeholder="12">
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-autopoll-title">VESC Auto-Poll</h3>
      <label class="checkbox-row" style="margin-top:0">
        <input type="checkbox" id="autopoll_enabled" onchange="document.getElementById('autopoll_opts').style.display=this.checked?'':'none'">
        <span id="lbl-autopoll-enabled">Poll VESC even when Web-UI is closed</span>
      </label>
      <div id="autopoll_forced_hint" style="display:none;font-size:11px;color:var(--accent);margin-top:6px">
        <span id="lbl-autopoll-forced">Required by BLE Auto mode or AP Auto mode — these need ERPM data.</span>
      </div>
      <div id="autopoll_opts" style="display:none;margin-top:8px">
        <label id="lbl-autopoll-int">Poll interval (seconds, 1-60)</label>
        <input type="text" id="autopoll_interval" maxlength="3" placeholder="5">
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-update-title">Update Server</h3>
      <label id="lbl-version-url">Version URL (version.txt)</label>
      <input type="text" id="version_url" placeholder="https://...">
      <label id="lbl-firmware-url">Firmware URL (firmware.bin)</label>
      <input type="text" id="update_url" placeholder="https://...">
    </div>
    <div class="section">
      <h3 id="lbl-wifi-title">WiFi Networks</h3>
      <div style="display:flex;gap:8px;margin-bottom:10px">
        <button class="btn sm" onclick="scanWifi()" id="scanBtn">Scan</button>
        <button class="btn green sm" onclick="addWifi()" id="addBtn">+ Manual</button>
      </div>
      <div id="scanResults" style="display:none;margin-bottom:10px"></div>
      <div id="wifiList"></div>
    </div>
    <div class="section">
      <h3 id="lbl-leds-title">LEDs</h3>
      <label class="checkbox-row" style="margin-top:0">
        <input type="checkbox" id="leds_enabled">
        <span id="lbl-leds-enabled">Enable WS28XX control</span>
      </label>

    </div>
    <button class="btn" onclick="saveConfig()" id="saveBtn">Save</button>
    <button class="btn" style="margin-top:8px;background:#e0a030" onclick="restartDevice()" id="restartBtn">Restart</button>
    <button class="btn red" style="margin-top:8px" onclick="factoryReset()" id="factoryBtn">Factory Reset</button>
  </div>

  <!-- OTA -->
  <div class="panel" id="tab-ota">
    <div class="section">
      <h3 id="lbl-server-update">Server Update</h3>
      <div id="updateInfo" style="font-size:12px;color:#666;margin-bottom:12px"></div>
      <button class="btn" id="checkBtn" onclick="checkUpdate()" style="margin-bottom:8px">Check for Updates</button>
      <button class="btn green" id="installBtn" style="display:none" onclick="installUpdate()">Install Update</button>
      <div class="msg" id="updateMsg"></div>
    </div>
    <div class="section">
      <h3 id="lbl-manual-flash">Manual Flash</h3>
      <div class="drop-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
        <div class="icon">&#128190;</div>
        <div class="label" id="lbl-drop">Drop firmware.bin here<br>or click to select</div>
        <div class="filename" id="fileName"></div>
      </div>
      <input type="file" id="fileInput" accept=".bin">
      <button class="btn" id="uploadBtn" disabled onclick="startUpload()">Flash</button>
      <div class="progress-wrap" id="progressWrap">
        <div class="progress-bar-bg"><div class="progress-bar" id="progressBar"></div></div>
        <div class="status" id="otaStatus">Uploading...</div>
      </div>
    </div>
  </div>

  <!-- API -->
  <div class="panel" id="tab-api">
    <div class="section">
      <h3>API Reference</h3>
      <div class="api-h2">GET</div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/info" target="_blank">/api/info</a><div class="desc">Device status, VESC data, WiFi/BLE state</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/config" target="_blank">/api/config</a><div class="desc">All configuration values</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/update/status" target="_blank">/api/update/status</a><div class="desc">Firmware version info</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/update/check" target="_blank">/api/update/check</a><div class="desc">Fetch version.txt and compare</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/wifi/scan" target="_blank">/api/wifi/scan</a><div class="desc">Scan WiFi networks</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/wifi/disconnect-reasons" target="_blank">/api/wifi/disconnect-reasons</a><div class="desc">All ESP-IDF WiFi disconnect reason numbers and names</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/uart/log" target="_blank">/api/uart/log</a><div class="desc">UART debug log (requires debug mode)</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/boot/status" target="_blank">/api/boot/status</a><div class="desc">Last boot/reset reason, watchdog, panic and brownout status</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/time" target="_blank">/api/time</a><div class="desc">Current clock, sync source and NTP status</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/ping" target="_blank">/api/ping</a><div class="desc">Keepalive — activates VESC polling</div></div>
      <div class="api-h2">POST</div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/config</span><div class="desc">Save config and restart (JSON body)</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/uart/clear</span><div class="desc">Clear UART debug log</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/update/install</span><div class="desc">Download and flash from update_url</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/ap/start</span><div class="desc">Manually start or repair the access point (debug test)</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/time?epoch=...&amp;source=app</span><div class="desc">Set Unix time; source may be supplied as query parameter or JSON field</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/restart</span><div class="desc">Restart the ESP</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/factory-reset</span><div class="desc">Clear NVS and restart</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/update</span><div class="desc">Manual OTA (multipart/form-data, field: firmware)</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path" style="color:#ffb74d">:8080/update</span><div class="desc">Emergency OTA — always available<br><span style="color:var(--text3)">curl -X POST http://IP:8080/update -F "firmware=@firmware.bin"</span></div></div>
      <div class="api-h2" style="margin-top:16px">Debug</div>
      <div class="section" style="padding:12px">
        <label class="checkbox-row" style="margin-top:0">
          <input type="checkbox" id="debug_toggle" onchange="setDebug(this.checked)">
          <span id="lbl-debug-mode">Debug Mode (UART log)</span>
        </label>
        <div id="debugLogWrap" style="display:none;margin-top:10px">
          <div id="lbl-debug-boot-note" style="margin-bottom:8px;font-size:11px;color:var(--text3)">Boot/reset diagnostics are always logged: power-on, software restart, panic/exception, watchdog and brownout.</div>
          <div style="margin-bottom:8px;font-size:12px;color:var(--text2)">
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex;margin-right:12px">
              <input type="checkbox" id="dbg_ble" onchange="updateFilter()"> BLE
            </label>
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex;margin-right:12px">
              <input type="checkbox" id="dbg_wifi" onchange="updateFilter()"> WiFi
            </label>
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex;margin-right:12px">
              <input type="checkbox" id="dbg_poll" onchange="updateFilter()"> Poll
            </label>
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex">
              <input type="checkbox" id="dbg_status" onchange="updateFilter()"> Status
            </label>
          </div>
          <div style="display:flex;gap:8px;margin-bottom:8px">
            <button class="btn sm" onclick="loadUartLog()">&#x21BB; Refresh</button>
            <button class="btn red sm" onclick="clearUartLog()">Clear</button>
          </div>
          <div id="uartLog" style="background:var(--bg3);border:1px solid var(--border2);border-radius:4px;padding:8px;font-size:11px;max-height:300px;overflow-y:auto;color:var(--text2)">-</div>
        </div>
      </div>
    </div>
  </div>
</div>

<script>
var lang = (document.cookie.match(/lang=([a-z]+)/)||[])[1] || (navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}

function applyTranslations(){
  var s=function(id,en,d){var el=document.getElementById(id);if(el)el.textContent=de()?d:en;};
  var sh=function(id,en,d){var el=document.getElementById(id);if(el)el.innerHTML=de()?d:en;};
  s('lbl-status',           'Status',                                     'Status');
  s('lbl-loading',          'Loading...',                                  'Laden...');
  s('lbl-ble-name',         'BLE Name (visible in VESC Tool)',             'BLE Name (sichtbar in VESC Tool)');
  s('lbl-ap-title',         'Access Point (Fallback)',                     'Access Point (Fallback)');
  s('lbl-ap-name',          'AP Name (SSID)',                              'AP Name (SSID)');
  s('lbl-ap-pass',          'AP Password (leave empty for open network)',   'AP Passwort (leer = offenes Netz)');
  s('lbl-ap-timeout',       'Idle timeout (seconds, AP off after no movement and no AP client)', 'Inaktivit\u00e4ts-Timeout (Sek., AP aus nach Stillstand ohne AP-Client)');
  s('lbl-ap-mode-sel',      'AP Mode',                                      'AP Modus');
  s('opt-apmode-on',        'On (always on)',                               'An (immer an)');
  s('opt-apmode-auto',      'Auto (on when riding, off when idle)',         'Auto (an beim Fahren, aus bei Stillstand)');
  s('lbl-apmode-hint',      'Like BLE Auto: riding above the ERPM threshold keeps the AP awake and brings it back after timeout. A connected AP client pauses the timer.', 'Wie BLE-Auto: Fahren \u00fcber der ERPM-Schwelle h\u00e4lt den AP wach und holt ihn nach dem Timeout zur\u00fcck. Ein verbundener AP-Client pausiert den Timer.');
  s('lbl-erpm-title',       'Movement Detection',                           'Bewegungserkennung');
  s('lbl-erpm-power',       'ERPM threshold to wake BLE/WiFi when riding', 'ERPM-Schwelle zum Aufwecken von BLE/WLAN beim Fahren');
  s('lbl-erpm-hint',        'When BLE Auto mode or AP Auto mode is active and has switched off after the idle timeout, riding above this ERPM value switches BLE/AP back on. Higher value = needs faster riding to wake.', 'Wenn BLE-Auto-Modus oder AP-Auto-Modus aktiv ist und sich nach dem Timeout abgeschaltet hat, schaltet das \u00dcberschreiten dieses ERPM-Werts beim Fahren BLE/WLAN wieder ein. H\u00f6herer Wert = schnelleres Fahren n\u00f6tig zum Aufwecken.');
  s('lbl-conn-title',       'Connection',                                   'Verbindung');
  s('lbl-port',             'TCP Port (default: 65101)',                    'TCP Port (Standard: 65101)');
  s('lbl-vesc-poll',        'Read VESC data (voltage, temp, fault)',        'VESC Daten auslesen (Spannung, Temp, Fault)');
  s('lbl-autoreboot',       'Auto reboot if no client connected',           'Auto-Neustart wenn kein Client verbunden');
  s('lbl-autoreboot-time',  'Reboot after (seconds, min 60)',               'Neustart nach (Sekunden, min 60)');
  s('lbl-autoreboot-nowifi','Reboot even when connected to WiFi (no active VESC client needed)', 'Neustart auch wenn im WLAN (ohne aktiven VESC Client)');
  s('lbl-roam-title',       'WiFi Roaming',                                 'WiFi Roaming');
  s('lbl-roam-enabled',     'Switch to stronger AP with same SSID',         'Zu st\u00e4rkerem AP mit gleicher SSID wechseln');
  s('lbl-roam-thr',         'Search threshold (dBm, e.g. -75 \u2014 weaker = roam search starts)', 'Suchschwelle (dBm, z.B. -75 \u2014 schw\u00e4cher = Roam-Suche startet)');
  s('lbl-roam-hyst',        'Min. improvement (dB) to actually switch',     'Min. Verbesserung (dB) f\u00fcr Wechsel');
  s('lbl-autopoll-title',   'VESC Auto-Poll',                                'VESC Auto-Polling');
  s('lbl-autopoll-enabled', 'Poll VESC even when Web-UI is closed',          'VESC pollen auch wenn Web-UI geschlossen');
  s('lbl-autopoll-forced',  'Required by BLE Auto mode or AP Auto mode — these need ERPM data.', 'Erforderlich f\u00fcr BLE-Auto-Modus oder AP-Auto-Modus \u2014 diese brauchen ERPM-Daten.');
  s('lbl-autopoll-int',     'Poll interval (seconds, 1-60)',                 'Poll-Intervall (Sekunden, 1-60)');
  s('lbl-blemode-title',    'BLE Mode',                                      'BLE Modus');
  s('lbl-ble-pin-en',       'Require PIN for BLE pairing',                   'PIN beim BLE-Koppeln verlangen');
  s('lbl-ble-pin',          'BLE pairing PIN (6 digits)',                    'BLE-Kopplungs-PIN (6 Ziffern)');
  s('lbl-ble-pin-hint',     'Exactly 6 digits required (leading zeros allowed, e.g. 001234). With PIN enabled, unpaired devices cannot communicate — pairing with PIN is enforced (takes effect after restart). Already paired devices stay paired. Without the checkbox, pairing is accepted automatically (Just Works).', 'Genau 6 Ziffern erforderlich (fuehrende Nullen erlaubt, z.B. 001234). Mit PIN koennen ungekoppelte Geraete nicht kommunizieren — Kopplung mit PIN wird erzwungen (greift nach Neustart). Bereits gekoppelte Geraete bleiben gekoppelt. Ohne Haken wird die Kopplung automatisch angenommen (Just Works).');
  s('lbl-ble-fullpwr',      'Disable BLE power saving (full performance)',   'BLE-Energiesparmodus deaktivieren (volle Leistung)');
  s('lbl-ble-fullpwr-hint', 'Without the checkbox, advertising slows down after 15s idle to give WiFi more airtime (BLE and WiFi share one radio). With the checkbox, advertising stays permanently at the fast interval (20-40ms) — the device is always instantly discoverable and connects fastest, but WiFi throughput may suffer. Takes effect immediately, no restart needed.', 'Ohne Haken wird das Advertising nach 15s Leerlauf verlangsamt, damit das WLAN mehr Airtime bekommt (BLE und WLAN teilen sich ein Funkmodul). Mit Haken bleibt das Advertising dauerhaft im schnellen Intervall (20-40ms) — der ESP ist jederzeit sofort auffindbar und verbindet am schnellsten, das WLAN kann dafuer langsamer werden. Greift sofort, kein Neustart noetig.');
  s('lbl-leds-title',       'LEDs',                                         'LEDs');
  s('lbl-leds-enabled',     'Enable WS28XX control',                        'WS28XX Steuerung aktivieren');
  s('lbl-blemode-sel',      'Mode',                                          'Modus');
  s('opt-mode-on',          'On (always advertise)',                         'An (immer advertisen)');
  s('opt-mode-off',         'Off (no BLE)',                                  'Aus (kein BLE)');
  s('opt-mode-auto',        'Auto (on when moving)',                         'Auto (an bei Bewegung)');
  s('lbl-blemode-off',      'Idle timeout (seconds, BLE off after no movement and no client)', 'Inaktivit\u00e4ts-Timeout (Sek., BLE aus nach Stillstand ohne Client)');
  s('lbl-blemode-hint',     'Boot default: BLE on. Movement above threshold resets the idle timer. Active connection (BLE/TCP/Web-UI) pauses the timer.', 'Boot-Default: BLE an. Bewegung \u00fcber der Schwelle setzt den Timer zur\u00fcck. Aktive Verbindung (BLE/TCP/Web-UI) pausiert den Timer.');
  s('lbl-update-title',     'Update Server',                                'Update Server');
  s('lbl-version-url',      'Version URL (version.txt)',                    'Version URL (version.txt)');
  s('lbl-firmware-url',     'Firmware URL (firmware.bin)',                  'Firmware URL (firmware.bin)');
  s('lbl-wifi-title',       'WiFi Networks',                                'WiFi Netzwerke');
  s('scanBtn',              'Scan',                                         'Scan');
  s('addBtn',               '+ Manual',                                     '+ Manuell');
  s('saveBtn',              'Save',                                         'Speichern');
  s('restartBtn',           'Restart',                                      'Neustart');
  s('factoryBtn',           'Factory Reset',                                'Werkseinstellungen');
  s('lbl-server-update',    'Server Update',                                'Server Update');
  s('checkBtn',             'Check for Updates',                            'Auf Updates prüfen');
  s('installBtn',           'Install Update',                               'Update installieren');
  s('lbl-manual-flash',     'Manual Flash',                                 'Manueller Flash');
  sh('lbl-drop',            'Drop firmware.bin here<br>or click to select', 'firmware.bin ablegen<br>oder klicken');
  s('uploadBtn',            'Flash',                                        'Flashen');
  s('lbl-debug-mode',       'Debug Mode (UART log)',                        'Debug Modus (UART Log)');
  s('lbl-debug-boot-note',  'Boot/reset diagnostics are always logged: power-on, software restart, panic/exception, watchdog and brownout.', 'Boot-/Resetdiagnose wird immer protokolliert: Power-On, Software-Neustart, Panic/Exception, Watchdog und Brownout.');
}

function showTab(name){
  document.querySelectorAll('.tab').forEach(function(t,i){t.classList.toggle('active',['info','config','ota','api'][i]===name);});
  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active');});
  document.getElementById('tab-'+name).classList.add('active');
  applyTranslations();
  if(name==='info') loadInfo();
  if(name==='config') loadConfig();
  if(name==='ota') loadUpdateStatus();
  if(name==='api') initDebugTab();
}

var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=document.getElementById('themeBtn');if(b)b.textContent=theme==='dark'?'☀️':'🌙';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();

function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
document.getElementById('langBtn').textContent=de()?'EN':'DE';
applyTranslations();

// Wenn ueber ?tab=xyz aufgerufen (z.B. von der LED-Seite), den Tab oeffnen.
(function(){
  var m=location.search.match(/[?&]tab=([a-z]+)/);
  if(m && ['info','config','ota','api'].indexOf(m[1])>=0) showTab(m[1]);
})();

// Im reinen AP-Betrieb gibt es eventuell weder Heimnetz/NTP noch eine App,
// die die Uhr setzt. Dann uebernimmt die WebUI EINMALIG die Systemzeit des
// Browsers. Eine bereits gueltige ESP-Zeit wird niemals ueberschrieben.
async function syncBrowserTimeIfInvalid(){
  try {
    var response=await fetch('/api/time',{cache:'no-store'});
    if(!response.ok)return;

    var status=await response.json();
    if(status.valid)return;

    var epochMs=Date.now();
    // Dieselbe Plausibilitaetsgrenze wie im ESP-Zeitdienst: ab 2020.
    if(!Number.isFinite(epochMs)||epochMs<1577836800000)return;

    // only_if_invalid=1 wird serverseitig nochmals geprueft. Falls zwischen
    // GET und POST bereits NTP synchronisiert hat, bleibt diese Zeit erhalten.
    await fetch('/api/time?epoch='+encodeURIComponent(String(epochMs))+'&only_if_invalid=1&source=browser',{
      method:'POST',
      cache:'no-store'
    });
  }catch(e){
    // Keine Fehlermeldung in der UI: Uptime-Logging funktioniert weiterhin.
  }
}
syncBrowserTimeIfInvalid();

function timeSourceLabel(source){
  source=String(source||'').toLowerCase();
  if(source==='ntp')return 'NTP';
  if(source==='browser')return de()?'Browser':'Browser';
  if(source==='app')return de()?'App':'App';
  if(source==='home_assistant')return 'Home Assistant';
  if(source==='api')return 'API';
  return source?source.toUpperCase():'';
}

function loadInfo(){
  fetch('/api/info').then(function(r){return r.json();}).then(function(d){
    document.getElementById('statusBar').textContent=d.mode==='ap'&&!d.ssid?'AP: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
    document.getElementById('infoContent').innerHTML=
      '<div class="info-row"><span>BLE Name</span><span class="info-val">'+d.ble_name+'</span></div>'+
      '<div class="info-row"><span>BLE MAC</span><span class="info-val">'+d.ble_mac+'</span></div>'+
      '<div class="info-row"><span>BLE Client</span><span class="info-val">'+( d.ble_connected?(de()?'Verbunden':'Connected'):(de()?'Getrennt':'Disconnected'))+'</span></div>'+
      '<div class="info-row"><span>WiFi Client</span><span class="info-val">'+(d.wifi_client_connected?(de()?'Verbunden':'Connected'):(de()?'Getrennt':'Disconnected'))+'</span></div>'+
      '<div class="info-row"><span>IP</span><span class="info-val">'+d.ip+'</span></div>'+
      (d.mode!=='ap'?'<div class="info-row"><span>SSID</span><span class="info-val">'+d.ssid+'</span></div>':'')+
      (d.mode!=='ap'?'<div class="info-row"><span>RSSI</span><span class="info-val">'+d.rssi+' dBm</span></div>':'')+
      '<div class="info-row"><span>Free RAM</span><span class="info-val">'+(d.heap>=1024?(d.heap/1024).toFixed(1)+' KB':d.heap+' B')+'</span></div>'+
      '<div class="info-row"><span>AP</span><span class="info-val" style="color:'+(d.ap_active?'var(--ok)':'var(--text3)')+'">'+( d.ap_active?(de()?'Aktiv':'Active'):(de()?'Aus':'Off'))+(d.ap_active?' ('+d.ap_ip+')':'')+'</span></div>'+
      (d.ap_client_ip?'<div class="info-row"><span>'+(de()?'AP-Client IP':'AP client IP')+'</span><span class="info-val">'+d.ap_client_ip+'</span></div>':'')+
      (d.ap_active&&d.ap_timeout_remaining>=0?'<div class="info-row"><span>'+(de()?'AP aus in':'AP off in')+'</span><span class="info-val">'+d.ap_timeout_remaining+'s</span></div>':'')+
      (d.ap_active&&d.ap_timeout_remaining===-2?'<div class="info-row"><span>'+(de()?'AP aus in':'AP off in')+'</span><span class="info-val">'+(de()?'pausiert (Client verbunden)':'paused (client connected)')+'</span></div>':'')+
      '<div class="info-row"><span>TCP Port</span><span class="info-val">'+d.port+'</span></div>'+
      '<div class="info-row"><span>UART</span><span class="info-val">RX=GPIO'+d.rx_pin+' TX=GPIO'+d.tx_pin+'</span></div>'+
      '<div style="margin:10px 0 6px;font-size:11px;color:#666;text-transform:uppercase;letter-spacing:1px">VESC</div>'+
      '<div class="info-row"><span>VESC</span><span class="info-val" style="color:'+(d.vesc_connected?'#81c784':'#e57373')+'">'+(d.vesc_connected?(de()?'Verbunden':'Connected'):(de()?'Nicht verbunden':'Disconnected'))+'</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>'+(de()?'Spannung':'Voltage')+'</span><span class="info-val">'+d.vesc_voltage+' V</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>Temp FET</span><span class="info-val">'+d.vesc_temp_fet+' °C</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>Temp Motor</span><span class="info-val">'+d.vesc_temp_motor+' °C</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>'+(de()?'Fehlercode':'Fault')+'</span><span class="info-val" style="color:'+(d.vesc_fault===0?'#81c784':'#e57373')+'">'+(d.vesc_fault_str||'OK')+'</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>ERPM</span><span class="info-val">'+d.vesc_erpm+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'Systemzeit':'System time')+'</span><span class="info-val" style="color:'+(d.time_valid?'var(--text2)':'#e0a030')+'">'+(d.time_valid?esc(d.time_local)+(d.time_source?' ('+esc(timeSourceLabel(d.time_source))+')':''):(de()?'Nicht synchronisiert':'Not synchronized'))+'</span></div>'+
      '<div class="info-row"><span>Uptime</span><span class="info-val">'+d.uptime+'</span></div>'+
      '<div class="info-row"><span>Build</span><span class="info-val">'+d.build+'</span></div>'+
      '<div style="margin:10px 0 6px;font-size:11px;color:#666;text-transform:uppercase;letter-spacing:1px">'+(de()?'Letzter Start':'Last boot')+'</div>'+
      '<div class="info-row"><span>'+(de()?'Resetgrund':'Reset reason')+'</span><span class="info-val" style="color:'+(d.reset_brownout||d.reset_panic||d.reset_watchdog?'#e57373':(d.reset_reason==='SOFTWARE_RESTART'?'#e0a030':'var(--ok)'))+'">'+d.reset_reason+' ('+d.reset_reason_code+')</span></div>'+
      (d.planned_restart?'<div class="info-row"><span>'+(de()?'Neustart-Ausloeser':'Restart source')+'</span><span class="info-val">'+esc(d.planned_restart)+'</span></div>':'')+
      '<div style="margin:10px 0 6px;font-size:11px;color:#666;text-transform:uppercase;letter-spacing:1px">'+(de()?'Diagnose (WLAN/Loop)':'Diagnostics (WiFi/Loop)')+'</div>'+
      '<div class="info-row"><span>'+(de()?'STA-Scans':'STA scans')+'</span><span class="info-val">'+d.diag_scans+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'STA verbunden':'STA connects')+'</span><span class="info-val">'+d.diag_sta_conn+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'STA Abbrueche':'STA disconnects')+'</span><span class="info-val" style="color:'+(d.diag_sta_disc>0?'#e0a030':'var(--text2)')+'">'+d.diag_sta_disc+(d.diag_sta_disc>0?' ('+(de()?'Grund':'reason')+' '+d.diag_disc_reason+' '+esc(d.diag_disc_reason_name||'UNKNOWN_OR_RESERVED')+')':'')+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'AP-Client verb./getr.':'AP client conn/disc')+'</span><span class="info-val">'+d.diag_ap_conn+' / '+d.diag_ap_disc+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'AP-Watchdog Eingriffe':'AP watchdog fires')+'</span><span class="info-val" style="color:'+(d.diag_wd_fires>0?'#e0a030':'var(--text2)')+'">'+d.diag_wd_fires+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'Loop max (ms)':'Loop max (ms)')+'</span><span class="info-val" style="color:'+(d.diag_loop_max_us>50000?'#e57373':(d.diag_loop_max_us>10000?'#e0a030':'var(--ok)'))+'">'+(d.diag_loop_max_us/1000).toFixed(1)+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'Loops/Sek':'Loops/sec')+'</span><span class="info-val">'+d.diag_loops_per_sec+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'Heap-Tiefstand':'Min free heap')+'</span><span class="info-val">'+(d.diag_min_heap>=1024?(d.diag_min_heap/1024).toFixed(1)+' KB':d.diag_min_heap+' B')+'</span></div>'+
      '<div class="info-row"><span>'+(de()?'Probe-Requests (RSSI)':'Probe requests (RSSI)')+'</span><span class="info-val">'+d.diag_probe_reqs+(d.diag_probe_reqs>0?' ('+d.diag_probe_rssi+' dBm)':'')+'</span></div>';
  }).catch(function(){document.getElementById('infoContent').innerHTML='<div style="color:#e57373;font-size:13px">'+(de()?'Fehler':'Error')+'</div>';});
}
loadInfo();
// LED-Reiter-Sichtbarkeit direkt beim Laden setzen (nicht erst bei Config-Besuch)
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  document.getElementById('tab-leds-link').style.display = d.leds_enabled?'':'none';
}).catch(function(){});
setInterval(function(){if(document.getElementById('tab-info').classList.contains('active'))loadInfo();},1000);
setInterval(function(){fetch('/api/ping');},2000);

function loadUpdateStatus(){
  fetch('/api/update/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('updateInfo').innerHTML=
      '<div class="info-row"><span>'+(de()?'Version':'Version')+'</span><span class="info-val">'+d.current+'</span></div>'+
      (d.available?'<div class="info-row"><span>Server</span><span class="info-val">'+d.available+'</span></div>':'')+
      (d.error?'<div style="color:var(--err);margin-top:6px">'+d.error+'</div>':'')+
      (!d.update_url?'<div style="color:var(--text3);margin-top:6px">'+(de()?'Kein Update-Server':'No update server configured')+'</div>':'');
  }).catch(function(){});
}

function checkUpdate(){
  var msg=document.getElementById('updateMsg'),btn=document.getElementById('checkBtn');
  msg.className='msg';msg.style.display='none';btn.disabled=true;btn.textContent=de()?'Prüfe...':'Checking...';
  fetch('/api/update/check').then(function(r){return r.json();}).then(function(d){
    btn.disabled=false;btn.textContent=de()?'Auf Updates prüfen':'Check for Updates';
    document.getElementById('updateInfo').innerHTML='<div class="info-row"><span>'+(de()?'Aktuell':'Current')+'</span><span class="info-val">'+d.current+'</span></div><div class="info-row"><span>Server</span><span class="info-val">'+d.available+'</span></div>';
    if(d.update_available){msg.textContent=de()?'Neue Version!':'New version!';msg.className='msg ok';document.getElementById('installBtn').style.display='block';}
    else if(d.error){msg.textContent='Error: '+d.error;msg.className='msg err';}
    else{msg.textContent=de()?'Aktuell.':'Up to date.';msg.className='msg ok';document.getElementById('installBtn').style.display='none';}
  }).catch(function(){btn.disabled=false;msg.textContent='Error';msg.className='msg err';});
}

function installUpdate(){
  var msg=document.getElementById('updateMsg'),btn=document.getElementById('installBtn');
  btn.disabled=true;msg.textContent=de()?'Installiere...':'Installing...';msg.className='msg ok';
  fetch('/api/update/install',{method:'POST'}).then(function(){msg.textContent=de()?'Neustart...':'Restarting...';setTimeout(function(){location.reload();},15000);}).catch(function(){msg.textContent='Error';msg.className='msg err';btn.disabled=false;});
}

function scanWifi(){
  var btn=document.getElementById('scanBtn'),res=document.getElementById('scanResults');
  btn.disabled=true;btn.textContent=de()?'Scanne...':'Scanning...';res.style.display='none';
  fetch('/api/wifi/scan').then(function(r){return r.json();}).then(function(nets){
    btn.disabled=false;btn.textContent='Scan';
    if(!nets.length){res.innerHTML='<div style="color:#666;font-size:12px">'+(de()?'Keine':'None found')+'</div>';}
    else{res.innerHTML=nets.sort(function(a,b){return b.rssi-a.rssi;}).map(function(n){return'<div style="display:flex;justify-content:space-between;padding:6px 8px;background:#2a2a2a;border-radius:4px;margin-bottom:4px;font-size:12px;cursor:pointer" onclick="addWifiFromScan(\''+esc(n.ssid)+'\')"><span>'+esc(n.ssid)+' '+(n.secure?'🔒':'')+'</span><span style="color:#666">'+n.rssi+' dBm</span></div>';}).join('');}
    res.style.display='block';
  }).catch(function(){btn.disabled=false;btn.textContent='Scan';});
}

function addWifiFromScan(ssid){
  if(wifiNetworks.length>=10){alert('Max 10');return;}
  wifiNetworks.push({ssid:ssid,pass:'',static:false,ip:'',gateway:'',subnet:'255.255.255.0',dns:''});
  renderWifiList();document.getElementById('scanResults').style.display='none';
  var pw=document.querySelectorAll('#wifiList input[type=password]');if(pw.length)pw[pw.length-1].focus();
}

function factoryReset(){
  if(!confirm(de()?'Zurücksetzen?':'Reset all settings?'))return;
  fetch('/api/factory-reset',{method:'POST'}).catch(function(){});
}

function restartDevice(){
  if(!confirm(de()?'Gerät neu starten?':'Restart device?'))return;
  fetch('/api/restart',{method:'POST'}).catch(function(){});
  alert(de()?'Neustart läuft… Seite in ein paar Sekunden neu laden.':'Restarting… reload the page in a few seconds.');
}

var wifiNetworks=[];
function renderWifiList(){
  var list=document.getElementById('wifiList');
  if(!wifiNetworks.length){list.innerHTML='<div style="color:#666;font-size:12px;padding:8px 0">'+(de()?'Keine Netzwerke':'No networks configured')+'</div>';return;}
  list.innerHTML=wifiNetworks.map(function(n,i){return(
    '<div class="wifi-entry">'+
    '<div class="wifi-entry-header"><span class="wifi-entry-num">'+(de()?'Netzwerk':'Network')+' '+(i+1)+'</span>'+
    '<button class="btn red sm" onclick="removeWifi('+i+')">&#x2715;</button></div>'+
    '<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px">'+
    '<div><label>SSID</label><input type="text" maxlength="32" placeholder="SSID" value="'+esc(n.ssid)+'" onchange="wifiNetworks['+i+'].ssid=this.value"></div>'+
    '<div><label>'+(de()?'Passwort':'Password')+'</label>'+
    '<div style="display:flex;gap:4px">'+
    '<input type="password" id="pw_'+i+'" maxlength="64" value="'+esc(n.pass)+'" onchange="wifiNetworks['+i+'].pass=this.value" style="flex:1;min-width:0">'+
    '<button type="button" onclick="var f=document.getElementById(\'pw_'+i+'\');var show=f.type===\'password\';f.type=show?\'text\':\'password\';this.style.background=show?\'var(--err)\':\' var(--accent)\';this.style.color=\'#111\'" style="padding:0 10px;background:var(--accent);border:1px solid var(--border);border-radius:4px;color:#111;cursor:pointer;font-size:14px;flex-shrink:0">👁</button>'+
    '</div></div></div>'+
    '<label class="checkbox-row" style="margin-top:10px"><input type="checkbox" '+(n.static?'checked':'')+' onchange="wifiNetworks['+i+'].static=this.checked;renderWifiList()">'+(de()?'Statische IP':'Static IP')+'</label>'+
    (n.static?'<div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-top:8px">'+
    '<div><label>IP</label><input type="text" maxlength="15" placeholder="192.168.1.100" value="'+esc(n.ip||'')+'" onchange="wifiNetworks['+i+'].ip=this.value"></div>'+
    '<div><label>GW</label><input type="text" maxlength="15" placeholder="192.168.1.1" value="'+esc(n.gateway||'')+'" onchange="wifiNetworks['+i+'].gateway=this.value"></div>'+
    '<div><label>Subnet</label><input type="text" maxlength="15" placeholder="255.255.255.0" value="'+esc(n.subnet||'255.255.255.0')+'" onchange="wifiNetworks['+i+'].subnet=this.value"></div>'+
    '<div><label>DNS</label><input type="text" maxlength="15" placeholder="8.8.8.8" value="'+esc(n.dns||'')+'" onchange="wifiNetworks['+i+'].dns=this.value"></div></div>':'')+
    '</div>');}).join('');
}

function esc(s){return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;');}
function addWifi(){if(wifiNetworks.length>=10){alert('Max 10');return;}wifiNetworks.push({ssid:'',pass:'',static:false,ip:'',gateway:'',subnet:'255.255.255.0',dns:''});renderWifiList();}
function removeWifi(i){wifiNetworks.splice(i,1);renderWifiList();}

function updateErpmVisibility(){
  var bleAuto = document.getElementById('ble_mode').value==2;
  var apAuto  = document.getElementById('ap_mode').value==2;
  document.getElementById('erpm_section').style.display=(bleAuto||apAuto)?'':'none';
  // Auto-Poll wird von BLE-Auto und AP-Auto zwingend benoetigt (brauchen ERPM).
  // In dem Fall den Haken setzen, sperren und die Optionen einblenden.
  var needPoll = bleAuto || apAuto;
  var ap = document.getElementById('autopoll_enabled');
  if(needPoll){
    ap.checked = true;
    ap.disabled = true;
    document.getElementById('autopoll_opts').style.display='';
    document.getElementById('autopoll_forced_hint').style.display='';
  } else {
    ap.disabled = false;
    document.getElementById('autopoll_forced_hint').style.display='none';
    // Sichtbarkeit der Optionen wieder an den tatsaechlichen Haken-Zustand koppeln
    document.getElementById('autopoll_opts').style.display=ap.checked?'':'none';
  }
}

function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.getElementById('ble_name').value    = d.ble_name||'';
    document.getElementById('ble_pin_enabled').checked = d.ble_pin_enabled===true;
    window.origBlePinEn = d.ble_pin_enabled===true;
    document.getElementById('ble_pin').value     = (d.ble_pin!==undefined)?String(d.ble_pin).padStart(6,'0'):'123456';
    document.getElementById('blepin_wrap').style.display = d.ble_pin_enabled===true?'':'none';
    document.getElementById('ble_full_power').checked = d.ble_full_power===true;
    document.getElementById('ap_ssid').value     = d.ap_ssid||'';
    document.getElementById('ap_pass').value     = d.ap_pass||'';
    document.getElementById('ap_timeout').value  = d.ap_timeout||120;
    document.getElementById('ap_mode').value     = d.ap_mode||1;
    document.getElementById('apmode_auto').style.display = (d.ap_mode==2)?'':'none';
    document.getElementById('vesc_port').value   = d.port||65101;
    document.getElementById('rx_pin').value      = d.rx_pin||6;
    document.getElementById('tx_pin').value      = d.tx_pin||5;
    document.getElementById('vesc_poll').checked = d.vesc_poll!==false;
    document.getElementById('autoreboot').checked = d.autoreboot===true;
    document.getElementById('autoreboot_time').value = d.autoreboot_time||300;
    document.getElementById('autoreboot_no_wifi').checked = d.autoreboot_no_wifi===true;
    document.getElementById('autoreboot_opts').style.display = d.autoreboot?'':'none';
    document.getElementById('roam_enabled').checked = d.roam_enabled===true;
    document.getElementById('roam_threshold').value = d.roam_threshold||-75;
    document.getElementById('roam_hysteresis').value = d.roam_hysteresis||12;
    document.getElementById('roam_opts').style.display = d.roam_enabled?'':'none';
    document.getElementById('autopoll_enabled').checked = d.autopoll_enabled===true;
    document.getElementById('autopoll_interval').value = d.autopoll_interval||5;
    document.getElementById('autopoll_opts').style.display = d.autopoll_enabled?'':'none';
    document.getElementById('ble_mode').value = (d.ble_mode!==undefined)?d.ble_mode:1;
    document.getElementById('ble_auto_erpm_on').value = d.ble_auto_erpm_on||200;
    document.getElementById('ble_auto_off_sec').value = d.ble_auto_off_sec||120;
    document.getElementById('leds_enabled').checked = d.leds_enabled===true;
    document.getElementById('tab-leds-link').style.display = d.leds_enabled?'':'none';
    document.getElementById('blemode_auto').style.display = d.ble_mode==2?'':'none';
    updateErpmVisibility();
    document.getElementById('version_url').value = d.version_url||'';
    document.getElementById('update_url').value  = d.update_url||'';
    wifiNetworks=(d.wifi||[]).map(function(n){return{ssid:n.ssid||'',pass:n.pass||'',static:n.static||false,ip:n.ip||'',gateway:n.gateway||'',subnet:n.subnet||'255.255.255.0',dns:n.dns||''};});
    renderWifiList();
    markOriginals();
  });
}

function showToast(msg, ok, duration){
  var t=document.getElementById('toast');
  if(!t){t=document.createElement('div');t.id='toast';t.style.cssText='position:fixed;top:12px;left:50%;transform:translateX(-50%);padding:10px 18px;border-radius:6px;font-family:Ndot47,system-ui,-apple-system,sans-serif;font-size:13px;z-index:9999;transition:opacity .3s;pointer-events:none';document.body.appendChild(t);}
  t.textContent=msg;
  t.style.background=ok?'var(--ok)':'var(--err)';
  t.style.color=ok?'#111':'#fff';
  t.style.opacity='1';
  clearTimeout(t._hide);
  t._hide=setTimeout(function(){t.style.opacity='0';},duration||3000);
}

// Fields that need reboot
var rebootFields=['ble_name','ap_ssid','ap_pass','vesc_port','rx_pin','tx_pin'];
function needsReboot(){
  return rebootFields.some(function(id){
    var el=document.getElementById(id);
    return el && el._orig!==undefined && String(el.value)!==String(el._orig);
  });
}

function markOriginals(){
  rebootFields.forEach(function(id){
    var el=document.getElementById(id);
    if(el) el._orig=el.value;
  });
}

function saveConfig(){
  if(document.getElementById('ble_pin_enabled').checked && !/^\d{6}$/.test(document.getElementById('ble_pin').value)){
    alert(de()?'BLE-PIN muss genau 6 Ziffern haben. F\u00fchrende Nullen sind erlaubt, z.B. 001234.':'BLE PIN must be exactly 6 digits. Leading zeros are allowed, e.g. 001234.');
    return;
  }
  var wifi=wifiNetworks.filter(function(n){return n.ssid.trim().length>0;});
  var reboot=needsReboot();
  var bodyObj={
    ble_name:    document.getElementById('ble_name').value,
    ble_pin_enabled: document.getElementById('ble_pin_enabled').checked,
    ble_pin:     parseInt(document.getElementById('ble_pin').value)||0,
    ble_full_power: document.getElementById('ble_full_power').checked,
    ap_ssid:     document.getElementById('ap_ssid').value,
    ap_pass:     document.getElementById('ap_pass').value,
    ap_timeout:  parseInt(document.getElementById('ap_timeout').value)||120,
    ap_mode: parseInt(document.getElementById('ap_mode').value)||1,
    port:        parseInt(document.getElementById('vesc_port').value)||65101,
    rx_pin:      parseInt(document.getElementById('rx_pin').value)||6,
    tx_pin:      parseInt(document.getElementById('tx_pin').value)||5,
    vesc_poll:   document.getElementById('vesc_poll').checked,
    autoreboot:      document.getElementById('autoreboot').checked,
    autoreboot_time: parseInt(document.getElementById('autoreboot_time').value)||300,
    autoreboot_no_wifi: document.getElementById('autoreboot_no_wifi').checked,
    roam_enabled:    document.getElementById('roam_enabled').checked,
    roam_threshold:  parseInt(document.getElementById('roam_threshold').value)||-75,
    roam_hysteresis: parseInt(document.getElementById('roam_hysteresis').value)||12,
    autopoll_enabled:  document.getElementById('autopoll_enabled').checked || (document.getElementById('ble_mode').value==2) || (document.getElementById('ap_mode').value==2),
    autopoll_interval: parseInt(document.getElementById('autopoll_interval').value)||5,
    ble_mode:          parseInt(document.getElementById('ble_mode').value),
    ble_auto_erpm_on:  parseInt(document.getElementById('ble_auto_erpm_on').value)||200,
    ble_auto_off_sec:  parseInt(document.getElementById('ble_auto_off_sec').value)||120,
    leds_enabled:      document.getElementById('leds_enabled').checked,
    version_url: document.getElementById('version_url').value,
    update_url:  document.getElementById('update_url').value,
    wifi: wifi
  };
  if(!reboot) bodyObj.noreboot=true;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(bodyObj)})
    .then(function(r){
      if(r.ok){
        if(reboot){
          showToast(de()?'Gespeichert — ESP startet neu...':'Saved — ESP restarting...',true,8000);
          setTimeout(function(){location.reload();},5000);
        } else {
          var pinChanged=(window.origBlePinEn!==undefined)&&(window.origBlePinEn!==bodyObj.ble_pin_enabled);
          if(pinChanged){
            showToast(de()?'Gespeichert ✓ — Neustart noetig, damit der PIN-Schutz greift (Neustart-Button unten)':'Saved ✓ — restart required for PIN protection to take effect (Restart button below)',true,8000);
            window.origBlePinEn=bodyObj.ble_pin_enabled;
          } else {
            showToast(de()?'Gespeichert ✓':'Saved ✓',true,3000);
          }
          markOriginals();
        }
      } else {
        showToast(de()?'Fehler beim Speichern':'Error saving',false,4000);
      }
    }).catch(function(){showToast('Connection error',false,4000);});
}

// Debug
function updateFilter(){
  var f=(document.getElementById('dbg_ble').checked?1:0)|(document.getElementById('dbg_wifi').checked?2:0)|(document.getElementById('dbg_poll').checked?4:0)|(document.getElementById('dbg_status').checked?8:0);
  fetch('/api/debug?en=1&filter='+f,{method:'POST'});
}
function setDebug(on){
  var f=(document.getElementById('dbg_ble').checked?1:0)|(document.getElementById('dbg_wifi').checked?2:0)|(document.getElementById('dbg_poll').checked?4:0)|(document.getElementById('dbg_status').checked?8:0);
  fetch('/api/debug?en='+(on?1:0)+'&filter='+f,{method:'POST'}).then(function(){
    document.getElementById('debugLogWrap').style.display=on?'':'none';
    if(on)loadUartLog();
  });
}
function loadUartLog(){
  fetch('/api/uart/log').then(function(r){return r.json();}).then(function(d){
    var el=document.getElementById('uartLog');
    el.innerHTML=d.length===0?'<span style="color:#666">empty</span>':d.map(function(l){
      var c=/BROWNOUT|PANIC|WATCHDOG/.test(l)?'#e57373':(/Geplanter Software-Neustart|\[SYSTEM\]/.test(l)?'#81c784':(/\[BOOT\]/.test(l)?'#ffb74d':'var(--text2)'));
      return'<div style="border-bottom:1px solid var(--border);padding:2px 0;color:'+c+'">'+esc(l)+'</div>';
    }).join('');
    el.scrollTop=el.scrollHeight;
  });
}
function clearUartLog(){fetch('/api/uart/clear',{method:'POST'}).then(function(){loadUartLog();});}
function initDebugTab(){
  fetch('/api/debug/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('debug_toggle').checked=d.enabled;
    document.getElementById('dbg_ble').checked  = !!(d.filter & 1);
    document.getElementById('dbg_wifi').checked = !!(d.filter & 2);
    document.getElementById('dbg_poll').checked = !!(d.filter & 4);
    document.getElementById('dbg_status').checked = !!(d.filter & 8);
    document.getElementById('debugLogWrap').style.display=d.enabled?'':'none';
    if(d.enabled)loadUartLog();
  }).catch(function(){});
}

// OTA
var dropZone=document.getElementById('dropZone'),fileInput=document.getElementById('fileInput');
fileInput.addEventListener('change',function(e){selectFile(e.target.files[0]);});
dropZone.addEventListener('dragover',function(e){e.preventDefault();dropZone.classList.add('dragover');});
dropZone.addEventListener('dragleave',function(){dropZone.classList.remove('dragover');});
dropZone.addEventListener('drop',function(e){e.preventDefault();dropZone.classList.remove('dragover');if(e.dataTransfer.files.length)selectFile(e.dataTransfer.files[0]);});
var selectedFile=null;
function selectFile(file){
  if(!file||!file.name.endsWith('.bin')){document.getElementById('otaStatus').textContent=de()?'Nur .bin!':'Only .bin!';document.getElementById('otaStatus').className='status err';document.getElementById('progressWrap').style.display='block';return;}
  selectedFile=file;document.getElementById('fileName').textContent=file.name+' ('+(file.size/1024).toFixed(1)+' KB)';document.getElementById('uploadBtn').disabled=false;document.getElementById('progressWrap').style.display='none';
}
function startUpload(){
  if(!selectedFile)return;
  document.getElementById('uploadBtn').disabled=true;document.getElementById('progressWrap').style.display='block';
  document.getElementById('progressBar').style.width='0%';document.getElementById('otaStatus').textContent='Uploading...';document.getElementById('otaStatus').className='status';
  var fd=new FormData();fd.append('firmware',selectedFile,selectedFile.name);
  var xhr=new XMLHttpRequest();xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);document.getElementById('progressBar').style.width=p+'%';document.getElementById('otaStatus').textContent='Uploading... '+p+'%';}};
  xhr.onload=function(){if(xhr.status===200){document.getElementById('progressBar').style.width='100%';document.getElementById('otaStatus').textContent=de()?'Fertig! Neustart...':'Done! Restarting...';document.getElementById('otaStatus').className='status ok';setTimeout(function(){location.reload();},7000);}else{document.getElementById('otaStatus').textContent='Error: '+xhr.responseText;document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;}};
  xhr.onerror=function(){document.getElementById('otaStatus').textContent='Error';document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;};
  xhr.send(fd);
}
</script>
</body>
</html>
)rawliteral";

// ── Web handlers ──────────────────────────────────────────────────────────────
void handleCaptivePortal() {
  String loc = "http://" + WiFi.softAPIP().toString() + "/";
  otaServer.sendHeader("Location", loc, true);
  otaServer.send(302, "text/plain", "");
}

bool isCaptivePortalRequest() {
  String host = otaServer.hostHeader();
  IPAddress clientIP = otaServer.client().remoteIP();
  IPAddress apIP     = WiFi.softAPIP();
  // Client im AP-Subnetz? (erste drei Oktette gleich der AP-IP)
  if (clientIP[0]==apIP[0] && clientIP[1]==apIP[1] && clientIP[2]==apIP[2])
    return (host != apIP.toString() && host != cfg_hostname + ".local");
  return false;
}

void handlePage() {
  if (isCaptivePortalRequest()) { handleCaptivePortal(); return; }
  otaServer.send(200, "text/html", PAGE_HTML);
}

// Liefert die IP-Adresse des ersten am AP verbundenen Clients (Handy) als String,
// oder "" wenn keiner verbunden ist / die IP noch nicht per DHCP vergeben wurde.
// Zweistufig: Stationen holen (MAC), dann ueber esp_netif auf DHCP-IPs mappen.
static String apClientIp() {
  if (WiFi.softAPgetStationNum() == 0) return "";
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) return "";
  // IDF 5: esp_netif_get_sta_list()/esp_netif_sta_list_t wurden entfernt.
  // Ersatz ist esp_wifi_ap_get_sta_list_with_ip() mit wifi_sta_mac_ip_list_t —
  // identische Semantik (MAC-Liste -> DHCP-IP-Zuordnung des AP-Netifs).
  wifi_sta_mac_ip_list_t netifList;
  if (esp_wifi_ap_get_sta_list_with_ip(&staList, &netifList) != ESP_OK) return "";
  for (int i = 0; i < netifList.num; i++) {
    esp_ip4_addr_t ip = netifList.sta[i].ip;
    if (ip.addr != 0) {   // gueltige (bereits vergebene) IP
      char buf[16];
      esp_ip4addr_ntoa(&ip, buf, sizeof(buf));
      return String(buf);
    }
  }
  return "";   // verbunden, aber noch keine IP vergeben
}

void handleApiInfo() {
  unsigned long up = millis() / 1000;
  String uptime = String(up/3600)+"h "+String((up%3600)/60)+"m "+String(up%60)+"s";
  String json = "{";
  json += "\"ble_name\":\""+cfg_ble_name+"\",";
  json += "\"ble_connected\":"+String(deviceConnected?"true":"false")+",";
  json += "\"ble_mac\":\""+String(NimBLEDevice::getAddress().toString().c_str())+"\",";
  json += "\"wifi_client_connected\":"+String((wifiClient&&wifiClient.connected())?"true":"false")+",";
  json += "\"ap_active\":"+String(apActive?"true":"false")+",";
  if (apActive && cfg_ap_mode == 2 && cfg_ap_timeout > 0) {
    if (WiFi.softAPgetStationNum() > 0) {
      // Client verbunden -> Timer pausiert
      json += "\"ap_timeout_remaining\":-2,";
    } else {
      unsigned long ref = (apLastClientGone > 0) ? apLastClientGone : apStartTime;
      long r = (long)cfg_ap_timeout - (long)((millis()-ref)/1000);
      json += "\"ap_timeout_remaining\":"+String(max(r,0L))+",";
    }
  } else json += "\"ap_timeout_remaining\":-1,";
  json += "\"ap_ip\":\""+WiFi.softAPIP().toString()+"\",";
  json += "\"ap_client_ip\":\""+apClientIp()+"\",";
  json += "\"heap\":"+String(ESP.getFreeHeap())+",";
  json += "\"uptime\":\""+uptime+"\",";
  json += "\"build\":\""+String(FIRMWARE_VERSION)+" ("+String(__DATE__)+" "+String(__TIME__)+")\",";
  json += "\"port\":"+String(cfg_port)+",";
  json += "\"rx_pin\":"+String(cfg_rx_pin)+",";
  json += "\"tx_pin\":"+String(cfg_tx_pin)+",";
  json += "\"vesc_connected\":"+String(vescStatus.connected?"true":"false")+",";
  json += "\"vesc_voltage\":"+String(vescStatus.voltage,2)+",";
  json += "\"vesc_temp_fet\":"+String(vescStatus.tempFet,1)+",";
  json += "\"vesc_temp_motor\":"+String(vescStatus.tempMotor,1)+",";
  json += "\"vesc_fault\":"+String(vescStatus.faultCode)+",";
  json += "\"vesc_erpm\":"+String(vescStatus.erpm)+",";
  json += "\"vesc_fault_str\":\""+vescFaultToString(vescStatus.faultCode)+"\",";
  // Letzter Boot-/Resetgrund fuer externe Diagnose (z.B. Home Assistant)
  json += "\"reset_reason_code\":"+String((int)bootGetResetReason())+",";
  json += "\"reset_reason\":\""+jsonEscapeDebug(bootGetResetName())+"\",";
  json += "\"planned_restart\":\""+jsonEscapeDebug(bootGetPlannedRestartReason())+"\",";
  json += "\"reset_brownout\":"+String(bootGetResetReason()==ESP_RST_BROWNOUT?"true":"false")+",";
  json += "\"reset_panic\":"+String(bootGetResetReason()==ESP_RST_PANIC?"true":"false")+",";
  json += "\"reset_watchdog\":"+String(bootIsWatchdog()?"true":"false")+",";
  // Uhrzeit: per NTP im Heimnetz oder per POST /api/time von der App.
  json += "\"time_valid\":" + String(timeServiceIsValid()?"true":"false") + ",";
  json += "\"time_epoch\":" + String((unsigned long)timeServiceEpoch()) + ",";
  json += "\"time_local\":\"" + jsonEscapeDebug(timeServiceLocalString()) + "\",";
  json += "\"time_utc\":\"" + jsonEscapeDebug(timeServiceUtcString()) + "\",";
  json += "\"time_source\":\"" + jsonEscapeDebug(timeServiceSource()) + "\",";
  json += "\"time_last_sync\":" + String((unsigned long)timeServiceLastSyncEpoch()) + ",";
  // ── Diagnose-Zaehler ──
  json += "\"diag_scans\":"+String(diagScanCount)+",";
  json += "\"diag_sta_conn\":"+String(diagStaConnects)+",";
  json += "\"diag_sta_disc\":"+String(diagStaDisconnects)+",";
  json += "\"diag_disc_reason\":"+String(diagLastDiscReason)+",";
  json += "\"diag_disc_reason_name\":\""+String(wifiDisconnectReasonName(diagLastDiscReason))+"\",";
  json += "\"diag_ap_conn\":"+String(diagApClientConn)+",";
  json += "\"diag_ap_disc\":"+String(diagApClientDisc)+",";
  json += "\"diag_wd_fires\":"+String(diagApWatchdogFires)+",";
  json += "\"diag_loop_max_us\":"+String(diagMaxLoopUs)+",";
  json += "\"diag_loops_per_sec\":"+String(diagLoopsPerSec)+",";
  json += "\"diag_min_heap\":"+String(diagMinHeap==0xFFFFFFFF?0:diagMinHeap)+",";
  json += "\"diag_probe_reqs\":"+String(diagProbeReqs)+",";
  json += "\"diag_probe_rssi\":"+String(diagLastProbeRssi)+",";
  if (WiFi.status() != WL_CONNECTED) {
    json += "\"mode\":\"ap\",\"ip\":\""+WiFi.softAPIP().toString()+"\"";
  } else {
    json += "\"mode\":\"client\",\"ip\":\""+WiFi.localIP().toString()+"\",";
    json += "\"ssid\":\""+WiFi.SSID()+"\",\"rssi\":"+String(WiFi.RSSI());
  }
  json += "}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigGet() {
  String json = "{";
  json += "\"ble_name\":\""+cfg_ble_name+"\",";
  json += "\"ap_ssid\":\""+cfg_ap_ssid+"\",";
  json += "\"ap_pass\":\""+cfg_ap_pass+"\",";
  json += "\"port\":"+String(cfg_port)+",";
  json += "\"vesc_poll\":"+String(cfg_vesc_poll?"true":"false")+",";
  json += "\"ap_timeout\":"+String(cfg_ap_timeout)+",";
  json += "\"rx_pin\":"+String(cfg_rx_pin)+",";
  json += "\"tx_pin\":"+String(cfg_tx_pin)+",";
  json += "\"autoreboot\":"+String(cfg_autoreboot?"true":"false")+",";
  json += "\"autoreboot_time\":"+String(cfg_autoreboot_time)+",";
  json += "\"autoreboot_no_wifi\":"+String(cfg_autoreboot_no_wifi?"true":"false")+",";
  json += "\"roam_enabled\":"+String(cfg_roam_enabled?"true":"false")+",";
  json += "\"roam_threshold\":"+String(cfg_roam_threshold)+",";
  json += "\"roam_hysteresis\":"+String(cfg_roam_hysteresis)+",";
  json += "\"autopoll_enabled\":"+String(cfg_autopoll_enabled?"true":"false")+",";
  json += "\"autopoll_interval\":"+String(cfg_autopoll_interval)+",";
  json += "\"ble_mode\":"+String(cfg_ble_mode)+",";
  json += "\"ble_auto_erpm_on\":"+String(cfg_ble_auto_erpm_on)+",";
  json += "\"ap_mode\":"+String(cfg_ap_mode)+",";
  json += "\"ble_pin_enabled\":"+String(cfg_ble_pin_enabled?"true":"false")+",";
  json += "\"ble_pin\":"+String(cfg_ble_pin)+",";
  json += "\"ble_full_power\":"+String(cfg_ble_full_power?"true":"false")+",";
  json += "\"ble_auto_off_sec\":"+String(cfg_ble_auto_off_sec)+",";
  json += "\"leds_enabled\":"+String(cfg_leds_enabled?"true":"false")+",";
  json += "\"update_url\":\""+cfg_update_url+"\",";
  json += "\"version_url\":\""+cfg_version_url+"\",";
  json += "\"wifi\":[";
  for (int i=0;i<(int)cfg_wifi.size();i++) {
    if (i) json += ",";
    json += "{\"ssid\":\""+cfg_wifi[i].ssid+"\",\"pass\":\""+cfg_wifi[i].pass+"\"";
    json += ",\"static\":"+String(cfg_wifi[i].staticIp?"true":"false");
    json += ",\"ip\":\""+cfg_wifi[i].ip+"\",\"gateway\":\""+cfg_wifi[i].gateway+"\"";
    json += ",\"subnet\":\""+cfg_wifi[i].subnet+"\",\"dns\":\""+cfg_wifi[i].dns+"\"}";
  }
  json += "]}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigPost() {
  String body = otaServer.arg("plain");
  auto extract = [&](String key) -> String {
    String s = "\""+key+"\":\"";
    int st = body.indexOf(s); if (st<0) return "";
    st += s.length();
    // Bis zum naechsten UNESCAPTEN Anfuehrungszeichen lesen (ein \" gehoert
    // noch zum Wert). Sonst wuerde ein escaptes " den Wert zu frueh abschneiden.
    int en = st;
    while (en < (int)body.length()) {
      char c = body.charAt(en);
      if (c == '"' ) break;                       // Ende des Strings
      if (c == '\\' && en + 1 < (int)body.length()) en++;  // escaptes Zeichen ueberspringen
      en++;
    }
    if (en >= (int)body.length()) return "";
    String v = body.substring(st, en);
    // JSON-Escapes zuruecknehmen. WICHTIG: \/ -> / (Androids org.json escaped
    // Slashes -> sonst landen Backslashes in URLs und der Hostname wird kaputt).
    v.replace("\\/", "/");
    v.replace("\\\"", "\"");
    v.replace("\\n", "\n");
    v.replace("\\t", "\t");
    v.replace("\\\\", "\\");   // zuletzt: doppelter Backslash -> einer
    return v;
  };
  auto parseInt2 = [&](String key, int def) -> int {
    String s = "\""+key+"\":";
    int st = body.indexOf(s); if (st<0) return def;
    st += s.length();
    int en = body.indexOf(",", st); if (en<0) en = body.indexOf("}", st); if (en<0) return def;
    int v = body.substring(st, en).toInt(); return v;
  };

  cfg_ble_name    = extract("ble_name");
  cfg_ap_ssid     = extract("ap_ssid");
  cfg_ap_pass     = extract("ap_pass");
  cfg_update_url  = extract("update_url");
  cfg_version_url = extract("version_url");
  cfg_port        = parseInt2("port", VESC_TCP_PORT); if (cfg_port<=0||cfg_port>65535) cfg_port=VESC_TCP_PORT;
  cfg_ap_timeout  = parseInt2("ap_timeout", 0);
  cfg_rx_pin      = parseInt2("rx_pin", VESC_RX_PIN); if (cfg_rx_pin<0||cfg_rx_pin>48) cfg_rx_pin=VESC_RX_PIN;
  cfg_tx_pin      = parseInt2("tx_pin", VESC_TX_PIN); if (cfg_tx_pin<0||cfg_tx_pin>48) cfg_tx_pin=VESC_TX_PIN;
  cfg_vesc_poll          = (body.indexOf("\"vesc_poll\":true") >= 0);
  cfg_autoreboot         = (body.indexOf("\"autoreboot\":true") >= 0);
  cfg_autoreboot_no_wifi = (body.indexOf("\"autoreboot_no_wifi\":true") >= 0);
  cfg_autoreboot_time    = parseInt2("autoreboot_time", 300);
  if (cfg_autoreboot_time < 60) cfg_autoreboot_time = 60;
  cfg_roam_enabled    = (body.indexOf("\"roam_enabled\":true") >= 0);
  cfg_roam_threshold  = parseInt2("roam_threshold", -75);
  cfg_roam_hysteresis = parseInt2("roam_hysteresis", 12);
  if (cfg_roam_threshold  > -40) cfg_roam_threshold  = -40;
  if (cfg_roam_threshold  < -90) cfg_roam_threshold  = -90;
  if (cfg_roam_hysteresis < 3)   cfg_roam_hysteresis = 3;
  if (cfg_roam_hysteresis > 30)  cfg_roam_hysteresis = 30;
  cfg_autopoll_enabled  = (body.indexOf("\"autopoll_enabled\":true") >= 0);
  cfg_autopoll_interval = parseInt2("autopoll_interval", 5);
  cfg_ble_mode          = parseInt2("ble_mode", 1);
  cfg_ble_auto_erpm_on  = parseInt2("ble_auto_erpm_on", 200);
  cfg_ap_mode           = parseInt2("ap_mode", 1);
  // BLE-PIN-Felder NUR uebernehmen, wenn sie im Body vorhanden sind. Grund:
  // eine (aeltere) Companion-App, die die Felder nicht kennt, wuerde beim
  // Speichern sonst die PIN stillschweigend deaktivieren (indexOf -> false)
  // und den Wert auf den Default zuruecksetzen. Fehlen die Felder, bleiben
  // die gespeicherten Werte unveraendert.
  if (body.indexOf("\"ble_pin_enabled\":") >= 0)
    cfg_ble_pin_enabled = (body.indexOf("\"ble_pin_enabled\":true") >= 0);
  if (body.indexOf("\"ble_pin\":") >= 0)
    cfg_ble_pin         = parseInt2("ble_pin", cfg_ble_pin);
  // Wie bei den PIN-Feldern: nur uebernehmen, wenn das Feld im Body vorhanden
  // ist. Eine aeltere Companion-App, die den Haken nicht kennt, wuerde ihn
  // beim Speichern sonst stillschweigend deaktivieren.
  if (body.indexOf("\"ble_full_power\":") >= 0)
    cfg_ble_full_power  = (body.indexOf("\"ble_full_power\":true") >= 0);
  cfg_ble_auto_off_sec  = parseInt2("ble_auto_off_sec", 120);
  bool ledsWasEnabled   = cfg_leds_enabled;   // alten Zustand merken
  cfg_leds_enabled      = (body.indexOf("\"leds_enabled\":true") >= 0);
  // Wenn die WS28XX-Steuerung gerade DEAKTIVIERT wurde -> LEDs sofort ausschalten.
  // (Greift auch ohne Reboot; beim Reboot waeren sie ohnehin aus.)
  if (ledsWasEnabled && !cfg_leds_enabled) ledsOff();
  if (cfg_autopoll_interval < 1)   cfg_autopoll_interval = 1;
  if (cfg_autopoll_interval > 60)  cfg_autopoll_interval = 60;
  if (cfg_ble_mode < 0 || cfg_ble_mode > 2) cfg_ble_mode = 1;
  if (cfg_ap_mode < 1 || cfg_ap_mode > 2)   cfg_ap_mode = 1;   // kein "Aus"
  if (cfg_ap_mode == 2 && cfg_ap_timeout <= 0) cfg_ap_timeout = 120; // Auto braucht sinnvollen Idle-Timeout
  if (cfg_ble_auto_erpm_on < 10)    cfg_ble_auto_erpm_on = 10;
  if (cfg_ble_pin < 0 || cfg_ble_pin > 999999) cfg_ble_pin = 123456;  // 6-stelliger Passkey
  // BLE-Security sofort uebernehmen (gilt fuer kuenftige Kopplungen, kein Reboot)
  if (NimBLEDevice::isInitialized()) applyBleSecurity();   // NimBLE 2.x: getInitialized() -> isInitialized()
  if (cfg_ble_auto_erpm_on > 50000) cfg_ble_auto_erpm_on = 50000;
  if (cfg_ble_auto_off_sec < 5)     cfg_ble_auto_off_sec = 5;
  if (cfg_ble_auto_off_sec > 3600)  cfg_ble_auto_off_sec = 3600;

  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  if (cfg_ap_ssid.isEmpty())  cfg_ap_ssid  = DEFAULT_AP_SSID;

  cfg_wifi.clear();
  int arrStart = body.indexOf("\"wifi\":[");
  if (arrStart >= 0) {
    String arr = body.substring(arrStart + 8); int pos = 0;
    while (pos < (int)arr.length() && (int)cfg_wifi.size() < MAX_WIFI_NETWORKS) {
      int ob = arr.indexOf('{', pos); if (ob<0) break;
      int oe = arr.indexOf('}', ob); if (oe<0) break;
      String e = arr.substring(ob, oe+1);
      auto ex = [&](String k) -> String {
        String s="\""+k+"\":\""; int st=e.indexOf(s); if(st<0)return "";
        st+=s.length(); int en=e.indexOf("\"",st); return en<0?"":e.substring(st,en);
      };
      String ssid = ex("ssid");
      if (ssid.length() > 0) {
        WiFiEntry w; w.ssid=ssid; w.pass=ex("pass");
        w.staticIp=(e.indexOf("\"static\":true")>=0);
        w.ip=ex("ip"); w.gateway=ex("gateway"); w.subnet=ex("subnet"); w.dns=ex("dns");
        if (w.subnet.isEmpty()) w.subnet="255.255.255.0";
        cfg_wifi.push_back(w);
      }
      pos = oe+1;
    }
  }

  saveConfig();
  bool doReboot = !(otaServer.hasArg("noreboot") || body.indexOf("\"noreboot\":true") >= 0);
  if (!doReboot) {
    // Refresh wifiMulti with new networks without reboot
    wifiMulti = WiFiMulti();
    for (auto &n : cfg_wifi) wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
  }
  otaServer.send(200, "text/plain", "OK");
  if (doReboot) { bootDiagMarkPlannedRestart("Konfiguration gespeichert"); ledsOff(); delay(500); ESP.restart(); }
}

void handleOTAUpdate() {
  HTTPUpload &u = otaServer.upload();
  if (u.status==UPLOAD_FILE_START) {
    ledsOff();   // LEDs aus, bevor das Flashen die Animation einfrieren laesst
    Update.begin(UPDATE_SIZE_UNKNOWN);
  }
  else if (u.status==UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
  else if (u.status==UPLOAD_FILE_END) Update.end(true);
}

void handleOTAFinish() {
  if (Update.hasError()) otaServer.send(500,"text/plain",Update.errorString());
  else { bootDiagMarkPlannedRestart("Manuelles OTA-Update"); otaServer.send(200,"text/plain","OK"); ledsOff(); delay(500); ESP.restart(); }
}

void handleApiUpdateCheck() {
  if (WiFi.status()!=WL_CONNECTED){otaServer.send(400,"application/json","{\"error\":\"WiFi only\"}");return;}
  if (cfg_version_url.isEmpty()){otaServer.send(400,"application/json","{\"error\":\"No URL\"}");return;}

  // Sicherheitshalber die URL saeubern: eine gueltige URL hat nie einen
  // Backslash. So schlaegt der Check nicht mehr an einer \/ -verunstalteten
  // URL fehl ("DNS Failed for \\"), egal wie sie in cfg_version_url kam.
  cfg_version_url.replace("\\/", "/");

  // Der Check scheitert sporadisch, weil die TLS-Verbindung zu GitHub (mehrstufige
  // Redirect-Kette, je eigener Handshake) auf dem ESP mal RAM-/Funk-bedingt nicht
  // durchkommt. Daher: bis zu 3 Versuche mit Pause dazwischen. Jeder Versuch baut
  // seine TLS-Ressourcen frisch auf und gibt sie wieder frei (gegen Fragmentierung).
  const int MAX_TRIES = 3;
  int code = 0;
  String ver = "";
  bool ok = false;

  for (int attempt = 1; attempt <= MAX_TRIES && !ok; attempt++) {
    // Vor jedem Versuch pruefen, ob genug zusammenhaengender Heap fuer TLS da ist.
    // Ein TLS-Handshake braucht ~40 KB am Stueck; bei zu wenig gar nicht erst
    // versuchen, sondern kurz warten (evtl. gibt der Stack Speicher frei).
    if (ESP.getMaxAllocHeap() < 45000) {
      Serial.printf("UpdateCheck: zu wenig zusammenh. Heap (%u), warte...\n",
                    (unsigned)ESP.getMaxAllocHeap());
      delay(400);
    }

    HTTPClient http;
    WiFiClientSecure sc;
    sc.setInsecure();
    if (cfg_version_url.startsWith("https")) http.begin(sc, cfg_version_url);
    else                                     http.begin(cfg_version_url);
    http.setTimeout(12000);   // grosszuegiger: drei Handshakes hintereinander
    http.setConnectTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    code = http.GET();
    if (code == 200) {
      ver = http.getString();
      ver.trim();
      if (ver.length() > 0) ok = true;
    }
    http.end();   // TLS-Ressourcen sofort freigeben (wichtig gegen Fragmentierung)

    if (!ok) {
      Serial.printf("UpdateCheck: Versuch %d fehlgeschlagen (HTTP %d)\n", attempt, code);
      if (attempt < MAX_TRIES) delay(600);   // kurz beruhigen, dann neu
    }
  }

  if (ok) {
    updateState.availableVersion = ver;
    updateState.error = "";
    otaServer.send(200, "application/json",
      "{\"current\":\""+String(FIRMWARE_VERSION)+"\",\"available\":\""+ver+
      "\",\"update_available\":"+(ver!=String(FIRMWARE_VERSION)?"true":"false")+"}");
  } else {
    updateState.error = "HTTP "+String(code);
    otaServer.send(500, "application/json",
      "{\"error\":\"HTTP "+String(code)+" (nach "+String(MAX_TRIES)+" Versuchen)\"}");
  }
}

void handleApiUpdateInstall() {
  if (WiFi.status()!=WL_CONNECTED){otaServer.send(400,"text/plain","WiFi only");return;}
  if (cfg_update_url.isEmpty()){otaServer.send(400,"text/plain","No URL");return;}
  otaServer.send(200,"text/plain","OK"); delay(500);
  ledsOff();   // LEDs aus vor dem Server-Flash (sonst frieren sie ein)
  bootDiagMarkPlannedRestart("Server-OTA-Update");
  int updateResult;
  if (cfg_update_url.startsWith("https")) {
    WiFiClientSecure sc; sc.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    updateResult = httpUpdate.update(sc, cfg_update_url);
  } else {
    WiFiClient c;
    updateResult = httpUpdate.update(c, cfg_update_url);
  }
  // Bei Erfolg startet HTTPUpdate normalerweise direkt neu und kehrt nicht
  // zurueck. Kommt die Funktion zurueck, darf kein veralteter Marker bleiben.
  bootDiagClearPlannedRestart();
  dlog("Server-OTA beendet/fehlgeschlagen, Rueckgabecode: %d\n", updateResult);
}

// Liest ein einfaches String-Feld aus einem JSON-Body, z.B.
// {"epoch":1783830000000,"source":"app"}. Fuer die kleine Time-API
// reicht diese schlanke Auswertung; es wird keine zusaetzliche JSON-Library
// benoetigt.
static String parseApiJsonStringField(const String &json, const char *fieldName) {
  String key = "\"" + String(fieldName) + "\"";
  int keyPos = json.indexOf(key);
  if (keyPos < 0) return "";

  int colon = json.indexOf(':', keyPos + key.length());
  if (colon < 0) return "";

  int start = colon + 1;
  while (start < (int)json.length() &&
         (json.charAt(start) == ' ' || json.charAt(start) == '\t' ||
          json.charAt(start) == '\r' || json.charAt(start) == '\n')) {
    start++;
  }
  if (start >= (int)json.length()) return "";

  // Fuer source erwarten wir normalerweise einen JSON-String.
  if (json.charAt(start) == '"') {
    start++;
    String value;
    bool escaped = false;
    for (int i = start; i < (int)json.length(); i++) {
      char c = json.charAt(i);
      if (escaped) {
        value += c;
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        value.trim();
        return value;
      } else {
        value += c;
      }
    }
    return "";
  }

  // Zusaetzlich unquoted Werte bis Komma oder Objektende akzeptieren.
  int end = start;
  while (end < (int)json.length() && json.charAt(end) != ',' &&
         json.charAt(end) != '}' && json.charAt(end) != '\r' &&
         json.charAt(end) != '\n') {
    end++;
  }
  String value = json.substring(start, end);
  value.trim();
  return value;
}

// Prioritaet:
// 1. Query-/Form-Parameter source=app
// 2. JSON-Body {"source":"app"}
// 3. Fallback "api"
static String parseApiTimeSource() {
  String source = otaServer.arg("source");
  source.trim();
  if (!source.isEmpty()) return source;

  String body = otaServer.arg("plain");
  body.trim();
  if (body.startsWith("{")) {
    source = parseApiJsonStringField(body, "source");
    source.trim();
    if (!source.isEmpty()) return source;
  }

  return "api";
}

static bool parseApiEpoch(uint64_t &epochValue, String &error) {
  String raw = otaServer.arg("epoch");
  if (raw.isEmpty()) raw = otaServer.arg("plain");
  raw.trim();

  // JSON der App akzeptieren: {"epoch":1783826067}
  if (raw.startsWith("{")) {
    int keyPos = raw.indexOf("\"epoch\"");
    if (keyPos < 0) {
      error = "missing epoch";
      return false;
    }
    int colon = raw.indexOf(':', keyPos);
    if (colon < 0) {
      error = "invalid JSON";
      return false;
    }
    int start = colon + 1;
    while (start < (int)raw.length() &&
           (raw.charAt(start) == ' ' || raw.charAt(start) == '\t' || raw.charAt(start) == '"')) start++;
    int end = start;
    while (end < (int)raw.length() && raw.charAt(end) >= '0' && raw.charAt(end) <= '9') end++;
    raw = raw.substring(start, end);
  }

  if (raw.isEmpty()) {
    error = "missing epoch";
    return false;
  }

  char *endPtr = nullptr;
  unsigned long long parsed = strtoull(raw.c_str(), &endPtr, 10);
  if (endPtr == raw.c_str() || (endPtr && *endPtr != 0)) {
    error = "epoch must be an integer";
    return false;
  }

  epochValue = (uint64_t)parsed;
  return true;
}

void setupWebServer() {
  otaServer.on("/",                     HTTP_GET,  handlePage);
  // Ausgelagerte statische Assets (einmal im Flash, vom Browser gecacht).
  // Funktionieren offline im AP-Modus, da vom ESP selbst ausgeliefert.
  otaServer.on("/style.css", HTTP_GET, [](){
    otaServer.sendHeader("Cache-Control", "public, max-age=86400");
    // Geteiltes CSS, von beiden Seiten (/ und /leds) genutzt. Referenziert die
    // Ndot-Schrift per @font-face url('/font.woff2') -> Browser laedt + cacht 1x.
    otaServer.send(200, "text/css", STYLE_CSS);
  });
  // Ndot-Schrift als rohe WOFF2-Bytes (kein Base64-Overhead). Lange Cache-Zeit,
  // damit der Browser sie nur einmal laedt und fuer beide Seiten wiederverwendet.
  otaServer.on("/font.woff2", HTTP_GET, [](){
    otaServer.sendHeader("Cache-Control", "public, max-age=604800");
    otaServer.send_P(200, "font/woff2", (const char*)NDOT_FONT_WOFF2, NDOT_FONT_WOFF2_LEN);
  });
  otaServer.on("/api/info",             HTTP_GET,  handleApiInfo);
  otaServer.on("/api/config",           HTTP_GET,  handleApiConfigGet);
  otaServer.on("/api/config",           HTTP_POST, handleApiConfigPost);
  otaServer.on("/api/factory-reset",    HTTP_POST, [](){
    // Echtes "wie neu": die KOMPLETTE NVS-Partition loeschen, nicht nur einen
    // Namespace. Damit verschwinden auch Alt-Leichen aus frueheren Versionen
    // (umbenannte Keys etc.) und der "leds"-Namespace. Reihenfolge wichtig:
    // erst alle offenen Preferences schliessen, dann NVS deinit -> erase -> init.
    otaServer.send(200, "text/plain", "OK");   // Antwort noch senden, bevor wir loeschen
    ledsOff();
    delay(200);
    prefs.end();                 // evtl. offenen vesccfg-Handle schliessen
    nvs_flash_deinit();          // NVS freigeben (sonst "in use")
    nvs_flash_erase();           // GESAMTE NVS-Partition loeschen
    nvs_flash_init();            // frisch initialisieren (sauberer Zustand)
    bootDiagMarkPlannedRestart("Factory Reset / NVS geloescht");
    delay(300);
    ESP.restart();
  });
  otaServer.on("/api/wifi/scan",        HTTP_GET,  [](){ int n=WiFi.scanNetworks();String j="[";for(int i=0;i<n;i++){if(i)j+=",";j+="{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+",\"secure\":"+String(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN?"true":"false")+"}";}j+="]";WiFi.scanDelete();otaServer.send(200,"application/json",j); });
  otaServer.on("/api/wifi/disconnect-reasons", HTTP_GET, [](){ otaServer.send(200,"application/json",wifiDisconnectReasonsJson()); });
  otaServer.on("/api/update/status",    HTTP_GET,  [](){ otaServer.send(200,"application/json","{\"current\":\""+String(FIRMWARE_VERSION)+"\",\"available\":\""+updateState.availableVersion+"\",\"update_url\":\""+cfg_update_url+"\",\"version_url\":\""+cfg_version_url+"\",\"error\":\""+updateState.error+"\"}"); });
  otaServer.on("/api/update/check",     HTTP_GET,  handleApiUpdateCheck);
  otaServer.on("/api/update/install",   HTTP_POST, handleApiUpdateInstall);
  otaServer.on("/api/ping",             HTTP_GET,  [](){ lastBrowserPing=millis(); otaServer.send(200,"text/plain","ok"); });
  otaServer.on("/api/ap/start",         HTTP_POST, [](){
    bool ok = startAccessPointManual();
    otaServer.send(ok ? 200 : 500, "application/json", accessPointStatusJson(ok));
  });
  otaServer.on("/api/restart",          HTTP_POST, [](){ bootDiagMarkPlannedRestart("API-Neustart"); otaServer.send(200,"text/plain","OK");ledsOff();delay(500);ESP.restart(); });
  otaServer.on("/api/debug", HTTP_POST, [](){
    cfg_debug = (otaServer.arg("en") == "1");
    if (otaServer.hasArg("filter")) cfg_debug_filter = otaServer.arg("filter").toInt();
    prefs.begin("vesccfg", false);
    prefs.putBool("debug", cfg_debug);
    prefs.putInt("debug_filter", cfg_debug_filter);
    prefs.end();
    otaServer.send(200, "text/plain", "OK");
  });
  otaServer.on("/api/debug/status", HTTP_GET, [](){ otaServer.send(200,"application/json","{\"enabled\":"+String(cfg_debug?"true":"false")+",\"filter\":"+String(cfg_debug_filter)+"}"); });
  otaServer.on("/api/boot/status",  HTTP_GET, [](){ otaServer.send(200,"application/json",bootStatusJson()); });
  otaServer.on("/api/time", HTTP_GET, [](){
    otaServer.send(200, "application/json", timeServiceJson());
  });
  otaServer.on("/api/time", HTTP_POST, [](){
    // Browser-Synchronisierung darf nur eine noch ungueltige Uhr setzen.
    // Normale App/API-Aufrufe ohne only_if_invalid koennen die Zeit weiterhin
    // bewusst aktualisieren. Die serverseitige Pruefung vermeidet ein Rennen
    // mit einem NTP-Sync zwischen GET /api/time und diesem POST.
    if (otaServer.arg("only_if_invalid") == "1" && timeServiceIsValid()) {
      String response = timeServiceJson();
      response.remove(response.length() - 1);
      response += ",\"ok\":true,\"skipped\":true}";
      otaServer.send(200, "application/json", response);
      return;
    }

    uint64_t epochValue = 0;
    String error;
    if (!parseApiEpoch(epochValue, error)) {
      otaServer.send(400, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscapeDebug(error) + "\"}");
      return;
    }
    String source = parseApiTimeSource();
    if (!timeServiceSetEpoch(epochValue, error, source)) {
      otaServer.send(400, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscapeDebug(error) + "\"}");
      return;
    }
    String response = timeServiceJson();
    response.remove(response.length() - 1);
    response += ",\"ok\":true}";
    otaServer.send(200, "application/json", response);
  });
  otaServer.on("/api/uart/log",         HTTP_GET,  [](){ otaServer.send(200,"application/json",uartLogJson()); });
  otaServer.on("/api/uart/clear",       HTTP_POST, [](){ uartLogClear(); otaServer.send(200,"text/plain","OK"); });
  otaServer.on("/update",               HTTP_POST, handleOTAFinish, handleOTAUpdate);
  otaServer.on("/generate_204",         HTTP_GET,  handleCaptivePortal);
  otaServer.on("/gen_204",              HTTP_GET,  handleCaptivePortal);
  otaServer.on("/hotspot-detect.html",  HTTP_GET,  handlePage);
  otaServer.on("/library/test/success.html", HTTP_GET, handlePage);
  otaServer.on("/ncsi.txt",             HTTP_GET,  handleCaptivePortal);
  otaServer.on("/connecttest.txt",      HTTP_GET,  handleCaptivePortal);
  otaServer.on("/redirect",             HTTP_GET,  handleCaptivePortal);
  otaServer.onNotFound([](){ if(isCaptivePortalRequest())handleCaptivePortal();else otaServer.send(404,"text/plain","Not found"); });

  emergencyServer.on("/update", HTTP_POST, [](){
    emergencyServer.sendHeader("Connection","close");
    emergencyServer.send(200,"text/plain",Update.hasError()?"Update failed!":"Update successful. ESP restarting...");
    if (!Update.hasError()) bootDiagMarkPlannedRestart("Emergency-OTA auf Port 8080");
    ledsOff(); delay(100); ESP.restart();
  }, [](){
    HTTPUpload &u=emergencyServer.upload();
    if(u.status==UPLOAD_FILE_START){ledsOff();Update.begin(UPDATE_SIZE_UNKNOWN);}
    else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);
    else if(u.status==UPLOAD_FILE_END)Update.end(true);
  });

  // LED-Modul: registriert /leds (und spaeter LED-API) am Hauptserver.
  ledsSetup(&otaServer);
  ledsStartTask();   // LED-Rendering in eigenen Task auf Kern 1 auslagern

  otaServer.begin();
  emergencyServer.begin();
  xTaskCreate([](void*){
    for(;;) { emergencyServer.handleClient(); vTaskDelay(1); }
  }, "emergency", 4096, nullptr, 1, nullptr);
}

// ── Modul-Setup ───────────────────────────────────────────────────────────────
void webUiSetup() {
  setupWebServer();
  IPAddress apIP  = WiFi.softAPIP();
  IPAddress staIP = WiFi.localIP();
  Serial.printf("Web (AP):  http://%s/\n", apIP.toString().c_str());
  if (staIP[0] != 0) Serial.printf("Web (STA): http://%s/\n", staIP.toString().c_str());

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
}

// ── Modul-Loop ────────────────────────────────────────────────────────────────
void webUiLoop() {
  otaServer.handleClient();
  dnsServer.processNextRequest();
}

#endif // VESC_BRIDGE_UNITY_BUILD