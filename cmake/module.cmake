# 定义宏 CreateTarget，创建项目的目标文件
# ARGV0: ProjectName: 项目名称
# ARGV1: Type: 生成目标类型（可执行文件、库文件等）
# ARGV2: BuildDir 可以不填
macro(CreateTarget ProjectName Type)
   

    # 打印项目名称到控制台
    message(STATUS ${ProjectName})
    # 设置项目名称
    project(${ProjectName})

    # 定义当前路径，并将当前目录下的所有源码文件分类到相应的变量中
    set(CURRENT_PATH ${CMAKE_CURRENT_SOURCE_DIR})  # 当前源码目录
    set(HEADER_FILES "")       # 头文件（.h, .hpp）
    set(SOURCE_FILES "")       # 源文件（.c, .cpp） 
    
    # 递归查找指定类型的文件并存储到相应变量中
    file(GLOB_RECURSE HEADER_FILES "${CURRENT_PATH}/*.h" "${CURRENT_PATH}/*.hpp")
    file(GLOB_RECURSE SOURCE_FILES "${CURRENT_PATH}/*.c" "${CURRENT_PATH}/*.cpp")
    file(GLOB_RECURSE FORM_FILES "${CURRENT_PATH}/*.ui")
    file(GLOB_RECURSE RESOURCE_FILES "${CURRENT_PATH}/*.qrc")

    # 根据不同的平台对文件进行分类显示（方便 IDE 中查看）
    if(CMAKE_CXX_PLATFORM_ID MATCHES "Windows")
        # Windows 平台按目录结构分类显示
        source_group(TREE ${CURRENT_PATH} PREFIX "Header Files" FILES ${HEADER_FILES})
        source_group(TREE ${CURRENT_PATH} PREFIX "Source Files" FILES ${SOURCE_FILES})
        source_group(TREE ${CURRENT_PATH} PREFIX "Form Files" FILES ${FORM_FILES})
        source_group(TREE ${CURRENT_PATH} PREFIX "Resource Files" FILES ${RESOURCE_FILES})
    elseif(CMAKE_CXX_PLATFORM_ID MATCHES "MinGW")
        # MinGW 平台按照文件类型分组
        source_group("Header Files" FILES ${HEADER_FILES})
        source_group("Source Files" FILES ${SOURCE_FILES})
        source_group("Form Files" FILES ${FORM_FILES})
        source_group("Resource Files" FILES ${RESOURCE_FILES})
    elseif(CMAKE_CXX_PLATFORM_ID MATCHES "Linux")
        # Linux 平台不做额外处理（可以根据需要扩展）
    endif()

    # 添加头文件搜索路径 
	include_directories(${ROOT_DIR}/src)         
	include_directories(${ROOT_DIR}/src/Lib)      	    
	include_directories(${ROOT_DIR}/src/Lib/LDdsCore)    

    # 设置静态库文件搜索路径 
	link_directories(${ROOT_DIR}/bin)              # bin 目录下的 lib 
	link_directories(${ROOT_DIR}/bin/lib)              # bin 目录下的 lib 
 
    # 为多种构建类型设置输出目录
    set(CONFIGURATION_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
    if(${ARGC} GREATER 2)
        foreach(CONFIGURATION_TYPE ${CONFIGURATION_TYPES})
            string(TOUPPER ${CONFIGURATION_TYPE} TYPE)
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin/${ARGV2}) # .lib and .a
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin/${ARGV2}) # .so and .dylib
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin/${ARGV2}) # .exe and .dll
        endforeach()
    else()
        foreach(CONFIGURATION_TYPE ${CONFIGURATION_TYPES})
            string(TOUPPER ${CONFIGURATION_TYPE} TYPE)
            set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin/lib) # .lib and .a
            set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin/lib) # .so and .dylib
            set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${TYPE} ${ROOT_DIR}/bin) # .exe and .dll
        endforeach()
    endif()

    # 设置后缀
    set(CMAKE_RELEASE_POSTFIX "")
    set(CMAKE_DEBUG_POSTFIX "d")
    set(CMAKE_MINSIZEREL_POSTFIX "")
    set(CMAKE_RELWITHDEBINFO_POSTFIX "")

    # 根据不同的类型生成相应的目标文件
    if(${Type} STREQUAL "Exe")
        # 生成可执行文件（带窗口）
        add_executable(${PROJECT_NAME}
            WIN32                              # Windows 下使用 Win32 子系统
            ${HEADER_FILES} ${SOURCE_FILES})    # 源码文件 
        set_target_properties(${PROJECT_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")  # 设置 Visual Studio 的调试工作目录
    elseif(${Type} STREQUAL "ExeCMD")
        # 生成命令行可执行文件（无窗口）
        add_executable(${PROJECT_NAME}
            ${HEADER_FILES} ${SOURCE_FILES})    # 源码文件 
        set_target_properties(${PROJECT_NAME} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")  # 设置 Visual Studio 的调试工作目录
    else()
        # 生成库文件
        if(${Type} STREQUAL "Lib")
            # 静态库
            add_library(${PROJECT_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
        elseif(${Type} STREQUAL "Dll")
            # 动态库
            add_library(${PROJECT_NAME} SHARED ${HEADER_FILES} ${SOURCE_FILES} ${FORM_FILES} ${RESOURCE_FILES})
		endif()
    endif()
 
    # 添加自定义库到目标文件的链接库中
	foreach(_lib ${SELF_LIBRARY_LIST})
		include_directories(${CURRENT_PATH}/../)
		target_link_libraries(${PROJECT_NAME} ${_lib}$<$<CONFIG:Debug>:d>)
	endforeach()
	
    # 针对 Windows 平台，整理自动生成的文件分类
    if(WIN32)
        source_group("CMake Rules"  REGULAR_EXPRESSION "^$")          # CMake 自动生成的文件
        source_group("Header Files" REGULAR_EXPRESSION "^$")          # 头文件
        source_group("Source Files" REGULAR_EXPRESSION "^$")          # 源文件
        source_group("zero"         REGULAR_EXPRESSION "\\.h$|\\.cpp$|\\.stamp$|\\.rule$")  # 特殊文件
    endif()
endmacro()
