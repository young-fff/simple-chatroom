# simple-chatroom
A simple chat room that relies on boost::asio

##开发环境
- 操作系统：Ubuntu 22.04
- 编辑器：Vscode
- 编译器：g++ 11.3.0

## 技术点
* 使用智能指针管理客户端连接
* 使用asio来进行线程调度
* 设计简易的message格式作为信息包
* 使用c/s形式通过chatserver进行消息转发

## 运行方式
* git clone 到本地
* 进入项目根目录
* server端
```
make server
./server 7788
```
* 新建另外几个终端作为client端
```
make client
./client localhost 7788
```
* 然后client发送中英文消息即可
