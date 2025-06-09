Sistem Otomatisasi Hidroponik Wick dengan berbasis ESP8266, MQTT, MIT App Inventor, Node-Red, dengan sensor Waterlevel Sensor module(Analog) sebagai sensor pengukur level air dan DHT 11 sebagai sensor suhu dan kelambaban

## Deskripsi

Proyek ini bertujuan untuk mengotomatisasi sistem hidroponik wick dengan fitur monitoring dan kontrol jarak jauh. Sistem menggunakan ESP8266 sebagai mikrokontroler, sensor water level dan DHT11, serta dikendalikan melalui MQTT yang terhubung ke aplikasi Android (MIT App Inventor) dan dashboard Node-RED.

- **Mode Otomatis & Manual**: Pompa air dapat dikendalikan secara otomatis berdasarkan level air, atau secara manual lewat aplikasi.
- **Pemantauan Real-time**: Suhu, kelembapan, dan level air dapat dipantau dari aplikasi Android maupun dashboard Node-RED.
- **Keamanan Pompa**: Fitur timer dan proteksi agar pompa tidak menyala terlalu lama.
- **Threshold & Logging**: Batas bawah/atas level air dapat diatur melalui aplikasi.

## Fitur Utama

- Kontrol Pompa: Otomatis/manual, dengan proteksi waktu jalan.
- Monitoring: Water level, suhu, dan kelembapan.
- Threshold: Pengaturan batas ON/OFF pompa.
- Komunikasi: MQTT untuk integrasi IoT.
- Dashboard & App: Monitoring dan kontrol via Node-RED & Android.


## Topik MQTT

| Topik                   | Fungsi                   |
|-------------------------|--------------------------|
| `kel4/water_level`      | Publikasi level air (%)  |
| `kel4/suhu`             | Publikasi suhu (°C)      |
| `kel4/kelembapan`       | Publikasi kelembapan (%) |
| `kel4/pompa/control`    | Kontrol pompa (ON/OFF)   |
| `kel4/pompa/status`     | Status pompa             |
| `kel4/mode/control`     | Kontrol mode (AUTO/MANUAL)|
| `kel4/mode/status`      | Status mode              |
| `kel4/threshold/set`    | Set threshold (misal: 25,75) |
| `kel4/threshold/status` | Status threshold         |
| `kel4/system/status`    | Status sistem (JSON)     |



