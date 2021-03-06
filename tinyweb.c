﻿#include "tinyweb.h"
#include "tools.h"

#ifdef __GNUC__
#include <uv.h>
#define _strncmpi strncasecmp
#define strcmpi strcasecmp
#endif // __GNUC__

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <memory.h>

//TinyWeb 增加与完善功能，by lzpong 2016/11/24

#define TW_SEND_SIZE 1024*8

typedef struct tw_file_t {
	//uchar flag;   //连接的标志
	FILE* fp;    //文件指针
	size_t fsize;//文件大小
	long lsize;//文件剩余大小
}tw_file_t;

typedef struct tw_client {
	tw_peerAddr pa;//客户端连接的地址
	tw_file_t ft;//发往客户端的文件,断点续传记录
	WebSocketHandle hd;
}tw_client;
//=================================================

//发送文件到客户端
static char tw_http_send_file(uv_stream_t* client, const char* content_type, const char* file, tw_reqHeads* heads);

//关闭客户端连接后，释放客户端连接的数据
static void after_uv_close_client(uv_handle_t* client) {
	tw_client* clidata = (tw_client*)client->data;
	//如果有发送文件
	if (clidata->ft.fp) {
		fclose(clidata->ft.fp);
		printf("send file close left:%ld\n", clidata->ft.lsize);
	}
	//如果是WebSocket
	if (clidata->pa.flag & 0x2) { //WebSocket
		membuf_uninit(&clidata->hd.buf);
	}
	//清理
	free(client->data);
	free(client);
}

//关闭客户端连接
void tw_close_client(uv_stream_t* client) {
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	tw_client* clidata = (tw_client*)client->data;
	//关闭连接回调
	if (tw_conf->on_close)
		tw_conf->on_close(tw_conf->data, client, &clidata->pa);
	uv_close((uv_handle_t*)client, after_uv_close_client);
}

//发送数据后,free数据，关闭客户端连接
static void after_uv_write(uv_write_t* w, int status) {
	tw_client* clidata = (tw_client*)w->handle->data;
	if (w->data)
		free(w->data); //copyed data
	//长连接就不关闭了
	if (clidata->ft.fp)
		tw_http_send_file(w->handle, NULL, NULL, NULL);
	else if (!(clidata->pa.flag & 0x1))
		uv_close((uv_handle_t*)w->handle, after_uv_close_client);
	free(w);
}

//发送数据到客户端; 如果是短连接,则发送完后会关闭连接
//data：待发送数据
//len： 数据长度, -1 将自动计算数据长度
//need_copy_data：是否需要复制数据
//need_free_data：是否需要free数据, 如果need_copy_data非零则忽略此参数
void tw_send_data(uv_stream_t* client, const void* data, unsigned int len, int need_copy_data, int need_free_data) {
	uv_buf_t buf;
	uv_write_t* w;
	void* newdata = (void*)data;

	if (data == NULL || len == 0) return;
	if (len == (unsigned int)-1)
		len = strlen((char*)data);

	if (need_copy_data) {
		newdata = malloc(len);
		memcpy(newdata, data, len);
	}

	buf = uv_buf_init((char*)newdata, len);
	w = (uv_write_t*)malloc(sizeof(uv_write_t));
	w->data = (need_copy_data || need_free_data) ? newdata : NULL;
	uv_write(w, client, &buf, 1, after_uv_write); //free w and w->data in after_uv_write()
}

//发送'200 OK' 响应; 不会释放(free)传入的数据(u8data)
//content_type：Content Type 文档类型
//u8data：utf-8编码的数据
//content_length：数据长度，为-1时自动计算(strlen)
//respone_size：获取响应最终发送的数据长度，为0表示放不需要取此长度
void tw_send_200_OK(uv_stream_t* client, const char* content_type, const void* u8data, int content_length, int* respone_size)
{
	int repSize;
	const char *p = strchr(content_type, '/');
	//有'.'    没有'/'   至少有两个'/'    '/'是在开头    '/'是在末尾
	//都要重新取文件类型
	if (p) {
		if (strchr(content_type, '.'))// 有'.'
			p = tw_get_content_type(content_type);
		else {
			p = strchr(p + 1, '/');
			if (p)//至少有两个'/'
				p = tw_get_content_type(content_type);
			else {
				p = strchr(content_type, '/');
				if (p == content_type || p == (content_type + strlen(content_type) - 1))
					p = tw_get_content_type(content_type);
				else
					p = content_type;
			}
		}
	}//没有'/'
	else
		p = tw_get_content_type(content_type);
	char *data = tw_format_http_respone(client, "200 OK", p, u8data, content_length, &repSize);
	tw_send_data(client, data, repSize, 0, 1);//发送后free data
	if (respone_size)
		*respone_size = repSize;
}

//返回格式华的HTTP响应内容（需要free返回数据）
//status: "200 OK"
//content_type: 文件类型，如："text/html" ；可以调用tw_get_content_type()得到
//content: any utf-8 data, need html-encode if content_type is "text/html"
//content_length: can be -1 if content is c_str (end with NULL)
//respone_size: if not NULL,可以获取发送的数据长度 the size of respone will be writen to request
//returns malloc()ed c_str, need free() by caller
char* tw_format_http_respone(uv_stream_t* client, const char* status, const char* content_type, const char* content, int content_length, int* respone_size) {
	int totalsize, header_size;
	char* respone;
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	if (content_length < 0)
		content_length = content ? strlen(content) : 0;
	totalsize = strlen(status) + strlen(content_type) + content_length + 128;
	respone = (char*)malloc(totalsize);
	header_size = snprintf(respone, totalsize - 1, "HTTP/1.1 %s\r\nServer: TinyWeb\r\nConnection: close\r\nContent-Type:%s;charset=%s\r\nContent-Length:%d\r\n\r\n"
		, status, content_type, tw_conf->charset, content_length);
	assert(header_size > 0);
	if (content_length) {
		memcpy(respone + header_size, content, content_length);
	}
	if (respone_size)
		*respone_size = header_size + content_length;
	return respone;
}

//发送404响应
static void tw_404_not_found(uv_stream_t* client, const char* pathinfo) {
	char* respone;
	char buffer[128];
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	snprintf(buffer, sizeof(buffer), "<h1>404 Not Found</h1><p p='%s'>%s</p>", tw_conf->doc_dir, pathinfo);
	respone = tw_format_http_respone(client, "404 Not Found", "text/html", buffer, -1, NULL);
	tw_send_data(client, respone, -1, 0, 1);
}
//发送301响应
//
static void tw_301_Moved(uv_stream_t* client, tw_reqHeads* heads) {
	int len = 76 + strlen(heads->path);
	char buffer[512];
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	snprintf(buffer, sizeof(buffer), "HTTP/1.1 301 Moved Permanently\r\nServer: TinyWeb\r\nLocation: http://%s%s/%s%s\r\nConnection: close\r\n"
		"Content-Type:text/html;charset=%s\r\nContent-Length:%d\r\n\r\n<h1>Moved Permanently</h1><p>The document has moved <a href=\"%s\">here</a>.</p>"
		, heads->host, heads->path, (heads->query?"?":""), (heads->query?heads->query:"")
		, tw_conf->charset, len, heads->path);
	tw_send_data(client, buffer, -1, 1, 1);
}

//发送文件到客户端
static char tw_http_send_file(uv_stream_t* client, const char* content_type, const char* file, tw_reqHeads* heads) {
	char *respone;
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	tw_client* clidata = (tw_client*)client->data;
	tw_file_t* filet = &clidata->ft;
	//发送头部
	if (!filet->fp && file) {
		filet->fp = fopen(file, "rb");
		if (filet->fp) {
			fseek(filet->fp, 0, SEEK_END);
			filet->fsize = ftell(filet->fp) - heads->Range_frm;
			filet->lsize = filet->fsize - heads->Range_to;
			fseek(filet->fp, heads->Range_frm, SEEK_SET);
		}
		else {
			tw_404_not_found(client, heads->path);
			return 0;
		}
		respone = (char*)malloc(256 + 1);
		int respone_size = snprintf(respone, 256, "HTTP/1.1 200 OK\r\nServer: TinyWeb\r\nConnection: close\r\nContent-Type:%s;charset=%s\r\nContent-Length:%ld\r\n\r\n"
			, content_type, tw_conf->charset, filet->fsize);
		tw_send_data(client, respone, respone_size, 0, 1);
		return 1;
	}
	else { //发送文件
		size_t read_size;// read_bytes;
		if (filet->fp) {
			respone = (char*)malloc(TW_SEND_SIZE + 1);
			// fread //返回实际读取的单元个数。如果小于count，则可能文件结束或读取出错；可以用ferror()检测是否读取出错，用feof()函数检测是否到达文件结尾。如果size或count为0，则返回0。
			read_size = fread(respone, sizeof(char), TW_SEND_SIZE, filet->fp);
			if (read_size > 0) {
				filet->lsize -= read_size;
				if (filet->lsize <= 0) {
					fclose(filet->fp);
					filet->fp = 0;
					filet->fsize = 0;
				}
				tw_send_data(client, respone, read_size, 0, 1);
			}
			else
			{
				fclose(filet->fp);
				filet->fp = 0;
				filet->fsize = 0;
				tw_send_data(client, respone, TW_SEND_SIZE, 0, 1);
			}
			return 1;
		}
		return 0;
	}
}

//根据扩展名(不区分大小写)，返回文件类型 content_type
const char* tw_get_content_type(const char* fileExt) {
	const static char* octet = "application/octet-stream";
	if (fileExt)
	{
		//不管什么路径名或者文件名, 只要最后面有点(.),就认为是有扩展名的
		const char *p = strrchr(fileExt, '.');
		if (p) { // /aaa.txt
			fileExt = p + 1;
		}
	}
	else //否则没有扩展名
		return octet;
	if (strcmpi(fileExt, "htm") == 0 || strcmpi(fileExt, "html") == 0)
		return "text/html";
	else if (strcmpi(fileExt, "js") == 0)
		return "application/javascript";
	else if (strcmpi(fileExt, "css") == 0)
		return "text/css";
	else if (strcmpi(fileExt, "json") == 0)
		return "application/json";
	else if (strcmpi(fileExt, "log") == 0 || strcmpi(fileExt, "txt") == 0)
		return "text/plain";
	else if (strcmpi(fileExt, "jpg") == 0 || strcmpi(fileExt, "jpeg") == 0)
		return "image/jpeg";
	else if (strcmpi(fileExt, "png") == 0)
		return "image/png";
	else if (strcmpi(fileExt, "gif") == 0)
		return "image/gif";
	else if (strcmpi(fileExt, "ico") == 0)
		return "image/x-icon";
	else if (strcmpi(fileExt, "xml") == 0)
		return "application/xml";
	else if (strcmpi(fileExt, "xhtml") == 0)
		return "application/xhtml+xml";
	else if (strcmpi(fileExt, "wav") == 0)
		return "audio/wav";
	else if (strcmpi(fileExt, "wma") == 0)
		return "audio/x-ms-wma";
	else if (strcmpi(fileExt, "mp3") == 0)
		return "audio/mp3";
	else if (strcmpi(fileExt, "apk") == 0)
		return "application/vnd.android.package-archive";
	else
		return "application/octet-stream";
}

//处理客户端请求
//invoked by tinyweb when GET request comes in
//please invoke write_uv_data() once and only once on every request, to send respone to client and close the connection.
//if not handle this request (by invoking write_uv_data()), you can close connection using tw_close_client(client).
//pathinfo: "/" or "/book/view/1"
//query_stirng: the string after '?' in url, such as "id=0&value=123", maybe NULL or ""
void tw_request(uv_stream_t* client, tw_reqHeads* heads) {
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	char fullpath[260];//绝对路径（末尾不带斜杠）
	snprintf(fullpath, 259, "%s/%s\0", tw_conf->doc_dir, (heads->path[0] == '/' ? heads->path + 1 : heads->path));
	//去掉末尾的斜杠
	char *p = &fullpath[strlen(fullpath) - 1];
	while (*p == '/' || *p == '\\')
	{
		*p = 0;
		p--;
	}

	char file_dir = isExist(fullpath);
	//判断 文件或文件夹，或不存在
	switch (file_dir)
	{
	case 1://存在：文件
	{
		char* postfix = strrchr(heads->path, '.');//从后面开始找文件扩展名
		if (postfix)
		{
			postfix++;
			p = postfix + strlen(postfix) - 1;
			while (*p == '/' || *p == '\\')
			{
				*p = 0;
				p--;
			}
		}
		tw_http_send_file(client, postfix ? tw_get_content_type(postfix) : "application/octet-stream", fullpath, heads);
	}
	break;
	case 2://存在：文件夹
	{
		if (heads->path[strlen(heads->path) - 1] != '/')
		{
			tw_301_Moved(client, heads);
			break;
		}
		char tmp[260]; tmp[0] = 0;
		char *s = strdup(tw_conf->doc_index);
		p = strtok(s, ";");
		//是否有默认主页
		while (p)
		{
			snprintf(tmp, 259, "%s/%s", fullpath, p);
			if (isFile(tmp))
			{
				tw_http_send_file(client, "text/html", tmp, heads);
				break;
			}
			tmp[0] = 0;
			p = strtok(NULL, ";");
		}
		free(s);
		//没用默认主页
		if (!tmp[0])
		{
			char *p = "Welcome to TinyWeb.<br>Directory access forbidden.";
			if (tw_conf->dirlist) {
				p = listDir(fullpath, heads->path);
				if (strstr(tw_conf->charset, "utf"))
				{//下需要转换编码
					unsigned int len = strlen(p);
					char* p2 = GB2U8(p, &len);
					free(p);
					p = p2;
				}
			}
			char *respone = tw_format_http_respone(client, "200 OK", "text/html", p, -1, NULL);
			tw_send_data(client, respone, -1, 0, 1);
			if (tw_conf->dirlist && p)
				free(p);
		}
	}
	break;
	default://不存在
		tw_404_not_found(client, heads->path);
		break;
	}
}

//获取http头信息,返回指向 Sec-WebSocket-Key 的指针
static char* tw_get_http_heads(const uv_buf_t* buf, tw_reqHeads* heads) {
	char *key, *end, *p;
	char* data = strstr(buf->base, "\r\n\r\n");
	if (data) {
		*data = 0;
		heads->data = data += 4;
		//是否有 Sec-WebSocket-Key
		key = strstr(buf->base, "Sec-WebSocket-Key:");
		if (key) {
			key += 19;
			while (isspace(*key)) key++;
			end = strchr(key, '\r');
			if (end) *end = 0;
			return key;
		}
		//not http upgrade to WebSocket
		if (buf->base[0] == 'G' && buf->base[1] == 'E' && buf->base[2] == 'T' && buf->base[3] == ' ') {
			heads->method = 1;//GET
		}
		else if (buf->base[0] == 'P' && buf->base[1] == 'O' && buf->base[2] == 'S' && buf->base[3] == 'T' && buf->base[4] == ' ') {
			heads->method = 2;//POST
		}
		//是http get/post协议
		if (heads->method)
		{
			//search path
			heads->path = strchr(buf->base + 3, ' ') + 1;
			while (isspace(*heads->path)) heads->path++;
			end = strchr(heads->path, ' ');
			if (end) *end = 0;
			//
			url_decode(heads->path);
#ifdef _MSC_VER //Windows下需要转换编码,因为windows系统的编码是GB2312
			size_t len = strlen(heads->path);
			char *gb = U82GB(heads->path, &len);
			strncpy(heads->path, gb, len);
			heads->path[len] = 0;
			free(gb);
			//linux 下，系统和源代码文件编码都是是utf8的，就不需要转换
#endif // _MSC_VER
			//query param
			if (1 == heads->method) { //GET
				p = strchr(heads->path, '?');
				if (p) {
					heads->query = p + 1;
					*p = 0;
				}
			}
			else {  //POST
				heads->query = heads->data;
			}
			////------------尽可能的合并 "../"  "/./"
			//确保开头为'/'
			if (*heads->path != '/') {
				heads->path--;
				*heads->path = '/';
				*(heads->path - 1) = 0;
			}
			//确保结尾不是"/.."
			p = strrchr(heads->path, '.');
			if (p && *(p + 1) == 0 && *(p - 1) == '.' && *(p - 2) == '/') {
				*(p + 1) = '/';
				*(p + 2) = 0;
			}
			//去掉"/./"
			while ((p = strstr(heads->path, "/./"))) {
				memmove(p, p + 2, strlen(p + 2) + 1);
			}
			//尽可能的合并"../"
			while ((p = strstr(heads->path, "/.."))) {//存在 ..
				if ((p - heads->path) <= 1) {
					if ((end = strchr(heads->path + 2, '/')))
						heads->path = end;
					else
						*p = 0;
					continue;
				}
				*(p - 1) = 0;
				end = strrchr(heads->path, '/');
				if (end == NULL)
					end = heads->path;
				key = strchr(p + 2, '/');
				if (key)
					p = key;
				else
					break;
				memmove(end, p, strlen(p) + 1);
			}
			//search host
			heads->host = strstr(end + 4, "Host: ");
			if (heads->host) {
				heads->host += 5;
				while (isspace(*heads->host)) heads->host++;
				end = strchr(heads->host, '\r');
				if (end)
					*end = 0;
			}
			//Range: sizeFrom-[sizeTo]
			p = strstr(end + 1, "Range: ");
			if (p) {
				p += 7;
				end = strstr(p, "-");
				if (end) {
					heads->Range_frm = strtoull(p, &end, 10);
					p = strstr(++end, "\r\n");//跳过 '-'
					if (p - end > 0)
						heads->Range_to = strtoull(end, &p, 10);
					else
						heads->Range_to = 0;
				}
				end = p;
			}
			//data length
			heads->len = strlen(data);
			if (heads->len < 1)
				heads->data = NULL;
		}
	}
	return NULL;
}

//on_read_WebSocket
static void on_read_websocket(uv_stream_t* client, char* data, unsigned long Len) {
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	tw_client* clidata = (tw_client*)client->data;
	WebSocketHandle* hd = &clidata->hd;
	if (NULL == hd->buf.data)
		membuf_init(&hd->buf, 128);

	WebSocketGetData(hd, data, Len);
	clidata->pa.flag = bitRemove(clidata->pa.flag, 0x4);

	if (hd->isEof)
	{
		switch (hd->type) {
		case 0: //0x0表示附加数据帧
			break;
		case 1: //0x1表示文本数据帧
			clidata->pa.flag |= 0x4;
		case 2: //0x2表示二进制数据帧
			//接收数据回调
			if (tw_conf->on_data)
				tw_conf->on_data(tw_conf->data, client, &clidata->pa, &hd->buf);
			break;
		case 3: case 4: case 5: case 6: case 7: //0x3 - 7暂时无定义，为以后的非控制帧保留
			//membuf_uninit(hd->buf);
			//memset(hd, 0, sizeof(WebSocketHandle));
			break;
		case 8: //0x8表示连接关闭
			*(data + 1) = 0;//无数据
			tw_send_data(client, data, 2, 1, 0);
			if (hd->buf.size > 2) { //错误信息
				if (tw_conf->on_error) {
					char errstr[60] = { 0 };
					snprintf(errstr, 59, "-0:wserr,%s", hd->buf.data + 2);
					//出错信息回调
					tw_conf->on_error(tw_conf->data, client, &clidata->pa, 0, errstr);
				}
				else
					fprintf(stderr, "-0:wserr,%s\n", hd->buf.data + 2);
			}
			break;
		case 9: //0x9表示ping
		case 10://0xA表示pong
		default://0xB - F暂时无定义，为以后的控制帧保留
			*data += 1;//发送pong
			*(data + 1) = 0;//无数据
			tw_send_data(client, data, 2, 1, 0);
			//memset(hd, 0, sizeof(WebSocketHandle));
			break;
		}
		//释放 membuf , 收到消息时再分配
		clidata->hd.dfExt = clidata->hd.isEof = clidata->hd.type = 0;
		membuf_uninit(&hd->buf);
	}
}

//(循环)读取客户端发送的数据,接收客户的数据
static void on_uv_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
	tw_config* tw_conf = (tw_config*)(client->loop->data);
	tw_client* clidata = (tw_client*)client->data;
	membuf_t mbuf;
	if (nread > 0) {
		assert(clidata);
		//WebSocket
		if (clidata->pa.flag & 0x2) { //WebSocket
			on_read_websocket(client, buf->base, nread);
		}
		//long-link
		else if (clidata->pa.flag & 0x1) { //SOCKET //long-link
			//接收数据回调
			if (tw_conf->on_data) {
				mbuf.data = buf->base;
				mbuf.size = nread;
				mbuf.buffer_size = buf->len;
				tw_conf->on_data(tw_conf->data, client, &clidata->pa, &mbuf);
			}
		}
		//http 或 未知
		else { //http 或 未知
			char* p, *p2;
			tw_reqHeads heads;
			memset(&heads, 0, sizeof(tw_reqHeads));
			p = tw_get_http_heads(buf, &heads);//get Sec-WebSocket-Key ?
			if (p) { //WebSocket 握手
				clidata->pa.flag |= 3;//long-link & WebSocket
				p2 = WebSocketHandShak(p);
				tw_send_data(client, p2, -1, 1, 0);
				free(p2);
			}
			else if (heads.method) { //HTTP
				//所有请求全部回调,返回非0,则继续
				if (tw_conf->on_request == 0 || 0 == tw_conf->on_request(tw_conf->data, client, &clidata->pa, &heads)) {
					tw_request(client, &heads);
				}
			}
			else { //SOCKET
				clidata->pa.flag |= 1;//long-link
				//接收数据回调
				if (tw_conf->on_data) {
					mbuf.data = buf->base;
					mbuf.size = nread;
					mbuf.buffer_size = buf->len;
					tw_conf->on_data(tw_conf->data, client, &clidata->pa, &mbuf);
				}
			}
		}
	}
	else if (nread <= 0) {//在任何情况下出错, read 回调函数 nread 参数都<0，如：出错原因可能是 EOF(遇到文件尾)
		if (nread != UV_EOF) {
			if (tw_conf->on_error) {
				char errstr[60] = { 0 };
				snprintf(errstr, 59, "%d:%s,%s", (int)nread, uv_err_name((int)nread), uv_strerror((int)nread));
				//出错信息回调
				tw_conf->on_error(tw_conf->data, client, &clidata->pa, nread, errstr);
			}
			else
				fprintf(stderr, "%d:%s,%s\n", (int)nread, uv_err_name((int)nread), uv_strerror((int)nread));
		}
		//else //关闭连接
		tw_close_client(client);
	}
	//每次使用完要释放
	if (buf->base)
		free(buf->base);
}

//为每次读取数据分配内存缓存
static void on_uv_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
	buf->base = (char*)calloc(1, suggested_size);
	buf->len = suggested_size;
}

//获取客户端的 socket,ip,port
static char tw_getPeerAddr(uv_stream_t* client, tw_peerAddr* pa)
{
	struct sockaddr_in peeraddr[2], hostaddr[2]; //Windows 下不声明成数组(且数组长度大于1),getpeername会失败(errno=10014)
	int addrlen = sizeof(peeraddr);
	//memset(pa, 0, sizeof(PeerAddr));
	//客户端的地址
	if (client->type == UV_TCP) {
#ifdef WIN32
		pa->sk = ((uv_tcp_t*)client)->socket;
#else
		addrlen /= 2;
#if defined(__APPLE__)
		if (client->select)
			pa->sk = client->select;
		else
#endif
			pa->sk = client->io_watcher.fd;
#endif
		int er = uv_tcp_getpeername((uv_tcp_t*)client, (struct sockaddr*)peeraddr, &addrlen);
		if (er < 0) 
			memset(peeraddr, 0, addrlen);
		er = uv_tcp_getsockname((uv_tcp_t*)client, (struct sockaddr*)hostaddr, &addrlen);
		if (er < 0)
			memset(hostaddr, 0, addrlen);
	}
	else if (client->type == UV_UDP) {
#ifdef WIN32
		pa->sk = ((uv_udp_t*)client)->socket;
#else
		pa->sk = client->io_watcher.fd;
#endif
		int er = uv_udp_getsockname((uv_udp_t*)client, (struct sockaddr*)peeraddr, &addrlen);
		if (er < 0)
			memset(peeraddr, 0, addrlen);
		er = uv_udp_getsockname((uv_udp_t*)client, (struct sockaddr*)hostaddr, &addrlen);
		if (er < 0)
			memset(hostaddr, 0, addrlen);
	}
	else
		return 1;
	//网络字节序转换成主机字符序
	uv_ip4_name(peeraddr, pa->ip, sizeof(pa->ip));
	pa->port = ntohs(peeraddr[0].sin_port);
	uv_ip4_name(hostaddr, pa->fip, sizeof(pa->ip));
	pa->fport = ntohs(hostaddr[0].sin_port);

	return 0;
}

//客户端接入
static void tw_on_connection(uv_stream_t* server, int status) {
	//assert(server == (uv_stream_t*)&_server);
	tw_client* cli;
	if (status == 0) {
		//建立客户端信息,在关闭连接时释放 see after_uv_close_client
		uv_tcp_t* client = (uv_tcp_t*)calloc(1, sizeof(uv_tcp_t));
		//创建客户端的数据缓存块,在关闭连接时释放 see after_uv_close_client
		cli = client->data = calloc(1, sizeof(tw_client));
		uv_tcp_init(server->loop, client);//将客户端放入loop
		//接受客户，保存客户端信息
		uv_accept(server, (uv_stream_t*)client);
		client->close_cb = after_uv_close_client;
		//取客户端 socket,ip,port;
		tw_getPeerAddr((uv_stream_t*)client, &cli->pa);
		//开始读取客户端数据
		uv_read_start((uv_stream_t*)client, on_uv_alloc, on_uv_read);
		//客户端接入回调
		tw_config* tw_conf = (tw_config*)(server->loop->data);
		if (tw_conf->on_connect)
			tw_conf->on_connect(tw_conf->data, (uv_stream_t*)client, &cli->pa);
	}
}

//==================================================================================================

//TinyWeb 线程开始运行
static void tw_run(uv_loop_t* loop) {
	tw_config* tw_conf = (tw_config*)loop->data;
	printf("TinyWeb v1.0.0 is started, listening on %s:%d...\n", tw_conf->ip, tw_conf->port);
	uv_run(loop, UV_RUN_DEFAULT);
	uv_stop(loop);
	free(tw_conf->doc_dir);
	free(tw_conf->doc_index);
	free(tw_conf);
	if (!uv_loop_close(loop) && loop != uv_default_loop()) {
		uv_loop_delete(loop);
	}
	printf("TinyWeb v1.0.0 is stoped, listening on %s:%d...\n", tw_conf->ip, tw_conf->port);
}

//start web server, start with the config
//loop: if is NULL , it will be uv_default_loop()
//conf: the server config
int tinyweb_start(uv_loop_t* loop, tw_config* conf) {
	int ret;
	char p;
	assert(conf != NULL);
	if (conf->ip == NULL || (conf->ip != NULL && conf->ip[0] == '*'))
		conf->ip = "0.0.0.0";
	struct sockaddr_in addr;
	uv_ip4_addr(conf->ip, conf->port, &addr);

	tw_config* tw_conf = calloc(1, sizeof(tw_config));
	memcpy(tw_conf, conf, sizeof(tw_config));

	//设置主目录（末尾不带斜杠）
	if (conf->doc_dir)
	{
		tw_conf->doc_dir = strdup(conf->doc_dir);
	}

	printf("WebRoot Dir:%s\n", tw_conf->doc_dir);
	//设置默认主页（分号间隔）
	if (conf->doc_index && strcmpi(conf->doc_index, "") != 0)
		tw_conf->doc_index = strdup(conf->doc_index);
	else
		tw_conf->doc_index = strdup("index.htm;index.html");
	//设置more编码
	if (conf->charset)
		tw_conf->charset = strdup(conf->charset);
	else
		tw_conf->charset = strdup("utf-8");
	if (loop == NULL)
		loop = uv_default_loop();
	ret = uv_tcp_init(loop, &tw_conf->_server);
	if (ret < 0)
		return ret;
	ret = uv_tcp_bind(&tw_conf->_server, (const struct sockaddr*) &addr, 0);
	if (ret < 0)
		return ret;
	ret = uv_listen((uv_stream_t*)&tw_conf->_server, 8, tw_on_connection);
	if (ret < 0)
		return ret;
	loop->data = tw_conf;
	//开始线程
	uv_thread_t hare_id;
	uv_thread_create(&hare_id, (uv_thread_cb)tw_run, loop);
	return 0;
}

static void on_close_cb(uv_handle_t* handle) {
}

//stop TinyWeb
//当执行uv_stop之后，uv_run并不能马上退出，而是要等待其内部循环的下一个iteration到来时才会退出；
//如果提前free掉loop就会导致loop失效。当然也可以sleep几十毫秒然后再close，但这么搞不太雅观。
//uv_stop以后不能马上执行uv_loop_close()
//貌似关闭及释放loop等资源不是很完善的样子
void tinyweb_stop(uv_loop_t* loop)
{
	if (loop == NULL)
		loop = uv_default_loop();
	uv_stop(loop);
	if (loop->data)
		uv_close((uv_handle_t*)&((tw_config*)loop->data)->_server, on_close_cb);
	uv_loop_close(loop);
}

