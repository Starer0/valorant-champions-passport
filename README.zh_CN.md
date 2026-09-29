<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 无畏契约冠军赛随身卡

为 FoloToy AI Passport 制作的黑金风格上海 2026 无畏契约冠军赛工具。查看下一场、今日赛程和赛事阶段。比赛进行时，大数字显示当前地图的回合比分，队标下圆点记录地图胜场：BO3 每队两个点，BO5 每队三个点。时间为北京时间（UTC+8）。

这是基于 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) 的非官方爱好者项目。赛事及战队商标归其权利人所有。

## 快速上手

1. 首次启动显示 `WIFI SETUP`，准备可联网的 2.4 GHz Wi-Fi。
2. 用手机或电脑连接屏幕显示的 `Passport-VCT-*` 热点，密码以屏幕为准。打开 `http://192.168.4.1`，填写 Wi-Fi 名称和密码，点 **Connect**。连接成功后热点关闭，设备进入赛事卡片。
3. 上下键切换页面。右侧最下方为确定键，短按刷新；页面也会自动轮播。
4. 换网时长按确定键至 `WIFI SETUP`，重新配网。重启后自动连接已保存的 Wi-Fi。
5. 如需备用源，在同一配网页面用 **Save API key** 保存 HenrikDev 密钥。主源无需 API Key。

## 比赛数据

VLR.gg 提供赛程、比赛状态和回合比分；可选的 HenrikDev 备用源同样基于 VLR。直播回合请求在上次完成后等待五秒，直播赛程每 60 秒查询一次。网络延迟和数据源更新会影响实际刷新速度。请求失败时保留最近成功数据并退避重试；回合比分暂不可用时显示 `--:--`。

Wi-Fi 密码和 API Key 只保存在设备中，不编译进固件。数据来源、按键及验证详情见[应用指南](docs/development/champions-card.zh_CN.md)。

## 构建与固件

使用 ESP-IDF 5.5.3、ESP32-C3 目标及 8 MB Flash。先按[环境指南](docs/development/engineering/environment-setup.zh_CN.md)准备工具，再运行：

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

从 GitHub Releases 下载合并固件，经设备所有者同意后从 `0x0` 写入。完整镜像写入可能重置已保存的 Wi-Fi 和 API Key。生成的固件作为 Release 附件发布，不提交到源码版本控制中。

## 验证情况

2026-09-28 已通过完整 ESP-IDF 构建与主机测试。Linux 合并镜像 SHA-256 为 `c2e59268c9432bb2e91964b93f84a55c121015a8fc8424ffc29b416f1d6df1a8`。

此前的 Windows 镜像已在真机验证赛程获取、队标和三页切换。Linux 发布镜像未刷机；直播五秒刷新、地图切换及备用源直播回合比分仍待验证。详见[验证记录](docs/development/champions-card.zh_CN.md)。

## 项目结构

- `main/champions_*`：应用界面、比赛模型、解析器及打包队标。
- `main/passport_net.*`：联网和本地配网页面。
- `components/bsp`：上游项目的可复用板级支持。
- `tests`：主机检查；`tools/validate.sh`：验证入口。
- [上游文档](docs/README.zh_CN.md)及[仓库规范](AGENTS.zh_CN.md)。

保留上游[许可证](LICENSE)。赛事和队标素材的来源列于应用指南，其权利仍属于各权利人。
