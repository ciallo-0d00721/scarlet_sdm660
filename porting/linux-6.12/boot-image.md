# 独立主线调试 boot.img

本镜像使用上游 v6.12（adc218676eef25575469234709c2d87185ca223a）、实验性 Wayne DTB，以及 `tools/wayne/debug-init.c` 编译的静态 AArch64 PID 1。它只挂载 RAM/伪文件系统，输出内核版本、内存、块设备与 pstore 信息并保持运行；不启动 Android，不挂载 system/vendor/data。

## 在 Work / VS Code Remote SSH 复现

先完成 `tools/wayne/build.sh image-6.12`，并安装 AArch64 GCC。`gen_init_cpio` 使用同一次内核构建的 `out-6.12/usr/gen_init_cpio`。镜像使用 AOSP 独立 mkbootimg 脚本；本机 Ubuntu mkbootimg 包缺少 gki 模块，因此不修改系统安装，改用独立脚本。

工具来源：<https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/tags/android-11.0.0_r1/mkbootimg.py>。以 `?format=TEXT` 获取并 base64 解码后保存到工作目录的 `mkbootimg-aosp-android11.py`，SHA256 必须为 `5579fb6bcb9e89e790a70fb9ccf3c00cf56e3e46aef74d84fcb0964887e5576e`。

在已打开的 vendor 源码根目录运行（工作目录为其父目录）：

```bash
python3 tools/wayne/make-boot.py \
  --work-root .. --output ../boot-6.12 \
  --mkbootimg ../mkbootimg-aosp-android11.py
```

也可运行 VS Code 任务 `Wayne: package 6.12 debug boot.img (Remote SSH)`。输出 `boot.img` 为 67,108,864 字节，`boot-compact.img` 为相同内容的 14,696,448 字节版本；额外空间只填零，不改变有效载荷。

## 封装与验证

- Android header v1、4096 字节页、Wayne board 名，内核为 Image.gz 后附 Wayne DTB。
- 当前 Image 的 text_offset 为 0、image_size 为 0x02560000。加载地址选择 kernel 0x80000000、DTB 0x83e00000、ramdisk 0x84000000，使内核符合 ARM64 的对齐要求且各段互不重叠。该选择尚未经过 Wayne 引导程序验证。
- AOSP unpack_bootimg 独立解包与逐字节比较通过；SHA1 image ID、有效载荷、newc/静态 ELF 和全零填充检查通过，篡改内核或填充的反例被拒绝。
- QEMU virt 的 cpu=max、单核、initcall_debug 配置使用相同内核和 ramdisk，观察到 PID 1、版本与 heartbeat=1；测试随后由 timeout 停止。cortex-a53 SMP 和未启用 initcall_debug 的测试配置在时限内未进入 PID 1，不能宣称所有配置通过。
- QEMU 不加载 Wayne 设备树，不解析 Android boot 容器，也不证明手机显示、USB、存储、充电或通信正常。完整 dtbs_check 与真机启动仍未完成。
- DSH 对初始 PID 1 与镜像几何进行 6 步静态审查；主助手独立编译、验证，并补充必要配置检查、补零校验和 printk.devkmsg=on 调试参数。服务未返回费用金额。

实际产物和测试范围见 `boot-validation.json`。运行时使用独立调试 ramdisk，无 Android 界面；当前未执行任何真机刷写。

ARM64 放置约束依据：[Linux 启动文档](https://cdn.kernel.org/doc/html/latest/arch/arm64/booting.html)。
