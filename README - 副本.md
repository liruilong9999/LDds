# LDds 设计与逻辑

## 一、功能

实现简易dds（数据分发服务），包括功能

- 发布数据功能
- 订阅数据功能
- 设置域id功能
- 查找主题数据功能
- 主题数据临时缓存
- 查找下一个主题数据
- 查找一个主题所有数据
- qos质量配置（含配置文件）
- 可以通过lidl.exe将描述语言生成为cpp,h文件，用于数据传递（含序列号反序列化）
- 其它拓展.......

## 二、数据格式和要求

### 1.lidl描述文件示例

file1.lidl

```lidl
package P1
{
    package P2
    {
    	//<这是注释1,表示结构体>
        struct Handle
        {
            [handle,100,"这是handle的注释，默认参数为100，表示句柄，唯一标识符']
            int handle;
            [datatime,0,"这是handle的注释，默认参数为0，表示发送时间，这是datatime的注释，毫秒时间戳']
            int64 datatime; 
        };
    };	 
    struct Param1:extend P1::P2::Handle
    {
    	[str1,"测试字符串","这是str1的注释，默认参数为 测试字符串，表示句柄，唯一标识符']
        string str1;
        double data1;
        int data2;
    	[data1Vec,,"这是data1Vec的注释，容器无默认参数']
        vector<int> data1Vec;
        map<int,string> m1;
        vector<map<int,double>> vm;
    };
};

["xx主题1"]
HANDLE_TOPIC=P1::P2::Handle

["xx主题2"]
PARAM1_TOPIC=P1::Param1

```

file2.lidl

```lidl
#include<file1.lidl>

package P3
{
	struct TestParam:extend P1::P2::Handle
	{
		int a;
	};
};
TESTPARAM_TOPIC=P3::TestParam
```

- package 类似C++的namespace

- extend类似C++的继承
- 例如：TestParam含有父类的属性 handle和datatime



可以根据这个生成对应的C++代码(xx_define.h,xx_export.h,xx_topic.h,对应的cpp)，如下：

file1_define.h

```cpp
#ifndef FILE1_DEFINE_H
#define FILE1_DEFINE_H

#include <vector>
#include <string>
#include <map>

namespace P1 {
namespace P2 {
struct Handle
{
    int       handle{100}; // 这是handle的注释，默认参数为100，表示句柄，唯一标识符
    long long datatime{0}; // 这是handle的注释，默认参数为0，表示发送时间，这是datatime的注释，毫秒时间戳
    
    void* get(){返回该结构体的指针(序列化)}; 
};
} // namespace P2


struct Param1 : public P1::P2::Handle
{
    std::string                        str1{"测试字符串"}; // 这是str1的注释，默认参数为 测试字符串，表示句柄，唯一标识符
    double                             data1;
    int                                data2;
    std::vector<int>                   data1Vec; // 这是data1Vec的注释，容器无默认参数
    std::map<int, std::string>         m1;
    std::vector<std::map<int, double>> vm;
};

} // namespace P1
#endif // ! FILE1_DEFINE_H
```

file1_export.h

```C++
这里写导入导出宏，并且define中 的结构体需要用到这个宏
```

file1_topic.h

file1_topic.cpp

```Cpp
这里写主题和类的绑定关系，后面具体说明(主题在代码中对应一个枚举类型（或者int）的值，不重复)
```







## 三、需要接口

1.LIdl类

lidl将描述性文件生成代码（如：file1_export file1_topic file1_define 等，里面还包括序列号反序列化）（这个代码文件编译出来是一个库）



2.LDds类函数

```cpp
// 返回回调存根，用于注销回调等，topic 主题 ， data表示接收到的二进制数据
int subcribeTopic(int topic,std::function<void(const std::any& data>)；

// 发布订阅
void publishTopic(int topic,数据指针(struct.get));
                  
// 取消订阅    id(回调存根)        
void unSubcribeTopic(int id);

void setQos(xx);
xx getQos                   
```

3.LDomain类

```C++
init // 初始化域
getDomain//获取域指针
getDomainId//获取域id
getDomainName//获取域名称
getDataTypeByTopic//通过主题获取最新的数据
getFindSetByTopic//创建并获取查找集（离开外部调用函数作用域销毁）
getTopicData//通过查找集获取当前主题最新数据
getNextTopicData//通过查找集获取当前主题下一个数据（从新到旧，直到当前查找集迭代完成）
```

## 五、需要实现工具

1.lidl转换代码工具

2.dds库

