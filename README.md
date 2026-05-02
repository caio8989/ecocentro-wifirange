# ESP32 Wi-Fi Range Logger

PlatformIO firmware for walking a site with an ESP32 and a phone. The ESP32 scans nearby Wi-Fi networks every five seconds, logs RSSI/channel/security data to onboard flash, and accepts GPS fixes from a phone over Bluetooth LE so each scan row can be geotagged.

## Hardware

- ESP32 development board, such as `esp32dev`
- USB power bank for walking tests
- Android phone with Chrome for the included Web Bluetooth GPS bridge

## Build and Flash

```powershell
pio run -t upload
pio device monitor -b 115200
```

The default board is `esp32dev` in `platformio.ini`. Change `board = ...` if your ESP32 uses a different PlatformIO board ID.

If upload fails with a timeout, check the serial port first:

```powershell
pio device list
```

The ESP32 should appear as a USB serial device, commonly from Silicon Labs CP210x, WCH CH340, FTDI, or Espressif. Do not choose a port described as `Standard Serial over Bluetooth link`; that is a Windows Bluetooth COM port and cannot flash the ESP32.

If no USB serial port appears, try a data-capable USB cable, another USB port, or install the USB-to-serial driver for your board. Some ESP32 boards also need the `BOOT` button held while PlatformIO prints `Connecting...`, then released when upload starts.

## Phone GPS Bridge

The ESP32 advertises as `WiFiRangeLogger` using the Nordic UART BLE service.

It usually will not appear in the phone's normal Bluetooth pairing screen. Do not pair it from Android or iOS system Bluetooth settings. Connect from the web page below, or from a BLE scanner/terminal app such as nRF Connect that can discover BLE peripherals.

The included page at `docs/index.html` uses Web Bluetooth plus browser geolocation. Web Bluetooth and geolocation require a secure context, so serve it from HTTPS. GitHub Pages is a good fit.

After publishing with GitHub Pages, the URL will look like:

```text
https://YOUR_GITHUB_USERNAME.github.io/ecocentro-wifirange/
```

Use Chrome on Android and open that URL on the phone.

### Publish the GPS Bridge with GitHub Pages

Create a GitHub repository named `ecocentro-wifirange`, then run these commands from this project folder:

```powershell
git init
git add .
git commit -m "Add ESP32 Wi-Fi range logger"
git branch -M main
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/ecocentro-wifirange.git
git push -u origin main
```

Then enable Pages in GitHub:

1. Open the repository on GitHub.
2. Go to `Settings` > `Pages`.
3. Under `Build and deployment`, set `Source` to `Deploy from a branch`.
4. Set `Branch` to `main` and folder to `/docs`.
5. Click `Save`.

GitHub usually publishes the page within a minute or two.

One quick local option:

```powershell
cd docs
python -m http.server 8000
```

Then open `http://localhost:8000/phone-gps-ble.html` on the same Android phone if you are serving directly on the phone. For a laptop-hosted page, use an HTTPS tunnel or host the file somewhere HTTPS-capable.

Click `Connect ESP32`, choose `WiFiRangeLogger`, and allow location access. The phone streams JSON fixes like:

```json
{"lat":-23.55052,"lon":-46.63331,"accuracy":8}
```

You can also send the same JSON from any BLE terminal app that supports the Nordic UART service.

If the phone still cannot find `WiFiRangeLogger`:

- Open the ESP32 serial monitor and confirm it prints `WiFiRangeLogger ready`.
- Press the ESP32 reset button, then scan again from the web page or BLE app.
- On Android, enable Bluetooth and Location, and allow nearby-device/location permissions for Chrome or the BLE app.
- If Chrome says `User denied the browser permission to scan for Bluetooth devices`, open Android app settings for Chrome and allow `Nearby devices` and `Location`. Then open the page's site settings in Chrome and reset/allow Bluetooth for the GitHub Pages URL.
- Use Chrome on Android for the web page. iPhone Safari and Chrome on iOS do not support Web Bluetooth.
- Make sure you are scanning for BLE devices, not only paired Classic Bluetooth devices.

## Inspect the Log

On the phone page, tap `Ask ESP32 to Dump Log`. The ESP32 streams the CSV back over BLE, and the page shows a `Download CSV` link when the transfer finishes.

You can also open the serial monitor and send:

```text
DUMP
```

The ESP32 prints the CSV between `BEGIN WIFI RANGE CSV` and `END WIFI RANGE CSV`, and also sends it over BLE if a phone is connected.

To clear the log, send:

```text
ERASE
```

## CSV Columns

```text
uptime_ms,scan_id,ssid,bssid,rssi_dbm,channel,encryption,lat,lon,alt_m,accuracy_m,speed_mps,gps_age_ms
```

`gps_age_ms` is the age of the latest phone GPS fix at the moment the Wi-Fi scan was written. Rows with empty GPS fields were captured before the phone sent a usable fix.

## Field Workflow

1. Flash the ESP32.
2. Power it from a battery.
3. Connect the phone GPS bridge.
4. Walk the test route slowly, pausing at points of interest if you want denser samples.
5. Return to USB serial and send `DUMP`.
6. Save the CSV output for mapping or spreadsheet inspection.

For best range data, keep the ESP32 in the same orientation during the walk and record which AP or BSSID you care about before analyzing the log.
