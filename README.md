# TinyWeb v1.0.0
auth [lzpong](https://github.com/lzpong)  
A tiny web server based on libuv, can accept Socket,WebSocket,or http protocol,and callBack func's
```
TinyWeb功能说明

auth lzpong 2016/11/24
功能基于 libuv 跨平台库

0.支持设置文档编码,默认 utf-8
1.支持使用HTTP: GET/POST方式访问
2.支持Socket, WebSocket 连接
3.支持返回404错误页面
4.支持指定根目录（默认程序所在目录）
5.支持任意格式文件访问(带/不带扩展名, 文件下载)
	a.支持静态网页访问：html/htm
	b.支持其他静态文件：js, css, png, jpeg/jpg, gif, ico, txt, xml, json, log, wam, wav, mp3, apk
	c.支持其他文件格式, 默认文件类型为："application/octet-stream"
	d.支持不带扩展名文件访问
	e.支持 Range 请求参数下载大文件
6.支持默认index页面(index.html/index.htm)，可以自定义设置
7.支持目录列表
8.不允许访问根目录上级文件或文件夹
9.支持回调
	a.接收到HTTP请求后先回调（此功能便于程序返回自定义功能）,回调失败或返回0时执行普通http响应
	b.WebSocket 数据回调
	c.socket 数据回调

==============future
1.支持cookie/session
2.支持认证
```
# Use age.
1.include the head file
```
#include "tinyweb.h"
```
2.set server configs ,and the callback func's
```
//配置TinyWeb
tw_config conf;
memset(&conf, 0, sizeof(conf));
conf.dirlist = 1;//是否允许查看目录列表
//conf.ip = NULL;//监听IP,默认"0.0.0.0";
conf.port = 8080;//监听端口
//conf.doc_index = NULL;//默认主页
//配置回调函数
conf.on_request = on_request;
conf.on_data = on_socket_read_data;
```
other congfig items see `struct tw_config` in tinyweb.h

3.start server
```
//启动TinyWeb
tinyweb_start(uv_default_loop(), &conf);
```
