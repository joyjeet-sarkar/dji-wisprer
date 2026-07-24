# dji-wisprer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) ![Platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)

**Use a DJI wireless mic's button to trigger [Wispr Flow](https://wisprflow.ai) dictation on macOS — with no kernel driver and no system extension.**

Tap the volume button on your DJI mic → Wispr Flow hands-free dictation toggles on. Tap again → it stops and inserts your text. The button no longer changes your volume; it becomes your push-to-dictate.

Tested on macOS 26.5 (Apple Silicon) with a **DJI Mic Mini** receiver. It should work with other DJI receivers and, with a one-line change, other USB mics whose buttons emit an HID consumer event (see [Adapting to your device](#adapting-to-your-device)).

---

## Why this is harder than it sounds

A DJI mic button isn't a keyboard, so macOS — and therefore Wispr — can't see it as a shortcut. But the DJI **USB receiver** quietly exposes an HID interface, and its volume button emits **one** standard event: `Consumer / Volume Up` (`usagePage 0x0C`, `usage 0xE9`). That's the only signal we get, and we hijack it.

The "normal" way to remap that event is [Karabiner-Elements](https://karabiner-elements.pqrs.org/). But Karabiner needs a **DriverKit system extension**, and on **MDM-managed / locked-down Macs the IT policy blocks that approval** (`activated waiting for user`, forever). Dead end.

**dji-wisprer runs entirely in user space.** It needs only two ordinary permission checkboxes — Input Monitoring and Accessibility — which managed Macs typically leave open even when they block drivers.

> ⚠️ Discovered limitations:
>
> - Works over **USB only**. Over Bluetooth the DJI button sends macOS _nothing_. (USB is also better audio — 48 kHz vs ~16 kHz Bluetooth headset.)
> - **Both** volume buttons emit the same code, so both become the trigger (you lose DJI-side volume control).
> - **Don't hold** the button — a long press triggers the DJI's own Bluetooth pairing.

### Your keyboard shortcut keeps working — pick the DJI's key at install

dji-wisprer never replaces your keyboard trigger — Wispr Flow allows several shortcuts per action. **By default the DJI button emits `Fn`**, the key most people already bind to Wispr, so the button reuses your existing shortcut with no extra Wispr setup. At install time `install.sh` **asks which key the DJI button should send** — `Fn` (default), the unique `Ctrl+Opt+F18` chord, or a custom keycode. Full walkthrough in **[setup.md](setup.md)**.

---

## How it works

```
DJI volume button (USB)
   │
   │  press → HID "Volume Up"  (usagePage 0x0C, usage 0xE9)
   ▼
dji-wisprer   ·   user-space, no driver / system extension
   1. SEIZE the DJI HID device
        → the OS never sees the event, so the volume never moves
   2. synthesize a keystroke  (default: Fn — or Ctrl+Opt+F18)
   │
   │  Fn
   ▼
Wispr Flow shortcut   →   dictation toggles on / off
```

1. **Seize** — `IOHIDManagerOpen(..., kIOHIDOptionsTypeSeizeDevice)` takes exclusive control of the DJI HID interface. The system never receives the "Volume Up", so the volume stops changing. This is the trick that replaces Karabiner's event-replacement — no driver required.
2. **Inject** — on each press we synthesize a keystroke Wispr listens for. **By default that's the `Fn` / Globe key** — the key most people already bind to Wispr — so the DJI button reuses your existing shortcut with no extra Wispr setup. macOS doesn't always let software synthesize `Fn`, though; if the button does nothing, switch to **Ctrl+Opt+F18**, a "phantom" chord (no physical F18 key, no default macOS binding) that can never be typed by accident or collide with another app. For the chord we press the _real_ Ctrl and Opt keys around F18 (not just event flags), because macOS reconciles synthesized flags against the actual hardware modifier state.
3. **Bind** — point a Wispr Flow shortcut at whichever key the button emits. With the `Fn` default you likely already have one; for `Ctrl+Opt+F18` add it as a new Hands-free shortcut. Either way each tap is a clean toggle (one tap on, one tap off) — a perfect match for a momentary button.

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
git clone https://github.com/joyjeet-sarkar/dji-wisprer.git
cd dji-wisprer
./install.sh
```

`install.sh` builds the binaries, installs `dji-wisprer` to `~/Library/Application Support/dji-wisprer/`, ad-hoc code-signs it, **asks which key the DJI button should send** (so it won't clash with your keyboard's Wispr shortcut), writes and loads the LaunchAgent, and opens the permission panes. Then finish the two manual steps it prints. See **[setup.md](setup.md)** for the full walkthrough.

### 1. Grant Accessibility (and Input Monitoring)

In **System Settings → Privacy & Security → Accessibility**, click **`+`**, press **⌘⇧G**, paste:

```
~/Library/Application Support/dji-wisprer/dji-wisprer
```

Select it and switch its toggle **ON**. If you're prompted for **Input Monitoring** too, allow that. Then reload the service so it picks up the grant:

```sh
launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge
```

> These permissions are the same kind Zoom/Loom request to control the screen — **not** a system extension, so an MDM driver block does not apply.

### 2. Point Wispr at the button

By default the DJI button emits **Fn** — the same key most people already bind to Wispr — so it may already trigger dictation with no further setup. To bind it explicitly (or if you chose `Ctrl+Opt+F18` at install): **Wispr Flow → Settings → General → Shortcuts → Hands-free** → click the shortcut box so it reads _"listening…"_, then **press the DJI volume button once**. Wispr captures whatever key the button sends. Save.

(For `Ctrl+Opt+F18`, you can't type F18 on a Mac keyboard — pressing the button _is_ how you enter it.)

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
   ./build/dji-wisprer 0xVVVV 0xPPPP
   ```
   If your button reports a _different_ `usagePage`/`usage`, edit the `USAGE_*` constants near the top of [`src/dji-wisprer.c`](src/dji-wisprer.c). To change the emitted key, set `DJI_WISPRER_EMIT` (`fn` / `chord` / `custom`) in the LaunchAgent and rebind Wispr accordingly.
3. To make it permanent, edit the `ProgramArguments` in `~/Library/LaunchAgents/com.djiwisprer.bridge.plist` to include your ids, then `launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge`.

---

## Troubleshooting

| Symptom | Cause / Fix |
| --- | --- |
| Button still changes the **volume** | Service isn't seizing. Check `cat /tmp/dji-wisprer.log`. `seize/open failed 0xe00002e2` → grant **Input Monitoring** to the binary. Make sure you're on **USB**, not Bluetooth. |
| Log shows `button press -> emitted trigger` but **nothing happens** in Wispr | macOS is dropping the synthesized **Fn** key (the default; this is common). Switch to the chord: set `DJI_WISPRER_EMIT=chord` in `~/Library/LaunchAgents/com.djiwisprer.bridge.plist`, reload, and bind **Ctrl+Opt+F18** in Wispr. |
| Wispr says **"must include a modifier key"** | The keystroke landed but without modifiers (chord mode). Make sure you're running the current build (it holds real Ctrl/Opt keys). |
| Button press does **nothing** in Wispr's recorder | **Accessibility** not granted/active. Re-add the binary and `launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge`. |
| Worked, then broke after I **recompiled** | Ad-hoc signatures are content-hashed; rebuilding invalidates the grant. Re-add the binary in Accessibility. |
| Nothing after **reboot/replug** | `launchctl print gui/$(id -u)/com.djiwisprer.bridge | grep state`. Use the USB receiver; the LaunchAgent re-runs at login. |

Check it's alive:

```sh
launchctl print gui/$(id -u)/com.djiwisprer.bridge | grep -E 'state|pid'
tail -f /tmp/dji-wisprer.log
```

---

## Uninstall

```sh
./uninstall.sh
```

Then remove the leftover `dji-wisprer` entry from **Privacy & Security → Accessibility** with the `–` button, and reset your Wispr hands-free shortcut if you wish.

---

## Security notes

- 100% local, no network. The whole thing is ~120 lines of C in [`src/`](src) — read it.
- It **seizes** the matched DJI HID device (exclusive grab) and **synthesizes** one fixed keystroke. It does not log keystrokes or read any other device.
- It needs **Input Monitoring** (to read/seize the mic's HID) and **Accessibility** (to post the keystroke). Both are standard macOS TCC permissions you grant explicitly.

## License

MIT — see [LICENSE](LICENSE).

## Disclaimer

This is an independent, unofficial project. **Not affiliated with, endorsed by, or sponsored by Wispr Flow or DJI.** "Wispr Flow" and "DJI" are trademarks of their respective owners and are used here only to describe interoperability (nominative fair use).

---

_Built by reverse-engineering what the DJI receiver actually sends to macOS: it exposes an HID interface, its button is a single `Consumer/Volume Up` event, and a user-space seize is enough to repurpose it — no Karabiner, no driver, no IT ticket._
