I Just made # WrenOS

A small **KolibriOS-inspired** desktop shell for the M5Stack Cardputer.

Named after the wren — a small, common bird — as a nod to KolibriOS's
own name (Russian for "hummingbird") without borrowing it outright,
since this is an original, much smaller project built on top of it
only in spirit.

## Important reality check

This is **not** KolibriOS itself. KolibriOS is a real x86 operating
system, and the Cardputer's brain is an ESP32-S3 (Xtensa architecture)
— it cannot execute x86 machine code, so there is no way to make actual
KolibriOS "run on" a Cardputer. What this project does instead is copy
the *look and feel* — a flat colorful desktop, KolibriOS-style
top-left window buttons, and a fully mouse-driven UI — and implement
it from scratch as an Arduino sketch.

## Branding: logo, name, loading screen

- **Logo**: there's no image file to embed on a microcontroller sketch
  like this, so the logo is drawn as flat vector shapes — a rounded
  tile with a smaller accent-colored square inset (`drawLogo()`),
  echoing the window-manager theme. It appears on the boot splash and
  in the About app.
- **Loading screen**: every blocking operation (connecting WiFi,
  calling Claude, fetching a web page) now shows the same consistent
  screen — logo, a short message, and animated-looking trailing dots
  — via `showLoadingScreen()`, instead of each spot improvising its
  own "please wait" text. Since the sketch is single-threaded, it's
  one static frame drawn right before the blocking call starts; it
  can't animate *during* the wait itself.
- **Battery**: the taskbar's bottom-left corner shows a small battery
  icon and percentage, read via `M5.Power.getBatteryLevel()` and
  cached for a few seconds at a time (it's a small I2C round-trip, so
  it isn't re-read every frame). If your board/library combination
  doesn't report a level, it shows `--` instead of a number.

## Hardware support

Targets **Cardputer V1.1** (the StampS3A-based revision with 8MB
PSRAM) but also runs on the original Cardputer — M5Unified / M5GFX /
M5Cardputer auto-detect which board they're on, so the same sketch
covers both as long as your library versions are current (see
Requirements). For V1.1, enable PSRAM in **Tools → PSRAM → OPI PSRAM**
before flashing — not strictly required at this sketch's default
buffer sizes, but it gives headroom if you raise the Browser app's
page-size cap, and helps avoid heap-fragmentation issues under
sustained WiFi use.

## SD card storage

Notes and the new **Files** app use the Cardputer's microSD slot for
real, persistent storage — previously everything in this sketch lived
only in RAM and vanished on reboot. This is the single most useful
addition: your notes (or anything else you save) now survive a power
cycle.

- Insert a **FAT32-formatted** microSD card, contacts facing away from
  the screen, before powering on.
- Wiring/init follows M5Stack's own documented pins for the Cardputer's
  SD slot (SCK=40, MISO=39, MOSI=14, CS=12) — see
  [docs.m5stack.com/en/arduino/m5cardputer/sdcard](https://docs.m5stack.com/en/arduino/m5cardputer/sdcard).
- A second taskbar dot shows SD status: green if a card is mounted,
  grey if not (no card isn't treated as an error, just "storage
  unavailable" — Notes/Files still work, just without persistence).

### Notes app

Now has **Save** and **New** buttons (bottom-right of the window):
- **Save** writes the current buffer to `notesFilename` (shown in the
  status line, defaults to `/notes.txt`).
- **New** clears the buffer and resets the filename back to
  `/notes.txt`.

### Files app

Lists plain files in the SD card's root directory:
- Click a filename to load it into Notes for viewing/editing — Notes'
  Save button then writes back to that same file, so this doubles as
  a small generic text-file editor, not just a fixed notes pad.
- Click the small **x** next to a file to delete it.
- **Refresh** re-reads the directory (e.g. after saving a new file).
- Shows used/total SD space at the bottom.
- If there are more than 5 files, ▲/▼ buttons appear to scroll the list.

## What it does

- Boots to a desktop with app icons and a taskbar
- Connects to WiFi on startup, and mounts the SD card if present
  (status shown as dots in the taskbar)
- A tiny window manager (one window open at a time, with close /
  minimize buttons in the KolibriOS style, top-left of the title bar)
- **Dark, flat theme**: near-black desktop/taskbar/windows, light text,
  and one accent color for hover and focus states (plus a muted red
  reserved just for the close button) instead of a busier multi-color
  look — see palette constants (`COL_*`) near the top of the file if
  you want to retheme it
- A mouse **cursor drawn on screen**, moved with keys instead of a
  physical mouse — movement is continuous while a key is held (see
  "Why it feels smooth" below), not one step per keypress
- Six demo apps: **About**, **Notes** (text editor with SD save/load),
  **Calculator**, **AI Chat** (Claude or free Groq, switchable — see
  "The free AI option" below), **Browser** (fetches a URL and shows
  its text content), and **Files** (browse/open/delete files on the
  SD card)

### The Browser app

This is a **text-mode page reader**, not a graphical web browser: it
fetches a URL, strips out HTML tags plus the contents of `<script>`
and `<style>` blocks, and shows the remaining plain text, word-wrapped,
with on-screen ▲/▼ buttons (click with the cursor) to scroll. A real
graphical renderer — laying out CSS, images, and JavaScript-driven
pages — is well beyond what a 240×135 screen and an ESP32-S3 can
usefully display, so this focuses on making page *text* readable,
similar in spirit to old text-mode browsers like Lynx. It works well
on plain articles/docs; heavily JS-driven sites (most modern web apps)
will show little useful text since their content isn't in the raw
HTML at all.

To keep memory use bounded, it only reads the first ~6KB of a page's
raw HTML and shows up to ~1500 characters of the resulting text —
enough for a paragraph or two of an article, not a whole long page.

## Why it feels smooth

Two changes make the cursor and typing feel responsive instead of
steppy or laggy:

1. **Continuous cursor movement.** The keyboard's currently-held keys
   are read every loop iteration (not just on press/release), so
   holding an arrow-substitute key glides the cursor continuously.
   Earlier drafts only checked on the press/release "change" event,
   which moved the cursor once per keypress rather than while held.
   One-shot actions (typing a character, an Enter-click, Tab) are
   still tied to that change event, so holding Enter doesn't spam
   clicks and holding a letter doesn't spam-type.
2. **Tighter frame timing.** The main loop runs on a shorter `delay(15)`
   or a faster, steadier loop; `CURSOR_SPEED` was tuned down to match
   so on-screen speed feels about the same, just smoother.

## The "free AI" option (and an honest caveat)

The AI Chat app now supports two providers, toggled with a small
button in the window ("Claude" / "Free AI"):

- **Free AI** uses **Groq's API** (`llama-3.3-70b-versatile` by
  default). This is the closest real match to "free, unlimited, very
  smart" — but that combination doesn't actually exist anywhere, and
  it's worth being upfront about why: frontier-quality models cost
  real money to run, so every provider either charges per token, caps
  a free tier with real rate limits, or gives you a smaller/weaker
  free model. Groq's free tier is genuinely free (no credit card,
  renews rather than being a one-time trial credit), runs on Groq's
  custom LPU hardware so responses come back fast, and Llama 3.3 70B
  is a genuinely strong open-weight model — but it's rate-limited, not
  unlimited: roughly 30 requests/minute and on the order of
  14,400 requests/day for 70B-class models, and Groq can change these
  numbers. Get a key and check current limits at
  [console.groq.com](https://console.groq.com/). If you hit the cap,
  the app shows a "(rate limited...)" message rather than hanging.
- **Claude** remains available if you already have Anthropic API
  credit and prefer its answers — same setup as before.

Fill in `GROQ_API_KEY` alongside the other credentials near the top
of `WrenOS.ino`. The default provider is Groq (the free one); switch
apps remember whichever you last selected until you toggle again or
reboot.

## Before you flash: fill in your credentials

Near the top of `WrenOS.ino`:

```cpp
static const char* WIFI_SSID         = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD     = "YOUR_WIFI_PASSWORD";
static const char* ANTHROPIC_API_KEY = "YOUR_ANTHROPIC_API_KEY"; // sk-ant-...
static const char* GROQ_API_KEY      = "YOUR_GROQ_API_KEY";      // console.groq.com
```

Get a Claude key from the [Anthropic Console](https://console.anthropic.com/)
and a Groq key from [console.groq.com](https://console.groq.com/)
(the Groq one is free, no card needed).
**Treat this file as a secret once you've filled it in** — anyone with
the compiled firmware (or the source) can extract the key and rack up
API usage on your account. Don't commit a filled-in copy to a public
repo. For anything beyond personal experimenting, consider routing the
Cardputer through a small proxy server you control that holds the real
key instead of embedding it in firmware.

### A note on the AI Chat app's networking

The sketch uses `WiFiClientSecure::setInsecure()`, which skips TLS
certificate validation. This is the standard shortcut in ESP32 hobby
projects (pinning/updating a CA cert on a microcontroller is a hassle),
but it means the HTTPS connection isn't protected against a
man-in-the-middle on the network path. Fine on a trusted home WiFi
network for experimenting; if you want it hardened, replace
`setInsecure()` with `client.setCACert(...)` using Anthropic's current
root CA.

## Controls

The Cardputer has no dedicated arrow keys, so this sketch uses the
four keys in the bottom-right cluster as arrows (the same convention
most Cardputer homebrew and M5Stack's own examples use):

| Key | Action  |
|-----|---------|
| `;` | Up      |
| `.` | Down    |
| `,` | Left    |
| `/` | Right   |
| `Enter` | Click (left mouse button) |
| `Tab` | Inside a text field (Notes/Chat/Browser): toggle between "typing" and "moving the mouse" |
| `Del` (Backspace) | Inside a text field: delete last character |

While a text field has keyboard focus (after you click inside it),
plain typing goes into that field. Hold **Fn** together with
`; , . /` to move the mouse cursor without it being typed as
punctuation. Press **Tab** to swap modes explicitly.

## Requirements

Install via the Arduino IDE Library Manager, with board manager
`M5Stack` >= 3.2.2 and board set to **M5Cardputer**:

- `M5Unified` >= 0.2.8 (use the latest release for best V1.1/StampS3A support)
- `M5GFX` >= 0.2.10
- `M5Cardputer` >= 1.1.0
- `ArduinoJson` >= 6.21 (used for the AI Chat request/response and the Browser app's HTTP work)
- `SD` and `SPI` — bundled with the ESP32 Arduino core, no separate install needed

## Build & flash

**Option A — Arduino IDE (GUI, simplest):**
1. Open `WrenOS.ino` in the Arduino IDE.
2. Tools → Board → select **M5Cardputer**.
3. Tools → PSRAM → **OPI PSRAM** (for the V1.1's 8MB PSRAM).
4. Select the correct serial port.
5. Click Upload — or **Sketch → Export Compiled Binary** if you just
   want the `.bin` without flashing immediately (it lands in the
   sketch folder, under `build/<fqbn>/`).

**Option B — `arduino-cli` (scriptable, produces a `.bin` directly):**
Run `build.sh` (included alongside this file) on a machine with normal
internet access — it installs `arduino-cli`, the M5Stack board
package, and all four required libraries, then compiles with the
correct board/PSRAM settings and exports the `.bin` to `./build/`.
It can't be run inside a sandboxed assistant session like the one that
produced this sketch, since that environment's network access is
locked to an allowlist that doesn't include Arduino's or Espressif's
download servers — hence a script you run yourself rather than a
binary handed to you directly.

## Extending it

- Add more apps by adding an entry to `appNames[]`, bumping
  `APP_COUNT`, and adding a `drawAppXxx()` / handling case in
  `drawWindow()` / `handleWindowClick()`.
- The calculator's evaluator is intentionally simple (strict
  left-to-right, no operator precedence) — swap in a real expression
  parser if you want proper precedence.
- Cursor speed is controlled by `CURSOR_SPEED` near the top of the
  file.
- The chat app keeps no conversation history for either provider —
  every "Send" is a fresh, independent request. To make it a real
  back-and-forth conversation, accumulate a `messages`/`contents`
  array (alternating turns) instead of sending a single message each
  time.
- `CLAUDE_MODEL` and `GROQ_MODEL` near the top select each provider's
  model; swap either for a different model string if you want.
- The Browser app's `stripHtml()` is a simple tag-stripper, not a
  real HTML parser — it won't handle malformed markup gracefully.
  `RAW_CAP` in `fetchWebsite()` and the 1500-character cap in
  `stripHtml()` control how much of a page it reads/shows; raise them
  if you've enabled PSRAM and want to read more per page.
- The Files app lists files in a flat root directory (`MAX_FILES = 20`
  cap) rather than supporting subfolders — bump `MAX_FILES` if you
  need more, or add directory navigation if you want folders.
- `loadFileIntoNotes()` caps how much of a file it reads (`CAP = 3000`
  characters) for the same memory-safety reason as the Browser app;
  raise it if you've enabled PSRAM.
- `drawLogo(cx, cy, size)` is the whole logo — resize/recolor it via
  its own code or the `COL_*` palette constants; there's no separate
  image asset to swap.
