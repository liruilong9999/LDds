/**
 * @file LDds.h
 * @brief LDds框架主头文件
 *
 * Lightweight DDS Framework 公共接口定义
 */

#pragma once

// Core components
#include "LDomain.h"
#include "LFindSet.h"
#include "LQos.h"

// IDL components
#include "LIdlParser.h"
#include "LIdlGenerator.h"

#include "LDds_Global.h"
/**
 * @namespace LDdsFramework
 * @brief LDds框架根命名空间
 */
namespace LDdsFramework {

/**
 * @brief 获取框架版本号
 * @return 版本号字符串，格式: "主版本.次版本.修订版本"
 */
const char * getVersion() noexcept;

/**
 * @brief 获取框架构建时间
 * @return 构建时间字符串，格式: "YYYY-MM-DD HH:MM:SS"
 */
const char * getBuildTime() noexcept;

/**
 * @brief 初始化LDds框架
 *
 * 必须在调用任何其他框架功能之前调用。
 * 可重复调用，但只有第一次调用会执行实际初始化。
 *
 * @return true 初始化成功或已经初始化
 * @return false 初始化失败
 */
bool initialize() noexcept;

/**
 * @brief 关闭LDds框架
 *
 * 释放框架占用的所有资源。
 * 可重复调用，但只有第一次调用会执行实际清理。
 */
void shutdown() noexcept;

/**
 * @brief 检查框架是否已初始化
 * @return true 框架已初始化
 * @return false 框架未初始化
 */
bool isInitialized() noexcept;

} // namespace LDdsFramework
