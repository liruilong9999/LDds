#ifndef LDDS_GLOBAL_H
#define LDDS_GLOBAL_H

/**
 * @file LDds_Global.h
 * @brief LDdsCore 模块导出宏定义。
 *
 * 说明：
 * 1. 当编译 LDdsCore 动态库本体时，需定义 `LDDSCORE_LIBRARY`，宏会展开为 dllexport。
 * 2. 当业务工程或测试工程链接该动态库时，不定义 `LDDSCORE_LIBRARY`，宏会展开为 dllimport。
 * 3. 当前实现面向 Windows（MSVC）；如需跨平台，可在此文件统一扩展。
 */

#ifdef LDDSCORE_LIBRARY
/**
 * @brief 导出符号（用于构建 LDdsCore.dll）。
 */
#define LDDSCORE_EXPORT __declspec(dllexport)
#else
/**
 * @brief 导入符号（用于使用 LDdsCore.dll）。
 */
#define LDDSCORE_EXPORT __declspec(dllimport)
#endif

#ifdef _MSC_VER
/**
 * @brief 抑制 MSVC C4251 警告（DLL 导出类型包含 STL 成员）。
 *
 * 该警告在本工程中会产生大量噪声，影响真实问题定位。
 */
#pragma warning(disable : 4251)
#endif

#endif // !LDDS_GLOBAL_H
