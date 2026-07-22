# Setup guide

Step-by-step install for **dji-wisprer**. The whole thing takes ~3 minutes; the only fiddly part is two macOS permission toggles.

> **Key idea:** by default a **single click** emits **Fn** — the same key most people already bind to Wispr — so it reuses your existing shortcut and works with no extra Wispr setup. A **double click** emits a second keystroke (**Fn+Z** by default) you can bind to any other Wispr action, so one button drives two gestures. It never touches your keyboard's Wispr shortcut either way (Wispr Flow lets you bind several shortcuts to the same action). Prefer a separate, collision-proof binding? Choose the unique **Ctrl+Opt+F18** chord at install instead.

---

## 1. Prerequisites

- macOS (tested on 26.5, Apple Silicon)
- Xcode Command Line Tools: `xcode-select --install`
- [Wispr Flow](https://wisprflow.ai) installed
- DJI mic connected via its **USB receiver** (not Bluetooth — over Bluetooth the button sends macOS nothing)

---

## 2. Install

```sh
git clone https://github.com/joyjeet-sarkar/dji-wisprer.git
cd dji-wisprer
./install.sh
```

### Choosing your key (the install asks you)

`install.sh` prompts:

```
Which key should the DJI button send to Wispr?
  1) Fn / Globe     (default — reuses your existing Wispr keyboard shortcut)
  2) Ctrl+Opt+F18   (unique 'phantom' chord; bind it separately to Wispr Hands-free)
  3) Custom         (advanced — enter a macOS keycode + modifiers)
```

- **Option 1 — `Fn / Globe` (default).** This is the key most people already bind to Wispr, so the DJI button reuses your existing shortcut and works with no extra Wispr setup. Caveat: macOS does **not** always let software synthesize the Fn key, so on some setups it may not register — if the button does nothing, switch to Option 2. (Note too that the DJI button only _taps_, while a keyboard Fn is often _hold-to-talk_, so the feel can differ.)
- **Option 2 — `Ctrl+Opt+F18`.** `F18` is a "phantom" function key that doesn't physically exist on a Mac keyboard and has no default binding, so this chord can never be typed by accident or clash with another app or with your keyboard's Wispr shortcut. It's the most reliable choice; you bind it to Wispr's **Hands-free** action (a clean tap-on / tap-off toggle — perfect for a momentary button) as a _new_ shortcut.
- **Option 3 — Custom.** Enter any [macOS virtual keycode](https://eastmanreference.com/complete-list-of-applescript-key-codes) plus modifiers (`control,option,command,shift`). Use this to match a specific Wispr shortcut you already have, or to avoid a collision.

> ### Why your keyboard shortcut keeps working
>
> Wispr Flow supports **up to 4 shortcuts per action**. With the **Fn** default the DJI button simply emits the same key your keyboard already uses, so it rides on your existing binding — nothing to add. If you choose **Ctrl+Opt+F18** instead, you add it as an _additional_ binding in Wispr; your keyboard trigger stays exactly as it was and both keep working. Either way your keyboard shortcut is never touched.

---

## 3. Grant permissions (two macOS toggles)

`install.sh` opens **System Settings → Privacy & Security → Accessibility** and a Finder window at the binary.

1. **Accessibility** — drag `dji-wisprer` from the Finder window into the list (or `+` → `⌘⇧G` → paste `~/Library/Application Support/dji-wisprer/dji-wisprer`), then toggle it **ON**. This lets it send the keystroke.
2. **Input Monitoring** — if prompted, allow it too. This lets it read/seize the mic's button.
3. Reload so it picks up the grants:
   ```sh
   launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge
   ```

> Both are ordinary macOS permissions (the same kind Zoom/Loom use). They are **not** a system extension, so an MDM/IT driver block does not apply.

---

## 4. Add the shortcut in Wispr

If you kept the **Fn** default and already use Fn for Wispr, a single click works right away — you can skip this step. Otherwise (or to bind it explicitly): **Wispr Flow → Settings → General → Shortcuts** → next to **Hands-free** (or whichever action you want), click the box so it reads _"listening…"_, then **single-click the DJI volume button**. Wispr captures the key you chose at install (e.g. `Fn` or `⌃⌥F18`). **Save.**

For `Ctrl+Opt+F18` you can't type F18 on a Mac keyboard — pressing the button _is_ how you enter it. Your existing keyboard shortcut is still listed and still works.

### Optional: bind the double click

Want a second gesture? In the same **Shortcuts** screen, click the box of _another_ action so it reads _"listening…"_, then **double-click the DJI button** at a relaxed pace (two deliberate taps within the grouping window — **not** a fast drum-roll, which triggers the mic's own Bluetooth pairing). Wispr captures `Fn Z` (the default double-click key). **Save.**

> A single click is only recognized _after_ the grouping window elapses (default 600 ms, so the double click has time to arrive), which means single clicks fire with that much delay. To trade off latency vs. how fast you must double-tap, tune `DJI_WISPRER_DOUBLE_MS` (see below). If Wispr won't accept `Fn+Z`, swap the double to a plain chord with `DJI_WISPRER_DOUBLE_KEYCODE=6 DJI_WISPRER_DOUBLE_MODS=control,option` (→ `Ctrl+Opt+Z`).

---

## 5. Test

Click into any text field → **tap the DJI volume button** → dictation toggles on → speak → **tap again** → it stops and inserts your text. 🎤

---

## Changing the key later (no rebuild needed)

Both keys and the double-click window live in the LaunchAgent's environment, so you can switch them without recompiling (the Accessibility grant stays valid — recompiling would invalidate it):

```sh
# edit ~/Library/LaunchAgents/com.djiwisprer.bridge.plist  → EnvironmentVariables
#   single click:  DJI_WISPRER_EMIT = fn | chord | custom  (+ DJI_WISPRER_KEYCODE / _MODS)
#   double click:  DJI_WISPRER_DOUBLE_KEYCODE + DJI_WISPRER_DOUBLE_MODS  (default Fn+Z)
#   tap window:    DJI_WISPRER_DOUBLE_MS  (milliseconds, default 600)
#   (_MODS lists accept: control, option, command, shift, fn)
launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge
```

Then re-record the shortcut(s) in Wispr to match. The startup banner in `/tmp/dji-wisprer.log` echoes the active single/double keys, the window, and whether Accessibility is granted.

---

## Troubleshooting

See the table in the [README](README.md#troubleshooting). Quick checks:

```sh
launchctl print gui/$(id -u)/com.djiwisprer.bridge | grep -E 'state|pid'
tail -f /tmp/dji-wisprer.log
```
