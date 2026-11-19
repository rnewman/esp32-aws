# ESP32 Makita® Auto Start Wireless (AWS®) handling

An open-source ESP32 firmware to detect Makita® AWS® broadcasts.

This is [ESPHome](https://esphome.io/) firmware that lets an ESP32 stand in for a Makita WUT02 receiver. A tool fitted with a WUT01 Bluetooth module broadcasts its trigger state. The receiver picks that up and switches a relay, so your dust extractor starts with the tool and stops a few seconds after it.

It runs on:

| Device | Chip | What it drives | Status |
|---|---|---|---|
| [M5Stack NanoC6](https://docs.m5stack.com/en/core/M5NanoC6) | ESP32-C6 | A relay on the Grove port (your wiring) | Detection, pairing and LED tested with a track saw; relay not yet |
| [Athom ESP32-C3 Plug (US)](https://www.athom.tech/blank-1/esp32-c3-us-plug-for-esphome) | ESP32-C3 | Its own mains relay, with power metering | Compiles, untested |
| [Shelly Plug US Gen4](https://us.shelly.com/products/shelly-plug-us-gen4) | ESP32-C6 | Its own mains relay, with power metering | Tested with a track saw and a 40 W load |

Each device also reports to Home Assistant over the native API: tool present, trigger, tool state, RSSI, and the relay. Home Assistant isn't required, though. Pairing and relay switching all happen on the device.

Not affiliated with or endorsed by Makita or Festool. Makita and AWS are trademarks of Makita Corporation.

## How it works

A WUT01 sends a short, fixed Bluetooth LE advertisement whose payload is its current state. Nothing is encrypted and the WUT01 doesn't need a connection: the receiver just listens. Each tool is identified by its Bluetooth address, which is a static random address that stays the same.

```
02 01 06              Flags
05 FF b0 b1 b2 b3     Manufacturer data (b0 b1 are not a real company ID)
```

| State | Bytes | Meaning |
|---|---|---|
| poweron | `FC 33 03 05` | Battery in, AWS not armed. The trigger is ignored. |
| init | `FD 33 06 06` | AWS button clicked |
| waiting | `FC 33 03 06` | Armed and idle |
| trigger | `FD AA 03 06` | Trigger pulled |
| pair | `FF 33 0C 07` | Pairing (WUT01 LED flashing green) |
| unpair | `EF 33 18 08` | Unpairing |

Observed with a WUT01 in a track saw and no Makita receiver anywhere nearby:

- The WUT01 advertises about 5–10 times a second in `poweron`, and 15–20 times a second once armed.
- Holding the tool's AWS button puts it into `pair` for about 20 s, after which it moves to `waiting`. It did this even though the receiver only listened and never sent anything back.
- Before it has ever paired, a short click of the AWS button gives `init` for about 2 s, then it falls back to `poweron`.
- Once paired, every battery insert starts in `poweron`. One short click of the AWS button arms it (`init`). The first trigger pull sends `trigger`, and the tool then alternates between `trigger` and `waiting`.
- On release the tool sends `waiting` straight away, so the receiver doesn't have to wait for a timeout.

Unrecognised payloads from a known tool are logged as warnings. If you see one, please open an issue with the bytes.

## Using it

### Pairing

1. Hold the receiver's button for at least 2s (the LED shows blue), then release. The receiver listens for 20 s.
2. Hold the tool's AWS button until the WUT01 LED flashes green.
3. The receiver blinks white. The pairing is saved to flash about 5s later.

The receiver remembers up to four tools, dropping the oldest when full. While nothing is paired, it responds to any WUT01 in range, which is handy for testing but annoying on a shared jobsite.

| Hold the receiver button | On release |
|---|---|
| ≥ 2s | Pairing mode for 20s |
| ≥ 5s | Unpair mode for 20s: put the WUT01 into unpair mode to remove it |
| ≥ 10s | Forget all tools |

The same actions are exposed as buttons in Home Assistant.

### Day to day

Put the battery in, then click the tool's AWS button once. When you pull the trigger the relay turns on, and it turns off 3s after you release. You can change the run-on with the `run_on` substitution.

### Status LED

| | NanoC6 / Shelly ring | Athom (single blue LED) |
|---|---|---|
| No tool seen | Off | Off |
| Tool seen, idle | Red | On |
| Trigger pulled | Green | Fast flicker |
| Pairing mode | Blue pulse | Slow blink |
| Unpair mode | Orange pulse | Double blink |
| Just paired/unpaired | White blink | Rapid burst |

While you hold the button, the LED previews what releasing it will do.

## Dust extractors

### Festool CT15 and other auto-start extractors

A CT in auto mode starts when the tool plugged into its socket draws enough current. The figures people quote for the minimum range from 0.4 A to 0.9 A. Not verified yet; the plug variants' power meters will measure it.

1. Set the CT to auto and plug the Athom or Shelly plug into the CT's tool socket.
2. Plug a 100–150 W incandescent or halogen bulb into the plug. That draws about 0.8–1.25 A at 120 V.
3. On a trigger pull the relay lights the bulb, the CT sees the load and starts, and it runs on after the bulb goes off as it normally would.

An LED bulb draws far too little to work. The plug draws a few hundred milliwatts on its own, which shouldn't be enough to start the CT, but this is untested.

### Extractors without auto-start

Switch the extractor on and plug it into the relay plug. Check that its running and inrush current are within the plug's rating first.

### NanoC6

The NanoC6 drives a relay on its Grove port; set the `relay_pin` substitution to `GPIO1` or `GPIO2` to match your relay unit. Wiring is up to you.

**Mains is dangerous.** Unless you're qualified to build and enclose a mains relay safely, use one of the plug variants, which keep the mains side inside a finished product.

## Building

You need [ESPHome](https://esphome.io/guides/installing_esphome) 2026.10 or later.

```sh
git clone https://github.com/rnewman/esp32-aws.git
cd esp32-aws
cp secrets.yaml.example secrets.yaml   # then fill it in
esphome run m5nanoc6-makita-aws.yaml
```

The first flash needs a USB cable. After that, updates go over the air.

- **Athom plug:** it ships with ESPHome already, so you can flash it over the air from its web UI or with `esphome run`. The CSE7766 power meter uses the serial pins, so this config turns off serial logging; read logs over WiFi instead.
- **Shelly Plug US Gen4:** a plain ESPHome build won't boot on this plug. Convert the plug first with [automatous-io/shelly-gen4-esphome](https://github.com/automatous-io/shelly-gen4-esphome), following its INSTALL.md (through the stock web UI, no disassembly). Then flash this config over the air. The config pulls in that project's partition component so the plug keeps Shelly's flash layout.

  What worked here: a new plug on stock 1.7.99 was running from slot 0. Installing the offered stock update (2.0.3) moved it to slot 1. The v1.5.2 conversion zip, uploaded through the stock web UI, then converted it. Once the converted firmware was on WiFi, `esphome upload shelly-plug-makita-aws.yaml --device <ip>` flashed this config. The web conversion can't back up the stock firmware, so if you want a way back, take a UART backup first.

### Files

| File | |
|---|---|
| `makita_aws.h` | Advert parser and state names |
| `aws-common.yaml` | Shared logic (scanning, pairing, relay, Home Assistant entities), included as a package |
| `*-makita-aws.yaml` | One per device: pins, LED, button, power metering |

To support another ESP32, copy a device file. Your device config needs to provide three things:
- a switch with the id `extractor_relay`
- a button that runs the `aws_button_pressed` and `aws_button_released` scripts
- something that displays the `led_mode` global

### Sniffing

Set the logger to `VERBOSE` with `aws: VERBOSE` to get a per-second count of adverts and the latest payload. The `Tool sent other advert` warnings show the raw bytes of anything else a known tool sends.

## Prior art

- [Makita AWS on Wikipedia](https://en.wikipedia.org/wiki/Makita_AWS): message table and pairing procedure.
- [cillian64/makita_aws_rx](https://github.com/cillian64/makita_aws_rx): MicroPython receiver for the Pico W.
- [the-ress/makita-aws-adapter](https://github.com/the-ress/makita-aws-adapter): ESPHome receiver for the ESP32-C3.
- [automatous-io/shelly-gen4-esphome](https://github.com/automatous-io/shelly-gen4-esphome): Shelly Gen4 conversion, pinout and calibration.
- [athom-tech/esp32-configs](https://github.com/athom-tech/esp32-configs): Athom plug pinout.

## License

Apache 2.0. See [LICENSE](LICENSE).
