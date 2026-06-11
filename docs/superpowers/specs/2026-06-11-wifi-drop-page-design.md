# WiFi Drop Page Redesign — Design Spec

**Date:** 2026-06-11
**Status:** Approved (visual companion mockup `page-style-v2.html`, "C in red")
**Scope:** Presentation only — the embedded HTML page in `wifi_drop.cpp`. No endpoint or firmware-behavior changes.

## 1. Summary

Replace the bare-bones upload page with the approved **drop-zone-first, red-tone** design: dark slate, a large red dashed tap/drop target as the hero, red progress, and a styled book list with red-tinted delete chips.

**Definition of done:** the page at `http://192.168.4.1` matches the approved mockup; upload (tap-to-pick *and* desktop drag-drop), progress, list, and delete all still work.

## 2. Visual spec (from the approved mockup)

- **Page:** background `#0b0d10`; content card `#12151a`, radius 14px, max-width ~430px centered; font `'Segoe UI', sans-serif`; text `#e6e9ef`, secondary `#8a93a3`.
- **Header:** `RSVP Reader` (600) + ` / drop` (regular, secondary color).
- **Drop zone (hero):** 2px dashed `#ff3b3b`, radius 12px, background `rgba(255,59,59,.07)`, centered: red `⬇`, "Tap or drop a book here", small ".epub or .txt". Tapping opens the file picker (hidden `<input type=file>`); desktop `dragover`/`drop` uploads the dropped file. Drag-over highlights the border.
- **Progress:** small status line `Uploading "name"… 62%` + a 5px bar, track `#1d222a`, fill `#ff3b3b`; hidden when idle; "Done." / "Failed (N)." status after.
- **Book list:** label `ON THE CARD — N BOOKS` (small caps, letter-spaced); rows separated by `#1d222a` hairlines; each row: title + a `DELETE` chip (1px `#4a2a2e` border, radius 6, text `#ff6b6b`, 9px) with the existing confirm().

## 3. Constraints

Self-contained (no external assets), same endpoints (`/`, `/list`, `/upload?name=`, `/delete?name=`), XHR upload progress as today. Only the `page[]` string + its inline CSS/JS in `firmware/components/wifi_drop/wifi_drop.cpp` change.

## 4. Testing

On-device: page renders per mockup on a phone; tap-to-pick uploads with the red progress; drag-drop works from a desktop browser; list + delete (confirm) work; long titles ellipsize rather than break the row.
