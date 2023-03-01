//
// chat_message.hpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef CHAT_MESSAGE_HPP
#define CHAT_MESSAGE_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>

// s -> c , c -> s message {header, body} //header length 一般定长
class chat_message
{
public:
  enum { header_length = 4 };
  enum { max_body_length = 512 };

  chat_message()
    : body_length_(0)
  {
  }
//读取内容，返回信息头部地址
  const char* data() const
  {
    return data_;
  }
//可写入数据的版本
  char* data()
  {
    return data_;
  }
//整个message长度
  std::size_t length() const
  {
    return header_length + body_length_;
  }
//message头部地址 + 包头长度 = 包体指针
  const char* body() const
  {
    return data_ + header_length;
  }
//可写入数据版本
  char* body()
  {
    return data_ + header_length;
  }

  std::size_t body_length() const
  {
    return body_length_;
  }
//超过最大长度部分切断
  void body_length(std::size_t new_length)
  {
    body_length_ = new_length;
    if (body_length_ > max_body_length)
      body_length_ = max_body_length;
  }
//解析包头
  bool decode_header()
  {
    char header[header_length + 1] = "";//多一位保存'\0'
    std::strncat(header, data_, header_length);//将data_的前四个字节放到header里来
    body_length_ = std::atoi(header);//包头存储的是包体长度
    if (body_length_ > max_body_length)//包体长度不合法
    {
      body_length_ = 0;
      return false;
    }
    return true;
  }
//对包体进行填充，告诉包体头部到底有多少包体字节
  void encode_header()
  {
    char header[header_length + 1] = "";
    std::sprintf(header, "%4d", static_cast<int>(body_length_));//将body_length_转换为4字节数字存于header中
    std::memcpy(data_, header, header_length);
  }

private:
  char data_[header_length + max_body_length];
  std::size_t body_length_;
};

#endif // CHAT_MESSAGE_HPP