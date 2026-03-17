#ifndef __ALIYUN_OTA_H__
#define __ALIYUN_OTA_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * 阿里云 MQTT OTA 模块
 *
 * 依赖：
 *   - mqtt_at.c   MQTT 连接/订阅/发布
 *   - http_ota.c  HTTP 分段下载固件
 *   - w25q32.c    写入下载区
 *
 * 阿里云 OTA Topic（固定格式）：
 *   上报版本：/ota/device/inform/${ProductKey}/${DeviceName}
 *   接收升级：/ota/device/upgrade/${ProductKey}/${DeviceName}
 *   上报进度：/ota/device/progress/${ProductKey}/${DeviceName}
 */

/* 设备三元组，填入阿里云控制台获取的值 */
#define ALIYUN_PRODUCT_KEY    "k1qu5q0WFBN"
#define ALIYUN_DEVICE_NAME    "D001"
#define ALIYUN_DEVICE_SECRET  ""   /* 不需要，密码已预先生成 */

/* MQTT Broker 地址 */
#define ALIYUN_MQTT_HOST  "iot-06z00fh8ito58jt.mqtt.iothub.aliyuncs.com"
#define ALIYUN_MQTT_PORT  1883

/*
 * 连接阿里云 MQTT 并订阅 OTA 升级 Topic
 * 连接成功后自动上报当前固件版本
 */
bool aliyun_ota_connect(void);

/*
 * 上报当前固件版本到阿里云
 * 阿里云控制台触发升级时会推送消息到 upgrade topic
 */
bool aliyun_ota_report_version(void);

/*
 * 检查是否收到升级通知，有则执行下载和升级
 * 建议在独立任务里周期性调用，或由 MQTT 回调触发
 */
bool aliyun_ota_process(void);

#endif /* __ALIYUN_OTA_H__ */
