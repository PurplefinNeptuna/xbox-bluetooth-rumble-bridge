# Xbox Bluetooth Rumble Bridge

Some Bluetooth gamepads report themselves to Android as Xbox controllers but
arrive without a usable vibrator. Their buttons work in games; vibration does
not. This root module creates a virtual Android gamepad with `FF_RUMBLE` and
passes each rumble command to the physical controller through Bluetooth HID.
It targets Bluetooth devices reporting `045e:02fd`, including third-party
controllers that use that ID.

The verified device is a GuliKit TT Max in **Bluetooth Android mode** on a
Samsung SM-X810 running Android 16 and KernelSU Next. Eden was used for the
game test. Other `045e:02fd` devices, Magisk, APatch, and other Android builds
have not been tested. A shared vendor and product ID does not prove two
controllers use the same output report.

## Why a bridge is needed

On the tested tablet, the TT Max's Bluetooth Android mode exposed buttons and
sticks but no `EV_FF` capability. Android therefore told gamepad testers and
games that the controller had no vibration motor. A direct report to its
`/dev/hidraw` device started both motors; a zero-strength report stopped them.
The motors and Bluetooth output path worked, while the input device offered no
force feedback interface to Android.

Replacing an Xbox `.kl` file cannot fix this. [Android key layout files](https://source.android.com/docs/core/interaction/input/key-layout-files)
translate scan codes to keys. They do not add force feedback or change a
Bluetooth controller's output protocol.

```text
controller buttons and sticks -> evdev -> bridge -> virtual gamepad -> Android / Eden
controller motors             <- HID   <- bridge <- FF_RUMBLE       <- Android / Eden
```

The bridge matches Bluetooth `045e:02fd` input to a hidraw node belonging to
the same HID device. It reads that device's HID descriptor to select a known
report shape: the TT Max's tested nine-byte report or the seven-byte Xbox
Bluetooth report described by the [Linux Microsoft HID driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-microsoft.c).
If neither shape is present, the bridge logs the unsupported report and leaves
the controller alone. Report shape is only a compatibility check; hardware
testing is still needed before another controller can be called supported.

The bridge copies the physical gamepad's keys and axes into a virtual `uinput`
device, then grabs and forwards its input events. The virtual device advertises
`FF_RUMBLE` and retains the Xbox vendor and product ID for Android's existing
button layout. Two motor magnitudes are scaled and rounded separately. When
effects overlap, the highest requested strength for each motor wins. Each
effect has its own start and stop time, with at most **3000 ms** of playback.
The bridge sends an explicit stop report when both motors are idle. A forced
process kill or a lost Bluetooth connection can prevent that last report; turn
the controller off if vibration continues unexpectedly.

## Install or upgrade

The ZIP contains an arm64 program and a boot service. Install it from the
**Modules** screen in KernelSU Next, Magisk, or APatch; do not flash it from
recovery. KernelSU Next is the only manager tested with this project. The
[Magisk module guide](https://topjohnwu.github.io/Magisk/guides.html) and
[APatch module guide](https://apatch.dev/apm-guide.html) describe the same
`service.sh` and `customize.sh` hooks used by this package. Their runtime
behavior with this bridge remains unverified.

1. If `TT Max Bluetooth Rumble Bridge` (`v0.2.0`) is installed, remove or
   disable it in your root manager and reboot. The new module has a different
   ID; running both would make them compete for the same controller.
2. Download `xbox-bluetooth-rumble-bridge-v0.3.0.zip` from the release and
   install it in your root manager's Modules screen. Reboot.
3. Pair the controller over Bluetooth. For the TT Max, select **Android mode**.
4. In Eden's Player 1 controls, choose **Xbox Bluetooth Rumble Bridge** and
   enable controller vibration. The renamed virtual device may require you to
   redo your button mapping.

In a gamepad tester, select **Xbox Bluetooth Rumble Bridge** and verify its
buttons, sticks, and vibration. Test a short vibration and confirm that the
motors stop. On the tested KernelSU Next tablet, the module starts after boot
and retries when the controller reconnects. It currently bridges one
controller at a time.

The `v0.3.0` ZIP was installed on the tested tablet after removing `v0.2.0`.
After reboot, Eden controls and rumble worked. Separate motor pulses, a
three-second pulse, overlapping effects, and their stop times were checked
through the virtual input device.

For diagnostics, read
`/data/adb/modules/xbox-bluetooth-rumble-bridge/bridge.log` through your root
manager or a permitted root shell. If the virtual controller does not appear,
check its Bluetooth mode, the module's enabled state, and the log. Messages
about an unreadable HID descriptor or an unsupported report mean the bridge
left that controller alone. If controls work but a game does not vibrate,
select the virtual controller in the game and enable its vibration setting.

To remove the bridge, disable or uninstall its module in the root manager and
reboot. The physical controller will then use its normal Android input path.

## Build and test

On a Linux host with Clang, LLD, and Python 3:

```sh
./build.sh
sh -n build.sh module/service.sh module/customize.sh
python3 -m unittest discover -s tests -v
sha256sum dist/xbox-bluetooth-rumble-bridge-v0.3.0.zip
```

Generated binaries and ZIPs stay in the ignored `dist/` directory. CI checks
the arm64 executable, archive contents and permissions, report encoding, and
effect timing. A host test cannot establish that a physical controller accepts
a report. The release ZIP must be installed and tested on a device before
publication.

## Limits and permissions

- Bluetooth `045e:02fd` is the only discovery ID. Other Xbox product IDs,
  wired modes, and the TT Max's Switch mode are outside this release.
- With two matching Bluetooth gamepads connected, the bridge selects the first
  matching input event node it scans and creates one virtual controller for
  it. The other gamepad stays on its normal Android input path; this module
  adds no rumble to that second pad. The selected pad's HID output is matched
  to its own physical device, so its rumble is not sent to the other pad.
  If the selected pad disconnects, the service retries and may then select
  the remaining one. If the first pad has an unsupported HID report, retries
  keep selecting it and a compatible second pad may never be reached.
- The two main `FF_RUMBLE` motor strengths are separate. Trigger motors,
  motion sensors, consumer-control buttons, and Switch HD rumble are not
  forwarded. An app must send two distinct strengths to use the motors
  differently.
- The TT Max report has eight-bit motor fields; the Xbox report specifies
  values from 0 to 100. Rounding avoids losing small nonzero effects, but
  cannot create more physical strength levels. Motor response is not
  necessarily linear.
- Effect playback is capped at three seconds. Stop timing is subject to the
  service's polling interval and Bluetooth delivery. An abrupt process kill
  cannot guarantee a stop report.
- The HID descriptor selects the report layout, not guaranteed compatibility.
  Other `045e:02fd` controllers are experimental until tested on hardware.
- KernelSU Next is verified. Magisk and APatch install hooks are included but
  have not been tested on either manager. Their SELinux domains and device
  labels can differ by build.
- The service requires root access to input, `uinput`, and hidraw devices.
  On KernelSU Next, `sepolicy.rule` grants its `ksu` domain access to the
  relevant device types. The installer adapts the policy for APatch's domain;
  Magisk uses its own root-domain policy. These grants cover device types,
  not only the paired controller, so install only in a root environment you
  trust.

If you test another `045e:02fd` controller, a useful report includes its
model, firmware, root manager, Android version, HID report descriptor or
report length, and whether each motor starts and stops. Remove Bluetooth
addresses and device serials from logs before sharing them.

## Credits

PurplefinNeptuna directed and tested the bridge. OpenAI Codex helped develop
the code and documentation. The project is available under the [MIT License](LICENSE).
