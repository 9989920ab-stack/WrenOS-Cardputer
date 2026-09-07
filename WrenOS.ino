/*
  WrenOS
  ------
  A small KolibriOS-inspired desktop shell for the M5Stack Cardputer.
  Named after the wren -- a small, common bird -- as a nod to
  KolibriOS's own name (Russian for "hummingbird") without borrowing
  it outright, since this is an original, much smaller project built
  on top of it only in spirit.

  Targets Cardputer V1.1 (StampS3A controller, 8MB PSRAM) but also runs
  on the original Cardputer -- the M5Unified/M5GFX/M5Cardputer stack
  auto-detects which board it's on, so the same sketch covers both as
  long as your library versions are recent (see REQUIREMENTS).

  This is NOT KolibriOS itself -- KolibriOS is a real x86 operating
  system and the Cardputer's ESP32-S3 (Xtensa) CPU cannot run x86 code,
  so a true port is not possible. This sketch instead borrows
  KolibriOS's look and feel (flat colorful desktop, top-left window
  buttons, mouse-driven UI) and implements it as an Arduino program on
  top of M5Unified / M5GFX / M5Cardputer.

  FEATURES
  --------
  - Boots into a desktop with a taskbar and clickable app icons
  - A window manager: one "window" open at a time, with a KolibriOS-
    style title bar (colored buttons top-left: close / minimize)
  - A mouse cursor drawn on screen, moved with keys since the Cardputer
    has no physical arrow keys. Cursor movement is continuous (checked
    every frame while a key is held) so it glides instead of stepping
    once per keypress -- see SMOOTHNESS NOTES below.
    The four keys in the bottom-right of the keyboard are used as
    arrows (the same convention M5Stack's own examples and most
    Cardputer homebrew use):
        ;  = Up
        .  = Down
        ,  = Left
        /  = Right
    Hold Fn while pressing them if a text field currently has focus
    (so ; , . / can still be typed as normal punctuation); otherwise
    they move the cursor directly.
  - Enter is the mouse "click".
  - Dark, flat theme: a near-black desktop/taskbar/window background,
    light text, and a single accent color for hover/focus states
    (plus muted red for the close button) rather than a busier
    multi-color button scheme.
  - A drawn logo (a small vector mark, since there's no image file to
    embed) shown on the boot splash and in the About app, plus a
    consistent loading screen used for every blocking operation
    (connecting WiFi, calling Claude, fetching a page) instead of
    each one improvising its own "please wait" message.
  - A battery percentage readout in the taskbar's bottom-left corner.
  - Six demo apps: About, Notes (text editor with SD save/load),
    Calculator, AI Chat (sends a prompt to Anthropic's Claude API over
    WiFi), Browser (fetches a URL and shows its text content -- a
    simple text-mode page reader, not a graphical HTML/CSS renderer;
    see the Browser app section below for why), and Files (browse,
    open, and delete files on the SD card).

  SMOOTHNESS NOTES
  -----------------
  Two things were changed from earlier drafts specifically to make
  input feel smooth instead of steppy/laggy:
  1. Cursor movement is now read from the keyboard's *current* held
     state every loop iteration, rather than only reacting to
     isChange() edges. isChange() only fires when the set of pressed
     keys changes (press or release), so if movement were driven off
     it alone, holding an arrow key down would move the cursor once
     and then sit still until you released and re-pressed. One-shot
     actions (typing a character, Enter-click, Tab) are still
     edge-triggered off isChange(), so holding Enter doesn't spam
     clicks and holding a letter doesn't spam-type.
  2. The main loop delay was tightened (see `delay(15)` near the
     bottom of loop()) for a higher, steadier frame rate; CURSOR_SPEED
     was reduced to match so on-screen motion speed stays about the
     same, just smoother.

  REQUIREMENTS (Arduino IDE Library Manager)
  -------------------------------------------
  - Board:   M5Cardputer (via M5Stack board manager >= 3.2.2)
  - M5Unified   >= 0.2.8   (use the latest release for best V1.1 support)
  - M5GFX       >= 0.2.10
  - M5Cardputer >= 1.1.0
  - ArduinoJson >= 6.21 (for the AI Chat / Browser apps' JSON and HTTP work)

  For Cardputer V1.1 specifically: it ships with 8MB of PSRAM. Enable
  it in Arduino IDE under Tools > PSRAM > "OPI PSRAM" before flashing.
  This isn't strictly required for this sketch's default buffer sizes,
  but it gives real headroom if you raise the Browser app's page-size
  cap or otherwise expand it, and avoids heap-fragmentation crashes
  under sustained WiFi use.

  Before flashing, fill in WIFI_SSID / WIFI_PASSWORD / ANTHROPIC_API_KEY /
  GROQ_API_KEY below. Just open this .ino in Arduino IDE with the
  M5Cardputer board selected, install the libraries above, and flash.

  SD CARD
  -------
  Notes and the Files app use the Cardputer's microSD slot for real,
  persistent storage -- previously everything in this sketch lived
  only in RAM and vanished on reboot. Uses the pins/init sequence from
  M5Stack's own Cardputer microSD documentation:
  https://docs.m5stack.com/en/arduino/m5cardputer/sdcard
  Insert a FAT32-formatted card with contacts facing away from the
  screen before powering on. If no card is present, Notes/Files still
  work as in-memory-only (a grey SD dot in the taskbar, plus a message
  in the Files app, tells you storage isn't available).

  AI CHAT: TWO PROVIDERS
  -----------------------
  The AI Chat app can call either Anthropic's Claude API or Groq's
  free API -- click the small provider button to switch. Neither
  provider is free-and-unlimited-and-maximally-smart all at once
  (nothing genuinely is; that's a real three-way trade-off, not a
  detail this sketch works around). Groq's free tier is the closer
  fit for "free": no credit card required, it renews rather than
  being a one-time trial credit, it runs on Groq's custom LPU
  hardware so responses come back fast, and Llama 3.3 70B (the
  default model here) is a genuinely strong open-weight model. It is
  still rate-limited, not unlimited -- roughly 30 requests/minute and
  on the order of 14,400 requests/day for 70B-class models, and these
  numbers are Groq's to change. Get a key and check current limits at
  https://console.groq.com/. Claude remains the other option if you
  already have Anthropic API credit and prefer its answers.
*/

#include "M5Cardputer.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>

// ---------------------------------------------------------------------
// SD card (see docs.m5stack.com/en/arduino/m5cardputer/sdcard)
// ---------------------------------------------------------------------
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

bool sdReady = false;

// ---------------------------------------------------------------------
// Fill these in before flashing
// ---------------------------------------------------------------------
static const char* WIFI_SSID         = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD     = "YOUR_WIFI_PASSWORD";
static const char* ANTHROPIC_API_KEY = "YOUR_ANTHROPIC_API_KEY"; // sk-ant-...
static const char* CLAUDE_MODEL      = "claude-haiku-4-5-20251001";
static const char* GROQ_API_KEY    = "YOUR_GROQ_API_KEY";     // console.groq.com
static const char* GROQ_MODEL      = "llama-3.3-70b-versatile";

bool wifiConnected = false;

// ---------------------------------------------------------------------
// Display / canvas
// ---------------------------------------------------------------------
static M5Canvas canvas(&M5Cardputer.Display);

static int SCREEN_W = 240;
static int SCREEN_H = 135;

// Dark, flat palette. Only two accents are used on purpose (red for
// close, blue-grey for everything else that needs to stand out --
// hover states, focus borders, the minimize button) instead of the
// earlier red/yellow "candy button" look, for a simpler feel.
#define COL_DESKTOP   0x18C3   // near-black charcoal
#define COL_TASKBAR   0x2104   // dark slate, a touch lighter than desktop
#define COL_TASKBAR_TXT 0xBDF7 // soft light grey
#define COL_WIN_BG    0x2104   // same dark slate as taskbar -- unified panels
#define COL_WIN_TITLE 0x18C3   // same as desktop, so the title bar reads as
                                // a thin flat strip rather than a separate block
#define COL_WIN_TXT   0xDEFB   // near-white text
#define COL_BTN_CLOSE 0xB000   // muted red -- close, kept as the one semantic color
#define COL_ACCENT    0x3D5F   // single accent: hover / focus / minimize
#define COL_ICON_BG   0x2965   // dark blue-grey icon tiles
#define COL_ICON_TXT  0xDEFB
#define COL_CURSOR    0xDEFB
#define COL_CURSOR_OUTLINE 0x18C3
#define COL_BORDER    0x39C7   // subtle structural lines/borders on dark bg
#define COL_MUTED     0x8410   // secondary/status text, unfocused borders

// ---------------------------------------------------------------------
// Battery readout (bottom-left of the taskbar)
// ---------------------------------------------------------------------
// Reading the battery involves an I2C round-trip on most boards, so it's
// cached and only re-read periodically rather than every frame.
int cachedBatteryPct = -1;         // -1 = unknown / not reported
unsigned long lastBatteryReadMs = 0;
const unsigned long BATTERY_READ_INTERVAL_MS = 3000;

// ---------------------------------------------------------------------
// Mouse cursor state (driven by the "arrow" keys)
// ---------------------------------------------------------------------
struct Cursor {
  float x, y;
  bool clickEdge;   // true for exactly one frame when a click happens
} cursor = { SCREEN_W / 2.0f, SCREEN_H / 2.0f, false };

const float CURSOR_SPEED = 3.0f;  // tuned for the ~66fps loop (delay(15))

// ---------------------------------------------------------------------
// Simple rectangle hit-testing helper
// ---------------------------------------------------------------------
struct Rect {
  int x, y, w, h;
  bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
  }
};

// ---------------------------------------------------------------------
// Apps
// ---------------------------------------------------------------------
enum AppId { APP_NONE = -1, APP_ABOUT = 0, APP_NOTES = 1, APP_CALC = 2, APP_CHAT = 3, APP_BROWSER = 4, APP_FILES = 5, APP_COUNT = 6 };

const char* appNames[APP_COUNT] = { "About", "Notes", "Calc", "AI Chat", "Browser", "Files" };

int openApp = APP_NONE;   // which app's window is currently open
Rect winRect = { 20, 10, 200, 110 };
Rect closeBtn, minBtn;

// Desktop icon rectangles (computed in layoutIcons)
Rect iconRects[APP_COUNT];

// ---- Notes app state ----
String notesText = "";
bool notesFocused = false;   // when true, plain key presses type text
String notesFilename = "/notes.txt";  // which file Save/the Files app wrote here
Rect notesSaveBtn, notesNewBtn;

// ---- Calculator app state ----
String calcExpr = "";
String calcResult = "";
// A tiny 4x4 calculator keypad drawn inside the window, navigated with
// the same mouse cursor.
const char calcKeys[4][4] = {
  {'7','8','9','/'},
  {'4','5','6','*'},
  {'1','2','3','-'},
  {'C','0','=','+'}
};
Rect calcKeyRects[4][4];

// ---- AI Chat app state ----
enum ChatProvider { PROVIDER_CLAUDE = 0, PROVIDER_GROQ = 1 };
int chatProvider = PROVIDER_GROQ;  // defaults to the free option
String chatInput = "";
String chatResponse = "(ask me something, then click Send)";
bool chatFocused = false;
Rect chatSendBtn, chatProviderBtn;

// ---- Browser app state ----
// A simple *text-mode* page reader: it fetches a URL and strips HTML
// tags/scripts/styles down to plain text. It does not parse CSS, run
// JavaScript, or lay out images -- a real graphical renderer is well
// beyond what a 240x135 screen and an ESP32-S3 can usefully show, so
// this focuses on making page text readable instead.
String browserUrl = "example.com";
String browserContent = "(enter a URL and click Go)";
bool browserFocused = false;
int browserScrollLine = 0;
Rect browserGoBtn, browserScrollUpBtn, browserScrollDownBtn;

// ---- Files app state ----
// Lists plain files in the SD card's root directory. Click a file to
// load it into Notes for viewing/editing (Notes' Save button then
// writes back to that same file); click the "x" to delete it.
struct SDFileEntry { String name; size_t size; };
const int MAX_FILES = 20;
const int FILES_VISIBLE_ROWS = 5;
SDFileEntry sdFiles[MAX_FILES];
int sdFileCount = 0;
int filesScroll = 0;   // index of the first visible row
Rect filesRefreshBtn, filesScrollUpBtn, filesScrollDownBtn;
Rect filesRowOpenRects[FILES_VISIBLE_ROWS];
Rect filesRowDelRects[FILES_VISIBLE_ROWS];

// ---------------------------------------------------------------------
// Forward decls
// ---------------------------------------------------------------------
void drawDesktop();
void drawTaskbar();
void drawCursor();
void layoutIcons();
void handleDesktopClick();
void openWindow(int appId);
void closeWindow();
void drawWindow();
void drawAppAbout();
void drawAppNotes();
void drawAppCalc();
void drawAppChat();
void drawAppBrowser();
void drawAppFiles();
void handleWindowClick();
void handleCalcClick();
void handleFilesClick();
void evalCalc();
void drawWrappedText(const String& text, int x, int y, int w, int maxH, int lineH);
void drawWrappedTextScrolled(const String& text, int x, int y, int w, int maxH, int lineH, int scrollLines);
int browserMaxScrollLines();
void connectWiFi();
String sendToClaude(const String& prompt);
String sendToGroq(const String& prompt);
String fetchWebsite(String url);
String stripHtml(const String& html);
void initSD();
void refreshFileList();
String sdPath(const String& name);
void saveNotesToSD();
bool loadFileIntoNotes(const String& path);
void updateCursorFromKeys(const Keyboard_Class::KeysState& status, bool arrowUp, bool arrowDown, bool arrowLeft, bool arrowRight);
void drawLogo(int cx, int cy, int size);
void showLoadingScreen(const String& message);
void updateBatteryReading();

// ---------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // true = enable keyboard
  M5Cardputer.Display.setRotation(1);

  SCREEN_W = M5Cardputer.Display.width();
  SCREEN_H = M5Cardputer.Display.height();
  cursor.x = SCREEN_W / 2.0f;
  cursor.y = SCREEN_H / 2.0f;

  canvas.setColorDepth(8);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  canvas.setTextSize(1);

  layoutIcons();

  // Boot splash: logo + name + tagline
  canvas.fillSprite(COL_DESKTOP);
  drawLogo(SCREEN_W / 2, 36, 30);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_WIN_TXT, COL_DESKTOP);
  canvas.drawString("WrenOS", SCREEN_W / 2, 60);
  canvas.setTextColor(COL_MUTED, COL_DESKTOP);
  canvas.drawString("a tiny shell for Cardputer", SCREEN_W / 2, 74);
  canvas.setTextDatum(top_left);
  canvas.pushSprite(0, 0);
  delay(1400);

  initSD();
  connectWiFi();
}

void loop() {
  M5Cardputer.update();
  cursor.clickEdge = false;
  updateBatteryReading();

  // Read the keyboard's *current* held state every frame (not just on
  // change) so cursor movement is continuous/smooth while a key is
  // held down, rather than a single step per keypress.
  bool kbPressed = M5Cardputer.Keyboard.isPressed();
  Keyboard_Class::KeysState status;
  if (kbPressed) {
    status = M5Cardputer.Keyboard.keysState();
  }

  if (kbPressed) {
    // ---- Arrow substitute keys: ; , . / (with or without Fn) ----
    bool up = false, down = false, left = false, right = false;
    for (auto c : status.word) {
      if (c == ';') up = true;
      else if (c == '.') down = true;
      else if (c == ',') left = true;
      else if (c == '/') right = true;
    }
    updateCursorFromKeys(status, up, down, left, right);
  }

  // ---- One-shot actions: only on the press/release edge, so holding
  // Enter doesn't spam clicks and holding a letter doesn't spam-type.
  if (M5Cardputer.Keyboard.isChange() && kbPressed) {
    // ---- Click: Enter acts as the mouse button ----
    if (status.enter) {
      cursor.clickEdge = true;
    }

    // ---- Text typing, only when a text field is focused ----
    if (openApp == APP_NOTES && notesFocused) {
      for (auto c : status.word) {
        // skip the arrow-substitute chars only when Fn is held,
        // so plain punctuation still works while typing normally
        if (status.fn && (c == ';' || c == ',' || c == '.' || c == '/')) continue;
        notesText += c;
      }
      if (status.del && notesText.length() > 0) {
        notesText.remove(notesText.length() - 1);
      }
    } else if (openApp == APP_CHAT && chatFocused) {
      for (auto c : status.word) {
        if (status.fn && (c == ';' || c == ',' || c == '.' || c == '/')) continue;
        chatInput += c;
      }
      if (status.del && chatInput.length() > 0) {
        chatInput.remove(chatInput.length() - 1);
      }
    } else if (openApp == APP_BROWSER && browserFocused) {
      for (auto c : status.word) {
        if (status.fn && (c == ';' || c == ',' || c == '.' || c == '/')) continue;
        browserUrl += c;
      }
      if (status.del && browserUrl.length() > 0) {
        browserUrl.remove(browserUrl.length() - 1);
      }
    }

    // Tab toggles focus between "desktop navigation" and typing
    // inside a text field, so arrow-substitute keys can still move
    // the cursor even while an app is open.
    if (status.tab) {
      if (openApp == APP_NOTES) notesFocused = !notesFocused;
      else if (openApp == APP_CHAT) chatFocused = !chatFocused;
      else if (openApp == APP_BROWSER) browserFocused = !browserFocused;
    }
  }

  // Clamp cursor to screen
  if (cursor.x < 0) cursor.x = 0;
  if (cursor.y < 0) cursor.y = 0;
  if (cursor.x > SCREEN_W - 1) cursor.x = SCREEN_W - 1;
  if (cursor.y > SCREEN_H - 1) cursor.y = SCREEN_H - 1;

  // ---- Click handling ----
  if (cursor.clickEdge) {
    if (openApp == APP_NONE) {
      handleDesktopClick();
    } else {
      handleWindowClick();
    }
  }

  // ---- Draw ----
  canvas.fillSprite(COL_DESKTOP);
  drawDesktop();
  if (openApp != APP_NONE) drawWindow();
  drawTaskbar();
  drawCursor();
  canvas.pushSprite(0, 0);

  delay(15);
}

// ---------------------------------------------------------------------
// Cursor movement
// ---------------------------------------------------------------------
void updateCursorFromKeys(const Keyboard_Class::KeysState& status,
                           bool arrowUp, bool arrowDown, bool arrowLeft, bool arrowRight) {
  // If a text field currently has keyboard focus, don't also move the
  // mouse on every keystroke -- only move when Fn is held, so ; , . /
  // can still be typed as normal punctuation.
  bool typing = (openApp == APP_NOTES && notesFocused) || (openApp == APP_CHAT && chatFocused)
                || (openApp == APP_BROWSER && browserFocused);
  bool allow = (!typing) || status.fn;
  if (!allow) return;

  if (arrowUp)    cursor.y -= CURSOR_SPEED;
  if (arrowDown)  cursor.y += CURSOR_SPEED;
  if (arrowLeft)  cursor.x -= CURSOR_SPEED;
  if (arrowRight) cursor.x += CURSOR_SPEED;
}

// ---------------------------------------------------------------------
// Desktop
// ---------------------------------------------------------------------
void layoutIcons() {
  int iw = 32, ih = 34, gap = 4, startX = 4, startY = 10;
  for (int i = 0; i < APP_COUNT; i++) {
    iconRects[i] = { startX + i * (iw + gap), startY, iw, ih };
  }
}

// ---------------------------------------------------------------------
// Logo
// ---------------------------------------------------------------------
// There's no image file to embed on a microcontroller sketch like this,
// so the "logo" is drawn as flat vector shapes: a rounded tile (echoing
// the window-manager theme) with a smaller accent-colored square inset.
// Centered at (cx, cy); `size` is the outer tile's width/height in px.
void drawLogo(int cx, int cy, int size) {
  int half = size / 2;
  canvas.fillRoundRect(cx - half, cy - half, size, size, size / 5, COL_ICON_BG);
  canvas.drawRoundRect(cx - half, cy - half, size, size, size / 5, COL_BORDER);
  int inner = (int)(size * 0.45f);
  canvas.fillRoundRect(cx - inner / 2, cy - inner / 2, inner, inner, 2, COL_ACCENT);
}

// ---------------------------------------------------------------------
// Loading screen
// ---------------------------------------------------------------------
// A single consistent "please wait" screen used before every blocking
// operation (WiFi connect, a Claude API call, fetching a web page).
// Since the whole sketch is single-threaded, this draws one static
// frame right before the blocking call starts -- it can't animate
// *during* the wait, but the trailing dots do change frame-to-frame
// across repeated calls, and it at least replaces what used to be a
// handful of separately-worded ad hoc messages with one consistent look.
void showLoadingScreen(const String& message) {
  canvas.fillSprite(COL_DESKTOP);
  drawLogo(SCREEN_W / 2, 32, 20);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_WIN_TXT, COL_DESKTOP);
  canvas.drawString(message, SCREEN_W / 2, 54);

  int dotCount = (millis() / 300) % 4;
  String dots = "";
  for (int i = 0; i < dotCount; i++) dots += ".";
  canvas.setTextColor(COL_MUTED, COL_DESKTOP);
  canvas.drawString(dots, SCREEN_W / 2, 68);

  canvas.setTextDatum(top_left);
  canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------
// Cached and refreshed periodically rather than every frame, since
// reading it is a small I2C round-trip on most boards. -1 means the
// board/library combination isn't reporting a level.
void updateBatteryReading() {
  if (millis() - lastBatteryReadMs < BATTERY_READ_INTERVAL_MS && lastBatteryReadMs != 0) return;
  cachedBatteryPct = M5.Power.getBatteryLevel();
  lastBatteryReadMs = millis();
}

void drawDesktop() {
  canvas.setTextSize(1);
  for (int i = 0; i < APP_COUNT; i++) {
    Rect r = iconRects[i];
    canvas.fillRoundRect(r.x, r.y, r.w, r.h, 4, COL_ICON_BG);
    canvas.drawRoundRect(r.x, r.y, r.w, r.h, 4, COL_BORDER);
    canvas.setTextColor(COL_ICON_TXT, COL_ICON_BG);
    canvas.setTextDatum(top_center);
    canvas.drawString(appNames[i], r.x + r.w / 2, r.y + r.h / 2 - 4);
  }
  canvas.setTextDatum(top_left);
}

void handleDesktopClick() {
  for (int i = 0; i < APP_COUNT; i++) {
    if (iconRects[i].contains(cursor.x, cursor.y)) {
      openWindow(i);
      return;
    }
  }
}

// ---------------------------------------------------------------------
// Window manager
// ---------------------------------------------------------------------
void openWindow(int appId) {
  openApp = appId;
  notesFocused = false;
  chatFocused = false;
  browserFocused = false;
  calcExpr = "";
  calcResult = "";
  winRect = { 20, 8, SCREEN_W - 40, SCREEN_H - 30 };
  closeBtn = { winRect.x + 4, winRect.y + 3, 10, 10 };
  minBtn   = { winRect.x + 18, winRect.y + 3, 10, 10 };
  chatSendBtn = { winRect.x + winRect.w - 46, winRect.y + winRect.h - 16, 42, 12 };
}

void closeWindow() {
  openApp = APP_NONE;
  notesFocused = false;
}

void drawWindow() {
  Rect r = winRect;
  canvas.fillRect(r.x, r.y, r.w, r.h, COL_WIN_BG);
  canvas.drawRect(r.x, r.y, r.w, r.h, COL_BORDER);

  // Title bar
  canvas.fillRect(r.x, r.y, r.w, 20, COL_WIN_TITLE);
  canvas.drawFastHLine(r.x, r.y + 20, r.w, COL_BORDER);

  // KolibriOS-style title-bar buttons (top-left)
  canvas.fillRoundRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, 2, COL_BTN_CLOSE);
  canvas.fillRoundRect(minBtn.x, minBtn.y, minBtn.w, minBtn.h, 2, COL_ACCENT);

  canvas.setTextColor(COL_WIN_TXT, COL_WIN_TITLE);
  canvas.setTextDatum(top_center);
  canvas.drawString(appNames[openApp], r.x + r.w / 2, r.y + 4);
  canvas.setTextDatum(top_left);

  // App content area
  switch (openApp) {
    case APP_ABOUT: drawAppAbout(); break;
    case APP_NOTES: drawAppNotes(); break;
    case APP_CALC:  drawAppCalc();  break;
    case APP_CHAT:  drawAppChat();  break;
    case APP_BROWSER: drawAppBrowser(); break;
    case APP_FILES: drawAppFiles(); break;
  }
}

void handleWindowClick() {
  if (closeBtn.contains(cursor.x, cursor.y)) {
    closeWindow();
    return;
  }
  if (minBtn.contains(cursor.x, cursor.y)) {
    // "Minimize" just returns to desktop without resetting app state
    openApp = APP_NONE;
    return;
  }

  if (openApp == APP_NOTES) {
    if (notesSaveBtn.contains(cursor.x, cursor.y)) {
      saveNotesToSD();
    } else if (notesNewBtn.contains(cursor.x, cursor.y)) {
      notesText = "";
      notesFilename = "/notes.txt";
    } else {
      // Click inside the text area focuses it for typing
      Rect textArea = { winRect.x + 4, winRect.y + 24, winRect.w - 8, winRect.h - 46 };
      if (textArea.contains(cursor.x, cursor.y)) {
        notesFocused = true;
      }
    }
  } else if (openApp == APP_CALC) {
    handleCalcClick();
  } else if (openApp == APP_CHAT) {
    Rect textArea = { winRect.x + 4, winRect.y + 40, winRect.w - 8, 18 };
    if (chatProviderBtn.contains(cursor.x, cursor.y)) {
      chatProvider = (chatProvider == PROVIDER_CLAUDE) ? PROVIDER_GROQ : PROVIDER_CLAUDE;
    } else if (chatSendBtn.contains(cursor.x, cursor.y)) {
      if (chatInput.length() > 0) {
        showLoadingScreen(chatProvider == PROVIDER_CLAUDE ? "Asking Claude" : "Asking free AI");
        chatResponse = (chatProvider == PROVIDER_CLAUDE) ? sendToClaude(chatInput) : sendToGroq(chatInput);
        chatInput = "";
        chatFocused = false;
      }
    } else if (textArea.contains(cursor.x, cursor.y)) {
      chatFocused = true;
    }
  } else if (openApp == APP_BROWSER) {
    Rect urlBox = { winRect.x + 4, winRect.y + 24, winRect.w - 8 - 46, 16 };
    if (browserGoBtn.contains(cursor.x, cursor.y)) {
      if (browserUrl.length() > 0) {
        showLoadingScreen("Loading page");
        browserContent = fetchWebsite(browserUrl);
        browserScrollLine = 0;
        browserFocused = false;
      }
    } else if (urlBox.contains(cursor.x, cursor.y)) {
      browserFocused = true;
    } else if (browserScrollUpBtn.contains(cursor.x, cursor.y)) {
      if (browserScrollLine > 0) browserScrollLine--;
    } else if (browserScrollDownBtn.contains(cursor.x, cursor.y)) {
      int maxS = browserMaxScrollLines();
      if (browserScrollLine < maxS) browserScrollLine++;
    }
  } else if (openApp == APP_FILES) {
    handleFilesClick();
  }
}

// ---------------------------------------------------------------------
// About app
// ---------------------------------------------------------------------
void drawAppAbout() {
  Rect r = winRect;
  drawLogo(r.x + 18, r.y + 34, 22);
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);
  canvas.drawString("WrenOS v0.4", r.x + 34, r.y + 26);
  canvas.setTextColor(COL_MUTED, COL_WIN_BG);
  canvas.drawString("KolibriOS-styled shell", r.x + 34, r.y + 40);
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);
  canvas.drawString("for M5 Cardputer.", r.x + 6, r.y + 56);
  canvas.drawString("Arrows: ; , . /", r.x + 6, r.y + 72);
  canvas.drawString("Click: Enter", r.x + 6, r.y + 84);
}

// ---------------------------------------------------------------------
// Notes app
// ---------------------------------------------------------------------
void drawAppNotes() {
  Rect r = winRect;
  Rect textArea = { r.x + 4, r.y + 24, r.w - 8, r.h - 46 };
  canvas.drawRect(textArea.x, textArea.y, textArea.w, textArea.h,
                   notesFocused ? COL_ACCENT : COL_MUTED);
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);

  // naive word-wrap draw of notesText inside textArea
  int lineH = 10;
  int maxCharsPerLine = textArea.w / 6;
  int cx = textArea.x + 2, cy = textArea.y + 2;
  String line = "";
  for (unsigned int i = 0; i < notesText.length(); i++) {
    line += notesText[i];
    if (line.length() >= (unsigned)maxCharsPerLine || notesText[i] == '\n') {
      canvas.drawString(line, cx, cy);
      cy += lineH;
      line = "";
    }
  }
  if (line.length() > 0) canvas.drawString(line, cx, cy);

  // Save / New buttons (bottom-right, above the status line)
  notesSaveBtn = { r.x + r.w - 4 - 28, r.y + r.h - 24, 28, 12 };
  notesNewBtn  = { notesSaveBtn.x - 32, notesSaveBtn.y, 28, 12 };
  bool hoverSave = notesSaveBtn.contains(cursor.x, cursor.y);
  bool hoverNew  = notesNewBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(notesNewBtn.x, notesNewBtn.y, notesNewBtn.w, notesNewBtn.h, 2,
                        hoverNew ? COL_ACCENT : COL_ICON_BG);
  canvas.fillRoundRect(notesSaveBtn.x, notesSaveBtn.y, notesSaveBtn.w, notesSaveBtn.h, 2,
                        hoverSave ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_ICON_TXT, hoverNew ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString("New", notesNewBtn.x + notesNewBtn.w / 2, notesNewBtn.y + 2);
  canvas.setTextColor(COL_ICON_TXT, hoverSave ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString("Save", notesSaveBtn.x + notesSaveBtn.w / 2, notesSaveBtn.y + 2);
  canvas.setTextDatum(top_left);

  canvas.setTextColor(COL_MUTED, COL_WIN_BG);
  String status = (notesFocused ? "Typing... " : "Click to type ") +
                   (sdReady ? notesFilename : String("(no SD card)"));
  canvas.drawString(status, r.x + 4, r.y + r.h - 10);
}

// ---------------------------------------------------------------------
// AI Chat app (Anthropic Claude API over WiFi)
// ---------------------------------------------------------------------
void drawAppChat() {
  Rect r = winRect;

  // Provider toggle -- click to switch between Claude and free Groq
  chatProviderBtn = { r.x + 4, r.y + 24, 56, 12 };
  bool hoverProv = chatProviderBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(chatProviderBtn.x, chatProviderBtn.y, chatProviderBtn.w, chatProviderBtn.h, 2,
                        hoverProv ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_ICON_TXT, hoverProv ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString(chatProvider == PROVIDER_CLAUDE ? "Claude" : "Free AI",
                     chatProviderBtn.x + chatProviderBtn.w / 2, chatProviderBtn.y + 2);
  canvas.setTextDatum(top_left);

  Rect textArea = { r.x + 4, r.y + 40, r.w - 8, 18 };
  canvas.drawRect(textArea.x, textArea.y, textArea.w, textArea.h,
                   chatFocused ? COL_ACCENT : COL_MUTED);
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);
  canvas.drawString(chatInput, textArea.x + 2, textArea.y + 2);

  // Response area, wrapped, between the input box and the bottom status line
  int respY = textArea.y + textArea.h + 4;
  int respH = (r.y + r.h - 12) - respY;
  drawWrappedText(chatResponse, r.x + 4, respY, r.w - 8, respH, 10);

  // Send button
  bool hoverSend = chatSendBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(chatSendBtn.x, chatSendBtn.y, chatSendBtn.w, chatSendBtn.h, 2,
                        hoverSend ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextColor(COL_ICON_TXT, hoverSend ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.drawString("Send", chatSendBtn.x + chatSendBtn.w / 2, chatSendBtn.y + 2);
  canvas.setTextDatum(top_left);

  canvas.setTextColor(COL_MUTED, COL_WIN_BG);
  canvas.drawString(chatFocused ? "Typing... Tab=mouse" : "Click box to type",
                     r.x + 4, r.y + r.h - 10);
}

// Naive char-based word wrap into a bounded box, capped to the space
// available (older lines beyond maxH are simply not drawn).
void drawWrappedText(const String& text, int x, int y, int w, int maxH, int lineH) {
  int maxCharsPerLine = w / 6;
  if (maxCharsPerLine < 1) maxCharsPerLine = 1;
  int maxLines = maxH / lineH;
  if (maxLines < 1) maxLines = 1;

  int cy = y;
  int pos = 0;
  int lines = 0;
  while (pos < (int)text.length() && lines < maxLines) {
    int len = min(maxCharsPerLine, (int)text.length() - pos);
    canvas.drawString(text.substring(pos, pos + len), x, cy);
    pos += len;
    cy += lineH;
    lines++;
  }
}

// Connects to WiFi using the credentials near the top of the file.
// Shows status on screen; times out after ~15s so the OS still boots
// (with WiFi features disabled) if the network is unreachable.
void connectWiFi() {
  showLoadingScreen("Connecting WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  canvas.fillSprite(COL_DESKTOP);
  drawLogo(SCREEN_W / 2, 32, 20);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_WIN_TXT, COL_DESKTOP);
  canvas.drawString(wifiConnected ? "WiFi connected" : "WiFi failed (check creds)", SCREEN_W / 2, 54);
  canvas.setTextDatum(top_left);
  canvas.pushSprite(0, 0);
  delay(800);
}

// ---------------------------------------------------------------------
// SD card storage (used by Notes and the Files app)
// ---------------------------------------------------------------------

// Brings up the SD card over SPI using the Cardputer's documented
// pins, and does an initial directory listing if it succeeds. Safe to
// call even with no card inserted -- sdReady just stays false, and
// Notes/Files fall back to in-memory-only behavior.
void initSD() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  sdReady = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
  if (sdReady) {
    refreshFileList();
  }
}

// Some ESP32 SD library versions return a bare filename from
// File::name() rather than a full path; this makes sure whatever we
// pass back into SD.open()/SD.remove() has a leading slash.
String sdPath(const String& name) {
  if (name.length() > 0 && name[0] == '/') return name;
  return "/" + name;
}

// Re-reads the SD card's root directory into sdFiles[]. Only plain
// files are listed (not subdirectories) -- this sketch keeps
// everything in one flat folder for simplicity.
void refreshFileList() {
  sdFileCount = 0;
  filesScroll = 0;
  if (!sdReady) return;

  File root = SD.open("/");
  if (!root) return;
  File f = root.openNextFile();
  while (f && sdFileCount < MAX_FILES) {
    if (!f.isDirectory()) {
      sdFiles[sdFileCount].name = String(f.name());
      sdFiles[sdFileCount].size = f.size();
      sdFileCount++;
    }
    f = root.openNextFile();
  }
  root.close();
}

// Writes the Notes buffer to notesFilename on the SD card (overwriting
// it), then refreshes the Files app's listing so a newly created file
// shows up next time it's opened.
void saveNotesToSD() {
  if (!sdReady) return;
  File f = SD.open(notesFilename, FILE_WRITE);
  if (!f) return;
  f.print(notesText);
  f.close();
  refreshFileList();
}

// Reads `path` from the SD card into the Notes buffer (bounded to
// protect memory) and remembers it as the current file, so Notes'
// Save button writes back to the same place.
bool loadFileIntoNotes(const String& path) {
  if (!sdReady) return false;
  File f = SD.open(path);
  if (!f) return false;

  const size_t CAP = 3000;
  String content;
  content.reserve(min(CAP, (size_t)f.size()));
  while (f.available() && content.length() < CAP) {
    content += (char)f.read();
  }
  f.close();

  notesText = content;
  notesFilename = path;
  return true;
}

// Sends `prompt` to the Anthropic Messages API and returns the reply
// text (or a short error string starting with "(" on failure).
//
// NOTE: this uses WiFiClientSecure::setInsecure(), which skips TLS
// certificate validation. That's the common shortcut in ESP32 hobby
// sketches because storing/updating a root CA bundle on a
// microcontroller is a hassle, but it does mean the connection is not
// protected against a man-in-the-middle on the network path. Fine for
// experimenting on a trusted WiFi network; swap in a pinned root
// certificate (client.setCACert(...)) before relying on this for
// anything sensitive.
String sendToClaude(const String& prompt) {
  if (WiFi.status() != WL_CONNECTED) {
    return "(WiFi not connected)";
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, "https://api.anthropic.com/v1/messages")) {
    return "(HTTPS begin failed)";
  }
  https.addHeader("content-type", "application/json");
  https.addHeader("x-api-key", ANTHROPIC_API_KEY);
  https.addHeader("anthropic-version", "2023-06-01");

  StaticJsonDocument<512> reqDoc;
  reqDoc["model"] = CLAUDE_MODEL;
  reqDoc["max_tokens"] = 200;
  JsonArray messages = reqDoc.createNestedArray("messages");
  JsonObject msg = messages.createNestedObject();
  msg["role"] = "user";
  msg["content"] = prompt;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  int code = https.POST(reqBody);
  String result;
  if (code == 200) {
    String payload = https.getString();
    DynamicJsonDocument resDoc(4096);
    DeserializationError err = deserializeJson(resDoc, payload);
    if (!err) {
      const char* text = resDoc["content"][0]["text"] | "(no text in response)";
      result = String(text);
    } else {
      result = "(JSON parse error)";
    }
  } else {
    result = "(HTTP error " + String(code) + ")";
  }
  https.end();
  return result;
}

// Sends `prompt` to Groq's OpenAI-compatible chat completions API
// (the "free AI" option) and returns the reply text, or a short
// "(...)" error string. Free tier, no credit card required as of when
// this was written -- but "free" here means real, Groq-set rate
// limits (roughly 30 requests/min, on the order of 14,400/day for
// 70B-class models), not unlimited. Check current numbers at
// https://console.groq.com/ if you're relying on this for anything
// beyond casual use.
String sendToGroq(const String& prompt) {
  if (WiFi.status() != WL_CONNECTED) {
    return "(WiFi not connected)";
  }

  WiFiClientSecure client;
  client.setInsecure();  // see README: skips TLS cert validation

  HTTPClient https;
  if (!https.begin(client, "https://api.groq.com/openai/v1/chat/completions")) {
    return "(HTTPS begin failed)";
  }
  https.addHeader("content-type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  StaticJsonDocument<512> reqDoc;
  reqDoc["model"] = GROQ_MODEL;
  reqDoc["max_tokens"] = 300;
  JsonArray messages = reqDoc.createNestedArray("messages");
  JsonObject msg = messages.createNestedObject();
  msg["role"] = "user";
  msg["content"] = prompt;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  int code = https.POST(reqBody);
  String result;
  if (code == 200) {
    String payload = https.getString();
    DynamicJsonDocument resDoc(4096);
    DeserializationError err = deserializeJson(resDoc, payload);
    if (!err) {
      const char* text = resDoc["choices"][0]["message"]["content"] | "(no text in response)";
      result = String(text);
    } else {
      result = "(JSON parse error)";
    }
  } else if (code == 429) {
    result = "(rate limited -- free tier cap hit, try again shortly)";
  } else if (code > 0) {
    result = "(HTTP error " + String(code) + ")";
  } else {
    result = "(connection failed)";
  }
  https.end();
  return result;
}

// ---------------------------------------------------------------------
// Browser app (fetch + strip HTML down to readable text)
// ---------------------------------------------------------------------
void drawAppBrowser() {
  Rect r = winRect;
  Rect urlBox = { r.x + 4, r.y + 24, r.w - 8 - 46, 16 };
  browserGoBtn = { urlBox.x + urlBox.w + 2, urlBox.y + 1, 40, 14 };

  canvas.drawRect(urlBox.x, urlBox.y, urlBox.w, urlBox.h, browserFocused ? COL_ACCENT : COL_MUTED);
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);
  canvas.drawString(browserUrl, urlBox.x + 2, urlBox.y + 3);

  bool hoverGo = browserGoBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(browserGoBtn.x, browserGoBtn.y, browserGoBtn.w, browserGoBtn.h, 2,
                        hoverGo ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextColor(COL_ICON_TXT, hoverGo ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.drawString("Go", browserGoBtn.x + browserGoBtn.w / 2, browserGoBtn.y + 2);
  canvas.setTextDatum(top_left);

  int contentX = r.x + 4;
  int contentY = urlBox.y + urlBox.h + 4;
  int contentW = r.w - 8 - 16;   // leave a strip on the right for scroll buttons
  int contentH = (r.y + r.h - 12) - contentY;

  drawWrappedTextScrolled(browserContent, contentX, contentY, contentW, contentH, 10, browserScrollLine);

  // Scroll buttons, stacked in the right-hand strip
  browserScrollUpBtn   = { r.x + r.w - 4 - 14, contentY, 14, contentH / 2 - 1 };
  browserScrollDownBtn = { r.x + r.w - 4 - 14, contentY + contentH / 2 + 1, 14, contentH - contentH / 2 - 1 };
  bool hoverUp = browserScrollUpBtn.contains(cursor.x, cursor.y);
  bool hoverDown = browserScrollDownBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(browserScrollUpBtn.x, browserScrollUpBtn.y, browserScrollUpBtn.w, browserScrollUpBtn.h, 2,
                        hoverUp ? COL_ACCENT : COL_ICON_BG);
  canvas.fillRoundRect(browserScrollDownBtn.x, browserScrollDownBtn.y, browserScrollDownBtn.w, browserScrollDownBtn.h, 2,
                        hoverDown ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_ICON_TXT, hoverUp ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString("^", browserScrollUpBtn.x + browserScrollUpBtn.w / 2, browserScrollUpBtn.y + 2);
  canvas.setTextColor(COL_ICON_TXT, hoverDown ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString("v", browserScrollDownBtn.x + browserScrollDownBtn.w / 2, browserScrollDownBtn.y + 2);
  canvas.setTextDatum(top_left);
}

// Same idea as drawWrappedText, but starts partway through the text
// according to `scrollLines` wrapped lines already scrolled past.
void drawWrappedTextScrolled(const String& text, int x, int y, int w, int maxH, int lineH, int scrollLines) {
  int maxCharsPerLine = max(1, w / 6);
  int maxLines = max(1, maxH / lineH);
  int startPos = scrollLines * maxCharsPerLine;
  if (startPos > (int)text.length()) startPos = text.length();

  int cy = y, pos = startPos, lines = 0;
  while (pos < (int)text.length() && lines < maxLines) {
    int len = min(maxCharsPerLine, (int)text.length() - pos);
    canvas.drawString(text.substring(pos, pos + len), x, cy);
    pos += len;
    cy += lineH;
    lines++;
  }
}

// How many wrapped lines of browserContent can still be scrolled down
// past what's currently on screen, given the window's fixed content
// area. Used to clamp the scroll-down button.
int browserMaxScrollLines() {
  Rect r = winRect;
  int contentW = r.w - 8 - 16;
  int contentY = (r.y + 24 + 16 + 4);
  int contentH = (r.y + r.h - 12) - contentY;
  int maxCharsPerLine = max(1, contentW / 6);
  int maxLines = max(1, contentH / 10);
  int totalLines = (browserContent.length() + maxCharsPerLine - 1) / maxCharsPerLine;
  return max(0, totalLines - maxLines);
}

// Very small HTML-to-text extractor: drops all tags, and drops the
// *content* of <script>...</script> and <style>...</style> blocks too
// (otherwise you'd see raw JS/CSS as "text"). It does not understand
// nested/malformed markup the way a real parser would -- good enough
// for skimming an article, not a substitute for a browser engine.
String stripHtml(const String& html) {
  String out;
  out.reserve(min((size_t)1500, html.length()));
  bool inTag = false;
  bool readingTagName = false;
  bool skipContent = false;
  String tagName;

  for (size_t i = 0; i < html.length() && out.length() < 1500; i++) {
    char c = html[i];
    if (c == '<') {
      inTag = true;
      readingTagName = true;
      tagName = "";
      continue;
    }
    if (inTag) {
      if (c == '>') {
        inTag = false;
        readingTagName = false;
        String tl = tagName;
        tl.toLowerCase();
        if (tl.startsWith("script") || tl.startsWith("style")) skipContent = true;
        else if (tl.startsWith("/script") || tl.startsWith("/style")) skipContent = false;
        continue;
      }
      if (readingTagName) {
        if (c == ' ' || c == '\t' || c == '\n') readingTagName = false;
        else tagName += c;
      }
      continue;
    }
    if (skipContent) continue;

    if (c == '\r') continue;
    if (c == '\n' || c == '\t') c = ' ';
    if (c == ' ' && out.length() > 0 && out[out.length() - 1] == ' ') continue;
    out += c;
  }

  out.replace("&nbsp;", " ");
  out.replace("&amp;", "&");
  out.replace("&lt;", "<");
  out.replace("&gt;", ">");
  out.replace("&quot;", "\"");
  out.trim();
  if (out.length() == 0) out = "(no readable text found)";
  return out;
}

// Fetches `url` (adding "http://" if no scheme was given) and returns
// its text content via stripHtml(). Bounds how much of the raw
// response it reads so a large page can't exhaust heap memory.
String fetchWebsite(String url) {
  if (WiFi.status() != WL_CONNECTED) {
    return "(WiFi not connected)";
  }
  if (url.indexOf("://") < 0) {
    url = "http://" + url;
  }
  bool isHttps = url.startsWith("https://");

  HTTPClient http;
  WiFiClientSecure secureClient;
  bool began;
  if (isHttps) {
    secureClient.setInsecure();  // see README: skips TLS cert validation
    began = http.begin(secureClient, url);
  } else {
    began = http.begin(url);
  }
  if (!began) {
    return "(couldn't open that URL)";
  }

  http.addHeader("User-Agent", "WrenOS/0.4");
  http.setTimeout(8000);
  int code = http.GET();

  String result;
  if (code == HTTP_CODE_OK) {
    const size_t RAW_CAP = 6000;   // bound raw HTML read to protect heap
    String raw;
    raw.reserve(RAW_CAP);
    WiFiClient* stream = http.getStreamPtr();
    unsigned long start = millis();
    while (raw.length() < RAW_CAP && (millis() - start) < 8000) {
      if (stream->available()) {
        raw += (char)stream->read();
      } else if (!http.connected()) {
        break;
      } else {
        delay(1);
      }
    }
    result = stripHtml(raw);
  } else if (code > 0) {
    result = "(HTTP error " + String(code) + ")";
  } else {
    result = "(connection failed: " + http.errorToString(code) + ")";
  }

  http.end();
  return result;
}

// ---------------------------------------------------------------------
// Files app (SD card browser)
// ---------------------------------------------------------------------
void drawAppFiles() {
  Rect r = winRect;
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);

  int rowH = 12;
  int startY = r.y + 24;

  if (!sdReady) {
    canvas.drawString("No SD card detected.", r.x + 6, startY);
    canvas.drawString("Insert a FAT32 card", r.x + 6, startY + 14);
    canvas.drawString("and click Refresh.", r.x + 6, startY + 28);
  } else if (sdFileCount == 0) {
    canvas.drawString("(no files on SD card yet --", r.x + 6, startY);
    canvas.drawString("save something from Notes)", r.x + 6, startY + 14);
  } else {
    bool needsScroll = sdFileCount > FILES_VISIBLE_ROWS;
    int listW = r.w - 8 - (needsScroll ? 16 : 0);

    for (int i = 0; i < FILES_VISIBLE_ROWS; i++) {
      int idx = filesScroll + i;
      if (idx >= sdFileCount) break;
      int ry = startY + i * rowH;

      filesRowOpenRects[i] = { r.x + 4, ry, listW - 16, rowH };
      filesRowDelRects[i]  = { r.x + 4 + listW - 14, ry, 14, rowH - 1 };

      bool hoverOpen = filesRowOpenRects[i].contains(cursor.x, cursor.y);
      bool hoverDel  = filesRowDelRects[i].contains(cursor.x, cursor.y);

      canvas.setTextColor(hoverOpen ? COL_ACCENT : COL_WIN_TXT, COL_WIN_BG);
      String label = sdFiles[idx].name + " (" + String((unsigned long)sdFiles[idx].size) + "B)";
      canvas.drawString(label, filesRowOpenRects[i].x, ry + 1);

      canvas.fillRoundRect(filesRowDelRects[i].x, filesRowDelRects[i].y,
                            filesRowDelRects[i].w, filesRowDelRects[i].h, 2,
                            hoverDel ? COL_BTN_CLOSE : COL_ICON_BG);
      canvas.setTextDatum(top_center);
      canvas.setTextColor(COL_ICON_TXT, hoverDel ? COL_BTN_CLOSE : COL_ICON_BG);
      canvas.drawString("x", filesRowDelRects[i].x + filesRowDelRects[i].w / 2, filesRowDelRects[i].y + 1);
      canvas.setTextDatum(top_left);
    }

    if (needsScroll) {
      int listH = FILES_VISIBLE_ROWS * rowH;
      filesScrollUpBtn   = { r.x + r.w - 4 - 14, startY, 14, listH / 2 - 1 };
      filesScrollDownBtn = { r.x + r.w - 4 - 14, startY + listH / 2 + 1, 14, listH - listH / 2 - 1 };
      bool hoverUp = filesScrollUpBtn.contains(cursor.x, cursor.y);
      bool hoverDown = filesScrollDownBtn.contains(cursor.x, cursor.y);
      canvas.fillRoundRect(filesScrollUpBtn.x, filesScrollUpBtn.y, filesScrollUpBtn.w, filesScrollUpBtn.h, 2,
                            hoverUp ? COL_ACCENT : COL_ICON_BG);
      canvas.fillRoundRect(filesScrollDownBtn.x, filesScrollDownBtn.y, filesScrollDownBtn.w, filesScrollDownBtn.h, 2,
                            hoverDown ? COL_ACCENT : COL_ICON_BG);
      canvas.setTextDatum(top_center);
      canvas.setTextColor(COL_ICON_TXT, hoverUp ? COL_ACCENT : COL_ICON_BG);
      canvas.drawString("^", filesScrollUpBtn.x + filesScrollUpBtn.w / 2, filesScrollUpBtn.y + 2);
      canvas.setTextColor(COL_ICON_TXT, hoverDown ? COL_ACCENT : COL_ICON_BG);
      canvas.drawString("v", filesScrollDownBtn.x + filesScrollDownBtn.w / 2, filesScrollDownBtn.y + 2);
      canvas.setTextDatum(top_left);
    }
  }

  // Refresh button + free-space readout + hint, along the bottom
  filesRefreshBtn = { r.x + 4, r.y + r.h - 24, 46, 12 };
  bool hoverRef = filesRefreshBtn.contains(cursor.x, cursor.y);
  canvas.fillRoundRect(filesRefreshBtn.x, filesRefreshBtn.y, filesRefreshBtn.w, filesRefreshBtn.h, 2,
                        hoverRef ? COL_ACCENT : COL_ICON_BG);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(COL_ICON_TXT, hoverRef ? COL_ACCENT : COL_ICON_BG);
  canvas.drawString("Refresh", filesRefreshBtn.x + filesRefreshBtn.w / 2, filesRefreshBtn.y + 2);
  canvas.setTextDatum(top_left);

  canvas.setTextColor(COL_MUTED, COL_WIN_BG);
  if (sdReady) {
    unsigned long usedMB = (unsigned long)(SD.usedBytes() / (1024 * 1024));
    unsigned long totMB  = (unsigned long)(SD.totalBytes() / (1024 * 1024));
    canvas.drawString(String(usedMB) + "/" + String(totMB) + "MB", r.x + 56, r.y + r.h - 22);
  }
  canvas.drawString("Click a file to edit in Notes", r.x + 4, r.y + r.h - 10);
}

void handleFilesClick() {
  if (filesRefreshBtn.contains(cursor.x, cursor.y)) {
    refreshFileList();
    return;
  }
  if (sdFileCount > FILES_VISIBLE_ROWS) {
    if (filesScrollUpBtn.contains(cursor.x, cursor.y)) {
      if (filesScroll > 0) filesScroll--;
      return;
    }
    if (filesScrollDownBtn.contains(cursor.x, cursor.y)) {
      if (filesScroll < sdFileCount - FILES_VISIBLE_ROWS) filesScroll++;
      return;
    }
  }
  for (int i = 0; i < FILES_VISIBLE_ROWS; i++) {
    int idx = filesScroll + i;
    if (idx >= sdFileCount) break;
    if (filesRowDelRects[i].contains(cursor.x, cursor.y)) {
      SD.remove(sdPath(sdFiles[idx].name));
      refreshFileList();
      return;
    }
    if (filesRowOpenRects[i].contains(cursor.x, cursor.y)) {
      if (loadFileIntoNotes(sdPath(sdFiles[idx].name))) {
        openWindow(APP_NOTES);
      }
      return;
    }
  }
}

// ---------------------------------------------------------------------
// Calculator app
// ---------------------------------------------------------------------
void drawAppCalc() {
  Rect r = winRect;
  canvas.setTextColor(COL_WIN_TXT, COL_WIN_BG);
  canvas.drawString(calcExpr, r.x + 6, r.y + 24);
  canvas.setTextColor(COL_ACCENT, COL_WIN_BG);
  canvas.drawString(calcResult, r.x + 6, r.y + 36);

  int kw = 20, kh = 14, gap = 2;
  int startX = r.x + 6, startY = r.y + 52;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int kx = startX + col * (kw + gap);
      int ky = startY + row * (kh + gap);
      calcKeyRects[row][col] = { kx, ky, kw, kh };
      bool hover = calcKeyRects[row][col].contains(cursor.x, cursor.y);
      canvas.fillRoundRect(kx, ky, kw, kh, 2, hover ? COL_ACCENT : COL_ICON_BG);
      canvas.setTextColor(COL_ICON_TXT, hover ? COL_ACCENT : COL_ICON_BG);
      canvas.setTextDatum(top_center);
      canvas.drawString(String(calcKeys[row][col]), kx + kw / 2, ky + 3);
      canvas.setTextDatum(top_left);
    }
  }
}

void handleCalcClick() {
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      if (calcKeyRects[row][col].contains(cursor.x, cursor.y)) {
        char k = calcKeys[row][col];
        if (k == 'C') {
          calcExpr = "";
          calcResult = "";
        } else if (k == '=') {
          evalCalc();
        } else {
          calcExpr += k;
        }
        return;
      }
    }
  }
}

// Extremely small left-to-right, no-precedence evaluator: good enough
// for a demo calculator app (e.g. "12+7*2" is evaluated strictly
// left-to-right as ((12+7)*2), not with operator precedence).
void evalCalc() {
  if (calcExpr.length() == 0) return;
  double acc = 0;
  char pendingOp = '+';
  String numBuf = "";

  auto applyPending = [&](double val) {
    switch (pendingOp) {
      case '+': acc += val; break;
      case '-': acc -= val; break;
      case '*': acc *= val; break;
      case '/': acc = (val != 0) ? acc / val : 0; break;
    }
  };

  for (unsigned int i = 0; i < calcExpr.length(); i++) {
    char c = calcExpr[i];
    if (isDigit(c)) {
      numBuf += c;
    } else {
      applyPending(numBuf.toDouble());
      numBuf = "";
      pendingOp = c;
    }
  }
  applyPending(numBuf.toDouble());

  calcResult = String(acc);
}

// ---------------------------------------------------------------------
// Taskbar + cursor
// ---------------------------------------------------------------------
void drawTaskbar() {
  int barH = 14;
  int y = SCREEN_H - barH;
  canvas.fillRect(0, y, SCREEN_W, barH, COL_TASKBAR);
  canvas.drawFastHLine(0, y, SCREEN_W, COL_BORDER);
  canvas.setTextColor(COL_TASKBAR_TXT, COL_TASKBAR);

  // Battery: small icon + percentage, bottom-left corner
  int battX = 3, battY = y + 4, battW = 14, battH = 8;
  canvas.drawRect(battX, battY, battW, battH, COL_MUTED);
  canvas.fillRect(battX + battW, battY + 2, 2, battH - 4, COL_MUTED);  // terminal nub
  if (cachedBatteryPct >= 0) {
    int fillW = (battW - 2) * cachedBatteryPct / 100;
    uint16_t battColor = (cachedBatteryPct <= 15) ? COL_BTN_CLOSE
                        : (cachedBatteryPct <= 30) ? COL_ACCENT
                        : TFT_GREEN;
    if (fillW > 0) canvas.fillRect(battX + 1, battY + 1, fillW, battH - 2, battColor);
  }
  String battStr = (cachedBatteryPct >= 0) ? (String(cachedBatteryPct) + "%") : "--";
  canvas.drawString(battStr, battX + battW + 4, y + 3);

  // App/desktop label, shifted right to leave room for the battery readout
  String label = (openApp == APP_NONE) ? "Desktop" : appNames[openApp];
  canvas.drawString(label, battX + battW + 30, y + 3);

  // SD dot: green if a card is mounted, muted grey if not (not red --
  // no card isn't an error state, just "storage unavailable")
  canvas.fillCircle(SCREEN_W - 60, y + 7, 3, sdReady ? TFT_GREEN : COL_MUTED);
  canvas.fillCircle(SCREEN_W - 48, y + 7, 3, wifiConnected ? TFT_GREEN : TFT_RED);
  canvas.drawString(String((int)(millis() / 1000)) + "s", SCREEN_W - 38, y + 3);
}

void drawCursor() {
  int x = (int)cursor.x, y = (int)cursor.y;
  // simple arrow-shaped pointer
  canvas.fillTriangle(x, y, x, y + 10, x + 7, y + 7, COL_CURSOR);
  canvas.drawTriangle(x, y, x, y + 10, x + 7, y + 7, COL_CURSOR_OUTLINE);
}
