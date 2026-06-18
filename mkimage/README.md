when the rpi bootrom is booting from SPI, it will expect the 32bit BE magic number 0x55aaf00f at the start of the SPI image
it must then be followed by a 32bit BE length, and then $length bytes of payload

the ROM will load that payload into the L2 cache, and execute it, just like it does with `bootcode.bin` or `recovery.bin`


on the pi4/pi5, a filesystem is then built around that requirement, where a new triplet of magic+length+payload follows the previous payload (starting offset rounded up to the next multiple of 8), creating a singly linked list of magic+length+payload triplets, where you can just read the magic+length, and then skip $length bytes ahead


the following magic numbers are known
| magic    | description |
| -------- | ----------- |
| 55aaf00f | the main stage1 binary, must be at offset 0 |
| 55aaf11f | a file the user is meant to be modifying, the payload has a `char filename[16]` at the front, then `$length - 16` bytes of contents |
| 55aaf22f | a hidden file, same format as 55aaf11f |
| 55aaf33f | a [compressed file](https://git.venev.name/hristo/rpi-eeprom-compress/), the payload has a `char filename[16]` at the front, `$length - 16 - 32` bytes of content, and a `uint8_t sha256_uncompressed_hash[32]` at the end of the payload |
| 55aafeef | padding entries, payload is pure 0xff, intended to keep later files on an SPI block erase boundary, even if earlier files change in length |
| aa55f11f | uncompressed open firmware files, the payload has a `char filename[16]` at the front, and a `uint8_t sha256_hash[32]` at the end like with 55aaf33f, but the body is uncompressed |

currently, the open firmware only uses 55aaf00f, 55aafeef, and aa55f11f
the magic for open firmware has been modified so it wont parse closed files by accident

an example eeprom:
```
00000000  55 aa f0 0f 00 00 f4 fc  00 00 00 00 00 00 00 00  |U...............|
...
0000f500  f4 f4 00 80 ff ff ff ff  55 aa fe ef 00 00 02 f0  |........U.......|
0000f800  aa 55 f1 1f 00 11 f9 67  73 74 61 67 65 32 2e 65  |.U.....gstage2.e|
0000f810  6c 66 00 00 00 00 00 00  7f 45 4c 46 01 01 01 00  |lf.......ELF....|
```
at offset 0, is the 55aaf00f for stage1, which is 0xf4fc bytes long

at offset 0xf508, is the 55aafeef for padding, 0x2f0 bytes long

and at offset 0xf810 is a `stage2.elf`, 0x11f967 bytes long, and you can see the start of the ELF header
