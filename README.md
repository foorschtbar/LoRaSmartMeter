# LoRaSmartMeter

WIP...

## Flags

| Build Flag  | function                                                        |
| ----------- | --------------------------------------------------------------- |
| `LORA_OFF`  | Disable LoRa function                                           |
| `FAKE_SML`  | Parse a static test SML-Message every second and turns LoRa off |
| `SML_DEBUG` | Turn debug messages from SML parser lib on                      |

## Payload Decoder

```javascript
function decodeUplink(input) {
  var bytes = input.bytes;

  var version = (bytes[0] >> 4) + "." + (bytes[0] & 0xf); // Firmware version
  var activePosEnergyTotal = readInt(bytes, 1);
  var activeNegEnergyTotal = readInt(bytes, 9);
  var sumActivePower = readInt(bytes, 17, 4);
  var sumActivePowerL1 = readInt(bytes, 21, 4);
  var sumActivePowerL2 = readInt(bytes, 25, 4);
  var sumActivePowerL3 = readInt(bytes, 29, 4);
  var voltageL1 = readInt(bytes, 33, 4); 
  var voltageL2 = readInt(bytes, 37, 4); 
  var voltageL3 = readInt(bytes, 41, 4); 

  return {
    data: {
      version: version,
      activePosEnergyTotal: activePosEnergyTotal,
      activeNegEnergyTotal: activeNegEnergyTotal,
      sumActivePower: sumActivePower,
      sumActivePowerL1: sumActivePowerL1,
      sumActivePowerL2: sumActivePowerL2,
      sumActivePowerL3: sumActivePowerL3,
      voltageL1: voltageL1,
      voltageL2: voltageL2,
      voltageL3: voltageL3
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

- [olliiiver/sml_parser: C++ library to parse Smart Message Language (SML) data from smart meters.](https://github.com/olliiiver/sml_parser)
- [Smartmeter D0, SML](https://www.msxfaq.de/sonst/bastelbude/smartmeter_d0_sml.htm)
- [Smart Message Language — Stromzähler auslesen – Die Schatenseite](https://www.schatenseite.de/2016/05/30/smart-message-language-stromzahler-auslesen)
- [Volkszähler TTL Schreib- Lesekopf für Digitale Stromzähler](https://forum.iobroker.net/post/386478)
- [BSI - Bundesamt für Sicherheit in der Informationstechnik - BSI TR-03109-1 Anlage IVb: Feinspezifikation "Drahtgebundene LMN-Schnittstelle" Teil b: "SML – Smart Message Language"](https://www.bsi.bund.de/SharedDocs/Downloads/DE/BSI/Publikationen/TechnischeRichtlinien/TR03109/TR-03109-1_Anlage_Feinspezifikation_Drahtgebundene_LMN-Schnittstelle_Teilb.html)
- [SML-Interface](https://www.stefan-weigert.de/php_loader/sml.php)
