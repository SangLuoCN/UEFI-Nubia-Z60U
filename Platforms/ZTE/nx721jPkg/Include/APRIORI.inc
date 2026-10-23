APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF QcomPkg/Drivers/IortDtDxe/IortDtDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/EnvDxe/EnvDxeEnhanced.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/Smp2pDxe/Smp2pDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/SSRDxe/SSRDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/nx721j/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/nx721j/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf

!if $(NX721J_VENDOR_HW) == 1 && $(NX721J_STOCK_RUNTIME) == 1
  INF Binaries/nx721j/QcomPkg/Drivers/ResetRuntimeDxe/ResetRuntimeDxe.inf
!else
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
!endif

  INF Binaries/nx721j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  INF Binaries/nx721j/MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF Binaries/nx721j/MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/UFSDxe/UFSDxe.inf

  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF Binaries/nx721j/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

!if $(NX721J_VENDOR_HW) == 1 && $(NX721J_STOCK_RUNTIME) == 1
  INF Binaries/nx721j/QcomPkg/Drivers/VariableDxe/VariableDxe.inf
!else
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
!endif

  INF Binaries/nx721j/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/nx721j/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf

  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf


  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

  INF Binaries/nx721j/QcomPkg/Drivers/RscDxe/RscDxe.inf
!if $(NX721J_ZTE_CONFIG) == 1
  INF Binaries/nx721j/QcomPkg/Drivers/VendorLogfsDxe/VendorLogfsDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/CfgUtilDxe/CfgUtilDxe.inf
  INF Binaries/nx721j/QcomPkg/Drivers/NubiaBoardDxe/NubiaBoardDxe.inf
!endif
}
