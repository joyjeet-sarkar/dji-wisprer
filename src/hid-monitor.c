// hid-monitor — a discovery tool for adapting dji-wispr to YOUR device.
//
// It listens to every HID device and prints each input event as:
//   EVENT vid=0x.... pid=0x.... usagePage=0x.. usage=0x.. value=N
//
// Use it to find (a) your mic receiver's vendor/product id and (b) which
// usagePage/usage its button emits. Plug in your mic, run this, press the
// button, and read off the line that appears. Feed those numbers to dji-wispr.
//
// Build: clang -framework IOKit -framework CoreFoundation src/hid-monitor.c -o hid-monitor
// Note:  reading other devices' input requires Input Monitoring permission.

#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>

static int getIntProp(IOHIDDeviceRef d, CFStringRef key) {
    CFNumberRef n = (CFNumberRef)IOHIDDeviceGetProperty(d, key);
    int v = 0;
    if (n) CFNumberGetValue(n, kCFNumberIntType, &v);
    return v;
}

static void valueCb(void* ctx, IOReturn res, void* sender, IOHIDValueRef value) {
    IOHIDElementRef el = IOHIDValueGetElement(value);
    IOHIDDeviceRef dev = IOHIDElementGetDevice(el);
    int vid = getIntProp(dev, CFSTR(kIOHIDVendorIDKey));
    int pid = getIntProp(dev, CFSTR(kIOHIDProductIDKey));
    uint32_t page  = IOHIDElementGetUsagePage(el);
    uint32_t usage = IOHIDElementGetUsage(el);
    CFIndex v = IOHIDValueGetIntegerValue(value);
    printf("EVENT vid=0x%04x pid=0x%04x usagePage=0x%02X usage=0x%02X value=%ld\n",
           vid, pid, page, usage, (long)v);
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, NULL); // match ALL devices
    IOHIDManagerRegisterInputValueCallback(mgr, valueCb, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOReturn r = IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        printf("open failed code=0x%x (grant Input Monitoring and retry)\n", r);
        fflush(stdout);
        return 2;
    }
    printf("Listening to ALL HID devices. Press your mic button to see its codes.\n");
    fflush(stdout);
    CFRunLoopRun();
    return 0;
}
