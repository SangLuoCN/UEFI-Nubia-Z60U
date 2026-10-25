/**
  Identity of the NX721J passive UniXNU FAT payload.

  These values are generated from the payload selected for this UEFI build.
  They are not a UFS geometry or a filesystem driver; they prevent a stale
  Android initrd range or unrelated RAM from being exposed as Block I/O.
**/
#ifndef NX721J_PAYLOAD_IDENTITY_H
#define NX721J_PAYLOAD_IDENTITY_H

#define NX721J_PAYLOAD_BYTES      0x0C000000ULL
#define NX721J_PAYLOAD_CRC32      0xDF7BEA8EU
#define NX721J_PAYLOAD_FAT_LABEL  "NX721J     "

#endif
