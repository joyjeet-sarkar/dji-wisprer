// SPDX-License-Identifier: MIT
// dji-wisprer — turn a DJI wireless mic's volume button into a Wispr Flow trigger.
//
// How it works:
//   1. Open the DJI USB receiver's HID interface and *seize* it
//      (kIOHIDOptionsTypeSeizeDevice) so the OS never sees the button's
//      "Volume Up" event — that's why the system volume stops changing.
//   2. When the button is pressed, synthesize a keystroke that Wispr binds to.
//   3. Bind a Wispr Flow shortcut to that keystroke; each tap toggles dictation.
//
// IMPORTANT — your keyboard shortcut keeps working:
//   This only adds a *new* shortcut for the DJI button; it never touches your
//   existing keyboard trigger. Wispr allows several shortcuts per action, so
//   the keyboard key (e.g. Fn) and the DJI button can both trigger Flow. Pick a
//   DJI key here that does NOT collide with your keyboard one (Ctrl+Opt+F18 is a
//   safe default — see install.sh, which asks you which key to use).
//
// Choosing the emitted key (env vars):
//   DJI_WISPRER_EMIT = chord   (default)  -> Ctrl+Opt+F18 (unique "phantom" chord)
//                      fn                  -> Fn / Globe key (best-effort; macOS
//                                             rarely lets software synthesize Fn)
//                      custom              -> DJI_WISPRER_KEYCODE + DJI_WISPRER_MODS
//   DJI_WISPRER_KEYCODE = <decimal macOS virtual keycode>   (custom mode)
//   DJI_WISPRER_MODS    = comma list of: control,option,command,shift            )
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

// ---- Default key: Ctrl+Opt+F18 (F18 is a "phantom" key macOS has no binding for) ----
#define KEY_F18 79   // kVK_F18
#define KEY_FN  0x3F // kVK_Function (the Fn / Globe key)

// ---- The HID button we listen for: Consumer page, Volume Up / Volume Down ----
#define USAGE_PAGE_CONSUMER 0x0C
#define USAGE_VOLUME_UP     0xE9
#define USAGE_VOLUME_DOWN   0xEA

enum { EMIT_CUSTOM, EMIT_FN };  // "chord" is just custom with the default key+mods

static int gVendorId  = 0x2ca3;  // DJI Technology Co., Ltd.
static int gProductId = 0x4011;  // "Wireless Mic Rx" receiver
static int gEmitMode  = EMIT_CUSTOM;

// custom/chord config: a key plus held modifier keys
static CGKeyCode  gKey      = KEY_F18;
static CGKeyCode  gModKey[4];
static CGEventFlags gModBit[4];
static int        gModN     = 0;
static CGEventFlags gModAll = 0;

static void addMod(CGKeyCode key, CGEventFlags bit) {
    if (gModN < 4) { gModKey[gModN] = key; gModBit[gModN] = bit; gModN++; gModAll |= bit; }
}

static void parseMods(const char* s) {
    char buf[256]; strncpy(buf, s, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (char* t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if      (!strcmp(t,"control") || !strcmp(t,"ctrl"))               addMod(59, kCGEventFlagMaskControl);
        else if (!strcmp(t,"option")  || !strcmp(t,"opt") || !strcmp(t,"alt")) addMod(58, kCGEventFlagMaskAlternate);
        else if (!strcmp(t,"command") || !strcmp(t,"cmd"))               addMod(55, kCGEventFlagMaskCommand);
        else if (!strcmp(t,"shift"))                                     addMod(56, kCGEventFlagMaskShift);
    }
}

static void postKey(CGEventSourceRef src, CGKeyCode key, bool down, CGEventFlags flags) {
    CGEventRef e = CGEventCreateKeyboardEvent(src, key, down);
    CGEventSetFlags(e, flags);
    CGEventPost(kCGHIDEventTap, e);
    if (e) CFRelease(e);
}

// Hold the real modifier keys (not just event flags) around the target key:
// macOS reconciles synthesized flags against actual hardware modifier state, so
// flags alone get dropped and a shortcut recorder would see "no modifier".
static void postCustom(void) {
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventFlags acc = 0;
    for (int i = 0; i < gModN; i++) { acc |= gModBit[i]; postKey(src, gModKey[i], true, acc); }
    postKey(src, gKey, true,  gModAll);
    postKey(src, gKey, false, gModAll);
    for (int i = gModN - 1; i >= 0; i--) { acc &= ~gModBit[i]; postKey(src, gModKey[i], false, acc); }
    if (src) CFRelease(src);
}

// Best-effort Fn / Globe key tap. macOS often ignores synthesized Fn — see header.
static void postFn(void) {
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    postKey(src, KEY_FN, true,  kCGEventFlagMaskSecondaryFn);
    postKey(src, KEY_FN, false, 0);
    if (src) CFRelease(src);
}

static void emitTrigger(void) {
    if (gEmitMode == EMIT_FN) postFn();
    else                      postCustom();
}

static void valueCb(void* ctx, IOReturn res, void* sender, IOHIDValueRef value) {
    IOHIDElementRef e = IOHIDValueGetElement(value);
    uint32_t page  = IOHIDElementGetUsagePage(e);
    uint32_t usage = IOHIDElementGetUsage(e);
    CFIndex v = IOHIDValueGetIntegerValue(value);
    // value == 1 is a press (0 is the release). Fire once, on press.
    if (page == USAGE_PAGE_CONSUMER &&
        (usage == USAGE_VOLUME_UP || usage == USAGE_VOLUME_DOWN) && v == 1) {
        emitTrigger();
        printf("button press -> emitted trigger\n");
        fflush(stdout);
    }
}

static void configureFromEnv(void) {
    const char* mode = getenv("DJI_WISPRER_EMIT");
    if (mode && strcmp(mode, "fn") == 0) { gEmitMode = EMIT_FN; return; }
    gEmitMode = EMIT_CUSTOM;
    if (mode && strcmp(mode, "custom") == 0) {
        const char* kc = getenv("DJI_WISPRER_KEYCODE");
        const char* md = getenv("DJI_WISPRER_MODS");
        gKey = kc ? (CGKeyCode)strtol(kc, NULL, 0) : KEY_F18;
        if (md) parseMods(md);
    } else {
        // "chord" / default: Ctrl+Opt+F18
        gKey = KEY_F18;
        addMod(59, kCGEventFlagMaskControl);
        addMod(58, kCGEventFlagMaskAlternate);
    }
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
    printf("dji-wisprer running: device 0x%04x/0x%04x seized -> emit mode '%s'\n",
           gVendorId, gProductId, gEmitMode == EMIT_FN ? "fn" : "key");
    fflush(stdout);
    CFRunLoopRun();
    return 0;
}
