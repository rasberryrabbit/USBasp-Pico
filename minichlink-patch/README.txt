minichlink - Windows x64 (USBasp-Pico SWIO)

Contents
- minichlink.exe
- libusb-1.0.dll (required runtime library)

Keep both files in the same folder. Open Command Prompt or PowerShell in
that folder and run:

  .\minichlink.exe -C usbasppico -i
  .\minichlink.exe -C usbasppico -w firmware.bin 0x08000000

The USBasp-Pico must be connected and have its SWIO firmware installed.
Windows may also need a libusb-compatible USB driver for the USBasp-Pico;
Zadig can install one for the device (VID:PID 16C0:05DC).

This is a command-line program. Run .\minichlink.exe -h to see its options.
