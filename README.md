# LoRaSmartMeter

## Payload Decoder

```javascript
function decodeUplink(input) {
  var bytes = input.bytes;

  var fwVersion = (bytes[0] >> 4) + "." + (bytes[0] & 0xf); // Firmware version
  var activePosEnergyTotal = readInt(bytes, 1);
  var activeNegEnergyTotal = readInt(bytes, 9);
  var sumActivePower = readInt(bytes, 17);
  var sumActivePowerL1 = readInt(bytes, 25);
  var sumActivePowerL2 = readInt(bytes, 33);
  var sumActivePowerL3 = readInt(bytes, 41);

  return {
    data: {
      fwVersion: fwVersion,
      activePosEnergyTotal: activePosEnergyTotal,
      activeNegEnergyTotal: activeNegEnergyTotal,
      sumActivePower: sumActivePower,
      sumActivePowerL1: sumActivePowerL1,
      sumActivePowerL2: sumActivePowerL2,
      sumActivePowerL3: sumActivePowerL3,
    },
    warnings: [],
    errors: [],
  };
}

function readInt(array, start, size = 8) {
  var value = 0;
  var first = true;
  var pos = start;
  while (size--) {
    if (first) {
      let byte = array[pos++];
      value += byte & 0x7f;
      if (byte & 0x80) {
        value -= 0x80; // Treat most-significant bit as -2^i instead of 2^i
      }
      first = false;
    } else {
      value *= 256;
      value += array[pos++];
    }
  }
  return value;
}
```

## Links

- [olliiiver/sml_parser: C++ library to parse Smart Message Language (SML) data from smart meters.](https://github.com/olliiiver/sml_parser ")
- [Smartmeter D0, SML](https://www.msxfaq.de/sonst/bastelbude/smartmeter_d0_sml.htm)
- [Smart Message Language — Stromzähler auslesen – Die Schatenseite](https://www.schatenseite.de/2016/05/30/smart-message-language-stromzahler-auslesen)
- [Volkszähler TTL Schreib- Lesekopf für Digitale Stromzähler](https://forum.iobroker.net/post/386478)
