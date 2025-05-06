
# ESP32 OTA Automation

This repo holds an automated pipeline that:
1. Builds the firmware with GitHub Actions.
2. Exposes it via ngrok/Serveo.
3. Publishes the download URL over MQTT.
4. ESP32‑S3 pulls & flashes it via SIM800L.
