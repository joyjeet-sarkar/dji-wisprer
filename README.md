# dji-wisprer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) ![Platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)

**Use a DJI wireless mic's button to trigger [Wispr Flow](https://wisprflow.ai) dictation on macOS — with no kernel driver and no system extension.**

**Single-tap** the volume button on your DJI mic → Wispr Flow hands-free dictation toggles on; tap again → it stops and inserts your text. **Double-tap** → a second, separate shortcut fires (`Fn+Z` by default), so one button drives two Wispr actions. The button no longer changes your volume; it becomes your push-to-dictate.

Tested on macOS 26.5 (Apple Silicon) with a **DJI Mic Mini** receiver. It should work with other DJI receivers and, with a one-line change, other USB mics whose buttons emit an HID consumer event (see [Adapting to your device](#adapting-to-your-device)).

---

## Why this is harder than it sounds

A DJI mic button isn't a keyboard, so macOS — and therefore Wispr — can't see it as a shortcut. But the DJI **USB receiver** quietly exposes an HID interface, and its volume button emits **one** standard event: `Consumer / Volume Up` (`usagePage 0x0C`, `usage 0xE9`) — a single momentary pulse per tap, with no hold duration reported. That's the only signal we get, and we hijack it, then **count taps in software** to tell a single click from a double click. (Because the pulse carries no duration, a press-and-hold "long press" gesture is impossible — see the limitations below.)

The "normal" way to remap that event is [Karabiner-Elements](https://karabiner-elements.pqrs.org/). But Karabiner needs a **DriverKit system extension**, and on **MDM-managed / locked-down Macs the IT policy blocks that approval** (`activated waiting for user`, forever). Dead end.

**dji-wisprer runs entirely in user space.** It needs only two ordinary permission checkboxes — Input Monitoring and Accessibility — which managed Macs typically leave open even when they block drivers.

> ⚠️ Discovered limitations (from watching what the receiver actually sends):
>
> - Works over **USB only**. Over Bluetooth the DJI button sends macOS _nothing_. (USB is also better audio — 48 kHz vs ~16 kHz Bluetooth headset.)
> - **Both** volume buttons emit the same code, so both become the trigger (you lose DJI-side volume control).
> - **No long-press.** However long you hold, the receiver sends one identical ~2 ms pulse and never reports the hold — so a press-and-hold gesture can't be detected.
> - **Don't double-tap _fast_.** A rapid double-press is intercepted by the DJI's own firmware as Bluetooth pairing and disconnects the receiver. dji-wisprer detects a double click in software from two _normal_ taps, so keep them relaxed — a few hundred ms apart, within the grouping window (see below).

### Your keyboard shortcut keeps working — pick the DJI's key at install

dji-wisprer never replaces your keyboard trigger — Wispr Flow allows several shortcuts per action. **By default a single click emits `Fn`**, the key most people already bind to Wispr, so the button reuses your existing shortcut with no extra Wispr setup. At install time `install.sh` **asks which key a single click should send** — `Fn` (default), the unique `Ctrl+Opt+F18` chord, or a custom keycode. A **double click** emits a second keystroke (`Fn+Z` by default) you can bind to any other Wispr action. Full walkthrough in **[setup.md](setup.md)**.

---

## How it works

```
DJI volume button (USB)
   │
   │  each tap → one HID "Volume Up" pulse  (usagePage 0x0C, usage 0xE9)
   ▼
dji-wisprer   ·   user-space, no driver / system extension
   1. SEIZE the DJI HID device
        → the OS never sees the event, so the volume never moves
   2. COUNT taps within a short window (DJI_WISPRER_DOUBLE_MS, default 600 ms)
        → 1 tap = single click,  2 taps = double click
   3. synthesize a keystroke:
        single click → Fn     (default)
        double click → Fn+Z   (default)
   │
   ▼
Wispr Flow shortcuts   →   two independent actions
```

1. **Seize** — `IOHIDManagerOpen(..., kIOHIDOptionsTypeSeizeDevice)` takes exclusive control of the DJI HID interface. The system never receives the "Volume Up", so the volume stops changing. This is the trick that replaces Karabiner's event-replacement — no driver required.
2. **Count taps** — the receiver only ever sends a momentary pulse (no hold duration, so no long-press). The first tap arms a short window (`DJI_WISPRER_DOUBLE_MS`, default 600 ms): one tap inside it is a **single** click, a second tap makes it a **double**. Trade-off — a single click can't fire until the window elapses, so it is delayed by roughly that long; shrink the window to cut the lag, grow it if slow double-taps read as two singles.
3. **Inject** — a single click synthesizes the single-click key (**default `Fn` / Globe**, the key most people already bind to Wispr); a double click synthesizes the double-click key (**default `Fn+Z`**). macOS doesn't always let software synthesize `Fn`, though; if a click does nothing, switch to a chord like **Ctrl+Opt+F18** (single) or **Ctrl+Opt+Z** (double) — "phantom" combos that can't be typed by accident. For a chord we press the _real_ Ctrl/Opt keys around the main key (not just event flags), because macOS reconciles synthesized flags against the actual hardware modifier state.
4. **Bind** — point one Wispr Flow shortcut at the single-click key and (optionally) another at the double-click key. With the `Fn` default you likely already have the single one; add the others as new shortcuts. Each single tap is a clean toggle (one on, one off) — a perfect match for a momentary button.

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
   If your button reports a _different_ `usagePage`/`usage`, edit the `USAGE_*` constants near the top of [`src/dji-wisprer.c`](src/dji-wisprer.c). To change the **single**-click key set `DJI_WISPRER_EMIT` (`fn` / `chord` / `custom`, plus `DJI_WISPRER_KEYCODE` / `DJI_WISPRER_MODS`); to change the **double**-click key set `DJI_WISPRER_DOUBLE_KEYCODE` + `DJI_WISPRER_DOUBLE_MODS` (both accept `control,option,command,shift,fn`; default is `Fn+Z`), and tune the tap-grouping window with `DJI_WISPRER_DOUBLE_MS` (milliseconds, default 600). Set these in the LaunchAgent and rebind Wispr accordingly.
3. To make it permanent, edit the `ProgramArguments` in `~/Library/LaunchAgents/com.djiwisprer.bridge.plist` to include your ids, then `launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge`.

---

## Troubleshooting

| Symptom | Cause / Fix |
| --- | --- |
| Button still changes the **volume** | Service isn't seizing. Check `cat /tmp/dji-wisprer.log`. `seize/open failed 0xe00002e2` → grant **Input Monitoring** to the binary. Make sure you're on **USB**, not Bluetooth. |
| Log shows `emitted single` / `emitted double` but **nothing happens** in Wispr | macOS is blocking keystroke injection. The startup banner says which: `accessibility (can inject keystrokes): NO` → grant **Accessibility** (see below). If it says `YES`, macOS is dropping the synthesized **Fn** key (common) — switch that gesture to a chord (e.g. `DJI_WISPRER_EMIT=chord` for single, or `DJI_WISPRER_DOUBLE_MODS=control,option` for double), reload, and bind the chord in Wispr. |
| Double click fires **two singles** instead | Your two taps were farther apart than the window. The log prints `pulse (gap since last: N ms, window M ms)` — if `N > M`, raise the window: `DJI_WISPRER_DOUBLE_MS` in the LaunchAgent, reload. (Keep taps relaxed, though — a _fast_ double triggers Bluetooth pairing.) |
| Wispr says **"must include a modifier key"** | The keystroke landed but without modifiers (chord mode). Make sure you're running the current build (it holds real Ctrl/Opt keys). |
| Button press does **nothing** in Wispr's recorder | **Accessibility** not granted/active — confirm with the `accessibility …: NO` banner line. Re-add the binary and `launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge`. |
| Worked, then broke after I **recompiled** | Ad-hoc signatures are content-hashed; rebuilding changes the hash and **invalidates the Accessibility grant** (the banner flips to `NO`). Remove the stale entry and re-add the binary in Accessibility, then reload. |
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

- 100% local, no network. The whole thing is ~280 lines of C in [`src/`](src) — read it.
- It **seizes** the matched DJI HID device (exclusive grab) and **synthesizes** one of two fixed keystrokes (single- or double-click). It does not log keystrokes or read any other device.
- It needs **Input Monitoring** (to read/seize the mic's HID) and **Accessibility** (to post the keystroke). Both are standard macOS TCC permissions you grant explicitly.

## License

MIT — see [LICENSE](LICENSE).

## Disclaimer

This is an independent, unofficial project. **Not affiliated with, endorsed by, or sponsored by Wispr Flow or DJI.** "Wispr Flow" and "DJI" are trademarks of their respective owners and are used here only to describe interoperability (nominative fair use).

---

_Built by reverse-engineering what the DJI receiver actually sends to macOS: it exposes an HID interface, its button is a single `Consumer/Volume Up` event, and a user-space seize is enough to repurpose it — no Karabiner, no driver, no IT ticket._
