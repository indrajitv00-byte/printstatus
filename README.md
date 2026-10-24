# PrintStatus

PrintStatus is a Firebase-backed printer monitoring project. An ESP32 agent polls supported printers and writes their status to Firebase Realtime Database. The React web dashboard displays the current status after Firebase Authentication login.

## Project structure

- `web/` - React and Vite dashboard
- `esp32/printstatus_esp32_agent/` - ESP32 polling agent
- `database.rules.json` - Realtime Database security rules
- `firebase.json` - Firebase Hosting and database configuration

## Web dashboard

Requirements:

- Node.js 18 or newer
- A Firebase project with Email/Password Authentication enabled
- Firebase Realtime Database configured with the rules in `database.rules.json`

Install dependencies and start the development server:

```bash
cd web
npm install
npm run dev
```

The Firebase web configuration is in `web/src/firebase.js`. Create the dashboard users in Firebase Authentication.

Build the dashboard for deployment:

```bash
cd web
npm run build
```

Deploy Firebase Hosting from the project root:

```bash
firebase login
firebase deploy
```

## ESP32 agent

The agent supports Klipper/Moonraker, PrusaLink, and Bambu Lab printers in LAN mode. It polls the configured printers every minute and writes results to `/status`.

1. Open `esp32/printstatus_esp32_agent/printstatus_esp32_agent.ino` in the Arduino IDE.
2. Install `ArduinoJson` version 7 and `PubSubClient` from the Arduino Library Manager.
3. Select an ESP32 Dev Module and set the serial monitor to 115200 baud.
4. Fill in the Wi-Fi, Firebase, and agent account settings in the configuration section.
5. Create the Firebase Authentication user used by the agent. The email must match the write rule in `database.rules.json`.
6. Add printer definitions under `/config/printers` in the Realtime Database.
7. Upload the sketch and monitor the serial output.

Printer definitions use these fields:

- Klipper: `id`, `name`, `enabled`, `kind: "klipper"`, `moonrakerUrl`
- PrusaLink: `id`, `name`, `enabled`, `kind: "prusa"`, `ip`, `accessCode`
- Bambu LAN: `id`, `name`, `enabled`, `kind: "bambu"`, `ip`, `serial`, `accessCode`

## Security notes

- Do not commit Wi-Fi passwords, Firebase service-account JSON files, or other private credentials.
- The Firebase web API key is intended for client-side use, but access is controlled by Authentication and Realtime Database rules.
- Change any credentials that have been exposed and keep the ESP32 configuration local.