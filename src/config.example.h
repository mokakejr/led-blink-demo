#pragma once
// Copie ce fichier en src/config.h puis remplis-le.  (config.h est ignoré par git)

// ---------- Wi-Fi ----------
#define WIFI_SSID "TON_WIFI"
#define WIFI_PASS "TON_MOT_DE_PASSE"

// ---------- Canal ntfy.sh ----------
// Secret long et aléatoire : `openssl rand -hex 12`
// Il sert de nom de canal : qui connaît le secret peut commander les sorties.
#define NTFY_SECRET "remplace-moi-par-24-caracteres-hex"

// ---------- Sorties ----------
#define PIN_UP 4
#define PIN_DOWN 5
#define RELAY_ACTIVE_HIGH 1   // 0 si ton module relais s'active à l'état bas
#define DEFAULT_MOVE_S 5
#define MAX_MOVE_S 20         // sécurité : durée max d'un mouvement

// ---------- Divers ----------
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Paris
