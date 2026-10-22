#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/GraphicsOutput.h>

EFI_STATUS
EFIAPI
Nx721jStage1Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Banner;

  Gop = NULL;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) {
    DEBUG ((DEBUG_ERROR, "[NX721J] STAGE1: GOP unavailable, status=%r\n", Status));
    return EFI_SUCCESS;
  }

  DEBUG ((
    DEBUG_ERROR,
    "[NX721J] STAGE1: GOP ready base=0x%lx size=0x%lx %ux%u stride=%u\n",
    Gop->Mode->FrameBufferBase,
    Gop->Mode->FrameBufferSize,
    Gop->Mode->Info->HorizontalResolution,
    Gop->Mode->Info->VerticalResolution,
    Gop->Mode->Info->PixelsPerScanLine
    ));

  ZeroMem (&Banner, sizeof (Banner));
  Banner.Blue = 0x20;
  Banner.Green = 0xA0;
  Banner.Red = 0x20;
  Gop->Blt (
    Gop,
    &Banner,
    EfiBltVideoFill,
    0,
    0,
    0,
    0,
    Gop->Mode->Info->HorizontalResolution,
    48,
    0
    );

  // Keep the temporary boot image on screen long enough to identify Stage 1.
  gBS->Stall (5 * 1000 * 1000);
  return EFI_SUCCESS;
}
