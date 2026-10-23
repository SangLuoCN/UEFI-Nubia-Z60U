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

主机端构建后查询 `fastboot devices` 为空，所以 v14 尚未实机启动。下一次仍只使用临时 `fastboot boot`，观察 `UFSDxe` 是否真正发布 Block I/O；在此之前不恢复 `CfgUtilDxe` 组，也不执行分区写入。

## 实机边界

v13 仅允许在用户确认第一阶段 fastboot 可见时临时启动：

```powershell
$fastboot = 'C:\Users\SangLuo\AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\fastboot.exe'
& $fastboot boot 'C:\Users\SangLuo\Desktop\Projects\uefi-z60u\Mu-nx721j-vendor-stock-v13-live-proc-dtb.img'
```

禁止 `fastboot flash`、`erase`，禁止修改 `boot`、`vendor_boot`、`dtbo`、`abl`、GPT 或 `userdata`。本轮只记录屏幕底部最终日志和 `ztecfg` 报错是否变化。
