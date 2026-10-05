# esp32-c3-test

Test ESP32-C3 : commande de 2 sorties relais depuis une page web (GitHub Pages),
via ntfy.sh. Pas de serveur : la page poste sur un canal, l'ESP32 l'écoute en HTTPS.

```
page web ──POST──► ntfy.sh/esp-<secret> ◄──flux HTTPS── ESP32-C3 ──► 2 relais (GPIO4 / GPIO5)
page web ◄── ntfy.sh/esp-<secret>-log ◄── POST (chaque action) ──────┘
```

## Firmware
```
cp src/config.example.h src/config.h     # puis remplis Wi-Fi + NTFY_SECRET
openssl rand -hex 12                      # → NTFY_SECRET
pio run -t upload && pio device monitor
```
`src/config.h` est dans `.gitignore` : ne le commit pas.

## Page
Dépôt public → Settings → Pages → `main` / `/docs`.
Lien : `https://<user>.github.io/<repo>/#s=<NTFY_SECRET>`

## Sécurité
- Durée max par action (`MAX_MOVE_S`), jamais les 2 sorties actives en même temps.
- Arrêt par timer matériel, ordres de plus de 30 s ignorés, reboot si le canal est muet 10 min.
