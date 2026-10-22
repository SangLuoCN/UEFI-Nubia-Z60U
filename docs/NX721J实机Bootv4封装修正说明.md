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
