//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include "chat_message.hpp"

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

using chat_message_queue = std::deque<chat_message>;

//----------------------------------------------------------------------
//聊天基类
class chat_participant
{
public:
  using pointer = std::shared_ptr<chat_participant>;
  virtual ~chat_participant() {}
  virtual void deliver(const chat_message& msg) = 0;//纯虚函数无法实例化
};

using chat_participant_ptr = std::shared_ptr<chat_participant>;

//----------------------------------------------------------------------
//聊天室类
class chat_room
{
public:
//客户端一加入服务器就会直接给该客户端发消息
  void join(chat_participant_ptr participant)
  {
    participants_.insert(participant);
    for (const auto& msg: recent_msgs_)
      participant->deliver(msg);
  }
//将客户从成员集合中去除，因为其为智能指针，会自动析构
  void leave(chat_participant_ptr participant)
  {
    participants_.erase(participant);
  }

  void deliver(const chat_message& msg)
  {
    recent_msgs_.push_back(msg);
    while (recent_msgs_.size() > max_recent_msgs)
      recent_msgs_.pop_front();

    for (auto& participant: participants_)
      participant->deliver(msg);
  }

private:
  std::set<chat_participant_ptr> participants_;
  enum { max_recent_msgs = 100 };
  chat_message_queue recent_msgs_;
};

//----------------------------------------------------------------------

class chat_session
  : public chat_participant,
    public std::enable_shared_from_this<chat_session>
{
public:
  chat_session(tcp::socket socket, chat_room& room)
    : socket_(std::move(socket)),
      room_(room)
  {
  }

  void start()
  {
    room_.join(shared_from_this());
    do_read_header();
  }

  void deliver(const chat_message& msg)
  {
    //第一次时 write_in_progress 为 false
    //防止多次调用do_write(),因为当消息队列非空时，do_write会自己继续调用do_write()
    //只有当消息队列为空时，才会从此处成功调用do_write()
    bool write_in_progress = !write_msgs_.empty();  
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      //第一次
      do_write();
    }
  }

private:
//读包头
//将客户端信息读到buffer（read_msg_.data()）中来，读4个字节
//当收到这四个字节时，调用回调函数
//捕获列表self防止自己失效
  void do_read_header()
  {
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) 
        {
          if (!ec && read_msg_.decode_header())//如果没有系统错误 且 头部信息合法
          {
            do_read_body();
          }
          else  //否则调用leave
          {
            room_.leave(shared_from_this());  //调用leave会将智能指针从set中erase,引用计数变为0，自动析构
          }
        });
  }
//读包体
//将客户端信息读到buffer（read_msg_.body()）中来，读body_length个字节(body_length由decode_header得到)
//当收到包体时，调用回调函数
//捕获列表self防止自己失效
  void do_read_body()
  {
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)//如果没有系统错误
          {
            room_.deliver(read_msg_);//分发消息
            do_read_header();//读完一条读下一条
          }
          else//否则调用leave
          {
            room_.leave(shared_from_this()); //调用leave会释放资源
          }
        });
  }
//异步写
//将队列头部第一条信息写到buffer
  void do_write()
  {
    auto self(shared_from_this());//防止被析构
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)  //如果没有发生错误
          {
            write_msgs_.pop_front();  //去除消息队列的第一条消息
            if (!write_msgs_.empty()) //如果非空
            {
              do_write(); //继续写
            }
          }
          else  //发生错误（一般网络问题，客户端出错）
          {
            room_.leave(shared_from_this());  //析构释放资源
          }
        });
  }

  tcp::socket socket_;
  chat_room& room_;//通过引用说明chat_room生命周期更长
  chat_message read_msg_;
  chat_message_queue write_msgs_; 
  //deque优点，在头部删除元素和尾部插入数据不会引起迭代器失效和内存分配
  //vector缺点，在头部删除元素非常耗时，且不提供pop_front()接口，且
  //在不断push_back()时可能导致内存重新分配，因为vector要保证内存连续性
  //list在此处也可行，但deque更省内存，且遍历时list更慢些
};

//----------------------------------------------------------------------

class chat_server
{
public:
  chat_server(boost::asio::io_context& io_context,
      const tcp::endpoint& endpoint)
    : acceptor_(io_context, endpoint)
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<chat_session>(std::move(socket), room_)->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
  chat_room room_;
};

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }

    boost::asio::io_context io_context;

    std::list<chat_server> servers;
    for (int i = 1; i < argc; ++i)
    {
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
      servers.emplace_back(io_context, endpoint);
    }

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}