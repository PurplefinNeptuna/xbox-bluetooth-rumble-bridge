# Project guidance

This repository contains a root service for the GuliKit TT Max in Bluetooth
Android mode. The tested path is a Samsung SM-X810 running KernelSU Next.

- Keep `src/ttmax_bridge.c` behavior and the module's SELinux rules in sync with
  the README. Treat controller modes and device IDs as tested facts, not as
  general compatibility claims.
- Build with `./build.sh`. It writes generated files to `dist/`, which stays
  outside Git. Run `sh -n build.sh module/service.sh module/customize.sh` and
  `python3 -m unittest discover -s tests -v` before committing.
- If you change the rumble protocol, input forwarding, or service lifecycle,
  verify the exact packaged ZIP on a device. Test a stop command and a Bluetooth
  reconnect. Do not present a host build as proof of physical rumble.
- Do not publish device serial numbers, Bluetooth addresses, ADB logs, or local
  account credentials. The model, mode, and USB/Bluetooth vendor and product
  IDs in the README are enough to explain the findings.
- Write technical prose plainly. Keep supported details while removing filler,
  inflated claims, and formulaic phrasing. Use the guidance in
  https://github.com/blader/humanizer/blob/main/SKILL.md for documentation and
  commit messages.
- Keep `module.prop` version, Git tag, release ZIP name, and release notes
  consistent. Release only the ZIP installed and tested on the tablet.
