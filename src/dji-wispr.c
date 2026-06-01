// SPDX-License-Identifier: MIT
// dji-wispr — turn a DJI wireless mic's volume button into a Wispr Flow trigger.
//
// How it works:
//   1. Open the DJI USB receiver's HID interface and *seize* it
//      (kIOHIDOptionsTypeSeizeDevice) so the OS never sees the button's
//      "Volume Up" event — that's why the system volume stops changing.
//   2. When the button is pressed, synthesize a Ctrl+Opt+F18 keystroke.
//   3. Bind Wispr Flow's hands-free shortcut to Ctrl+Opt+F18, and each tap
//      toggles dictation on/off.
//
// Why this exists: a DJI mic button isn't a keyboard, so macOS/Wispr can't
// see it directly. The receiver *does* expose one consumer-control "Volume Up"
// event over HID, which we hijack. The clean way to remap it (Karabiner) needs
// a system extension that locked-down/MDM-managed Macs block. This runs entirely
// in user space: no kernel driver, no system extension — only the Input
// Monitoring + Accessibility permissions.
//
// Build:  clang -framework IOKit -framework CoreFoundation \
//               -framework ApplicationServices src/dji-wispr.c -o dji-wispr
//
// Usage:  dji-wispr [vendorIdHex] [productIdHex]
//         Defaults to the DJI Mic Mini receiver (0x2ca3 / 0x4011).
//         Find your own values with the bundled hid-monitor tool.

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>

// ---- The keystroke we emit. Change these to retarget a different shortcut. ----
#define KEY_F18     79  // kVK_F18 — a "phantom" function key macOS has no binding for
#define KEY_CONTROL 59  // kVK_Control
#define KEY_OPTION  58  // kVK_Option

// ---- The HID button we listen for: Consumer page, Volume Up / Volume Down ----
#define USAGE_PAGE_CONSUMER 0x0C
#define USAGE_VOLUME_UP     0xE9
#define USAGE_VOLUME_DOWN   0xEA

static int gVendorId  = 0x2ca3;  // DJI Technology Co., Ltd.
static int gProductId = 0x4011;  // "Wireless Mic Rx" receiver

static void postKey(CGEventSourceRef src, CGKeyCode key, bool down, CGEventFlags flags) {
    CGEventRef e = CGEventCreateKeyboardEvent(src, key, down);
    CGEventSetFlags(e, flags);
    CGEventPost(kCGHIDEventTap, e);
    if (e) CFRelease(e);
}

// Physically hold Ctrl, then Opt, tap F18, then release. We hold the real
// modifier keys (not just event flags) because macOS reconciles synthesized
// flags against the actual hardware modifier state — flags alone get dropped,
// and a shortcut recorder would see a bare F18 with "no modifier".
static void postTrigger(void) {
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventFlags ctrl    = kCGEventFlagMaskControl;
    CGEventFlags ctrlOpt = kCGEventFlagMaskControl | kCGEventFlagMaskAlternate;
    postKey(src, KEY_CONTROL, true,  ctrl);     // Control down
    postKey(src, KEY_OPTION,  true,  ctrlOpt);  // Option down (Control held)
    postKey(src, KEY_F18,     true,  ctrlOpt);  // F18 down
    postKey(src, KEY_F18,     false, ctrlOpt);  // F18 up
    postKey(src, KEY_OPTION,  false, ctrl);     // Option up (Control held)
    postKey(src, KEY_CONTROL, false, 0);        // Control up
    if (src) CFRelease(src);
}

static void valueCb(void* ctx, IOReturn res, void* sender, IOHIDValueRef value) {
    IOHIDElementRef e = IOHIDValueGetElement(value);
    uint32_t page  = IOHIDElementGetUsagePage(e);
    uint32_t usage = IOHIDElementGetUsage(e);
    CFIndex v = IOHIDValueGetIntegerValue(value);
    // value == 1 is a press (0 is the release). Fire once, on press.
    if (page == USAGE_PAGE_CONSUMER &&
        (usage == USAGE_VOLUME_UP || usage == USAGE_VOLUME_DOWN) && v == 1) {
        postTrigger();
        printf("button press -> posted Ctrl+Opt+F18\n");
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc >= 2) gVendorId  = (int)strtol(argv[1], NULL, 0);
    if (argc >= 3) gProductId = (int)strtol(argv[2], NULL, 0);

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
    printf("dji-wispr running: device 0x%04x/0x%04x seized -> Ctrl+Opt+F18\n",
           gVendorId, gProductId);
    fflush(stdout);
    CFRunLoopRun();
    return 0;
}
