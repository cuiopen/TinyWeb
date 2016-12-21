﻿
#if TinyWeb_Function_Description //TinyWeb功能说明

auth lzpong 2016/11/24
功能基于 libuv 跨平台库

0.默认编码为 utf - 8
1.只支持使用GET方式
2.支持返回404错误页面
3.支持指定根目录（默认程序所在目录）
4.支持任意格式文件访问(带扩展名)
	a.支持静态网页访问：html / htm
	b.支持其他静态文件：js, css, png, jpeg / jpg, gif, ico, txt, xml, json, log, wam, wav, mp3, apk
	c.支持其他格式文件, 默认文件类型为："application/octet-stream"
5.支持默认index页面(index.html / index.html)，可以自定义设置
6.支持目录列表
7.支持回调
	b.404前回调（未找到页面 / 文件时回调, 此功能便于程序返回自定义功能）

== == == == == == == future
3.支持不带扩展名文件访问
4.支持POST
5.支持WebSocket
6.支持回调
	a.WebSocket 数据回调
7.支持cookie / session
8.支持认证
9.支持大文件响应（下载）

#endif


#include "tinyweb.h"
#include<stdlib.h>
#include<string.h>

//404前回调(未找到页面/文件时回调,此功能便于程序返回自定义功能)；返回0表示没有适合的处理请求，需要发送404错误
char on_request(uv_stream_t* client, reqHeads heads)
{
	struct sockaddr_in serveraddr, peeraddr;
	char serv_ip[17],peer_ip[17], tmp[1024];
	int addrlen = sizeof(struct sockaddr);
	int r;
	//获取clientAddr: http://www.codes51.com/article/detail_113112.html
	//本地接入地址
	r = uv_tcp_getsockname((uv_tcp_t*)client, (struct sockaddr*)&serveraddr, &addrlen);
	//网络字节序转换成主机字符序
	uv_ip4_name(&serveraddr, (char*)serv_ip, sizeof(serv_ip));
	//客户端的地址
	r = uv_tcp_getpeername((uv_tcp_t*)client, (struct sockaddr*)&peeraddr, &addrlen);
	//网络字节序转换成主机字符序
	uv_ip4_name(&peeraddr, (char*)peer_ip, sizeof(peer_ip));

	sprintf(tmp,"%s<br>%s<br>server：%s:%d\t\tpeer：%s:%d\n",heads.path, heads.query, serv_ip, ntohs(serveraddr.sin_port),peer_ip, ntohs(peeraddr.sin_port));
#ifdef _MSC_VER //Windows下需要转换编码
	size_t ll = strlen(tmp);
	char *ch = GB2U8(tmp,&ll);
	tw_send_200_OK(client, "text/html", ch, -1, 0);
	free(ch);
#else //linux 下，系统是和源代码文件编码都是是utf8的，就不需要转换
	tw_send_200_OK(client, "text/html", tmp, -1, 0);
#endif // _MSC_VER

	//char *data=tw_format_http_respone("200 OK", "text/html", ch, -1, 0);
	//tw_send_data(client, data, -1, 0, 1);
	//发送后free data
	return 1;
}
char on_socket_data(uv_stream_t* client, membuf_t* buf)
{
	//printf("%s\n", buf->data);
	return 1;
}




int main(int argc, char** argv)
{
	//测试WebSocket握手Key计算
	//char* p = WebSocketHandShak("jfd/fda6866we/==");
	//printf(p);
	//free(p);
	int i;
	for (i = 0; i < argc; i++)
		printf("arg[%d]:%s\n",i,argv[i]);

	uv_loop_t* loop = uv_default_loop();
	//配置TinyWeb
	tw_config conf;
	memset(&conf, 0, sizeof(conf));
	conf.dirlist = 0;//目录列表
	//conf.ip = NULL;// "127.0.0.1";
	conf.port = 8080;
	//conf.doc_root = NULL;
	//conf.doc_root = "E:\\Projects\\C++\\Lzp_Library\\Test\\tinyweb\\WWWRoot";
	//conf.doc_root = "I:\\WWWRoot\\CMS\\EmpireCMS_7.2_SC_UTF8";
	if (argc > 1)
		conf.doc_dir = argv[1];
	conf.doc_index = NULL;//默认主页
	//
	conf.on_request = on_request;
	conf.on_data = on_socket_data;
	//启动TinyWeb
	tinyweb_start(loop, &conf);
	uv_run(loop, UV_RUN_DEFAULT);
	//
	return 0;
}