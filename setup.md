# Setup guide

Step-by-step install for **dji-wisprer**. The whole thing takes ~3 minutes; the only fiddly part is two macOS permission toggles.

> **Key idea:** this adds a _separate_ shortcut for the DJI button. It never touches your keyboard's Wispr shortcut. Wispr Flow lets you bind several shortcuts to the same action, so **your keyboard key (e.g. Fn) and the DJI button both keep working.** During install you choose which key the DJI button sends — pick one that doesn't collide with your keyboard shortcut.

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
  1) Ctrl+Opt+F18   (recommended — unique 'phantom' chord; bind to Wispr Hands-free)
  2) Fn / Globe      (experimental — macOS rarely lets software synthesize Fn)
  3) Custom          (advanced — enter a macOS keycode + modifiers)
```

- **Option 1 — `Ctrl+Opt+F18` (recommended).** `F18` is a "phantom" function key that doesn't physically exist on a Mac keyboard and has no default binding, so this chord can never be typed by accident or clash with another app or with your keyboard's Wispr shortcut. You'll bind it to Wispr's **Hands-free** action (a clean tap-on / tap-off toggle — perfect for a momentary button).
- **Option 2 — `Fn / Globe`.** Tempting (it's what your keyboard already uses), but macOS does **not** reliably let software synthesize the Fn key, so this is best-effort and may simply not register. Also note the DJI button can only _tap_, while Fn is usually _hold-to-talk_ — so even if it fires, the feel differs. Use only if you want to experiment.
- **Option 3 — Custom.** Enter any [macOS virtual keycode](https://eastmanreference.com/complete-list-of-applescript-key-codes) plus modifiers (`control,option,command,shift`). Use this to match a specific Wispr shortcut you already have, or to avoid a collision.

> ### Why both your keyboard and the DJI keep working
>
> Wispr Flow supports **up to 4 shortcuts per action**. Your keyboard trigger stays exactly as it was. dji-wisprer just makes the DJI button emit a _different_ shortcut, which you add as an _additional_ binding in Wispr. Press the keyboard key → Flow runs. Tap the DJI button → Flow runs. Neither disables the other. The only rule: don't pick a DJI key identical to a key you press for something else, or you'll trigger Flow unintentionally — which is exactly why the unique `Ctrl+Opt+F18` is the default.

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

**Wispr Flow → Settings → General → Shortcuts** → next to **Hands-free** (or whichever action you want), click the box so it reads _"listening…"_, then **press the DJI volume button once**. Wispr captures the key you chose at install (e.g. `⌃⌥F18`). **Save.**

You can't type `F18` on a Mac keyboard — pressing the button _is_ how you enter it. Your existing keyboard shortcut is still listed and still works.

---

## 5. Test

Click into any text field → **tap the DJI volume button** → dictation toggles on → speak → **tap again** → it stops and inserts your text. 🎤

---

## Changing the key later (no rebuild needed)

The key lives in the LaunchAgent's environment, so you can switch it without recompiling (the Accessibility grant stays valid):

```sh
# edit ~/Library/LaunchAgents/com.djiwisprer.bridge.plist  → EnvironmentVariables
# e.g. set DJI_WISPRER_EMIT to chord | fn | custom (+ DJI_WISPRER_KEYCODE / _MODS)
launchctl kickstart -k gui/$(id -u)/com.djiwisprer.bridge
```

Then re-record the shortcut in Wispr to match.

---

## Troubleshooting

See the table in the [README](README.md#troubleshooting). Quick checks:

```sh
launchctl print gui/$(id -u)/com.djiwisprer.bridge | grep -E 'state|pid'
tail -f /tmp/dji-wisprer.log
```
