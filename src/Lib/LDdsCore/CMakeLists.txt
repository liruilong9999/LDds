# 引入外部函数
include(${ROOT_DIR}/cmake/module.cmake)

# 获取文件名称
set(AbsolutePathProject ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ProjectName ${AbsolutePathProject} NAME)

# Qt 库
set(QT_LIBRARY_LIST "Core" "Network" "Xml")

# 创建项目 
CreateTarget(${ProjectName} "Dll")
target_compile_definitions(${ProjectName} PRIVATE LDDSCORE_LIBRARY)
