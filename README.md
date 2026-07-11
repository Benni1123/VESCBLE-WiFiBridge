## Getting Started

### Hardware Setup:
**What you need:**
- ESP32 S3 Dev Board
- Some Wires
- Fitting connector for your Vesc Uart port (Most likely JST-PH2.0)
- USB C Cable

Connect the ESP32 S3 to your Vesc controller following the table below.

| ESP32 S3    || VESC          |
| ----------- |-| -------------|
| 5V          |->| 5V          |
| GND         |->| GND         |
| 20          |->| RX          |
| 21          |->| TX          |

If it doesnt work you can try swapping rx and tx pins cause they are swapped on some vesc controllers.

### Flashing Firmware:
To flash the firmware to the esp you need the following prerequisites:
- [Visual Studio Code](https://code.visualstudio.com/)
- [PlattformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)  extension for VSCode

Once you have VSCode and its PlattformIO extension installed, you need to open this git repo in VSCode. Then connect the esp32 to your computer and hit upload.

If you have trouble you can read the official [PlattformIO docs](https://docs.platformio.org/en/latest/integration/ide/vscode.html#ide-vscode)

## Contributing

We welcome contributions from the community! If you have ideas for improvements, feature requests, or bug reports, please open an issue or submit a pull request.

## Support

For any questions or issues, feel free open an issue.

Happy riding!
