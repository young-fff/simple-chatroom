//
// chat_client.cpp
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
#include <thread>
#include <boost/asio.hpp>
#include "chat_message.hpp"

using boost::asio::ip::tcp;

using chat_message_queue = std::deque<chat_message> ;

class chat_client
{
public:
//构造函数建立网络连接
  chat_client(boost::asio::io_context& io_context,
      const tcp::resolver::results_type& endpoints)
    : io_context_(io_context),
      socket_(io_context)
  {
    do_connect(endpoints);
  }
//先使用post函数，捕获列表msg是值拷贝
//其余部分与chat_server的deliver一致
//不使用post则io_context无法控制运行，io_context具有多线程下运行的能力，io_context确实是在多线程情况下运行
//真正的过程由io_context进行调度在thread t的run()中运行
  void write(const chat_message& msg)
  {
    boost::asio::post(io_context_,
        [this, msg]()
        {
          //第一次时 write_in_progress 为 false
          //防止多次调用do_write(),因为当消息队列非空时，do_write会自己继续调用do_write()
          //只有当消息队列为空时，才会从此处成功调用do_write()
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
          {
            do_write();
          }
        });
  }
//也调用post,post作用是生成一个事件，该事件在io_context控制下运行
//也不是在close线程中立即close，而是由io_context自由调度
  void close()
  {
    boost::asio::post(io_context_, [this]() { socket_.close(); });
  }

private:
//异步连接服务器，注册事件后就去做其他事
  void do_connect(const tcp::resolver::results_type& endpoints)
  {
    boost::asio::async_connect(socket_, endpoints,
        [this](boost::system::error_code ec, tcp::endpoint)
        {
          if (!ec)
          {
            do_read_header();
          }
        });
  }
//读头部四个字节放到read_msg_.data()
  void do_read_header()
  {
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec && read_msg_.decode_header()) //没出错 且 包头合格
          {
            do_read_body(); //读包体
          }
          else  //出错
          {
            socket_.close();  //客户端关闭  //此处的close其实就是在run()线程下运行，没有问题，可直接调用close
          }
        });
  }
//读包体信息到read_msg_.body()
  void do_read_body()
  {
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)  //没出错，cout包体 
          {
            std::cout.write(read_msg_.body(), read_msg_.body_length());
            std::cout << "\n";
            do_read_header(); //继续读
          }
          else  //出错
          {
            socket_.close();  //客户端关闭 //此处的close其实就是在run()线程下运行，没有问题，可直接调用close
          }
        });
  }
//异步写
  void do_write()
  {
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)  //没出错
          {
            write_msgs_.pop_front();  //去除消息队列的第一条消息
            if (!write_msgs_.empty()) //如果非空
            {
              do_write(); //继续写
            }
          }
          else  //出错
          {
            socket_.close();  //关闭连接
          }
        });
  }

private:
  boost::asio::io_context& io_context_; //chat_session此处为chat_room
  tcp::socket socket_;
  //read_msg_和write_msgs_使用默认构造函数
  chat_message read_msg_;
  chat_message_queue write_msgs_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: chat_client <host> <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(argv[1], argv[2]);
    chat_client c(io_context, endpoints); //异步连接对应的服务器，而真正连接服务器的时刻是在run()中
    //单独开一个线程跑io_context.run()
    std::thread t([&io_context](){ io_context.run(); });
    //主线程等待客户输入
    char line[chat_message::max_body_length + 1]; //多一位放'\0'
    //将键盘输入的信息放入line,最多512
    while (std::cin.getline(line, chat_message::max_body_length + 1)) 
    {
      chat_message msg;
      msg.body_length(std::strlen(line));
      std::memcpy(msg.body(), line, msg.body_length());
      msg.encode_header();  //调用encode_header()检查是否合格
      c.write(msg); 
      //调用write时，想办法把事件放到thread t中运行
      //post起这个作用
    }
    //输入出错时
    c.close();  //调用close是为了让io_context.run()退出，否则无法退出
    t.join(); //t在等待服务器信息，不会自己完结
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}