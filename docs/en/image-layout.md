# AirLink image layout

The release image is 1,694,499,328 bytes.

| Partition | Start | Sectors | Filesystem |
|---|---:|---:|---|
| Boot | 1 | 32768 | FAT16 |
| RootFS | 32769 | 3276800 | ext4 |

The boot partition contains `fip.bin`, `boot.sd`, `usb.host`, `ver`
and `board`. BLCP_2ND in the FIP contains the R27P C906L firmware at
`0x8fe00000`. The FIT image contains Linux, ramdisk and the board DTB.
