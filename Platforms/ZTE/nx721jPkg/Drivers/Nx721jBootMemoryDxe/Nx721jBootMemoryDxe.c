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

#define NX721J_RAMDISK_BLOCK_SIZE  512
#define NX721J_RAMDISK_DIAGNOSTIC_VERSION  25

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
  EFI_STATUS   Status;
  EFI_STATUS   ConnectStatus;
  INT32        Chosen;
  INT32        StartLength;
  INT32        EndLength;
  CONST VOID   *StartProp;
  CONST VOID   *EndProp;
  UINT64       Start;
  UINT64       End;
  EFI_HANDLE   Handle;

  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: /chosen is missing\n"));
    return EFI_NOT_FOUND;
  }

  StartProp = FdtGetProp (Fdt, Chosen, "linux,initrd-start", &StartLength);
  EndProp   = FdtGetProp (Fdt, Chosen, "linux,initrd-end", &EndLength);
  if ((StartProp == NULL) || (EndProp == NULL) ||
      (((StartLength != sizeof (UINT32)) && (StartLength != sizeof (UINT64))) ||
       ((EndLength != sizeof (UINT32)) && (EndLength != sizeof (UINT64)))))
  {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: ABL did not provide an initrd range\n"));
    return EFI_NOT_FOUND;
  }

  Start = ReadCells (StartProp, StartLength / sizeof (UINT32));
  End   = ReadCells (EndProp, EndLength / sizeof (UINT32));
  if ((End <= Start) || (((End - Start) % NX721J_RAMDISK_BLOCK_SIZE) != 0)) {
    DEBUG ((DEBUG_ERROR, "NX721JMEM: invalid initrd range 0x%lx-0x%lx\n", Start, End));
    return EFI_COMPROMISED_DATA;
  }

  mRamdiskBase              = (UINT8 *)(UINTN)Start;
  mMedia.MediaId            = 1;
  mMedia.RemovableMedia     = FALSE;
  mMedia.MediaPresent       = TRUE;
  mMedia.LogicalPartition   = FALSE;
  mMedia.ReadOnly           = TRUE;
  mMedia.WriteCaching       = FALSE;
  mMedia.BlockSize          = NX721J_RAMDISK_BLOCK_SIZE;
  mMedia.IoAlign            = 1;
  mMedia.LastBlock          = (End - Start) / NX721J_RAMDISK_BLOCK_SIZE - 1;

  DEBUG ((
    DEBUG_ERROR,
    "NX721JMEM: v%u FAT probe bytes %02x %02x %02x %02x sig %02x %02x sectors %lu\n",
    NX721J_RAMDISK_DIAGNOSTIC_VERSION,
    mRamdiskBase[0],
    mRamdiskBase[1],
    mRamdiskBase[2],
    mRamdiskBase[3],
    mRamdiskBase[510],
    mRamdiskBase[511],
    (UINT64)(mMedia.LastBlock + 1)
    ));

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mRamdiskDevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  NULL
                  );
  DEBUG ((DEBUG_ERROR, "NX721JMEM: ramdisk Block I/O 0x%lx-0x%lx (%r)\n", Start, End, Status));
  if (!EFI_ERROR (Status)) {
    //
    // This handle is created after the normal DXE dispatch pass.  NX721J's
    // BDS path does not call ConnectAll before starting UniXNU, so explicitly
    // connect DiskIo/Partition/Fat to publish SimpleFileSystem now.
    //
    ConnectStatus = gBS->ConnectController (Handle, NULL, NULL, TRUE);
    DEBUG ((DEBUG_ERROR, "NX721JMEM: connect ramdisk stack (%r)\n", ConnectStatus));
    LogFilesystemState (Handle);
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
