# SDLPal USB Host Keyboard Phase 1 Design

## Goal

Add non-blocking USB Host keyboard diagnostics to
`projects/Edgi_Talk_M55_SDLPAL`. The firmware must enumerate one directly
connected standard USB HID Boot Keyboard through the existing CherryUSB
Infineon DWC2 port and print key transitions plus future SDLPal control
mappings to the serial console.

This phase does not feed keyboard input into SDLPal. Touch input, the on-screen
controls, and the current game viewport remain unchanged.

## Scope

Phase 1 includes:

- Initialize CherryUSB Host bus 0 at `USBHS_BASE` during application startup.
- Enable the CherryUSB Host core, Infineon DWC2 host controller, and HID class
  in the checked-in SDLPal project configuration.
- Retain CherryUSB's HID class registration record in the target linker
  script.
- Accept one HID interface whose subclass is Boot Interface and whose protocol
  is Keyboard.
- Receive and decode standard 8-byte Boot Keyboard input reports.
- Print press and release transitions for ordinary keys, modifiers, and the
  eight keys that will control SDLPal in phase 2.
- Handle keyboard disconnect and later re-enumeration without restarting the
  game.
- Leave a pure-C report decoder that phase 2 can reuse.

Phase 1 excludes:

- SDL event generation or changes to `PalEngineBridge_PollEvent()`.
- Disabling touch input or removing on-screen controls.
- Viewport resizing or display scaling changes.
- NKRO keyboards, proprietary gaming keyboard reports, non-Boot report IDs,
  consumer/media controls, USB hubs, and multiple active keyboards.
- A Kconfig choice between touch and keyboard input; that belongs to phase 3.

## Integration Approach

Keep the CherryUSB core and HID class source unchanged. The project supplies
strong definitions of CherryUSB's weak `usbh_hid_run()` and
`usbh_hid_stop()` hooks. CherryUSB remains responsible for controller service,
enumeration, class matching, endpoint creation, and disconnect-time URB
cancellation. The SDLPal project owns only keyboard qualification, report
transport, decoding, and diagnostics.

The alternatives were rejected for the following reasons:

- Adding a callback registration API to CherryUSB would change a shared
  third-party component and widen the impact to unrelated projects.
- Polling devices and endpoints from the SDLPal application thread would make
  disconnect races, URB ownership, and response latency harder to control.

## Components

### Application Startup

`applications/main.c` initializes the keyboard diagnostics resources before
calling a project-level host start function. That function calls
`usbh_initialize(0, USBHS_BASE, NULL)` and logs its result.

USB initialization is not part of the SDLPal boot readiness state machine.
Failure to start the Host controller, absence of a keyboard, or later keyboard
errors must not block SD-card validation, touch initialization, engine startup,
or the existing heartbeat loop.

### Pure Keyboard Core

`platform/pal_usb_keyboard_core.c` and its header contain no RT-Thread or
CherryUSB dependencies. They own:

- An 8-byte Boot Keyboard report representation.
- Previous-state tracking.
- Report validation and `ErrorRollOver` detection.
- Duplicate Usage suppression.
- Press and release edge generation for six ordinary key slots and all eight
  modifiers.
- State reset that releases held keys on disconnect.
- HID Usage names for standard Boot Keyboard keys, with `UNKNOWN` fallback.
- The future game-control mapping described below.

The core reports semantic transitions through a caller-provided callback. It
does not print, submit USB transfers, or generate SDL events.

### CherryUSB Port

`platform/pal_usb_keyboard_port.c` and its header own target-only behavior:

- Static worker-thread, message-queue, and active-keyboard state.
- The strong `usbh_hid_run()` and `usbh_hid_stop()` hooks.
- Validation of Boot Interface subclass, Keyboard protocol, interrupt IN
  endpoint, and single-keyboard ownership.
- A `SET_PROTOCOL(Boot)` request before starting input transfers.
- Interrupt IN URB submission and resubmission.
- A DMA buffer in `.cy_socmem_data`, aligned to CherryUSB's 32-byte Infineon
  DWC2 requirement.
- Serial formatting and rate-limited diagnostic counters.

The USB completion callback only copies a complete report into a static,
non-blocking message queue, accounts for errors, and resubmits the URB when it
is valid to do so. A dedicated RT-Thread worker consumes the queue, calls the
pure keyboard core, and prints transitions. This keeps `rt_kprintf()` and
report parsing out of the USB completion context.

### Linker Integration

`board/linker_scripts/link.ld` adds the same CherryUSB class-registration
boundary symbols used by the M55 USB reference projects:

```text
__usbh_class_info_start__
KEEP(*(.usbh_class_info))
__usbh_class_info_end__
```

The block belongs in the read-only image near the existing RT-Thread init
records. Without the `KEEP`, section garbage collection could remove the HID
class descriptor, leaving a running Host controller with no HID class match.
The existing `.cy_socmem_data` load/copy/output definitions already satisfy the
Infineon DWC2 DMA-buffer requirement and remain unchanged.

## Data Flow

```text
main
  -> initialize keyboard queue/thread
  -> usbh_initialize(bus 0, USBHS_BASE)
  -> CherryUSB enumerates device and HID interface
  -> usbh_hid_run(hid)
  -> qualify Boot Keyboard and select Boot protocol
  -> submit interrupt IN URB
  -> completion callback copies report and resubmits URB
  -> keyboard worker decodes report
  -> serial press/release log
```

On disconnect, CherryUSB first cancels the HID URBs and then calls
`usbh_hid_stop()`. The project marks the instance inactive, queues a disconnect
message without retaining the soon-to-be-freed CherryUSB class pointer, and
releases single-keyboard ownership. The worker emits releases for held keys,
resets its state, and logs the disconnect. A later keyboard can then claim the
port normally.

## Key Mapping and Logs

Every changed Boot Keyboard key is logged. The phase-2 control annotations are:

| HID key | Future SDLPal control |
| --- | --- |
| Up Arrow | `PAL_CONTROL_UP` |
| Down Arrow | `PAL_CONTROL_DOWN` |
| Left Arrow | `PAL_CONTROL_LEFT` |
| Right Arrow | `PAL_CONTROL_RIGHT` |
| Enter | `PAL_CONTROL_A` |
| Escape | `PAL_CONTROL_B` |
| Page Up | `PAL_CONTROL_PGUP` |
| Page Down | `PAL_CONTROL_PGDN` |

Other keys use `action=NONE`. Examples:

```text
[PAL USB] host ready: bus=0 base=0x44900000
[PAL USB] keyboard connected: vid=0x1234 pid=0x5678 ep=0x81 mps=8
[PAL KEY] DOWN usage=0x52 key=UP action=PAL_CONTROL_UP
[PAL KEY] UP   usage=0x52 key=UP action=PAL_CONTROL_UP
[PAL KEY] DOWN usage=0x04 key=A action=NONE
[PAL USB] keyboard disconnected
```

Repeated reports with unchanged state produce no key log. Standard key names
cover letters, digits, punctuation, navigation keys, function keys, keypad
keys, and modifiers. Unsupported Usage values remain observable as
`key=UNKNOWN` with their hexadecimal value.

## Error Handling

- Host initialization failure is logged once; SDLPal continues with touch.
- A mouse, non-Boot HID interface, or additional keyboard is logged and
  ignored by the project hook.
- Failure of `SET_PROTOCOL(Boot)` or the first URB submission rejects that
  keyboard instance and leaves the game running.
- A successful report must be exactly 8 bytes. Other lengths increment an
  invalid-report counter and do not mutate key state.
- `-USB_ERR_NAK` causes a normal URB resubmission and is not logged per
  occurrence.
- Terminal URB errors stop resubmission for that instance and produce a
  rate-limited error summary.
- Queue-full conditions increment a dropped-report counter. The worker reports
  the accumulated count when it next runs instead of printing from the USB
  callback.
- Reports containing `ErrorRollOver`, `POSTFail`, or `ErrorUndefined` do not
  mutate ordinary-key state. One rate-limited rollover warning is printed, and
  the next valid report reconciles the state.
- Disconnect clears all held keys before accepting another keyboard, preventing
  stale state from crossing device generations.

## Configuration

The checked-in `.config` and generated `rtconfig.h` for
`Edgi_Talk_M55_SDLPAL` enable:

- `RT_USING_CHERRYUSB`
- `RT_CHERRYUSB_HOST`
- `RT_CHERRYUSB_HOST_DWC2_INFINEON`
- `RT_CHERRYUSB_HOST_HID`

Touch and LCD symbols remain enabled. Phase 1 does not add an input-mode
Kconfig choice and does not enable MSC, UVC, or other unused USB classes.

## Automated Verification

The existing Host test framework gains keyboard tests with the same C99,
warning, and error settings as the other platform tests.

Pure-core tests cover:

- Initial key press, unchanged report, and release.
- Simultaneous ordinary keys and modifiers.
- Six-key capacity and duplicate Usage suppression.
- Invalid lengths without state mutation.
- Rollover preservation followed by recovery.
- Disconnect-time releases and clean reconnect state.
- Names for representative ordinary, navigation, keypad, and modifier keys.
- Exact mapping of the eight future SDLPal controls and `NONE` for other keys.

Port lifecycle tests use RT-Thread and CherryUSB fakes to cover:

- Mouse and non-Boot HID rejection.
- Boot Keyboard acceptance and Boot protocol selection.
- Initial URB submission, successful-report resubmission, and NAK resubmission.
- Terminal-error stop behavior.
- Disconnect behavior and the prohibition on resubmission after stop.

Project contract tests assert that the four required CherryUSB symbols are in
the target configuration, the linker retains `.usbh_class_info`, target-only
USB code is present, and `PalEngineBridge_PollEvent()` still uses touch only.
Verification also runs the complete Host C/C++ and Python test suite, an ARM
firmware build, the ELF constraint checker, and the existing four-rotation
build matrix.

## Board Acceptance

The phase is accepted on the Edgi-Talk PSoC Edge board when all of the following
have been observed and recorded through the serial console:

- Boot without a keyboard reaches the game and retains working touch input.
- Inserting a standard wired keyboard produces normal CherryUSB enumeration
  followed by the project `keyboard connected` line.
- Each of the eight mapped keys produces one DOWN and one UP line with the
  expected action.
- An ordinary key and a modifier produce correct named transitions with
  `action=NONE`.
- A multi-key combination produces independent transitions, while a held key
  does not repeatedly print unchanged state.
- Disconnect while keys are held emits releases and a disconnect line.
- Reconnecting the same or another standard Boot Keyboard enumerates and logs
  normally.
- A USB mouse may enumerate in CherryUSB but is explicitly ignored by the
  project and does not disturb the game.
- Twenty consecutive keyboard disconnect/reconnect cycles complete without a
  crash or stuck state.
- A 30-minute run has no crash, hang, or continuous USB error output.
