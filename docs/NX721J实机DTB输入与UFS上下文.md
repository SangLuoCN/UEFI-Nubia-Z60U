# NX721J 实机 DTB 输入与 UFS 上下文

## 当前问题

目标是在不修改任何分区的条件下，让 ZTE Nubia Z60 Ultra（NX721J，SM8650）通过 `fastboot boot` 临时运行完整 NX721J UEFI 厂商链。当前直接阻塞是原厂 `CfgUtilDxe` 读取 `ztecfg` 分区返回 `No Media`，随后原厂错误路径解引用空输出缓冲区并发生数据异常。

这不是删除 `CfgUtilDxe` 的理由。只读 TWRP 采集已经证明 `ztecfg` 存在于 `/dev/block/sdf6`；故障说明 UEFI 尚未把真实 UFS 存储暴露为可读 Block I/O。

## 已确认的板级输入错误

原来的 `Resources/DTBs/nx721j.dtb` 与 `Resources/DTBs/giulia.dtb` 完全相同：

```text
SHA256: E694B5715E2F23EE0906F37C1B111FA3045CB21EF4356ED1A7CAC834D68CDCA3
model: Qualcomm Technologies, Inc. Pineapple MTP,giulia
```

它不是 NX721J 专用输入。Z60U 的只读运行时设备树来自 TWRP 的 `/proc/device-tree`：

```text
model: Qualcomm Technologies, Inc. Pineapple MTP
compatible: qcom,pineapple-mtp, qcom,pineapple, qcom,pineapplep-mtp, qcom,pineapplep, qcom,mtp
soc/ufshc@1d84000/status: ok
```

实机树公开了 UFS HCI、PHY 和 ICE 的实际寄存器窗口。这是启动链应优先使用的板级描述，不能继续把 giulia 副本作为 NX721J 的默认输入。

## 可复现生成

设备树转储和生成的二进制都不进入 Git。工程只保存生成器：`scripts/rebuild_dtb_from_proc_tree.py`。该脚本从外部 `/proc/device-tree` 层级生成 FDT v17，并校验 magic、总长度和各区段偏移。

```powershell
$python = 'C:\Users\SangLuo\AppData\Local\Programs\Python\Python312\python.exe'
$tree = 'C:\Users\SangLuo\Desktop\Projects\UniXNU-Z60U-artifacts\2026-10-22-twrp-readonly\dt\proc-device-tree'
$dtb = 'C:\Users\SangLuo\Desktop\Projects\UniXNU-Z60U-artifacts\2026-10-22-twrp-readonly\dt\nx721j-live-proc.dtb'
& $python scripts\rebuild_dtb_from_proc_tree.py --source $tree --output $dtb
```

当前只读采集结果：

```text
节点: 4867
属性: 28108
大小: 976380 字节
SHA256: 40b0e330adabecf143e61b9066c77849ea81f3c0012cfd7ce6376099f33d3b1e
```

## v13 构建与边界

v13 保留 v12 的完整 NX721J 厂商链，不删除 `RamManagerDxe`、`UFSDxe`、`CfgUtilDxe` 或 `VendorLogfsDxe`。新增的是 `build_uefi.py --dtb <外部文件>`：Android boot image 尾部使用实机重建 DTB，而不是受控 giulia 副本。

```powershell
$python = 'C:\Users\SangLuo\AppData\Local\Programs\Python\Python312\python.exe'
$dtb = 'C:\Users\SangLuo\Desktop\Projects\UniXNU-Z60U-artifacts\2026-10-22-twrp-readonly\dt\nx721j-live-proc.dtb'
& $python build_uefi.py -c -d nx721j -r DEBUG --nx721j-vendor-hw --dtb $dtb
```

构建脚本会在 Windows 自动定位 LLVM `clang.exe` 和 Visual Studio Build Tools 的 `nmake.exe`，并在 EDK II 创建 makefile 前设置 `CLANG_BIN`、`CLANG_HOST_BIN`、`PYTHONUTF8`，不再依赖未记录的 PowerShell 环境变量。

```text
镜像: C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v13-live-proc-dtb.img
大小: 2195456 字节
SHA256: E5FDEA881FEDB1AD3CF0BF9CE5B2C4AB217401B4CC5640FDD510FD625097171C
EDK II: 0050 Images Verified
```

离线验收确认 Android Boot header version 为 4；kernel 区可解压为 BootShim 加 `0x300000` 字节 UEFI FD；kernel 载荷末尾 DTB 哈希为 `40b0e330adabecf143e61b9066c77849ea81f3c0012cfd7ce6376099f33d3b1e`。v13 尚未在实机启动，不能宣称 UFS 已修复。

BootShim 保留 ABL 传入的 `x0`，但这不等价于 XBL 的 `DTBExtnProtocol` 内容。原厂 `UFSDxe` 还会经 DTB 扩展协议查找 `/soc/storage/ufs0`、`init_speed_params`、`ufs0_vcc`、`ufs0_vccq`、`ufs0_vdda`、`ufs0_qref` 等固件配置；Linux 运行时树的 UFS 节点是 `/soc/ufshc@1d84000`。因此 v13 是必要的板级输入修正和诊断版本，不是最终 UFS 修复。

若 v13 仍显示 `Error Reading ztecfg info: No Media`，下一步应定点记录 `DTBExtnProtocol` 实际数据、UFSDxe 选到的节点和返回状态，并修复 XBL HOB/扩展 DTB 的提供链。不得删除 CfgUtil、UFS、变量服务或吞掉异常。

## v14：参照 Ace5 的 UFS-first 调度边界

对照 Ace5 的 `giulia` 和 ZTE `nx729j` 平台后确认，Ace5 的 UEFI 核心链没有 `VendorLogfsDxe`、`CfgUtilDxe`、`NubiaBoardDxe`。这些是板级配置消费者，不是 UFS 控制器本身。Z60U 之前在 UFS 尚未发布介质时调度它们，导致 `CfgUtilDxe` 在 `No Media` 后走入空指针异常。

工程已增加 `NX721J_ZTE_CONFIG`：默认值为 `0`，完整链仍保留 `RamManagerDxe`、NX721J 原厂 `UFSDxe`、`DiskIoDxe`、`PartitionDxe` 和通信/电源链，只延后上述三个 ZTE 配置消费者。传入 `--nx721j-zte-config` 才恢复它们。该开关保留原厂文件，不是删除驱动。

v14 使用实机重建 DTB 和完整硬件链构建成功：

```text
镜像：C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v14-ufs-first.img
大小：2191360 字节
SHA256：576BAEE8F0BD08D26CCCBBA8FAD55BCCAC00591136BCFF06C4A27B0F1F055C26
EDK II：0050 Images Verified
```

主机端构建后查询 `fastboot devices` 为空，所以当时 v14 尚未实机启动。之后 v14 已通过临时 `fastboot boot` 发送到设备，未执行永久刷写。

## v14 实机结果

v14 已通过临时 `fastboot boot` 启动。用户照片 `IMG20261023205537_BURST000_COVER.jpg` 是循环日志；按用户确认的循环规则，当前最新停点仍是 `ASSERT [DxeCore] DxeMain.c(581)`。照片下方的 USB、Buttons、Tsens、SimpleFb 和 Limits 内容属于较早的循环内容，不能当作本轮已经越过的日志。日志中没有 `VendorLogfsDxe`、`CfgUtilDxe`、`NubiaBoardDxe`、`ztecfg No Media` 或 `CfgUtilDxe.dll` 异常，说明 UFS-first 开关确实隔离了此前的板级配置崩溃路径。

照片中最新停点前有 `Variable Arch Protocol not present`、`Variable Write Arch Protocol not present`、`Reset Arch Protocol not present` 和 `ASSERT [DxeCore] DxeMain.c(581)`，这才是当前需要修复的终止点。USB 的 NPA/PMIC/SPMI/Type-C 错误来自循环中的旧内容，当前不能据此判断它们是终止原因。

Ace5 和 ZTE `nx729j` 参考链的 `APRIORI/DXE` 都使用通用 `VariableRuntimeDxe` 和 `ResetSystemRuntimeDxe`，而当前 NX721J 完整链默认使用原厂 `VariableDxe` 和 `ResetRuntimeDxe`。因此下一版增加 `NX721J_STOCK_RUNTIME` 开关，默认采用 Ace5 的通用运行时链；原厂模块保留，只有传入 `--nx721j-stock-runtime` 才启用。先验证通用链能否消除当前四个架构协议缺失，再继续观察 UFS。不要恢复三个 ztecfg 消费者，也不要执行永久刷写。

## 实机边界

v13 仅允许在用户确认第一阶段 fastboot 可见时临时启动：

```powershell
$fastboot = 'C:\Users\SangLuo\AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\fastboot.exe'
& $fastboot boot 'C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v13-live-proc-dtb.img'
```

禁止 `fastboot flash`、`erase`，禁止修改 `boot`、`vendor_boot`、`dtbo`、`abl`、GPT 或 `userdata`。本轮只记录屏幕底部最终日志和 `ztecfg` 报错是否变化。

## v15：切换 Ace5 通用变量与复位运行时驱动

v14 的实际最新停点仍是循环日志最底部的 `ASSERT [DxeCore] DxeMain.c(581)`，而不是照片中更低位置的旧循环内容。对照 Ace5 和 `nx729j` 后，v15 将运行时驱动选择改为：

```text
VariableRuntimeDxe
ResetSystemRuntimeDxe
```

原厂 `VariableDxe`、`ResetRuntimeDxe` 文件没有删除，只由 `--nx721j-stock-runtime` 显式启用。v15 构建命令为：

```powershell
python build_uefi.py -c -d nx721j -r DEBUG --nx721j-vendor-hw --dtb <nx721j-live-proc.dtb>
```

构建报告和最终 `Guid.xref` 已确认 v15 实际进入 FV 的是通用运行时模块：

```text
EDK II: 0050 Images Verified
镜像: C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v15-generic-runtime.img
大小: 2199552 字节
SHA256: FB9E299060C521BF7A62BB8E6B9A26BA981A5543878E5A2167A813A3487AC4FC
Guid.xref: VariableRuntimeDxe, ResetSystemRuntimeDxe
```

手机实时检查显示 `320443959592 fastboot`，随后已执行一次临时启动：

```text
Sending 'boot.img' (2148 KB) OKAY
Booting OKAY
Finished. Total time: 10.161s
```

本次仍未执行 `fastboot flash`、擦除或任何分区写入。v15 实机照片仍停在 `Variable Arch Protocol not present!!`、`Variable Write Arch Protocol not present!!`、`Reset Arch Protocol not present!!` 后的 `ASSERT [DxeCore] DxeMain.c(581)`，因此通用运行时链没有解决问题。下一版改用 NX721J 原厂运行时对照，不再把 v15 的通用链当作最终方案。

## v16：NX721J 原厂运行时与去除跨设备输入

v16 的活动平台清单已移除所有 `Binaries/giulia` 和 `Binaries/nx729j` 引用，RAW 资源也不再打包 OnePlus 13R 面板/充电配置。硬件 DXE、UFS、USB 和运行时协议只使用 `Binaries/nx721j` 的输入；显示暂时使用官方 `SimpleFbDxe` 读取 ABL 已建立的帧缓冲。

本版显式使用 `--nx721j-stock-runtime`，最终 `FVMAIN.inf` 中为：

    Binaries/nx721j/.../VariableDxe/VariableDxe.inf
    Binaries/nx721j/.../ResetRuntimeDxe/ResetRuntimeDxe.inf

公共 DSC 的 `VariableRuntimeDxe`/`ResetSystemRuntimeDxe` 只出现在构建索引中，未进入本版 `FVMAIN`，所以不存在两套架构协议实现重复安装的问题。v16 本地构建已通过 EDK II `0050 Images Verified`。

镜像：`C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-0.img`
大小：`2007040` 字节
SHA256：`F6834D3B925487BB7E76B333681529C98E37F20797C8E29F4BF9D8D4E99A0E74`

本版尚未再次执行 `fastboot boot`；设备端验证仍需观察 DxeCore 断言是否消失，以及原厂 VariableDxe 是否能在 Z60U 的 QSEE/存储上下文中安装变量架构协议。没有执行永久刷写、擦除或分区写入。
