# WiFi Drop — Design Spec

**Date:** 2026-06-10
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare ESP32-S3-Touch-LCD-3.49
**Firmware stack:** ESP-IDF 5.5.2 (`esp_wifi`, `esp_http_server`), LVGL 9, C++17 `core` (doctest)

---

## 1. Summary

Let the user air-drop EPUB/TXT files onto the SD card (and delete books) over WiFi, with no card-pulling. Entering the **WiFi Drop** screen turns the device into a WPA2 access point hosting a small web page; a phone/laptop connects and uploads/deletes; leaving the screen shuts WiFi down. Uploaded files land in `/sdcard` and appear in the Library.

**Definition of done:** open WiFi Drop → connect a phone to `RSVP-Reader` → browse to `http://192.168.4.1` → upload an `.epub` → it appears in the Library after closing the screen; the page also lists books and deletes them. Closing the screen stops the AP.

**Out of scope:** joining an existing WiFi (STA mode), a captive portal / DNS redirect, HTTPS, progress bars, renaming, folders, OTA firmware update.

---

## 2. Components

- **`core` — `uploadname.{hpp,cpp}`** (new, host-tested, pure):
  ```cpp
  std::string sanitizeUploadName(const std::string& raw);
  // basename only (strip up to the last '/' or '\\'); "" if empty, starts with '.',
  // contains a control char, or lacks a .epub/.txt extension (case-insensitive). Keeps original case.
  ```
- **`firmware/components/wifi_drop/`** (new, **C**), `REQUIRES esp_wifi esp_http_server esp_netif esp_event nvs_flash sdcard_bsp core`:
  ```c
  typedef struct { char ssid[33]; char pass[33]; char url[32]; bool ok; } wifi_drop_info_t;
  void wifi_drop_start(wifi_drop_info_t* out);  // bring up AP + HTTP server
  void wifi_drop_stop(void);                    // stop server + WiFi
  ```
  - **AP:** SSID `RSVP-Reader`, password `rsvpdrop8` (WPA2, ≥8 chars), channel 1, max 2 clients; AP IP is the IDF default `192.168.4.1` → `url = "http://192.168.4.1"`.
  - One-time `esp_netif_init()` + `esp_event_loop_create_default()` (guarded; NVS is already initialized at boot).
  - **HTTP handlers** (`esp_http_server`):
    - `GET /` → the embedded HTML+JS page (a `const char[]` in flash).
    - `GET /list` → newline-separated `.epub`/`.txt` names in `/sdcard`.
    - `POST /upload?name=<file>` → `sanitizeUploadName(name)`; stream the request body in chunks (`httpd_req_recv` loop) to `/sdcard/<name>` via `fopen("wb")`/`fwrite`. 400 on bad name, 500 on write/SD failure, 200 OK otherwise.
    - `POST /delete?name=<file>` → sanitize, `remove("/sdcard/<name>")`. 400 bad name, 200 OK (idempotent).
- **`firmware/main/ui_menu.cpp`** (`SCR_WIFI`): replace the "coming soon" placeholder. On open call `wifi_drop_start`, show **SSID / password / `http://192.168.4.1`** and a "Ready — connect and open the page" line (or an error if `!info.ok`). BOOT (the existing `SCR_WIFI` nav case) calls `wifi_drop_stop()` then returns to the menu.

---

## 3. Web page (served from flash)

A single self-contained page (no external assets). Upload uses **`XMLHttpRequest`** (`POST /upload?name='+encodeURIComponent(file.name)`, body = the raw file) with an `xhr.upload.onprogress` handler driving a **progress bar** (`loaded/total` → a `<progress>` element + percent text), so large EPUBs show progress while sending. On completion it re-fetches `/list` and renders each book with a Delete button calling `fetch('/delete?name='+encodeURIComponent(n), {method:'POST'})`. Raw body + name-in-query means **no multipart parsing** in firmware (and the progress bar is entirely browser-side — no firmware change). Minimal inline CSS; status text for success/failure.

## 4. Data flow

Open screen → `wifi_drop_start` (AP + server) → screen shows credentials. Phone joins `RSVP-Reader`, opens `http://192.168.4.1` → page loads, `GET /list` populates the list. Upload → `POST /upload` streams to `/sdcard`. Delete → `POST /delete`. Close screen (BOOT) → `wifi_drop_stop`. Back in the menu, **Library** re-scans `/sdcard` (`list_books()`) and shows the new files.

## 5. Error handling

- Bad/missing `name` → 400; `sanitizeUploadName` rejects path traversal, hidden, wrong extension.
- `fopen`/`fwrite` failure (no SD, full) → 500; partial file is closed (best-effort).
- `wifi_drop_start` failure (WiFi/netif/httpd) → `info.ok = false` → screen shows "WiFi failed".
- Deleting a book leaves its `/sdcard/.rsvp/<name>.idx*` cache orphaned — harmless (it just won't be referenced); not cleaned up here.
- RAM: WiFi + httpd need a chunk of internal RAM; WiFi Drop is a dedicated screen (the reader/load task aren't active), so this is expected to fit — **verified on-device**.

## 6. Testing

- **Host (TDD):** `sanitizeUploadName` — `"foo.epub"→"foo.epub"`, `"../../etc/foo.epub"→"foo.epub"`, `"sub/book.txt"→"book.txt"`, `"foo.EPUB"→"foo.EPUB"`, and `""`/`".hidden.epub"`/`"foo.pdf"`/`"/etc/passwd"`→`""`.
- **On-device:** open WiFi Drop → screen shows SSID/pass/URL; connect a phone → page loads + lists books; upload an `.epub` → the **progress bar advances**, 200, file on `/sdcard`, shows in Library after closing; delete a book → gone from the list and Library; closing the screen stops the AP (phone drops); the touchscreen/menu still work afterward.
