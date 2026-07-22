// SPDX-License-Identifier: MIT
// dji-wisprer — turn a DJI wireless mic's volume button into a Wispr Flow trigger.
//
// How it works:
//   1. Open the DJI USB receiver's HID interface and *seize* it
//      (kIOHIDOptionsTypeSeizeDevice) so the OS never sees the button's
//      "Volume Up" event — that's why the system volume stops changing.
//   2. The receiver reports the button as a single momentary "Volume Up" pulse
//      (it does NOT report hold duration, so long-press is impossible). We count
//      pulses within a short window to tell a single click from a double click.
//   3. Single click and double click each synthesize their own keystroke.
//   4. Bind a Wispr Flow shortcut to each keystroke.
//
// IMPORTANT — your keyboard shortcut keeps working:
//   This never touches your existing keyboard trigger; Wispr allows several
//   shortcuts per action. By default a SINGLE click emits the Fn / Globe key —
//   the key most people already bind to Wispr — so the button reuses your
//   existing shortcut with no extra Wispr setup. A DOUBLE click emits Fn+Z by
//   default; bind that in Wispr to a second action.
//
//   Note on the double-click window: the DJI firmware hijacks a *very fast*
//   physical double-press for Bluetooth pairing (it disconnects the receiver),
//   so double-click it at a relaxed pace — two deliberate taps, not a rapid
//   drum-roll. The window (DJI_WISPRER_DOUBLE_MS) must be wider than your
//   natural double-tap gap. Because a single click must wait out this window to
//   be sure no second tap is coming, every single click is delayed by roughly
//   that many milliseconds — lower the window to cut the lag, raise it if slow
//   double-taps are being read as two singles.
//
// Choosing the SINGLE-click key (env vars):
//   DJI_WISPRER_EMIT = fn      (default)  -> Fn / Globe key (reuses your usual
//                                            Wispr shortcut; note macOS does not
//                                            always let software synthesize Fn)
//                      chord               -> Ctrl+Opt+F18 (unique "phantom" chord)
//                      custom              -> DJI_WISPRER_KEYCODE + DJI_WISPRER_MODS
//   DJI_WISPRER_KEYCODE = <decimal macOS virtual keycode>   (custom mode)
//   DJI_WISPRER_MODS    = comma list of: control,option,command,shift,fn
//
// Choosing the DOUBLE-click key (env vars):
//   Default (unset)            -> Fn+Z
//   DJI_WISPRER_DOUBLE_KEYCODE -> <decimal macOS virtual keycode>
//   DJI_WISPRER_DOUBLE_MODS    -> comma list of: control,option,command,shift,fn
//   DJI_WISPRER_DOUBLE_MS      -> click-grouping window in ms (default 600)
//   e.g. a rock-solid double binding (Fn synthesis can be flaky):
//        DJI_WISPRER_DOUBLE_KEYCODE=6 DJI_WISPRER_DOUBLE_MODS=control,option  (Ctrl+Opt+Z)
//
// Why this exists: a DJI mic button isn't a keyboard, so macOS/Wispr can't see
// it directly. The receiver exposes one consumer "Volume Up" HID event, which we
// hijack. The clean remap tool (Karabiner) needs a system extension that
// locked-down/MDM Macs block; this runs entirely in user space — no kernel
// driver — needing only Input Monitoring + Accessibility.
//
// Build:  clang -framework IOKit -framework CoreFoundation \
//               -framework ApplicationServices src/dji-wisprer.c -o dji-wisprer
// Usage:  dji-wisprer [vendorIdHex] [productIdHex]   (defaults: DJI 0x2ca3/0x4011)

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Emitted keys: Fn / Globe, plus the F18 / Z used by the built-in presets ----
#define KEY_F18 79   // kVK_F18
#define KEY_FN  0x3F // kVK_Function (the Fn / Globe key)
#define KEY_Z   0x06 // kVK_ANSI_Z

// ---- The HID button we listen for: Consumer page, Volume Up / Volume Down ----
#define USAGE_PAGE_CONSUMER 0x0C
#define USAGE_VOLUME_UP     0xE9
#define USAGE_VOLUME_DOWN   0xEA

// One emitted keystroke: optional Fn, an optional main key, and held modifiers.
// A single click and a double click are each just an Action.
typedef struct {
    int          useFn;      // include Fn / Globe in the chord
    int          hasKey;     // press `key` (0 => Fn-only tap, no main key)
    CGKeyCode    key;        // the main key
    CGKeyCode    modKey[4];  // real modifier keys to hold (ctrl/opt/cmd/shift)
    CGEventFlags modBit[4];  // their matching event flags
    int          modN;
} Action;

static int gVendorId  = 0x2ca3;  // DJI Technology Co., Ltd.
static int gProductId = 0x4011;  // "Wireless Mic Rx" receiver

static Action gSingle;           // single click: default Fn tap
static Action gDouble;           // double click: default Fn+Z

// ---- click grouping: how long to wait for a 2nd pulse before calling it single
static double            gWindowSec = 0.6;    // DJI_WISPRER_DOUBLE_MS / 1000
static CFRunLoopTimerRef gTimer     = NULL;   // non-NULL while a click is pending

static void addMod(Action* a, CGKeyCode key, CGEventFlags bit) {
    if (a->modN < 4) { a->modKey[a->modN] = key; a->modBit[a->modN] = bit; a->modN++; }
}

// Parse "control,option,fn,..." into an Action. "fn" flips useFn; the rest add
// real modifier keys we hold around the main key.
static void parseMods(Action* a, const char* s) {
    char buf[256]; strncpy(buf, s, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (char* t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if      (!strcmp(t,"control") || !strcmp(t,"ctrl"))                    addMod(a, 59, kCGEventFlagMaskControl);
        else if (!strcmp(t,"option")  || !strcmp(t,"opt") || !strcmp(t,"alt")) addMod(a, 58, kCGEventFlagMaskAlternate);
        else if (!strcmp(t,"command") || !strcmp(t,"cmd"))                     addMod(a, 55, kCGEventFlagMaskCommand);
        else if (!strcmp(t,"shift"))                                           addMod(a, 56, kCGEventFlagMaskShift);
        else if (!strcmp(t,"fn")      || !strcmp(t,"function"))                a->useFn = 1;
    }
}

static void postKey(CGEventSourceRef src, CGKeyCode key, bool down, CGEventFlags flags) {
    CGEventRef e = CGEventCreateKeyboardEvent(src, key, down);
    CGEventSetFlags(e, flags);
    CGEventPost(kCGHIDEventTap, e);
    if (e) CFRelease(e);
}

// Emit one Action. We hold the *real* modifier keys (not just event flags)
// around the target key: macOS reconciles synthesized flags against actual
// hardware modifier state, so flags alone get dropped and a shortcut recorder
// would see "no modifier". Fn is best-effort — macOS often ignores a
// synthesized Fn (see header); if Fn+key doesn't register in Wispr, switch the
// double binding to a plain chord via DJI_WISPRER_DOUBLE_MODS.
static void emitAction(const Action* a) {
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventFlags acc = 0;
    for (int i = 0; i < a->modN; i++) { acc |= a->modBit[i]; postKey(src, a->modKey[i], true, acc); }

    CGEventFlags flags = acc;
    if (a->useFn) flags |= kCGEventFlagMaskSecondaryFn;

    if (a->useFn && a->hasKey) {          // Fn + key
        postKey(src, KEY_FN, true,  flags);
        postKey(src, a->key, true,  flags);
        postKey(src, a->key, false, flags);
        postKey(src, KEY_FN, false, acc);
    } else if (a->useFn) {                // Fn tap only
        postKey(src, KEY_FN, true,  flags);
        postKey(src, KEY_FN, false, acc);
    } else {                              // plain key (+ any non-Fn modifiers)
        postKey(src, a->key, true,  flags);
        postKey(src, a->key, false, flags);
    }

    for (int i = a->modN - 1; i >= 0; i--) { acc &= ~a->modBit[i]; postKey(src, a->modKey[i], false, acc); }
    if (src) CFRelease(src);
}

// Window expired with no 2nd pulse => it was a single click.
static void singleTimerCb(CFRunLoopTimerRef t, void* info) {
    emitAction(&gSingle);
    printf("single click -> emitted single\n");
    fflush(stdout);
    CFRunLoopTimerInvalidate(t);
    CFRelease(t);
    gTimer = NULL;
}

// Called once per button pulse (press edge). First pulse arms a window; a
// second pulse inside that window means double click.
static void onPulse(void) {
    static CFAbsoluteTime last = 0;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    printf("pulse (gap since last: %.0f ms, window %.0f ms)\n",
           last ? (now - last) * 1000.0 : -1.0, gWindowSec * 1000.0);
    fflush(stdout);
    last = now;
    if (gTimer == NULL) {
        gTimer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                     CFAbsoluteTimeGetCurrent() + gWindowSec, 0, 0, 0,
                     singleTimerCb, NULL);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(), gTimer, kCFRunLoopDefaultMode);
    } else {
        CFRunLoopTimerInvalidate(gTimer);
        CFRelease(gTimer);
        gTimer = NULL;
        emitAction(&gDouble);
        printf("double click -> emitted double\n");
        fflush(stdout);
    }
}

static void valueCb(void* ctx, IOReturn res, void* sender, IOHIDValueRef value) {
    IOHIDElementRef e = IOHIDValueGetElement(value);
    uint32_t page  = IOHIDElementGetUsagePage(e);
    uint32_t usage = IOHIDElementGetUsage(e);
    CFIndex v = IOHIDValueGetIntegerValue(value);
    // value == 1 is a press (0 is the release). Count one pulse per press.
    if (page == USAGE_PAGE_CONSUMER &&
        (usage == USAGE_VOLUME_UP || usage == USAGE_VOLUME_DOWN) && v == 1) {
        onPulse();
    }
}

static void configureFromEnv(void) {
    // --- single click ---
    const char* mode = getenv("DJI_WISPRER_EMIT");
    if (!mode || strcmp(mode, "fn") == 0) {
        gSingle.useFn = 1;                         // default: Fn / Globe tap
    } else if (strcmp(mode, "custom") == 0) {
        const char* kc = getenv("DJI_WISPRER_KEYCODE");
        const char* md = getenv("DJI_WISPRER_MODS");
        gSingle.hasKey = 1;
        gSingle.key    = kc ? (CGKeyCode)strtol(kc, NULL, 0) : KEY_F18;
        if (md) parseMods(&gSingle, md);
    } else {                                       // "chord": Ctrl+Opt+F18
        gSingle.hasKey = 1;
        gSingle.key    = KEY_F18;
        addMod(&gSingle, 59, kCGEventFlagMaskControl);
        addMod(&gSingle, 58, kCGEventFlagMaskAlternate);
    }

    // --- double click (default: Fn+Z) ---
    const char* dk = getenv("DJI_WISPRER_DOUBLE_KEYCODE");
    const char* dm = getenv("DJI_WISPRER_DOUBLE_MODS");
    if (!dk && !dm) {
        gDouble.useFn = 1; gDouble.hasKey = 1; gDouble.key = KEY_Z;
    } else {
        gDouble.hasKey = 1;
        gDouble.key    = dk ? (CGKeyCode)strtol(dk, NULL, 0) : KEY_Z;
        if (dm) parseMods(&gDouble, dm);
    }

    // --- double-click window ---
    const char* ms = getenv("DJI_WISPRER_DOUBLE_MS");
    if (ms) { long m = strtol(ms, NULL, 10); if (m > 0) gWindowSec = m / 1000.0; }
}

static void describe(const Action* a, char* out, size_t n) {
    char mods[128] = {0};
    if (a->useFn) strncat(mods, "Fn+", sizeof mods - strlen(mods) - 1);
    for (int i = 0; i < a->modN; i++) {
        const char* nm = a->modBit[i] == kCGEventFlagMaskControl   ? "Ctrl+"
                       : a->modBit[i] == kCGEventFlagMaskAlternate ? "Opt+"
                       : a->modBit[i] == kCGEventFlagMaskCommand   ? "Cmd+"
                       : "Shift+";
        strncat(mods, nm, sizeof mods - strlen(mods) - 1);
    }
    if (a->hasKey) snprintf(out, n, "%skeycode %d", mods, a->key);
    else           snprintf(out, n, "%s(tap)", mods);
}

int main(int argc, char** argv) {
    if (argc >= 2) gVendorId  = (int)strtol(argv[1], NULL, 0);
    if (argc >= 3) gProductId = (int)strtol(argv[2], NULL, 0);
    configureFromEnv();

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    CFNumberRef vidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &gVendorId);
    CFNumberRef pidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &gProductId);
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), vidNum);
    CFDictionarySetValue(match, CFSTR(kIOHIDProductIDKey), pidNum);
    IOHIDManagerSetDeviceMatching(mgr, match);

    IOHIDManagerRegisterInputValueCallback(mgr, valueCb, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    // Seize: take exclusive control so the OS doesn't also act on the button.
    IOReturn r = IOHIDManagerOpen(mgr, kIOHIDOptionsTypeSeizeDevice);
    if (r != kIOReturnSuccess) {
        printf("seize/open failed code=0x%x "
               "(0xe00002e2 = grant Input Monitoring to this binary)\n", r);
        fflush(stdout);
        return 2;
    }
    char s[160], d[160];
    describe(&gSingle, s, sizeof s);
    describe(&gDouble, d, sizeof d);
    printf("dji-wisprer running: device 0x%04x/0x%04x seized\n"
           "  accessibility (can inject keystrokes): %s\n"
           "  single click -> %s\n"
           "  double click -> %s   (window %.0f ms)\n",
           gVendorId, gProductId,
           AXIsProcessTrusted() ? "YES" : "NO  <-- Mac is blocking; re-grant in Privacy & Security > Accessibility",
           s, d, gWindowSec * 1000.0);
    fflush(stdout);
    CFRunLoopRun();
    return 0;
}
