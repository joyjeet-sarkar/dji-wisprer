# dji-wispr

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)

**Use a DJI wireless mic's button to trigger [Wispr Flow](https://wisprflow.ai) dictation on macOS — with no kernel driver and no system extension.**

Tap the volume button on your DJI mic → Wispr Flow hands-free dictation toggles on. Tap again → it stops and inserts your text. The button no longer changes your volume; it becomes your push-to-dictate.

Tested on macOS 26.5 (Apple Silicon) with a **DJI Mic Mini** receiver. It should work with other DJI receivers and, with a one-line change, other USB mics whose buttons emit an HID consumer event (see [Adapting to your device](#adapting-to-your-device)).

---

## Why this is harder than it sounds

A DJI mic button isn't a keyboard, so macOS — and therefore Wispr — can't see it as a shortcut. But the DJI **USB receiver** quietly exposes an HID interface, and its volume button emits **one** standard event: `Consumer / Volume Up` (`usagePage 0x0C`, `usage 0xE9`). That's the only signal we get, and we hijack it.

The "normal" way to remap that event is [Karabiner-Elements](https://karabiner-elements.pqrs.org/). But Karabiner needs a **DriverKit system extension**, and on **MDM-managed / locked-down Macs the IT policy blocks that approval** (`activated waiting for user`, forever). Dead end.

**dji-wispr runs entirely in user space.** It needs only two ordinary permission checkboxes — Input Monitoring and Accessibility — which managed Macs typically leave open even when they block drivers.

> ⚠️ Discovered limitations:
> - Works over **USB only**. Over Bluetooth the DJI button sends macOS *nothing*. (USB is also better audio — 48 kHz vs ~16 kHz Bluetooth headset.)
> - **Both** volume buttons emit the same code, so both become the trigger (you lose DJI-side volume control).
> - **Don't hold** the button — a long press triggers the DJI's own Bluetooth pairing.

---

## How it works

```
┌─────────────┐   USB HID    ┌──────────────────────────────┐   CGEvent    ┌────────────┐
│ DJI volume  │  Volume Up   │           dji-wispr           │  Ctrl+Opt+   │ Wispr Flow │
│   button    │ ───────────▶ │  1. SEIZE the HID device      │ ───F18────▶  │ hands-free │
│  (press)    │  0x0C/0xE9   │     (OS never sees the event, │   keystroke  │  shortcut  │
└─────────────┘              │      so volume doesn't move)  │              └────────────┘
                             │  2. synthesize Ctrl+Opt+F18   │
                             └──────────────────────────────┘
```

1. **Seize** — `IOHIDManagerOpen(..., kIOHIDOptionsTypeSeizeDevice)` takes exclusive control of the DJI HID interface. The system never receives the "Volume Up", so the volume stops changing. This is the trick that replaces Karabiner's event-replacement — no driver required.
2. **Inject** — on each press we synthesize **Ctrl+Opt+F18**. `F18` is a "phantom" function key (no physical key, no default macOS binding), so the chord can never be typed by accident or collide with another app. We press the *real* Ctrl and Opt keys around it (not just event flags) because macOS reconciles synthesized flags against the actual hardware modifier state.
3. **Bind** — Wispr's hands-free shortcut is set to Ctrl+Opt+F18, which is a clean toggle (one tap on, one tap off) — a perfect match for a momentary button.

It runs as a **LaunchAgent**, so it auto-starts at login and restarts if it crashes.

---

## Requirements

- macOS (tested on 26.5, Apple Silicon)
- Xcode Command Line Tools (`xcode-select --install`) — provides `clang`
- [Wispr Flow](https://wisprflow.ai) installed
- A DJI mic connected via its **USB receiver**

---

## Install

```sh
git clone https://github.com/<you>/dji-wispr.git
cd dji-wispr
./install.sh
```

`install.sh` builds the binaries, installs `dji-wispr` to `~/Library/Application Support/dji-wispr/`, ad-hoc code-signs it (so the permission grant sticks), writes and loads the LaunchAgent, and opens the permission panes. Then finish the two manual steps it prints:

### 1. Grant Accessibility (and Input Monitoring)

In **System Settings → Privacy & Security → Accessibility**, click **`+`**, press **⌘⇧G**, paste:

```
~/Library/Application Support/dji-wispr/dji-wispr
```

Select it and switch its toggle **ON**. If you're prompted for **Input Monitoring** too, allow that. Then reload the service so it picks up the grant:

```sh
launchctl kickstart -k gui/$(id -u)/com.djiwispr.bridge
```

> These permissions are the same kind Zoom/Loom request to control the screen — **not** a system extension, so an MDM driver block does not apply.

### 2. Point Wispr at the button

**Wispr Flow → Settings → General → Shortcuts → Hands-free** → click the shortcut box so it reads *"listening…"*, then **press the DJI volume button once**. Wispr captures `⌃⌥F18`. Save.

(You can't type F18 on a Mac keyboard — pressing the button *is* how you enter it.)

### Test

Click into any text field, **tap the DJI volume button** → dictation starts; speak; **tap again** → it stops and inserts the text. 🎤

---

## Adapting to your device

The defaults target the DJI Mic Mini receiver (`0x2ca3 / 0x4011`). For another mic:

1. Build and run the bundled discovery tool, then press your button:
   ```sh
   make
   ./build/hid-monitor          # press your mic button; note the line that appears
   ```
   You'll see something like:
   ```
   EVENT vid=0x2ca3 pid=0x4011 usagePage=0x0C usage=0xE9 value=1
   ```
2. Pass your `vid`/`pid` to the bridge (it matches the device; it already listens for Volume Up/Down on the consumer page):
   ```sh
   ./build/dji-wispr 0xVVVV 0xPPPP
   ```
   If your button reports a *different* `usagePage`/`usage`, edit the `USAGE_*` constants near the top of [`src/dji-wispr.c`](src/dji-wispr.c). To emit a different shortcut than Ctrl+Opt+F18, change the `KEY_*` constants and rebind Wispr accordingly.
3. To make it permanent, edit the `ProgramArguments` in `~/Library/LaunchAgents/com.djiwispr.bridge.plist` to include your ids, then `launchctl kickstart -k gui/$(id -u)/com.djiwispr.bridge`.

---

## Troubleshooting

| Symptom | Cause / Fix |
| --- | --- |
| Button still changes the **volume** | Service isn't seizing. Check `cat /tmp/dji-wispr.log`. `seize/open failed 0xe00002e2` → grant **Input Monitoring** to the binary. Make sure you're on **USB**, not Bluetooth. |
| Wispr says **"must include a modifier key"** | The keystroke landed but without modifiers. Make sure you're running the current build (it holds real Ctrl/Opt keys). |
| Button press does **nothing** in Wispr's recorder | **Accessibility** not granted/active. Re-add the binary and `launchctl kickstart -k gui/$(id -u)/com.djiwispr.bridge`. |
| Worked, then broke after I **recompiled** | Ad-hoc signatures are content-hashed; rebuilding invalidates the grant. Re-add the binary in Accessibility. |
| Nothing after **reboot/replug** | `launchctl print gui/$(id -u)/com.djiwispr.bridge | grep state`. Use the USB receiver; the LaunchAgent re-runs at login. |

Check it's alive:
```sh
launchctl print gui/$(id -u)/com.djiwispr.bridge | grep -E 'state|pid'
tail -f /tmp/dji-wispr.log
```

---

## Uninstall

```sh
./uninstall.sh
```

Then remove the leftover `dji-wispr` entry from **Privacy & Security → Accessibility** with the `–` button, and reset your Wispr hands-free shortcut if you wish.

---

## Security notes

- 100% local, no network. The whole thing is ~120 lines of C in [`src/`](src) — read it.
- It **seizes** the matched DJI HID device (exclusive grab) and **synthesizes** one fixed key chord. It does not log keystrokes or read any other device.
- It needs **Input Monitoring** (to read/seize the mic's HID) and **Accessibility** (to post the keystroke). Both are standard macOS TCC permissions you grant explicitly.

## License

MIT — see [LICENSE](LICENSE).

---

*Built by reverse-engineering what the DJI receiver actually sends to macOS: it exposes an HID interface, its button is a single `Consumer/Volume Up` event, and a user-space seize is enough to repurpose it — no Karabiner, no driver, no IT ticket.*
