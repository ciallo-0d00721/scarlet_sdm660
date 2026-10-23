# Wayne 6.12 启动配置审查（2026-10-23）

DSH 在单独的公开源码副本中完成 35 步审查，仅生成报告，没有修改设备树。主助手对照完整仓库复核后记录以下结论；未采纳缺失输入导致的不确定推断。

- QHEE 6 MiB 与 RMTFS 2 MiB 合起来覆盖 `0x85800000..0x86000000`，与下游 8 MiB 保留区一致。
- 上游 TZ 比下游多保留 6 MiB，是保守多保留；现有地址范围没有与 MPSS 重叠。没有据此修改内存布局。
- CDSP 地址段通过扩展上游 `buffer_mem` 保护为 6 MiB；这仅保留内存，不表示 CDSP 驱动或固件已适配。
- framebuffer 地址和大小来自下游 `sdm660.dtsi` 的 `cont_splash_mem`。
- 下游 `arch/arm64/boot/dts/vendor/qcom/xiaomi/longcheer/common/longcheer-sdm660-ramoops.dtsi` 明确给出 `0xa0000000`、4 MiB、console/pmsg 各 2 MiB，证明当前 ramoops 参数来源。该文件未包含在 DSH 输入中。
- 下游 `arch/arm64/boot/dts/vendor/qcom/sdm660-pinctrl.dtsi` 的 `uart_console_active` 使用 gpio4/gpio5、blsp_uart2、2 mA 和 bias-disable，与上游 UART 状态一致。引脚配置一致不代表串口已真机验证。
- eMMC 供电映射有下游 `sdm660-mtp.dtsi` 的 PM660L L4 与 PM660 L8 依据；上游继承 8-bit、non-removable 和完整 pinctrl。没有在缺少启动日志时改电压、时序或启用 HS400/CQE。
- 根节点 legacy ID 的 schema 兼容性仍需核对。上游 qcom.yaml 有专门的 legacy ID 允许列表，不能将其一概视为无条件允许的旧属性。

主助手在实际 Work Kbuild DTB 上验证了 6 项板级属性和 16 段静态保留内存无重叠；扩大 framebuffer 造成重叠和截断 DTB 的反例均被拒绝。该检查已接入构建脚本。

6.12 Image 和 DTB 均编译完成，见 build-validation.json。完整 dtbs_check 尚未完成；未做启动镜像封装、真机启动或 Android 14 厂商接口验证。DSH 服务未返回可核对的费用金额，不能据步数推算账单。
