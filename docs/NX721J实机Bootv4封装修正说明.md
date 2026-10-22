# NX721J 实机 Boot v4 封装修正说明

## 结论

当前 `Mu-nx721j-0.img` 能被 ABL 接收，但它不是按照 Z60U 原厂启动链和 UniXNU 官网推荐的 v4 方式封装的。`fastboot boot` 返回成功只能证明 ABL 接受了 Android Boot Image，不能证明 BootShim 已经进入 UEFI。

## 实机只读证据

2026 年 10 月 23 日，手机处于 TWRP recovery，当前槽位为 `_a`。从 `boot_a` 和 `vendor_boot_a` 的首个 4 KiB 读取到：

| 项目 | 实机值 |
|---|---:|
| `boot_a` magic | `ANDROID!` |
| `boot_a` header version | `4` |
| `boot_a` header size | `1584` |
| `boot_a` kernel size | `35564032` |
| `boot_a` ramdisk size | `0` |
| `boot_a` signature size | `0` |
| `vendor_boot_a` header version | `4` |
| `vendor_boot_a` page size | `4096` |
| `vendor_boot_a` vendor ramdisk size | `8781792` |
| `vendor_boot_a` DTB size | `2763902` |
| `vendor_boot_a` bootconfig size | `198` |

原始头部备份在 Git 外部目录：

`C:\Users\SangLuo\Desktop\Projects\UniXNU-Z60U-artifacts\2026-10-22-twrp-readonly\headers-2026-10-23`

## 当前错误镜像与实机的差异

当前 `Mu-nx721j-0.img` 的头部是：

- header version 为 `4`，但 `ramdisk_size` 为 `5`，内容是 `dummy`；
- `signature_size` 为 `4096`，而实机 boot v4 的值为 `0`；
- 写入了自定义 `os_version` 和 `os_patch_level`，没有复用实机头部；
- gzip 载荷后又拼接了从 OnePlus giulia 复制来的 DTB；
- 这个 DTB 不是 NX721J 的完整原厂设备树。

这些差异共同导致当前实验不能作为“UEFI 已成功运行”的证据。尤其是 Z60U 的原厂 Android Boot v4 已经通过 `vendor_boot_a` 提供 DTB，不能再沿用 sheng 的旧版 v0 “gzip 载荷加 appended DTB”方式，也不应把 giulia DTB 塞进 Z60U 的 v4 kernel 区域。

## 官网流程对应关系

官网的移植顺序要求先完成 UEFI 阶段 1：

1. ABL 接受 boot image。
2. BootShim 校验 ARM64 Android kernel header，复制或原地执行 UEFI FD。
3. UEFI 提供 GOP、Simple Text、Simple FS、Block IO 和可用的串口服务。
4. 设备能进入菜单，显示 framebuffer，并能发现 payload volume。
5. 阶段 1 确认后，才进入 UniXNU 菜单、DarwinBoot、XNU、存储、根文件系统和用户空间。

官网 sheng 的 v4 打包脚本使用实机 boot v4 头部的固定字段，载荷只放 `gzip(BootShim + UEFI FD)`，不追加 DTB；DTB 留给设备上的 `vendor_boot`。NX721J 的实机头部和 vendor_boot 结构与这一条件吻合，所以本工程先采用同一路径。

## 修正后的打包脚本

脚本：

`C:\Users\SangLuo\Desktop\Projects\uefi-z60u\scripts\package-nx721j-boot-v4.py`

运行命令：

```powershell
$python = 'C:\Users\SangLuo\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$header = 'C:\Users\SangLuo\Desktop\Projects\UniXNU-Z60U-artifacts\2026-10-22-twrp-readonly\headers-2026-10-23\boot_a.header.bin'
& $python 'C:\Users\SangLuo\Desktop\Projects\uefi-z60u\scripts\package-nx721j-boot-v4.py' `
  --stock-header $header `
  --output 'C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-v4-stock.img'
```

脚本会检查：

- UEFI FD 必须是 `0x300000` 字节；
- BootShim 必须含 `ARM\x64` header；
- BootShim header 描述的长度必须正好覆盖 `BootShim + UEFI FD`；
- 输出镜像的 kernel、ramdisk、signature 字段和实机 v4 头部一致；
- gzip 解压结果必须正好等于 `BootShim + UEFI FD`；
- 不追加 DTB，DTB 由手机现有 `vendor_boot_a` 提供。

## 下一次实机测试

## 2026 年 10 月 23 日 SEC 内存表修正

修正显示保留区后，Stage 1 已经能进入 UEFI SEC，并在屏幕显示 `MMU: 38 regions`。随后旧版内存表报错：

```text
TZApps_Reserved (0xD8800000) and Display_Reserved (0xE3940000) overlap
```

原因是旧表仍使用 giulia 的 `TZApps_Reserved` 长度 `0x1B000000`，结束地址为 `0xF3800000`。NX721J 实机 DTB 的只读节点是：

```text
/reserved-memory/tz_merged_region = 0xD8000000/0x00800000
/reserved-memory/splash_region    = 0xE3940000/0x02B00000
```

因此 `Platforms/ZTE/nx721jPkg/Library/MemoryMapLib/MemoryMapLib.c` 已改为：

```text
TZApps_Reserved  = 0xD8000000/0x00800000
Display_Reserved = 0xE3940000/0x02B00000
```

修正后重新构建通过，EDK II 验证 50 个镜像。新的临时镜像为：

```text
C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-stage1-v4-e394-tzfix.img
SHA256: e36c3ef18356ffc4485906481e3d725151b05f2f577befe7bdceb5057cb9e6d4
```

下一步仍只执行 `fastboot boot`，不执行 `fastboot flash`。

这次只允许临时启动，不刷写分区：

```powershell
$adb = 'C:\Users\SangLuo\AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\adb.exe'
$fastboot = 'C:\Users\SangLuo\AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\fastboot.exe'
& $adb reboot bootloader
& $fastboot devices
& $fastboot boot 'C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-v4-stock.img'
```

如果仍然只出现闪电标志并回到 charger，下一步不应继续改 XNU 或 GPU，而应在 UEFI 阶段加入最早期串口/内存标记，确认失败发生在 ABL 跳转、BootShim 解压、UEFI DXE、GOP 初始化还是菜单载荷阶段。只有看到 UEFI 菜单或 `UNIXNU:` 日志后，才继续 UniXNU 和 iOS 的后续移植。

本说明不执行 `fastboot flash`、不修改 `boot`、`vendor_boot`、`dtbo`、`abl`、GPT 或 `userdata`。

## 2026 年 10 月 23 日 Stage 1 已进入 BDS

在修正 `Display_Reserved`、`TZApps_Reserved`，并从 Stage 1 中移除依赖 Qualcomm RAM Partition Protocol 的 `RamManagerDxe` 后，使用临时镜像：

```text
C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-stage1-v4-gop-menu.img
SHA256: 7888c097ac277e77201f341a4d5f29ac43c4509a64c65fdda470d21fb1209568
```

实机现象为：屏幕显示 `failed to find operating system`，随后自动重启。这个结果与当前 Stage 1 的驱动列表完全一致，含义如下：

1. `SEC: memory HOBs done`、`SEC: platform HOBs done (Success)` 和 `SEC: loading DXE core` 已经通过；
2. `Nx721jStage1Dxe` 已经通过真实的 `0xE3940000` framebuffer 建立 GOP；
3. `RamManagerDxe` 不再被调度，因此不再触发 `Failed to Locate RAM Partition Protocol`；
4. BDS 已经运行，但 Stage 1 只包含 `DiskIoDxe`、`PartitionDxe`、`FatDxe` 等通用层，没有 Qualcomm UFS 控制器和供电/时钟依赖，也没有 DarwinBoot 或其他 OS 载荷，因此没有可启动的文件系统和 `Boot####`；
5. `SiliciumPkg/Library/BootManagerLib/BootManager.c` 的默认逻辑在无启动项时显示无系统图形，等待 10 秒后调用 `EfiResetShutdown`，所以随后回到 bootloader/Android 是预期行为，不是新的 SEC 或 GOP 崩溃证据。

因此当前已完成“ABL -> BootShim -> SEC -> DXE -> GOP -> BDS”的 Stage 1 验证。下一阶段应建立独立的 UFS 存储测试配置，按 `UFSDxe -> CmdDb/PwrUtils/Rpmh/Npa/ULog/Vcs/Clock/SPMI/ICB/Smem` 的依赖顺序逐组加入，并每次只使用 `fastboot boot` 验证。现有 Stage 1 镜像必须保留为回退基线。

## 2026 年 10 月 23 日 UFS 测试构建

已新增显式开关 `--nx721j-ufs-test`。默认构建仍使用已验证的显示-only Stage 1；打开该开关时，FDF 切换到独立的 UFS 测试列表，加入 `UFSDxe` 及 `CmdDbDxe`、`PwrUtilsDxe`、`RpmhDxe`、`NpaDxe`、`ULogDxe`、`VcsDxe`、`ClockDxe`、`SPMIDxe`、`ICBDxe`、`SmemDxe`，保留 NX721J 的 SimpleFb/GOP，不加入完整 giulia 显示、USB、触控、充电和 RAM 管理链。

构建结果：

```text
EDK II 镜像验证：50
UEFI FD：0x300000 字节
临时镜像：C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-stage1-ufs-test-v4.img
SHA256：50be0cfa1e7703e410bddec6dfae2bae86fe48c95e0d2b648133161ce087de4d
```

## 2026 年 10 月 23 日 RamManagerDxe 与 Ace5 对照修正

重新检查 Ace5 的完整 `giulia.fdf` 后确认，Ace5 并没有移除 `RamManagerDxe`。其完整固件在 `Include/APRIORI.inc`、`Include/DXE.inc` 和 RAW 文件之后明确加入：

```text
INF SiliciumPkg/Drivers/RamManagerDxe/RamManagerDxe.inf
```

因此，之前从 NX721J Stage 1 中去除 `RamManagerDxe` 的理由，只适用于“显示-only 的最小验证链”：当前 `NX721J_VENDOR_HW=0`，没有加载完整 Qualcomm 厂商硬件链，也没有建立 Qualcomm RAM Partition Protocol；若保留该驱动，启动日志会出现 `Failed to Locate RAM Partition Protocol`，随后在 `RamManager.c` 的断言处停止。去除它让我们能够验证 SEC、DXE、GOP 和 BDS，但这不是 Ace5 的最终驱动布局。

当前 `nx721j.fdf` 的条件关系是正确的：

```text
NX721J_VENDOR_HW=0  -> Stage 1，不加载 RamManagerDxe
NX721J_VENDOR_HW=1  -> 完整厂商链，加载 RamManagerDxe
```

下一步不能只恢复 `RamManagerDxe` 单个文件。Ace5 的做法是让完整厂商链、XBL HOB、RAM Partition Protocol 和 RamManager 一起工作。对 NX721J 应先确认以下条件：

1. `PcdBuildXblHobs` 是否应在完整链启用；
2. Z60U ABL 传入的 XBL HOB 是否包含 RAM Partition Protocol 所需的分区信息；
3. `MemoryMapLib` 的 Z60U 物理保留区是否已经覆盖 DDR、TZ merged、display、DBI 和其它不可用区域；
4. `EnvDxe`、`SmemDxe`、DAL/HWIO、RPMh/时钟/电源链是否已经匹配 Z60U，而不是仅仅因为 CPU 同为 SM8650 就直接复用 giulia 二进制。

所以结论是：**RamManagerDxe 从最终全链中去除是不对的，但从当前 Stage 1 中暂时去除是必要的诊断措施。** 当前不重新生成镜像，不把它单独加回 UFS 测试链；待完成 Ace5 与 NX721J 的完整厂商协议和 HOB 对照后，再以完整链整体恢复并测试。

实机测试使用：

```text
fastboot boot Mu-nx721j-stage1-ufs-test-v4.img
```

ABL 返回 `Sending OKAY`、`Booting OKAY`。等待 20 秒后主机没有重新枚举 fastboot 或 ADB；由于该测试镜像仍未加入 USB 调试链，这只能记录为“屏幕结果待确认”，不能单独判定 UFS 已初始化或已卡死。没有执行 `fastboot flash`、擦除、userdata 写入或其他分区修改。

## 2026 年 10 月 23 日 UFS 测试第一处阻塞

用户屏幕照片显示，UFS 测试镜像停在：

```text
Loading driver at ... EntryPoint=... EnvDxeEnhanced.efi
```

之后没有继续加载下一个驱动，也没有进入 BDS。`EnvDxeEnhanced` 是 giulia 厂商环境驱动，不属于 UFS 控制器的必要协议本身；它在 Z60U 上可能等待 giulia 专用环境变量、共享内存或硬件服务。下一版 UFS 测试已从 APRIORI 和 DXE 列表同时移除该驱动，其他 UFS 依赖保持不变，用于定位下一处真正阻塞点。

## 2026 年 10 月 23 日 EnvDxe 停止后的纠正

上一节记录的“移除 `EnvDxeEnhanced`”只是为了定位 UFS 裁剪链的下一处阻塞，不能作为最终移植方案。用户随后使用 Z60U 原厂提取的 `EnvDxeEnhanced.efi` 进行测试，仍然在该驱动位置停止，证明单独替换模块或删除模块都没有修复厂商启动上下文。

按照 Ace5 的完整工程重新检查后，正确方向是恢复完整厂商链：启用 `IortDtDxe`、完整 Qualcomm DXE 依赖、XBL HOB 构建和 `RamManagerDxe`，并把 Z60U 原厂的 `EnvDxeEnhanced`、`SmemDxe`、`UFSDxe` 放入对应的完整链位置。为此新增了 `--nx721j-vendor-hw` 构建开关，并修正 `DeviceBuild.py` 将该开关传递为 `NX721J_VENDOR_HW=1`。

本次完整链镜像：

```text
Mu-nx721j-vendor-stock-v1.img
SHA256: 6D739544888ED3F9EED7307D7E16FD1F71D049E6B73C28AF048CE145019FEB80
```

构建报告确认 `PcdBuildXblHobs=1`，并包含 `IortDtDxe` 与 `RamManagerDxe`。该镜像已经通过 `fastboot boot` 临时发送到实机，未执行任何分区刷写或擦除。下一步根据屏幕实际停点判断是继续通过完整厂商链，还是出现了下一个具体的板级协议或资源不匹配。

## 2026 年 10 月 23 日 v2 实机日志：已经越过 EnvDxe

`Mu-nx721j-vendor-stock-v2.img` 的实机照片 `IMG20261023063358.jpg` 显示，完整厂商链已经完成以下阶段：

- MMU 映射、HOB 构建、FV 解压和 DXE Core 启动成功；
- `IortDtDxe.efi` 加载成功，并报告 `Successfully Enabled Cache Coherency`；
- 原厂 `EnvDxeEnhanced.efi` 已经越过；
- `SmemDxe`、`DALSys`、`HWIODxe`、`ChipInfoDxe`、`PlatformInfoDxeDriver`、`HALIOMMU`、`IPCDxe`、`Smp2pDxe`、`SSRDxe`、`CmdDbDxe`、`PwrUtilsDxe`、`NpaDxe` 和 `RpmhDxe` 已开始或完成加载。

照片中出现 `ERROR: CPU Vectors region not present in cfg file`，但后续驱动仍然继续，当前应记录为配置缺项而不是立即判定为致命错误。照片没有包含底部，因此没有看到 v1 的 UFS sense data 和 `UFS.c +2168` 断言，不能把 v2 的最终停点归因于 UFS。

重新对照配置时发现，v2 仍将 `ULogDxe` 指向 `Binaries/giulia`，这是配置遗漏，不是应该删除 ULog。已改为：

```text
Binaries/nx721j/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
```

## 2026 年 10 月 23 日 v3 构建结果

修正 ULog 路径后，以下构建成功：

```text
build_uefi.py -d nx721j -r DEBUG --nx721j-vendor-hw
EDK II：0050 Images Verified
镜像：C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v3.img
大小：1212416 字节
SHA256：93d9ffbfc7c1bc65ea68fea6f329c2c2cc9632d6eaac670a91ac382138e26432
```

该镜像仍使用临时 Android Boot v4 封装，不追加 giulia DTB，不执行 `fastboot flash`。下一步先让设备回到第一阶段 bootloader，再使用 `fastboot boot` 启动 v3，拍摄日志最底部，确认 `RpmhDxe` 之后是否进入 `UFSDxe` 和文件系统驱动。当前不能因为某个协议警告就删除对应驱动；应先确认它是否真正阻断了后续执行。
