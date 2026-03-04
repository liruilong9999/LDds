# 引入外部函数
include(${ROOT_DIR}/cmake/module.cmake)

# 获取绝对路径
set(AbsolutePathProject ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ProjectName ${AbsolutePathProject} NAME)

# 链接库
set(SELF_LIBRARY_LIST "LDdsCore")
# 创建项目
CreateTarget(${ProjectName} "ExeCMD")

# LIdl 是纯命令行目标，不依赖 Qt 元对象生成流程，关闭可避免 AUTOGEN 警告。
set_target_properties(${ProjectName} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
