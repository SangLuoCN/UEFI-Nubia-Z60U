/** @file
  Expose the Android boot ramdisk handed over by the NX721J bootloader.

  This is a board-port adapter for the standard Linux device-tree handoff:
  ABL writes the ramdisk range into /chosen/linux,initrd-start and
  /chosen/linux,initrd-end.  The official UniXNU boot manager consumes the
  resulting EFI filesystem through Block I/O and FatDxe.

  The implementation deliberately contains no Qualcomm register addresses,
  UFS code, vendor protocols, or Ace5/sheng device binaries.  The sheng
  project was used only to confirm this generic handoff contract.
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DiskIo.h>
#include <Protocol/SimpleFileSystem.h>

#include "Nx721jPayloadIdentity.h"

#define NX721J_RAMDISK_BLOCK_SIZE  512
#define NX721J_RAMDISK_DIAGNOSTIC_VERSION  26

// This is the NX721J Kernel reservation from MemoryMapLib, not an Ace5 range.
// The ABL DTB initrd range observed on the phone is stale and much smaller.
#define NX721J_KERNEL_RAMDISK_START  0xA8000000ULL
#define NX721J_KERNEL_RAMDISK_END    0xB8000000ULL

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} NX721J_RAMDISK_DEVICE_PATH;
#pragma pack()

STATIC NX721J_RAMDISK_DEVICE_PATH  mRamdiskDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      { (UINT8)sizeof (VENDOR_DEVICE_PATH), (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8) }
    },
    { 0x4d8e3d71, 0x7e4e, 0x4b19, { 0x9d, 0x8e, 0x61, 0xa2, 0x73, 0x18, 0x42, 0x0c } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC UINT8                    *mRamdiskBase;
STATIC EFI_BLOCK_IO_MEDIA       mMedia;

STATIC
UINT64
ReadCells (
  IN CONST VOID  *Data,
  IN UINT32      Cells
  )
{
  if (Cells == 2) {
    return Fdt64ToCpu (ReadUnaligned64 (Data));
  }

  return Fdt32ToCpu (ReadUnaligned32 (Data));
}

STATIC
EFI_STATUS
EFIAPI
RamdiskReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  (VOID)This;
  (VOID)ExtendedVerification;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
RamdiskRead (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                 MediaId,
  IN  EFI_LBA                Lba,
  IN  UINTN                  BufferSize,
  OUT VOID                   *Buffer
  )
{
  (VOID)This;
  if ((Buffer == NULL) || (MediaId != mMedia.MediaId)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % NX721J_RAMDISK_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((Lba > mMedia.LastBlock) ||
      ((BufferSize / NX721J_RAMDISK_BLOCK_SIZE) > (mMedia.LastBlock + 1 - Lba)))
  {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Buffer, mRamdiskBase + Lba * NX721J_RAMDISK_BLOCK_SIZE, BufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
RamdiskWrite (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  (VOID)This;
  (VOID)MediaId;
  (VOID)Lba;
  (VOID)BufferSize;
  (VOID)Buffer;
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
RamdiskFlush (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  (VOID)This;
  return EFI_SUCCESS;
}

STATIC EFI_BLOCK_IO_PROTOCOL  mBlockIo = {
  EFI_BLOCK_IO_PROTOCOL_REVISION,
  &mMedia,
  RamdiskReset,
  RamdiskRead,
  RamdiskWrite,
  RamdiskFlush
};

STATIC
VOID
LogFilesystemState (
  IN EFI_HANDLE  Handle
  );

STATIC
BOOLEAN
IsPayloadAt (
  IN  UINT8  *Base,
  IN  UINT64 AvailableBytes,
  OUT UINT64 *PayloadBytes
  )
{
  UINT32  Crc;
  UINT64  Bytes;

  if ((Base == NULL) || (PayloadBytes == NULL) ||
      (AvailableBytes < NX721J_PAYLOAD_BYTES)) {
    return FALSE;
  }

  if ((Base[510] != 0x55) || (Base[511] != 0xAA) ||
      (ReadUnaligned16 ((CONST UINT16 *)(Base + 11)) != NX721J_RAMDISK_BLOCK_SIZE) ||
      (CompareMem (Base + 82, "FAT32   ", 8) != 0) ||
      (CompareMem (Base + 71, NX721J_PAYLOAD_FAT_LABEL, 11) != 0)) {
    return FALSE;
  }

  Bytes = (UINT64)ReadUnaligned32 ((CONST UINT32 *)(Base + 32)) *
          NX721J_RAMDISK_BLOCK_SIZE;
  if ((Bytes != NX721J_PAYLOAD_BYTES) || (Bytes > AvailableBytes)) {
    return FALSE;
  }

  if (EFI_ERROR (gBS->CalculateCrc32 (Base, (UINTN)Bytes, &Crc)) ||
      (Crc != NX721J_PAYLOAD_CRC32)) {
    return FALSE;
  }

  *PayloadBytes = Bytes;
  return TRUE;
}

STATIC
EFI_STATUS
InstallRamdisk (
  IN UINT64 Start,
  IN UINT64 PayloadBytes
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  ConnectStatus;
  EFI_HANDLE  Handle;

  if ((Start > MAX_UINT64 - PayloadBytes) ||
      (PayloadBytes == 0) || ((PayloadBytes % NX721J_RAMDISK_BLOCK_SIZE) != 0)) {
    return EFI_COMPROMISED_DATA;
  }

  mRamdiskBase            = (UINT8 *)(UINTN)Start;
  mMedia.MediaId          = 1;
  mMedia.RemovableMedia   = FALSE;
  mMedia.MediaPresent     = TRUE;
  mMedia.LogicalPartition = FALSE;
  mMedia.ReadOnly         = TRUE;
  mMedia.WriteCaching     = FALSE;
  mMedia.BlockSize        = NX721J_RAMDISK_BLOCK_SIZE;
  mMedia.IoAlign          = 1;
  mMedia.LastBlock        = PayloadBytes / NX721J_RAMDISK_BLOCK_SIZE - 1;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mRamdiskDevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  NULL
                  );
  DEBUG ((
    DEBUG_ERROR,
    "NX721JMEM: payload at 0x%lx size 0x%lx, Block I/O (%r)\n",
    Start,
    PayloadBytes,
    Status
    ));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // The handle is created after the normal DXE dispatch pass.  Connect the
  // official DiskIo/Partition/Fat stack explicitly before UniXNU starts.
  ConnectStatus = gBS->ConnectController (Handle, NULL, NULL, TRUE);
  DEBUG ((DEBUG_ERROR, "NX721JMEM: connect ramdisk stack (%r)\n", ConnectStatus));
  LogFilesystemState (Handle);
  return Status;
}

STATIC
EFI_STATUS
ScanRamdiskRange (
  IN UINT64 Start,
  IN UINT64 End,
  IN UINT32 Stage
  )
{
  UINT64  At;
  UINT64  PayloadBytes;

  if ((End <= Start) || ((End - Start) < NX721J_PAYLOAD_BYTES)) {
    return EFI_NOT_FOUND;
  }

  Start = ALIGN_VALUE (Start, NX721J_RAMDISK_BLOCK_SIZE);
  DEBUG ((DEBUG_ERROR, "NX721JMEM: v%u scan 0x%lx-0x%lx\n", Stage, Start, End));
  for (At = Start; At <= End - NX721J_PAYLOAD_BYTES; At += NX721J_RAMDISK_BLOCK_SIZE) {
    if (!IsPayloadAt ((UINT8 *)(UINTN)At, End - At, &PayloadBytes)) {
      continue;
    }

    DEBUG ((DEBUG_ERROR, "NX721JMEM: v%u matched NX721J FAT at 0x%lx\n", Stage, At));
    return InstallRamdisk (At, PayloadBytes);
  }

  return EFI_NOT_FOUND;
}

STATIC
VOID
LogFilesystemState (
  IN EFI_HANDLE  Handle
  )
{
  EFI_STATUS  Status;
  VOID        *Protocol;
  EFI_HANDLE  *Handles;
  UINTN       Count;

  Status = gBS->HandleProtocol (Handle, &gEfiDiskIoProtocolGuid, &Protocol);
  DEBUG ((DEBUG_ERROR, "NX721JMEM: DiskIo on ramdisk handle (%r)\n", Status));

  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, &Protocol);
  DEBUG ((DEBUG_ERROR, "NX721JMEM: SimpleFileSystem on ramdisk handle (%r)\n", Status));

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                              ByProtocol,
                              &gEfiSimpleFileSystemProtocolGuid,
                              NULL,
                              &Count,
                              &Handles
                              );
  DEBUG ((DEBUG_ERROR, "NX721JMEM: global SimpleFileSystem handles %u (%r)\n", (UINT32)Count, Status));
  if (Handles != NULL) {
    FreePool (Handles);
  }
}

STATIC
EFI_STATUS
PublishRamdisk (
  IN CONST VOID  *Fdt
  )
{
  EFI_STATUS    Status;
  INT32         Chosen;
  INT32         StartLength;
  INT32         EndLength;
  CONST VOID    *StartProp;
  CONST VOID    *EndProp;
  UINT64        Start;
  UINT64        End;

  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: /chosen is missing\n"));
    goto ScanKernelReservation;
  }

  StartProp = FdtGetProp (Fdt, Chosen, "linux,initrd-start", &StartLength);
  EndProp   = FdtGetProp (Fdt, Chosen, "linux,initrd-end", &EndLength);
  if ((StartProp == NULL) || (EndProp == NULL) ||
      (((StartLength != sizeof (UINT32)) && (StartLength != sizeof (UINT64))) ||
       ((EndLength != sizeof (UINT32)) && (EndLength != sizeof (UINT64)))))
  {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: ABL did not provide an initrd range\n"));
    goto ScanKernelReservation;
  }

  Start = ReadCells (StartProp, StartLength / sizeof (UINT32));
  End   = ReadCells (EndProp, EndLength / sizeof (UINT32));
  DEBUG ((DEBUG_ERROR, "NX721JMEM: ABL initrd range 0x%lx-0x%lx\n", Start, End));

  // The phone's ABL range is not authoritative for this v4 payload.  Accept
  // it only when it is inside NX721J's Kernel reservation and large enough to
  // contain the complete, CRC-checked FAT image.
  if ((Start >= NX721J_KERNEL_RAMDISK_START) &&
      (End <= NX721J_KERNEL_RAMDISK_END) &&
      (End > Start) && ((End - Start) >= NX721J_PAYLOAD_BYTES)) {
    Status = ScanRamdiskRange (Start, End, 1);
    if (Status != EFI_NOT_FOUND) {
      return Status;
    }
  } else {
    DEBUG ((
      DEBUG_ERROR,
      "NX721JMEM: ABL range is stale/short; expected 0x%lx bytes in Kernel reservation\n",
      NX721J_PAYLOAD_BYTES
      ));
  }

  // The NX721J MemoryMapLib reserves this whole window for the Android
  // kernel/ramdisk handoff.  Search only that board-proven reservation; do
  // not scan arbitrary RAM or copy Ace5's Giulia address window.
ScanKernelReservation:
  Status = ScanRamdiskRange (
             NX721J_KERNEL_RAMDISK_START,
             NX721J_KERNEL_RAMDISK_END,
             2
             );
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: no complete NX721J FAT payload found\n"));
  }
  return Status;
}

EFI_STATUS
EFIAPI
Nx721jBootMemoryDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HOB_GUID_TYPE  *Hob;
  UINT64             Dtb;

  Hob = GetFirstGuidHob (&gSiliciumEntryArgHobGuid);
  if (Hob == NULL) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: entry argument HOB is missing\n"));
    return EFI_SUCCESS;
  }

  Dtb = *(UINT64 *)GET_GUID_HOB_DATA (Hob);
  if ((Dtb == 0) || ((Dtb & 3) != 0) || (FdtCheckHeader ((VOID *)(UINTN)Dtb) != 0)) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: x0 0x%lx is not a valid DTB\n", Dtb));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "NX721JMEM: diagnostic v%u entry\n", NX721J_RAMDISK_DIAGNOSTIC_VERSION));
  DEBUG ((DEBUG_ERROR, "NX721JMEM: DTB 0x%lx, %u bytes\n", Dtb, (UINT32)FdtTotalSize ((VOID *)(UINTN)Dtb)));
  PublishRamdisk ((VOID *)(UINTN)Dtb);
  return EFI_SUCCESS;
}
