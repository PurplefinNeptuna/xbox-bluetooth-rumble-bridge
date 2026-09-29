# TT Max Bluetooth rumble bridge

The GuliKit TT Max has working motors, but on our rooted Android tablet its
Bluetooth Android mode appeared to games as a controller without rumble. This
KernelSU Next module gives that mode an Android gamepad with a vibrator and
forwards vibration commands to the TT Max over Bluetooth. The goal is simple:
working Xbox-style rumble without a cable.

This is a device-specific bridge. It was developed and tested with a GuliKit
TT Max in **Bluetooth Android mode** (`045e:02fd`) on a Samsung SM-X810 running
Android 16 and KernelSU Next. Eden was used for the in-game test. Other
controllers, modes, and Android builds have not been verified.

## What led to the bridge

The controller's behavior changed with its connection mode:

| Connection | Android sees | Rumble result |
| --- | --- | --- |
| Bluetooth Android mode | `GuliKit Controller AD`, `045e:02fd` | Buttons and sticks worked; Android exposed no `EV_FF` motor. |
| Bluetooth NS mode | `Nintendo Switch Pro Controller`, `057e:2009` | Buttons worked; Android exposed no `EV_FF` motor. |
| USB PC mode | `Microsoft X-Box 360 pad`, `045e:028e` | The kernel's `xpad` driver exposed `FF_RUMBLE`; a gamepad tester drove the physical motors. |

On the tested tablet, Bluetooth Android mode also created a `/dev/hidraw`
device. A direct output report sent through that device started the motors. A
second report with zero motor strength stopped them. That established a usable
Bluetooth output path even though Android did not offer one to games.

We initially considered replacing an Xbox `.kl` key layout with a PlayStation
layout. [Android key layout files](https://source.android.com/docs/core/interaction/input/key-layout-files)
translate input scan codes into Android key codes. They do not add a force
feedback device or change the controller's Bluetooth output protocol. The
bridge addresses the missing output path instead.

## How it works

```text
TT Max buttons and sticks -> evdev -> bridge -> uinput gamepad -> Android / Eden
TT Max motors            <- hidraw <- bridge <- FF_RUMBLE    <- Android / Eden
```

The arm64 bridge finds the TT Max by its Bluetooth bus, `045e:02fd` ID, and
`GuliKit Controller AD` input name. It copies the physical device's key and
axis capabilities to a virtual gamepad made with Linux
[`uinput`](https://docs.kernel.org/input/uinput.html). It grabs the physical
input stream and forwards button, axis, and synchronization events to the
virtual device, so Android receives one working set of controls. The virtual
device advertises `FF_RUMBLE`, which makes Android list it as a gamepad with a
vibrator. It uses the same vendor and product ID so the tablet's existing Xbox
key layout still maps its buttons.

When an app uploads and plays a rumble effect, the bridge converts its strong
and weak motor magnitudes to the TT Max's tested Bluetooth HID report. It sends
an explicit zero-strength report when playback stops. While the process is
running, it also stops an effect after its requested duration, capped at one
second. The timeout matters because the direct HID test showed that the
controller can keep vibrating until it receives a stop report. An abrupt
process kill cannot provide that guarantee; power off the controller if the
motors ever continue unexpectedly.

The KernelSU service waits for Android to finish booting, then starts the
bridge when the TT Max is present. It retries after disconnection, so a later
Bluetooth reconnection creates a fresh virtual gamepad. It changes no system
key layout files. The bridge itself is a small, statically linked arm64 program
that uses Linux syscalls directly; the build does not need the Android NDK.

## Install

1. Download `ttmax-rumble-bridge-v0.2.0.zip` from the GitHub release.
2. Install the ZIP in KernelSU Next's Modules screen and reboot.
3. Pair the TT Max with the Android tablet in **Bluetooth Android mode**.
4. In Eden's Player 1 controls, choose **TT Max Rumble Bridge** and enable
   controller vibration.

The virtual pad should also appear in a gamepad tester. Select **TT Max Rumble
Bridge**, check a button and stick, then try its rumble control. The physical
motors should start and stop. The `v0.2.0` ZIP was installed on the tested
tablet, followed by a reboot. Eden controls and physical rumble worked after
the controller reconnected, and the rumble stopped normally.

If you have ADB, `adb shell dumpsys input` should list the bridge with a
`VIBRATOR` class. The service writes diagnostics to
`/data/adb/modules/ttmax-rumble-bridge/bridge.log`.

To disable or remove the module, use KernelSU Next's Modules screen and
reboot. The original controller input path returns when the bridge is no
longer running.

## Build from source

On a Linux host, install Clang, LLD, and Python 3, then run:

```sh
./build.sh
python3 -m unittest discover -s tests -v
sha256sum dist/ttmax-rumble-bridge-v0.2.0.zip
```

The build compiles `src/ttmax_bridge.c` for arm64 and packages it with the
KernelSU module files. Outputs stay in the ignored `dist/` directory. The
module version in `module/module.prop` determines the ZIP name. CI checks the
same build, the archive's files and permissions, and the executable's ELF
architecture. These checks cannot prove physical rumble; that requires a
controller and an Android device.

## Limits and permissions

- Only the TT Max's tested Bluetooth Android mode is supported. NS mode, USB
  mode, other controller models, and other tablets have not been validated.
- The bridge forwards keys, axes, and synchronization events. It does not
  forward motion sensors or the controller's separate consumer-control input.
- Standard `FF_RUMBLE` carries two motor strengths. The bridge does not expose
  trigger motors or the full detail of Switch HD rumble.
- The current device search matches the HID vendor and product ID. If two
  devices with that same ID are connected, the output path may choose the
  wrong one.
- `sepolicy.rule` allows KernelSU's process to read input devices, use
  `uinput`, and write devices labeled `ovr_device`. SELinux applies these rules
  to device types, not just this controller. Install the module only on a
  tablet where you trust the KernelSU environment and its other modules.

If the virtual pad does not appear, confirm the TT Max is in Android mode and
the module is enabled, then inspect `bridge.log`. If buttons work but a game
does not rumble, check that the game uses **TT Max Rumble Bridge** and has
controller vibration enabled. If vibration continues after a process failure,
power off the TT Max before further testing.

## Credits and references

PurplefinNeptuna directed and tested this project. The initial bridge and
documentation were developed with OpenAI Codex. The Bluetooth rumble report
was checked against the [Linux HID Microsoft driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-microsoft.c),
then tested on the TT Max itself. Module packaging follows the
[KernelSU module guide](https://kernelsu.org/guide/module.html).

The project is released under the [MIT License](LICENSE).
