##
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = nx721j
  PLATFORM_GUID                  = 4F09ED65-1C90-4E79-9E86-8A39B7E1E2C6
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/nx721jPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = nx721jPkg/nx721j.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  # 0 = Stage 1: keep the ABL framebuffer and load Mu drivers only.
  # 1 = load the NX721J stock vendor hardware chain after the Stage 1 proof.
  NX721J_VENDOR_HW               = 0

  # 0 = known-good display-only Stage 1.
  # 1 = add only the Qualcomm storage dependency closure for UFS probing.
  NX721J_UFS_TEST                = 0

  # 0 = UFS-first: defer ztecfg/logfs/board consumers until UFS Block I/O works.
  # 1 = dispatch the stock NX721J ztecfg/logfs/board consumer chain.
  NX721J_ZTE_CONFIG               = 0

  # 0 = Ace5-style generic VariableRuntimeDxe/ResetSystemRuntimeDxe.
  # 1 = use the stock NX721J VariableDxe/ResetRuntimeDxe pair.
  NX721J_STOCK_RUNTIME            = 0

  #
  # 0 = SM8650-AA
  # 1 = SM8650-Q-AA
  # 2 = SM8650-AB. Z60U live qcom,msm-id matches the reconstructed NX721J DTB.
  # 3 = SM8650-Q-AB
  # 4 = SM8650-AC
  #
  SOC_TYPE                       = 2

!include LanaiPkg/LanaiPkg.dsc.inc

[PcdsFixedAtBuild]
!if $(NX721J_STOCK_RUNTIME) == 0
  # Generic VariableRuntimeDxe has no NX721J FVB in temporary bring-up
  # images. Use a RAM-backed variable store until the stock storage path is
  # proven; the NX721J stock VariableDxe path keeps its original behavior.
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
!endif

  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"ZTE"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"NX721J"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"nubia Z60 Ultra"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"NX721J"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"NX721J"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1116
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2480
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferFontScale|2
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferDelay|30000

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

!if $(NX721J_VENDOR_HW) == 0
  # Without the stock vendor chain, do not consume stale XBL vendor HOBs.
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|FALSE
!endif

[LibraryClasses]
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|nx721jPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|nx721jPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  # Stage 1 proof driver.  The FDF APRIORI/DXE lists control dispatch order.
  nx721jPkg/Drivers/Nx721jStage1Dxe/Nx721jStage1Dxe.inf

  # Official UniXNU firmware integration.  These are public boot-layer
  # components; no Ace5/giulia hardware binaries are used here.
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf

  #
  # ACPI Tables
  #
  #nx721j/AcpiTables.inf
