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
#include "logship.h"
#include "backup.h"


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

// ── Geteiltes Stylesheet ─────────────────────────────────────────────────────
//
// Quelltext unter web/style.css, gepackt eingebunden wie die Seiten. Wird von
// allen drei Oberflaechen ueber /style.css geladen und vom Browser einen Tag
// lang behalten.
#include "style_page_gz.h"

// ── HTML PAGE ─────────────────────────────────────────────────────────────────
// ── Startseite ───────────────────────────────────────────────────────────────
//
// Der Quelltext steht als eigene Datei unter web/index.html und wird beim Bauen
// gepackt (tools/gzip_pages.py erzeugt index_page_gz.h). Eingebunden wird nur
// das Ergebnis: rund 33 statt 116 KB im Flash.
#include "index_page_gz.h"

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
  // Nicht zwischenspeichern lassen. Die Seite steckt in der Firmware: nach
  // einem Update liefert der ESP sofort die neue Oberflaeche, der Browser
  // zeigte aber weiter seine alte Kopie — inklusive fehlender neuer Knoepfe.
  // Das sieht wie ein nicht eingespieltes Update aus und kostet nur Sucherei.
  otaServer.sendHeader("Cache-Control", "no-store, must-revalidate");
  otaServer.sendHeader("Content-Encoding", "gzip");
  otaServer.send_P(200, "text/html", (PGM_P)INDEX_PAGE_GZ, INDEX_PAGE_GZ_LEN);
}

// Liefert die IP-Adresse des ersten am AP verbundenen Clients (Handy) als String,
// oder "" wenn keiner verbunden ist / die IP noch nicht per DHCP vergeben wurde.
// Zweistufig: Stationen holen (MAC), dann ueber esp_netif auf DHCP-IPs mappen.
static String apClientIp() {
  if (apClientCount() == 0) return "";
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
    if (apClientCount() > 0) {
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
  json += "\"psram_free\":"+String(ESP.getFreePsram())+",";
  json += "\"psram_total\":"+String(ESP.getPsramSize())+",";
  // Interner Die-Temperatursensor des ESP32-S3. Misst die Chiptemperatur,
  // nicht die Umgebung — im Gehaeuse am VESC liegt sie deutlich ueber der
  // Aussentemperatur, das ist normal.
  //
  // Der Sensor meldet einen Fehlwert, wenn er nicht bereitsteht. Der landet
  // als NaN im float, und String(NaN,1) schreibt "nan" — damit waere das
  // ganze JSON kaputt und die Startseite bliebe leer. Deshalb hier null,
  // und das Frontend blendet die Zeile dann aus.
  {
    float cpuT = temperatureRead();
    if (isnan(cpuT) || cpuT < -50.0f || cpuT > 200.0f) json += "\"cpu_temp\":null,";
    else                                               json += "\"cpu_temp\":"+String(cpuT,1)+",";
  }
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
  // Zustand des Stall-Waechters und der Blackbox. Wichtig vor allem, um zu
  // sehen, ob noch ein Eintrag im Flash auf seine Uebertragung wartet.
  json += "\"blackbox\":" + blackboxStatusJson() + ",";
  json += "\"coredump\":" + coreDumpStatusJson() + ",";
  // Griffheizung: Zustand UND Einstellungen in einem Rutsch. Die Seite /heat
  // zieht sich daraus alles und braucht keinen zweiten Abruf.
  // Beide Modulschalter: /heat und /leds bauen daraus ihre Reiterleiste. Ohne
  // sie sah man vom einen Modul das andere nicht und musste ueber die
  // Startseite zurueck.
  json += "\"leds_enabled\":" + String(cfg_leds_enabled?"true":"false") + ",";
  json += "\"heat_enabled\":" + String(cfg_heat_enabled?"true":"false") + ",";
  json += "\"egg\":" + String(cfg_egg_enabled?"true":"false") + ",";
  // Deutlich mitschicken, wenn ERPM simuliert ist. Ohne diese Kennzeichnung
  // haelt man die Werte spaeter im Log fuer echt und sucht einen Fehler, den
  // es nie gab.
  json += "\"vesc_sim\":" + vescSimStatusJson() + ",";
  json += "\"heat\":" + heatStatusJson() + ",";
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
  json += "\"heat_enabled\":"+String(cfg_heat_enabled?"true":"false")+",";
  json += "\"logship_enabled\":"+String(cfg_logship_enabled?"true":"false")+",";
  json += "\"logship_url\":\""+jsonEscapeDebug(cfg_logship_url)+"\",";
  // Das Token selbst wird NICHT ausgeliefert, nur ob eines gesetzt ist.
  json += "\"logship_token_set\":"+String(cfg_logship_token.length()>0?"true":"false")+",";
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

  // ── Snapshot aller Nicht-LED-Felder VOR dem Parsen ──────────────────────────
  // Wenn sich beim Speichern AUSSER der WS28XX-Steuerung (leds_enabled) nichts
  // aendert, sparen wir uns den Neustart: Die LED-Umschaltung greift ohnehin
  // live (der LED-Task synct sich in vescLoop an cfg_leds_enabled, die Strips
  // sind seit dem Boot initialisiert). Aendert sich etwas anderes -> Neustart.
  String old_ble_name=cfg_ble_name, old_ap_ssid=cfg_ap_ssid, old_ap_pass=cfg_ap_pass;
  String old_update_url=cfg_update_url, old_version_url=cfg_version_url;
  int  old_port=cfg_port, old_ap_timeout=cfg_ap_timeout, old_rx_pin=cfg_rx_pin, old_tx_pin=cfg_tx_pin;
  int  old_autoreboot_time=cfg_autoreboot_time, old_roam_threshold=cfg_roam_threshold, old_roam_hysteresis=cfg_roam_hysteresis;
  int  old_autopoll_interval=cfg_autopoll_interval, old_ble_mode=cfg_ble_mode, old_ble_auto_erpm_on=cfg_ble_auto_erpm_on;
  int  old_ap_mode=cfg_ap_mode, old_ble_pin=cfg_ble_pin, old_ble_auto_off_sec=cfg_ble_auto_off_sec;
  bool old_vesc_poll=cfg_vesc_poll, old_autoreboot=cfg_autoreboot, old_autoreboot_no_wifi=cfg_autoreboot_no_wifi;
  bool old_roam_enabled=cfg_roam_enabled, old_autopoll_enabled=cfg_autopoll_enabled;
  bool old_ble_pin_enabled=cfg_ble_pin_enabled, old_ble_full_power=cfg_ble_full_power;
  std::vector<WiFiEntry> old_wifi = cfg_wifi;
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
  // Genau wie bei den LEDs: dieser Haken allein loest KEINEN Neustart aus, er
  // taucht deshalb unten im Vergleich nicht auf.
  bool heatWasEnabled   = cfg_heat_enabled;
  cfg_heat_enabled      = (body.indexOf("\"heat_enabled\":true") >= 0);
  // Log-Upload: wie bei den PIN-Feldern nur uebernehmen, wenn im Body vorhanden.
  // Eine aeltere Companion-App, die die Felder nicht kennt, wuerde den Upload
  // sonst beim Speichern stillschweigend abschalten. Ein LEERES Token bedeutet
  // "unveraendert lassen" — sonst wuerde die Web-UI (die das Token nie
  // ausliefert) es bei jedem Speichern loeschen.
  if (body.indexOf("\"logship_enabled\":") >= 0)
    cfg_logship_enabled = (body.indexOf("\"logship_enabled\":true") >= 0);
  if (body.indexOf("\"logship_url\":") >= 0)
    cfg_logship_url     = extract("logship_url");
  if (body.indexOf("\"logship_token\":") >= 0) {
    String tok = extract("logship_token");
    if (tok.length() > 0) cfg_logship_token = tok;
  }
  if (cfg_logship_url.isEmpty()) cfg_logship_enabled = false;   // ohne Ziel kein Versand
  logShipApplyConfig();   // Sende-Task auf die neuen Werte umstellen
  // Wenn die WS28XX-Steuerung gerade DEAKTIVIERT wurde -> LEDs sofort ausschalten.
  // (Greift auch ohne Reboot; beim Reboot waeren sie ohnehin aus.)
  if (ledsWasEnabled && !cfg_leds_enabled) ledsOff();
  // Haken raus -> sofort stromlos. Erst beim naechsten Neustart aufzuhoeren
  // waere bei einer Heizung die falsche Reihenfolge.
  if (heatWasEnabled && !cfg_heat_enabled) heatOff();
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

  // ── Hat sich AUSSER der WS28XX-Steuerung etwas geaendert? ────────────────────
  // Nein -> kein Neustart (LED-Umschaltung greift live). Ja -> Neustart wie bisher,
  // damit die geaenderte Einstellung zuverlaessig wirkt. Im Zweifel (irgendein
  // Feld unterscheidet sich) wird neugestartet -> sichere Richtung.
  bool nonLedChanged =
       cfg_ble_name          != old_ble_name           ||
       cfg_ap_ssid           != old_ap_ssid            ||
       cfg_ap_pass           != old_ap_pass            ||
       cfg_update_url        != old_update_url         ||
       cfg_version_url       != old_version_url        ||
       cfg_port              != old_port               ||
       cfg_ap_timeout        != old_ap_timeout         ||
       cfg_rx_pin            != old_rx_pin             ||
       cfg_tx_pin            != old_tx_pin             ||
       cfg_autoreboot_time   != old_autoreboot_time    ||
       cfg_roam_threshold    != old_roam_threshold     ||
       cfg_roam_hysteresis   != old_roam_hysteresis    ||
       cfg_autopoll_interval != old_autopoll_interval  ||
       cfg_ble_mode          != old_ble_mode           ||
       cfg_ble_auto_erpm_on  != old_ble_auto_erpm_on   ||
       cfg_ap_mode           != old_ap_mode            ||
       cfg_ble_pin           != old_ble_pin            ||
       cfg_ble_auto_off_sec  != old_ble_auto_off_sec   ||
       cfg_vesc_poll         != old_vesc_poll          ||
       cfg_autoreboot        != old_autoreboot         ||
       cfg_autoreboot_no_wifi!= old_autoreboot_no_wifi ||
       cfg_roam_enabled      != old_roam_enabled       ||
       cfg_autopoll_enabled  != old_autopoll_enabled   ||
       cfg_ble_pin_enabled   != old_ble_pin_enabled    ||
       cfg_ble_full_power    != old_ble_full_power;
  if (!nonLedChanged) {
    if (cfg_wifi.size() != old_wifi.size()) nonLedChanged = true;
    else for (size_t i = 0; i < cfg_wifi.size(); i++) {
      const WiFiEntry &a = cfg_wifi[i]; const WiFiEntry &b = old_wifi[i];
      if (a.ssid!=b.ssid || a.pass!=b.pass || a.staticIp!=b.staticIp ||
          a.ip!=b.ip || a.gateway!=b.gateway || a.subnet!=b.subnet || a.dns!=b.dns) {
        nonLedChanged = true; break;
      }
    }
  }
  bool onlyLedsChanged = !nonLedChanged;   // nur WS28XX-Steuerung (oder gar nichts)

  bool explicitNoReboot = otaServer.hasArg("noreboot") || body.indexOf("\"noreboot\":true") >= 0;
  bool doReboot = !explicitNoReboot;
  if (onlyLedsChanged) doReboot = false;   // WS28XX greift live -> kein Neustart noetig

  if (!doReboot) {
    // Ohne Neustart: neue WLAN-Netze in wifiMulti uebernehmen (bei reiner
    // LED-Umschaltung unveraendert, schadet aber nicht).
    wifiMulti = WiFiMulti();
    for (auto &n : cfg_wifi) wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
    // Antwort: fuer noreboot-Skripte bleibt es beim alten "OK". Beim Auto-Skip
    // (nur LED / nichts geaendert) sagen wir dem Browser explizit Bescheid.
    if (explicitNoReboot) otaServer.send(200, "text/plain", "OK");
    else                  otaServer.send(200, "application/json", "{\"ok\":true,\"reboot\":false}");
    return;
  }
  otaServer.send(200, "text/plain", "OK");
  bootDiagMarkPlannedRestart("Configuration saved");
  ledsOff(); delay(500); ESP.restart();
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
  else { bootDiagMarkPlannedRestart("Manual OTA update"); otaServer.send(200,"text/plain","OK"); ledsOff(); delay(500); ESP.restart(); }
}

// ── WLAN-Scan, AP-schonend (kanalweise) ─────────────────────────────────────
// Ein kompletter Scan (WiFi.scanNetworks() ohne Kanal-Parameter) haelt das
// Funkmodul 1,5-4 s AM STUECK von unserem AP-Kanal fern: waehrend der Treiber
// die 13 Kanaele durchlaeuft, sendet der AP keine Beacons. Ein verbundener
// Client (z.B. das Handy mit der Web-UI) verpasst dadurch viele Beacons in
// Folge, wertet den AP als verschwunden und fliegt raus — beim Wiederverbinden
// zaehlt softAPgetStationNum() dann alte, noch nicht ausgealterte Eintraege
// mit ("clients=3" bei nur einem Geraet).
// Loesung: kanalweise scannen. Nach jedem Kanal kehrt der Treiber auf den
// AP-Kanal zurueck; die kurze Pause dazwischen laesst Beacons und Client-
// Verkehr durch. Clients verpassen so nur 1-2 Beacons am Stueck und bleiben
// verbunden. Gesamtdauer ~3,5 s — vergleichbar mit vorher, nur ohne Abriss.
static void handleApiWifiScan() {
  String j = "[";
  bool first = true;
  for (uint8_t ch = 1; ch <= 13; ch++) {
    // synchron, keine versteckten SSIDs, aktiver Scan, max. 140 ms pro Kanal
    int n = WiFi.scanNetworks(false, false, false, 140, ch);
    for (int i = 0; i < n; i++) {
      if (!first) j += ",";
      first = false;
      j += "{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+
           ",\"secure\":"+String(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN?"true":"false")+"}";
    }
    WiFi.scanDelete();
    delay(120);   // Atempause auf dem AP-Kanal: Beacons raus, Client bedienen
  }
  j += "]";
  otaServer.send(200, "application/json", j);
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
      Serial.printf("UpdateCheck: too little contiguous heap (%u), waiting...\n",
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
      Serial.printf("UpdateCheck: attempt %d failed (HTTP %d)\n", attempt, code);
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
  bootDiagMarkPlannedRestart("Server OTA update");
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
  dlog("Server OTA finished/failed, return code: %d\n", updateResult);
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
    // Gepackt ausliefern. Laenge zwingend mitgeben: gzip-Daten enthalten
    // Nullbytes, und die Fassung ohne Laengenangabe misst per strlen().
    otaServer.sendHeader("Content-Encoding", "gzip");
    otaServer.send_P(200, "text/css", (PGM_P)STYLE_PAGE_GZ, STYLE_PAGE_GZ_LEN);
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
    bootDiagMarkPlannedRestart("Factory reset / NVS cleared");
    delay(300);
    ESP.restart();
  });
  otaServer.on("/api/wifi/scan",        HTTP_GET,  handleApiWifiScan);   // kanalweise, AP-schonend
  otaServer.on("/api/wifi/disconnect-reasons", HTTP_GET, [](){ otaServer.send(200,"application/json",wifiDisconnectReasonsJson()); });
  otaServer.on("/api/update/status",    HTTP_GET,  [](){ otaServer.send(200,"application/json","{\"current\":\""+String(FIRMWARE_VERSION)+"\",\"available\":\""+updateState.availableVersion+"\",\"update_url\":\""+cfg_update_url+"\",\"version_url\":\""+cfg_version_url+"\",\"error\":\""+updateState.error+"\"}"); });
  otaServer.on("/api/update/check",     HTTP_GET,  handleApiUpdateCheck);
  otaServer.on("/api/update/install",   HTTP_POST, handleApiUpdateInstall);
  otaServer.on("/api/ping",             HTTP_GET,  [](){ lastBrowserPing=millis(); otaServer.send(200,"text/plain","ok"); });
  otaServer.on("/api/ap/start",         HTTP_POST, [](){
    bool ok = startAccessPointManual();
    otaServer.send(ok ? 200 : 500, "application/json", accessPointStatusJson(ok));
  });
  otaServer.on("/api/restart",          HTTP_POST, [](){ bootDiagMarkPlannedRestart("API restart"); otaServer.send(200,"text/plain","OK");ledsOff();delay(500);ESP.restart(); });
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
  // API-Tab-Freischaltung: reiner RAM-Zustand, gilt bis zum naechsten Neustart.
  // GET liefert den Zustand, POST schaltet frei (POST ?en=0 sperrt wieder).
  otaServer.on("/api/debug/unlock", HTTP_GET,  [](){ otaServer.send(200,"application/json","{\"unlocked\":"+String(debugUnlocked?"true":"false")+"}"); });
  otaServer.on("/api/debug/unlock", HTTP_POST, [](){
    debugUnlocked = (otaServer.arg("en") != "0");   // Default: freischalten
    otaServer.send(200,"application/json","{\"unlocked\":"+String(debugUnlocked?"true":"false")+"}");
  });
  otaServer.on("/api/boot/status",  HTTP_GET, [](){ otaServer.send(200,"application/json",bootStatusJson()); });

  // Sicherung/Wiederherstellung der gesamten Konfiguration (eigenes Modul,
  // siehe backup.cpp — der NVS wird dort generisch ausgelesen).
  backupRegisterRoutes(otaServer);
  coreDumpRegisterRoutes(otaServer);
  // Blackbox-Bericht und RTC-Zeilen direkt abholbar machen. Wichtig fuer den
  // Fall ohne Heimnetz: ein Stillstand hinterlaesst KEIN Absturzabbild (der
  // Waechter startet geplant neu), die Diagnose steckt nur hier.
  blackboxRegisterRoutes(otaServer);
  // ── Log-Upload ──────────────────────────────────────────────────────────────
  // Eigene Endpunkte statt /api/config, weil das grosse Speichern den ESP
  // neustartet. Der Sende-Task liest cfg_logship_* laufend -> die Aenderung
  // wirkt sofort, ein Neustart waere hier nur stoerend (und wuerde ausgerechnet
  // den gefuellten Puffer verwerfen).
  otaServer.on("/api/logship", HTTP_GET, [](){
    otaServer.send(200, "application/json", logShipStatusJson());
  });
  otaServer.on("/api/logship", HTTP_POST, [](){
    String body = otaServer.arg("plain");
    auto ex = [&](const String &key) -> String {
      String s = "\""+key+"\":\"";
      int st = body.indexOf(s); if (st < 0) return "";
      st += s.length();
      int en = st;
      while (en < (int)body.length()) {
        char c = body.charAt(en);
        if (c == '"') break;
        if (c == '\\' && en + 1 < (int)body.length()) en++;
        en++;
      }
      if (en >= (int)body.length()) return "";
      String v = body.substring(st, en);
      v.replace("\\/", "/");
      v.replace("\\\"", "\"");
      v.replace("\\\\", "\\");
      return v;
    };

    if (body.indexOf("\"url\":") >= 0) cfg_logship_url = ex("url");
    // Leeres Token = unveraendert lassen. Die Oberflaeche liefert das
    // gespeicherte Token nie aus, wuerde es also sonst bei jedem Speichern
    // loeschen. Zum Entfernen gibt es "token_clear".
    if (body.indexOf("\"token\":") >= 0) {
      String tok = ex("token");
      if (tok.length() > 0) cfg_logship_token = tok;
    }
    if (body.indexOf("\"token_clear\":true") >= 0) cfg_logship_token = "";
    if (body.indexOf("\"enabled\":") >= 0)
      cfg_logship_enabled = (body.indexOf("\"enabled\":true") >= 0);
    if (cfg_logship_url.isEmpty()) cfg_logship_enabled = false;

    // Puffergroesse: greift erst beim naechsten Start, deshalb wird hier nur
    // gespeichert. Einen laufenden Puffer umzuhaengen waere ein Rennen mit dem
    // Sende-Task, der gerade daraus liest.
    if (body.indexOf("\"slots\":") >= 0) {
      int sl = -1;
      int k = body.indexOf("\"slots\":");
      if (k >= 0) sl = body.substring(k + 8).toInt();
      if (sl == -1 || sl == 0 || sl == 150 || sl == 500 || sl == 1000 || sl == 2000) {
        cfg_logship_slots = sl;
      }
    }

    // Nur die vier Schluessel schreiben (wie /api/debug), nicht saveConfig():
    // das wuerde den gesamten Konfigurationsblock neu schreiben.
    prefs.begin("vesccfg", false);
    prefs.putBool  ("lship_en",  cfg_logship_enabled);
    prefs.putString("lship_url", cfg_logship_url);
    prefs.putString("lship_tok", cfg_logship_token);
    prefs.putInt   ("lship_sl",  cfg_logship_slots);
    prefs.end();
    logShipApplyConfig();   // Sende-Task auf die neuen Werte umstellen

    dlog("Logship: %s, url='%s', token=%s\n",
         cfg_logship_enabled ? "enabled" : "disabled",
         cfg_logship_url.c_str(),
         cfg_logship_token.length() > 0 ? "set" : "none");

    if (cfg_logship_enabled) logShipRequestFlush();
    otaServer.send(200, "application/json", logShipStatusJson());
  });
  otaServer.on("/api/logship/test",  HTTP_POST, [](){
    logShipSendTestLine();
    otaServer.send(200, "application/json", logShipStatusJson());
  });
  // Pufferinhalt als Klartext. Der einzige Weg an die gesammelten Zeilen,
  // wenn kein Heimnetz da ist und nichts gesendet werden kann.
  otaServer.on("/api/logship/dump", HTTP_GET, [](){ logShipDumpChunked(otaServer); });

  // Easteregg an/aus. Eigene Route statt /api/config: dort loest jedes
  // Speichern einen Neustart aus, und dafuer ist ein Haken zu wenig Anlass.
  // Geschrieben wird nur dieser eine Schluessel.
  // ERPM-Simulation. Nur mit freigeschaltetem Debug — ein simulierter Wert
  // laesst die Heizung heizen, waehrend der Scooter steht, und weckt BLE/AP im
  // Auto-Modus. Das gehoert nicht in die Reichweite eines Fehlklicks.
  otaServer.on("/api/vesc/sim", HTTP_GET, [](){
    otaServer.send(200, "application/json", vescSimStatusJson());
  });
  otaServer.on("/api/vesc/sim", HTTP_POST, [](){
    if (!debugUnlocked) {
      otaServer.send(403, "application/json",
                     "{\"ok\":false,\"err\":\"Debug nicht freigeschaltet\"}");
      return;
    }
    int32_t erpm = otaServer.hasArg("erpm") ? (int32_t)otaServer.arg("erpm").toInt() : 0;
    int     sec  = otaServer.hasArg("sec")  ? otaServer.arg("sec").toInt()  : 0;
    vescSimSet(erpm, sec);
    otaServer.send(200, "application/json",
                   String("{\"ok\":true,\"sim\":") + vescSimStatusJson() + "}");
  });

  otaServer.on("/api/egg", HTTP_GET, [](){
    otaServer.send(200, "application/json",
                   String("{\"enabled\":") + (cfg_egg_enabled ? "true" : "false") + "}");
  });
  otaServer.on("/api/egg", HTTP_POST, [](){
    String body = otaServer.arg("plain");
    if (otaServer.hasArg("en")) {
      cfg_egg_enabled = (otaServer.arg("en").toInt() != 0);
    } else if (body.indexOf("\"enabled\":") >= 0) {
      cfg_egg_enabled = (body.indexOf("\"enabled\":true") >= 0);
    }
    prefs.begin("vesccfg", false);
    prefs.putBool("egg_en", cfg_egg_enabled);
    prefs.end();
    otaServer.send(200, "application/json",
                   String("{\"enabled\":") + (cfg_egg_enabled ? "true" : "false") + "}");
  });
  otaServer.on("/api/logship/clear", HTTP_POST, [](){
    logShipClear();
    otaServer.send(200, "application/json", logShipStatusJson());
  });
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
    if (!Update.hasError()) bootDiagMarkPlannedRestart("Emergency OTA on port 8080");
    ledsOff(); delay(100); ESP.restart();
  }, [](){
    HTTPUpload &u=emergencyServer.upload();
    if(u.status==UPLOAD_FILE_START){ledsOff();Update.begin(UPDATE_SIZE_UNKNOWN);}
    else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);
    else if(u.status==UPLOAD_FILE_END)Update.end(true);
  });

  // LED-Modul: registriert /leds (und spaeter LED-API) am Hauptserver.
  // Griffheizung: eigene Seite /heat plus /api/heat. Eigener NVS-Namensraum,
  // deshalb greift dort alles sofort statt wie in der Hauptkonfiguration erst
  // nach einem Neustart.
  heatSetup(&otaServer);
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