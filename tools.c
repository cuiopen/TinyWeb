﻿//this file codes is for windows
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef _MSC_VER
#include <windows.h>
#include <time.h>

//#  if defined(WIN32)
//#  define snprintf _snprintf
//#  endif

#else //__GNUC__
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <iconv.h>
#endif

#include "Tools.h"

//#include "membuf.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <limits.h>

//-----------------------------------------------------------------------------------membuf c-str  win/linux
#pragma region membuf c-str

#include <assert.h>
//初始化
void membuf_init(membuf_t* buf, unsigned int initial_buffer_size) {
	memset(buf, 0, sizeof(membuf_t));
	buf->data = initial_buffer_size > 0 ? (unsigned char*)calloc(1, initial_buffer_size) : NULL;
	//memset(buf->data, 0, initial_buffer_size);
	buf->buffer_size = initial_buffer_size;
}
//释放buffer
void membuf_uninit(membuf_t* buf) {
	if (buf->data)
		free(buf->data), buf->data = NULL;
	memset(buf, 0, sizeof(membuf_t) - sizeof(char));
}
//清除数据（数据覆盖为NULL），并缩小buffer大小
void membuf_clear(membuf_t* buf, unsigned int maxSize)
{
	if (buf->data)
	{
		if (maxSize>1 && buf->buffer_size > maxSize)
		{
			unsigned char* p = (unsigned char*)realloc(buf->data, maxSize);
			//防止realloc分配失败，或返回的地址一样
			assert(p);
			if (p != buf->data)
				buf->data = p;
			buf->size = 0;
			buf->buffer_size = maxSize;
		}
		else
		{
			buf->size = 0;
		}
		memset(buf->data, 0, buf->buffer_size);
	}
}
////扩展buffer大小
void membuf_reserve(membuf_t* buf, unsigned int extra_size) {
	if (extra_size > buf->buffer_size - buf->size) {
		//calculate new buffer size
		unsigned int new_buffer_size = buf->buffer_size == 0 ? extra_size : buf->buffer_size << 1;
		unsigned int new_data_size = buf->size + extra_size;
		while (new_buffer_size < new_data_size)
			new_buffer_size <<= 1;

		// malloc/realloc new buffer
		unsigned char* p = (unsigned char*)realloc(buf->data, new_buffer_size); // realloc new buffer
		//防止realloc分配失败，或返回的地址一样
		assert(p);
		if (p != buf->data)
			buf->data = p;
		memset(buf->data + buf->size, 0, new_buffer_size - buf->size);
		buf->buffer_size = new_buffer_size;
	}
}
//添加数据
unsigned int membuf_append_data(membuf_t* buf, void* data, unsigned int size) {
	assert(data && size > 0);
	membuf_reserve(buf, size);
	char* c=memmove(buf->data + buf->size, data, size);
	buf->size += size;
	return (buf->size - size);
}
//插入数据：offset位置，data数据，size数据大小
void membuf_insert(membuf_t* buf, unsigned int offset, void* data, unsigned int size) {
	assert(offset < buf->size);
	membuf_reserve(buf, size);
	memcpy(buf->data + offset + size, buf->data + offset, buf->size - offset);
	memcpy(buf->data + offset, data, size);
	buf->size += size;
}
//从末尾移除数据（不会填充为NULL，仅更改size）
void membuf_remove(membuf_t* buf, unsigned int offset, unsigned int size) {
	assert(offset < buf->size);
	if (offset + size >= buf->size) {
		buf->size = offset;
	}
	else {
		memmove(buf->data + offset, buf->data + offset + size, buf->size - offset - size);
		buf->size -= size;
	}
	if (buf->buffer_size >= buf->size)
		buf->data[buf->size] = 0;
}

#pragma endregion

//-----------------------------------------------------------------------------------文件/文件夹检测  win/linux
#pragma region 文件/文件夹检测

#ifdef _MSC_VER

//获取工作目录路径,不带'\'
char* getWorkPath()
{
	static char CurPath[260] = { 0 };
	GetCurrentDirectory(259, CurPath);
	return CurPath;
}
//获取程序文件所在路径,不带'\'
char* getProcPath()
{
	static char CurPath[260] = { 0 };
	GetModuleFileName(GetModuleHandle(NULL), CurPath, 259);
	//获取当前目录绝对路径，即去掉程序名，包括去掉最后的'\'
	int i = strlen(CurPath) - 1;
	for (; i > 0 && CurPath[i] != '\\'; --i)
	{
		CurPath[i] = 0;
	}
	if (i>2 && CurPath[i]=='\\')
		CurPath[i] = 0;
	return CurPath;
}

//获取文件/文件夹信息
inline WIN32_FIND_DATA GetFileInfo(const char* lpPath)
{
	WIN32_FIND_DATA fd;
	memset(&fd, 0, sizeof(WIN32_FIND_DATA));
	HANDLE hFind = FindFirstFile(lpPath, &fd);
	FindClose(hFind);
	return fd;
}

//路径是否存在(2：存在:文件夹  1：存在:文件  0：不存在)
char isExist(const char* path)
{
	WIN32_FIND_DATA fd = GetFileInfo(path);
	return (fd.cFileName[0] && fd.dwFileAttributes) ? ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)?2:1) : 0;
}

//是否文件(1:是文件  0:非文件/不存在)
char isFile(const char* path)
{
	WIN32_FIND_DATA fd = GetFileInfo(path);
	return (fd.cFileName[0] && !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
}

//是否目录(1:是目录  0;非目录/不存在)
char isDir(const char* path)
{
	WIN32_FIND_DATA fd = GetFileInfo(path);
	return (fd.cFileName[0] && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
}

//列表目录
char* listDir(const char* fullpath, const char* reqPath)
{
	int fnum = 0;
	char tmp[1024];
	membuf_t buf;
	membuf_init(&buf, 5120);
	sprintf(tmp, "<html><head><title>Index of %s</title>\r\n"
		"</head><body><h1>Index of %s</h1>\r\n"
		"<table>\r\n"
		"<thead><tr><th>@</th><th><a href=\"#\">Name</a></th><th><a href=\"#\">Size</a></th><th><a href=\"#\">Last modified</a></th></tr><tr><th colspan=\"4\"><hr></th></tr></thead>\r\n"
		"<tbody id=\"tbody\"></tbody>"
		"<tfoot><tr><th colspan=\"4\"><hr></th></tr></tfoot>"
		"</table>"
		"<address>TinyWeb Server</address>"
		"</body></tml>\r\n<script type=\"text/javascript\">\r\nvar files={\"path\":\"%s\",\"files\":[\r\n\0\0", reqPath, reqPath, reqPath);

	membuf_append_data(&buf, tmp, strlen(tmp));
	//文件(size>-1) 或 目录（size=-1）   [name:"file1.txt",mtime:"2016-11-28 16:25:46",size:123],\r\n
	WIN32_FIND_DATA fdt;
	HANDLE hFind;
	char szFind[256];
	FILETIME ft;//高低位时间
	SYSTEMTIME st;//年月日 时分秒
	DWORD sz;

	sprintf(szFind, "%s\\*", fullpath);
	hFind = FindFirstFile(szFind, &fdt);
	//[name:"file1.txt",mtime:"2016-11-28 16:25:46",size:123],\r\n
	while (hFind != INVALID_HANDLE_VALUE)//一次查找循环
	{
		ft = fdt.ftLastWriteTime;//最后修改时间
		FileTimeToLocalFileTime(&ft, &ft);
		FileTimeToSystemTime(&ft, &st);
		if (fdt.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)//文件夹
		{
			if (strcmp(fdt.cFileName, ".") == 0)
			{
				if (!FindNextFile(hFind, &fdt))
					break;//下一个文件
				continue;
			}
			sprintf(tmp,"{\"name\":\"%s/\",\"mtime\":\"%d-%02d-%02d %02d:%02d:%02d\",\"size\":\"-\",\"type\":\"D\"},\n", fdt.cFileName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}
		else //文件
		{
			sz = fdt.nFileSizeHigh*(MAXDWORD + 1) + fdt.nFileSizeLow; //#define MAXDWORD    0xffffffff
			sprintf(tmp,"{\"name\":\"%s\",\"mtime\":\"%d-%02d-%02d %02d:%02d:%02d\",\"size\":%ld,\"type\":\"F\"},\n", fdt.cFileName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, sz);
		}
		membuf_append_data(&buf, tmp, strlen(tmp));
		if (!FindNextFile(hFind, &fdt))
			break;//下一个文件
	}
	FindClose(hFind);
	//membuf_remove(&buf, buf.size-1, 1);
	buf.data[--buf.size] = 0; buf.data[--buf.size] = 0;
	sprintf(tmp, "],total:%d};\r\nif(files){var html=\"\", p=files.path[files.path.length-1];"
		"if(p!='/'){files.path+='/';}"
		"files.files.sort(function(a,b){return a.name.localeCompare(b.name)}).sort(function(a,b){return a.type.localeCompare(b.type)});"
		"for (var r in files.files)"
		"{"
			"r=files.files[r];"
			"html+=\"<tr><td>\"+r.type+\"</td><td><a href='\"+r.name+\"'>\"+r.name+\"</td><td>\"+r.size+\"</td><td>\"+r.mtime+\"</td></tr>\";"
		"}"
		"document.querySelector(\"tbody\").innerHTML = html;"
		"}\r\n</script>\0\0", fnum);
	//window下需要转换为UTF8编码，以发送给客户端
	membuf_append_data(&buf,tmp,strlen(tmp));
	buf.data[buf.size] = 0;
	return (char*)buf.data;
}

#else //_GNUC_

//获取工作目录路径,不带'/'
char* getWorkPath()
{
	static char CurPath[260] = { 0 };
	getcwd(CurPath, 259);
	return CurPath;
}

//获取程序文件所在路径,不带'/'
char* getProcPath()
{
	static char CurPath[260] = { 0 };
	int cnt = readlink("/proc/self/exe", CurPath, 259);
	if (cnt > 0 || cnt < 260)
	{
		//获取程序路径，即去掉程序名，包括去掉最后的'/'
		int i;
		for (i = cnt - 1; i > 0 && CurPath[i] != '/'; --i)
		{
			CurPath[i] = 0;
		}
		if (i>2 && CurPath[i]=='/')
			CurPath[i] = 0;
	}
	return CurPath;
}

//路径是否存在(2：存在:文件夹  1：存在:文件  0：不存在)
char isExist(const char* path)
{
	if (path && access(path, F_OK) == 0)
	{
		struct stat info;
		stat(path, &info);
		if (S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))//dir or link
			return 2;
		return 1;
	}
	return 0;
}
//是否目录(1:是目录  0;非目录/不存在)
char isDir(const char* path)
{
	struct stat info;
	if (path && !stat(path, &info))// && opendir(path)!=NULL)
	{
		if (S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))//dir or link
			return 1;
	}
	return 0;
}
//是否文件(1:是文件  0:非文件/不存在)
char isFile(const char* path)
{
	if (path)
	{
		struct stat info;
		stat(path, &info);
		if (S_ISREG(info.st_mode))//普通文件
			return 1;
	}
	return 0;
}

//列表目录
char* listDir(const char* fullpath, const char* pathinfo)
{
	int fnum = 0;
	char tmp[1024];
	struct tm *mtime;
	DIR *dp;
	struct dirent *fileInfo;
	struct stat statbuf;
	membuf_t buf;
	membuf_init(&buf, 5120);

	sprintf(tmp, "<html><head><title>Index of %s</title>\r\n"
		"</head><body><h1>Index of %s</h1>\r\n"
		"<table>\r\n"
		"<thead><tr><th>@</th><th><a id=\"fname\" href=\"#\">Name</a></th><th><a id=\"fsize\" href=\"#\">Size</a></th><th><a id=\"ftime\" href=\"#\">Last modified</a></th></tr><tr><th colspan=\"4\"><hr></th></tr></thead>\r\n"
		"<tbody id=\"tbody\"></tbody>"
		"<tfoot><tr><th colspan=\"4\"><hr></th></tr></tfoot>"
		"</table>"
		"<address>TinyWeb Server</address>"
		"</body></tml>\r\n<script type=\"text/javascript\">\r\nvar files={\"path\":\"%s\",\"files\":[\r\n\0\0", pathinfo, pathinfo, pathinfo);

	membuf_append_data(&buf, tmp, strlen(tmp));
	//文件(size>-1) 或 目录（size=-1）   [name:"file1.txt",mtime:"2016-11-28 16:25:46",size:123],\r\n
	if((dp = opendir(fullpath))!= NULL)
	{
		while ((fileInfo = readdir(dp)) != NULL)
		{
			sprintf(tmp,"%s/%s\0\0", fullpath, fileInfo->d_name);
			stat(tmp, &statbuf);//stat函数需要传入绝对路径或相对（工作目录的）路径
			mtime=localtime(&statbuf.st_mtime);
			if (S_ISDIR(statbuf.st_mode))
			{
				if (strcmp(fileInfo->d_name, ".") == 0)
					continue;
				sprintf(tmp, "{\"name\":\"%s/\",\"mtime\":\"%d-%02d-%02d %02d:%02d:%02d\",\"size\":\"-\",\"type\":\"D\"},\n", fileInfo->d_name, (1900 + mtime->tm_year), (1 + mtime->tm_mon), mtime->tm_mday, mtime->tm_hour, mtime->tm_min, mtime->tm_sec);
			}
			else
				sprintf(tmp, "{\"name\":\"%s\",\"mtime\":\"%d-%02d-%02d %02d:%02d:%02d\",\"size\":%ld,\"type\":\"F\"},\n", fileInfo->d_name, (1900 + mtime->tm_year), (1 + mtime->tm_mon), mtime->tm_mday, mtime->tm_hour, mtime->tm_min, mtime->tm_sec, statbuf.st_size);
			membuf_append_data(&buf, tmp, strlen(tmp));
		}
		closedir(dp);
	}
	//membuf_remove(&buf, buf.size - 1, 1);
	buf.data[--buf.size] = 0; buf.data[--buf.size] = 0;
	sprintf(tmp, "],total:%d};\r\nif(files){var html=\"\", p=files.path[files.path.length-1];"
		"if(p!='/'){files.path+='/';}"
		"files.files.sort(function(a,b){return a.name.localeCompare(b.name)}).sort(function(a,b){return a.type.localeCompare(b.type)});"
		"for (var r in files.files)"
		"{"
		"r=files.files[r];"
		"html+=\"<tr><td>\"+r.type+\"</td><td><a href='\"+r.name+\"'>\"+r.name+\"</td><td>\"+r.size+\"</td><td>\"+r.mtime+\"</td></tr>\";"
		"}"
		"document.querySelector(\"tbody\").innerHTML = html;"
		"}\r\n</script>\0\0", fnum);
	membuf_append_data(&buf, tmp, strlen(tmp));
	buf.data[buf.size] = 0;
	return (char*)buf.data;
}
#endif

#pragma endregion

//-----------------------------------------------------------------------------------编码转换  win/linux
#pragma region 编码转换

#ifdef _MSC_VER
//GB2312 to unicode
wchar_t* GB2U(char* pszGbs, size_t* wLen)
{
	*wLen = MultiByteToWideChar(CP_ACP, 0, pszGbs, -1, NULL, 0);
	wchar_t* wStr = (wchar_t*)malloc(*wLen * sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, pszGbs, -1, wStr, *wLen);
	return wStr;
}
//unicode to utf8
char* U2U8(wchar_t* wszUnicode, size_t* aLen)
{
	*aLen = WideCharToMultiByte(CP_UTF8, 0, (PWSTR)wszUnicode, -1, NULL, 0, NULL, NULL);
	char* szStr = (char*)malloc(*aLen * sizeof(char));
	WideCharToMultiByte(CP_UTF8, 0, (PWSTR)wszUnicode, -1, szStr, *aLen, NULL, NULL);
	return szStr;
}
//utf8 to unicode
wchar_t* U82U(char* szU8, size_t* wLen)
{
	*wLen = MultiByteToWideChar(CP_UTF8, 0, szU8, -1, NULL, 0);
	wchar_t* wStr = (wchar_t*)malloc(*wLen * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, szU8, -1, wStr, *wLen);
	return wStr;
}
//unicode to GB2312
char* U2GB(wchar_t* wszUnicode, size_t* aLen)
{
	*aLen = WideCharToMultiByte(CP_ACP, 0, wszUnicode, -1, NULL, 0, NULL, NULL);
	char* szStr = (char*)malloc(*aLen * sizeof(char));
	WideCharToMultiByte(CP_ACP, 0, wszUnicode, -1, szStr, *aLen, NULL, NULL);
	return szStr;
}
//GB2312 to utf8
char* GB2U8(char* pszGbs, size_t* aLen)
{
	*aLen = MultiByteToWideChar(CP_ACP, 0, pszGbs, -1, NULL, 0);
	wchar_t* wStr = (wchar_t*)malloc(*aLen * sizeof(wchar_t));
	MultiByteToWideChar(CP_ACP, 0, pszGbs, -1, wStr, *aLen);

	*aLen = WideCharToMultiByte(CP_UTF8, 0, (PWSTR)wStr, -1, NULL, 0, NULL, NULL);
	char* szStr = (char*)malloc(*aLen * sizeof(char));
	WideCharToMultiByte(CP_UTF8, 0, (PWSTR)wStr, -1, szStr, *aLen, NULL, NULL);
	free(wStr);
	return szStr;
}
//utf8 to GB2312
char* U82GB(char* szU8, size_t* aLen)
{
	*aLen = MultiByteToWideChar(CP_UTF8, 0, szU8, -1, NULL, 0);
	wchar_t* wStr = (wchar_t*)malloc(*aLen * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, szU8, -1, wStr, *aLen);

	*aLen = WideCharToMultiByte(CP_ACP, 0, wStr, -1, NULL, 0, NULL, NULL);
	char* szStr = (char*)malloc(*aLen * sizeof(char));
	WideCharToMultiByte(CP_ACP, 0, wStr, -1, szStr, *aLen, NULL, NULL);
	free(wStr);
	return szStr;
}

#else

//代码转换:从一种编码转为另一种编码
int code_convert(char *from_charset, char *to_charset, char *inbuf, size_t inlen, char *outbuf, size_t* outlen)
{
	iconv_t cd;
	size_t rc = 0, len = *outlen;
	char **pin = &inbuf;
	char **pout = &outbuf;

	cd = iconv_open(to_charset, from_charset);
	if (cd == 0)
		return -1;
	memset(outbuf, 0, len);
	if (iconv(cd, pin, &inlen, pout, &len) == -1)
		rc = -1;
	iconv_close(cd);
	*outlen -= len;//返回已用长度
	return rc;
}

//GB2312 to unicode(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* GB2U(char* pszGbs, size_t* aLen)
{
	size_t len = *aLen * 4;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc=code_convert("gb2312", "unicode", pszGbs, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}
//unicode to utf8(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* U2U8(char* wszUnicode, size_t* aLen)
{
	size_t len = *aLen;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc = code_convert("unicode", "utf-8", wszUnicode, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}
//utf8 to unicode(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* U82U(char* szU8, size_t* aLen)
{
	size_t len = *aLen * 2;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc = code_convert("utf-8", "unicode", szU8, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}
//unicode to GB2312(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* U2GB(char* wszUnicode, size_t* aLen)
{
	size_t len = *aLen;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc = code_convert("unicode", "gb2312", wszUnicode, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}

//GB2312 to utf8(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* GB2U8(char* pszGbs, size_t* aLen)
{
	size_t len = *aLen * 3;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc = code_convert("gb2312", "utf-8", pszGbs, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}
//utf8 to GB2312(need free) 返回字串长度为:实际长度+1, 末尾\0站一字节（需要释放）
char* U82GB(char* szU8, size_t* aLen)
{
	size_t len = *aLen;
	char *outbuf = (char*)malloc(len + 1); outbuf[0] = 0;
	size_t rc = code_convert("utf-8", "gb2312", szU8, *aLen, outbuf, &len);
	if (rc < 0) *aLen = rc;
	else *aLen = len + 1;
	return outbuf;
}

#endif

/***************************************************************************
* 函数名称： UTF8ToUCS2
* 功能描述： 转换UTF8格式到UCS2格式（UCS2是双字节编码，Unicode是其中一种）
* 日	期： 2008-05-22 13:36:56
* 作	者： lianxiuzhu
* 参数说明： binUTF8 - UTF8字节流数组
*			 uCount - UTF8字节流数组中的字节数
*			 binUCS2 - UCS2字节流数组
* 返 回 值： 转换到UCS2字节流数组中的U16单元个数
***************************************************************************/
unsigned int UTF8ToUCS2(const unsigned char* binUTF8, unsigned int uCount, unsigned short* binUCS2)
{
	unsigned int uLength = 0;
	unsigned char* szTemp = (unsigned char*)binUTF8;
	while ((unsigned int)(szTemp - binUTF8) < uCount)
	{
		if (*szTemp <= 0x7F) //0xxxxxxx
		{
			binUCS2[uLength] = binUCS2[uLength] | (unsigned short)(*szTemp & 0x7F);
			szTemp = szTemp + 1;
		}
		else if (*szTemp <= 0xDF) //110xxxxx 10xxxxxx
		{
			binUCS2[uLength] = binUCS2[uLength] | (unsigned short)(*(szTemp + 1) & 0x3F);
			binUCS2[uLength] = binUCS2[uLength] | ((unsigned short)(*(szTemp) & 0x1F) << 6);
			szTemp = szTemp + 2;
		}
		else if (*szTemp <= 0xEF) //1110xxxx 10xxxxxx 10xxxxxx
		{
			binUCS2[uLength] = binUCS2[uLength] | (unsigned short)(*(szTemp + 2) & 0x3F);
			binUCS2[uLength] = binUCS2[uLength] | ((unsigned short)(*(szTemp + 1) & 0x3F) << 6);
			binUCS2[uLength] = binUCS2[uLength] | ((unsigned short)(*(szTemp) & 0x0F) << 12);
			szTemp = szTemp + 3;
		}
		else
		{
			return 0;
		}

		uLength++;
	}
	return uLength;
}

#pragma endregion

//-----------------------------------------------------------------------------------url编码解码  win/linux
#pragma region url编码解码

//url编码 (len为buf的长度)
size_t url_encode(const char *url, char *buf, size_t len)
{
	if (!buf || !url)
		return 0;

	const char *p;
	size_t x = 0;
	const char urlunsafe[] = "\r\n \"#%&+:;<=>?@[\\]^`{|}";
	const char hex[] = "0123456789ABCDEF";
	len--;

	for (p = url; *p; p++) {
		if (x >= len)
			break;

		if (*p < ' ' || *p > '~' || strchr(urlunsafe, *p)) {
			if ((x + 3) >= len)
				break;

			buf[x++] = '%';
			buf[x++] = hex[*p >> 4];
			buf[x++] = hex[*p & 0x0f];
		}
		else {
			buf[x++] = *p;
		}
	}
	buf[x] = '\0';

	return x;
}

//url解码
char* url_decode(char *url)
{
	char *o,*s;
	unsigned int tmp;

	for (o = s=url; *s; s++, o++) {
		if (*s == '%' && strlen(s) > 2 && sscanf(s + 1, "%2x", &tmp) == 1) {
			*o = (char)tmp;
			s += 2;
		}
		else {
			*o = *s;
		}
	}
	*o = '\0';
	return url;
}

#pragma endregion

//-----------------------------------------------------------------------------------Base64编码解码  win/linux
#pragma region Base64编码解码

char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char base64_end = '=';

inline char is_base64(unsigned char c) {
	return (isalnum(c) || (c == '+') || (c == '/'));
}

//Base64编码,需要释放返回值(need free return)
char* base64_Encode(unsigned char const* bytes_to_encode, unsigned int in_len)
{
	membuf_t ret;
	int i = 0, j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	membuf_init(&ret, in_len*3);//初始化缓存字节数为 长度的3被

	while (in_len--) {
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				membuf_append_data(&ret, &base64_table[char_array_4[i]], 1);
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			membuf_append_data(&ret, &base64_table[char_array_4[j]],1);

		while ((i++ < 3))
			membuf_append_data(&ret, &base64_end,1);
	}
	return (char*)ret.data;
}

//Base64解码,需要释放返回值(need free return)
char* base64_Decode(char* const encoded_string)
{
	int in_len = strlen(encoded_string);
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	membuf_t ret;

	while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_]; in_++;
		if (i == 4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = strstr(base64_table,(char*)&char_array_4[i])[0];

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++)
				membuf_append_data(&ret, &char_array_3[i],1);
			i = 0;
		}
	}

	if (i) {
		for (j = i; j <4; j++)
			char_array_4[j] = 0;

		for (j = 0; j <4; j++)
			char_array_4[j] = strstr(base64_table, (char*)&char_array_4[j])[0];

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++)
			membuf_append_data(&ret, &char_array_3[j], 1);
	}
	return (char*)ret.data;
}

#pragma endregion

//-----------------------------------------------------------------------------------Hash1加密  win/linux
#pragma region Hash1加密

/****************
* Rotate a 32 bit integer by n bytes
****************/
#if defined(__GNUC__) && defined(__i386__)
static inline u32 rol(u32 x, int n)
{
	__asm__("roll %%cl,%0"
		:"=r" (x)
		: "0" (x), "c" (n));
	return x;
}
#else
#define rol(x,n) ( ((x) << (n)) | ((x) >> (32-(n))) )
#endif

#define K1  0x5A827999L
#define K2  0x6ED9EBA1L
#define K3  0x8F1BBCDCL
#define K4  0xCA62C1D6L
#define F1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )
#define F2(x,y,z)   ( x ^ y ^ z )
#define F3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )
#define F4(x,y,z)   ( x ^ y ^ z )


#define M(i) ( tm = x[i&0x0f] ^ x[(i-14)&0x0f]    \
               ^ x[(i-8)&0x0f] ^ x[(i-3)&0x0f]    \
               , (x[i&0x0f] = rol(tm,1)) )


#define R(a,b,c,d,e,f,k,m)  do { e += rol( a, 5 ) \
            + f( b, c, d )                        \
            + k                                   \
            + m;                                  \
        b = rol( b, 30 );                         \
    } while(0)


void hash1_Reset(SHA1_CONTEXT* hd)
{
	hd->bFinal = 0;
	hd->h0 = 0x67452301;
	hd->h1 = 0xefcdab89;
	hd->h2 = 0x98badcfe;
	hd->h3 = 0x10325476;
	hd->h4 = 0xc3d2e1f0;
	hd->nblocks = 0;
	hd->count = 0;
	memset(hd->buf, 0, 64);
}

/*
* Transform the message X which consists of 16 32-bit-words
*/
static void hash1_transform(SHA1_CONTEXT* hd, unsigned char *data)
{
	uint a, b, c, d, e, tm;
	uint x[16];

	/* get values from the chaining vars */
	a = hd->h0;
	b = hd->h1;
	c = hd->h2;
	d = hd->h3;
	e = hd->h4;

#ifdef BIG_ENDIAN_HOST
	memcpy(x, data, 64);
#else
	{ int i;
	unsigned char *p2;
	for (i = 0, p2 = (unsigned char*)x; i < 16; i++, p2 += 4) {
		p2[3] = *data++;
		p2[2] = *data++;
		p2[1] = *data++;
		p2[0] = *data++;
	}
	}
#endif

	R(a, b, c, d, e, F1, K1, x[0]);
	R(e, a, b, c, d, F1, K1, x[1]);
	R(d, e, a, b, c, F1, K1, x[2]);
	R(c, d, e, a, b, F1, K1, x[3]);
	R(b, c, d, e, a, F1, K1, x[4]);
	R(a, b, c, d, e, F1, K1, x[5]);
	R(e, a, b, c, d, F1, K1, x[6]);
	R(d, e, a, b, c, F1, K1, x[7]);
	R(c, d, e, a, b, F1, K1, x[8]);
	R(b, c, d, e, a, F1, K1, x[9]);
	R(a, b, c, d, e, F1, K1, x[10]);
	R(e, a, b, c, d, F1, K1, x[11]);
	R(d, e, a, b, c, F1, K1, x[12]);
	R(c, d, e, a, b, F1, K1, x[13]);
	R(b, c, d, e, a, F1, K1, x[14]);
	R(a, b, c, d, e, F1, K1, x[15]);
	R(e, a, b, c, d, F1, K1, M(16));
	R(d, e, a, b, c, F1, K1, M(17));
	R(c, d, e, a, b, F1, K1, M(18));
	R(b, c, d, e, a, F1, K1, M(19));
	R(a, b, c, d, e, F2, K2, M(20));
	R(e, a, b, c, d, F2, K2, M(21));
	R(d, e, a, b, c, F2, K2, M(22));
	R(c, d, e, a, b, F2, K2, M(23));
	R(b, c, d, e, a, F2, K2, M(24));
	R(a, b, c, d, e, F2, K2, M(25));
	R(e, a, b, c, d, F2, K2, M(26));
	R(d, e, a, b, c, F2, K2, M(27));
	R(c, d, e, a, b, F2, K2, M(28));
	R(b, c, d, e, a, F2, K2, M(29));
	R(a, b, c, d, e, F2, K2, M(30));
	R(e, a, b, c, d, F2, K2, M(31));
	R(d, e, a, b, c, F2, K2, M(32));
	R(c, d, e, a, b, F2, K2, M(33));
	R(b, c, d, e, a, F2, K2, M(34));
	R(a, b, c, d, e, F2, K2, M(35));
	R(e, a, b, c, d, F2, K2, M(36));
	R(d, e, a, b, c, F2, K2, M(37));
	R(c, d, e, a, b, F2, K2, M(38));
	R(b, c, d, e, a, F2, K2, M(39));
	R(a, b, c, d, e, F3, K3, M(40));
	R(e, a, b, c, d, F3, K3, M(41));
	R(d, e, a, b, c, F3, K3, M(42));
	R(c, d, e, a, b, F3, K3, M(43));
	R(b, c, d, e, a, F3, K3, M(44));
	R(a, b, c, d, e, F3, K3, M(45));
	R(e, a, b, c, d, F3, K3, M(46));
	R(d, e, a, b, c, F3, K3, M(47));
	R(c, d, e, a, b, F3, K3, M(48));
	R(b, c, d, e, a, F3, K3, M(49));
	R(a, b, c, d, e, F3, K3, M(50));
	R(e, a, b, c, d, F3, K3, M(51));
	R(d, e, a, b, c, F3, K3, M(52));
	R(c, d, e, a, b, F3, K3, M(53));
	R(b, c, d, e, a, F3, K3, M(54));
	R(a, b, c, d, e, F3, K3, M(55));
	R(e, a, b, c, d, F3, K3, M(56));
	R(d, e, a, b, c, F3, K3, M(57));
	R(c, d, e, a, b, F3, K3, M(58));
	R(b, c, d, e, a, F3, K3, M(59));
	R(a, b, c, d, e, F4, K4, M(60));
	R(e, a, b, c, d, F4, K4, M(61));
	R(d, e, a, b, c, F4, K4, M(62));
	R(c, d, e, a, b, F4, K4, M(63));
	R(b, c, d, e, a, F4, K4, M(64));
	R(a, b, c, d, e, F4, K4, M(65));
	R(e, a, b, c, d, F4, K4, M(66));
	R(d, e, a, b, c, F4, K4, M(67));
	R(c, d, e, a, b, F4, K4, M(68));
	R(b, c, d, e, a, F4, K4, M(69));
	R(a, b, c, d, e, F4, K4, M(70));
	R(e, a, b, c, d, F4, K4, M(71));
	R(d, e, a, b, c, F4, K4, M(72));
	R(c, d, e, a, b, F4, K4, M(73));
	R(b, c, d, e, a, F4, K4, M(74));
	R(a, b, c, d, e, F4, K4, M(75));
	R(e, a, b, c, d, F4, K4, M(76));
	R(d, e, a, b, c, F4, K4, M(77));
	R(c, d, e, a, b, F4, K4, M(78));
	R(b, c, d, e, a, F4, K4, M(79));

	/* Update chaining vars */
	hd->h0 += a;
	hd->h1 += b;
	hd->h2 += c;
	hd->h3 += d;
	hd->h4 += e;

}


// Update the message digest with the contents of INBUF with length INLEN.
void hash1_Write(SHA1_CONTEXT* hd, unsigned char *inbuf, size_t inlen)
//static void sha1_write( SHA1_CONTEXT *hd, char *inbuf, size_t inlen)
{
	if (hd->bFinal)
		hash1_Reset(hd);
	if (hd->count == 64) { /* flush the buffer */
		hash1_transform(hd,hd->buf);
		hd->count = 0;
		hd->nblocks++;
	}
	if (!inbuf)
		return;
	if (hd->count) {
		for (; inlen && hd->count < 64; inlen--)
			hd->buf[hd->count++] = *inbuf++;
		hash1_Write(hd, NULL, 0);
		if (!inlen)
			return;
	}

	while (inlen >= 64) {
		hash1_transform(hd, inbuf);
		hd->count = 0;
		hd->nblocks++;
		inlen -= 64;
		inbuf += 64;
	}
	for (; inlen && hd->count < 64; inlen--)
		hd->buf[hd->count++] = *inbuf++;
}

/* The routine final terminates the computation and returns the digest.
* The handle is prepared for a new cycle, but adding bytes to the
* handle will the destroy the returned buffer.
* Returns: 20 bytes representing the digest.
*/
void hash1_Final(SHA1_CONTEXT* hd)
{
	uint t, msb, lsb;
	unsigned char *p;

	hash1_Write(hd, NULL, 0); /* flush */;

	t = hd->nblocks;
	/* multiply by 64 to make a byte count */
	lsb = t << 6;
	msb = t >> 26;
	/* add the count */
	t = lsb;
	if ((lsb += hd->count) < t)
		msb++;
	/* multiply by 8 to make a bit count */
	t = lsb;
	lsb <<= 3;
	msb <<= 3;
	msb |= t >> 29;

	if (hd->count < 56) { /* enough room */
		hd->buf[hd->count++] = 0x80; /* pad */
		while (hd->count < 56)
			hd->buf[hd->count++] = 0;  /* pad */
	}
	else { /* need one extra block */
		hd->buf[hd->count++] = 0x80; /* pad character */
		while (hd->count < 64)
			hd->buf[hd->count++] = 0;
		hash1_Write(hd, NULL, 0);  /* flush */;
		memset(hd->buf, 0, 56); /* fill next block with zeroes */
	}
	/* append the 64 bit count */
	hd->buf[56] = msb >> 24;
	hd->buf[57] = msb >> 16;
	hd->buf[58] = msb >> 8;
	hd->buf[59] = msb;
	hd->buf[60] = lsb >> 24;
	hd->buf[61] = lsb >> 16;
	hd->buf[62] = lsb >> 8;
	hd->buf[63] = lsb;
	hash1_transform(hd, hd->buf);

	p = hd->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(u32*)p = hd->h##a ; p += 4; } while(0)
#else /* little endian */
#define X(a) do { *p++ = hd->h##a >> 24; *p++ = hd->h##a >> 16; \
        *p++ = hd->h##a >> 8; *p++ = hd->h##a; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
#undef X
	//Hash1 operation finally
	hd->bFinal = 1;
}


unsigned char* hash1_Get(SHA1_CONTEXT* hd)
{
	if (!hd->bFinal)
		hash1_Final(hd);
	return hd->buf;
}

#pragma endregion

//-----------------------------------------------------------------------------------WebSocket  win/linux
#pragma region WebSocket Tool

//WebSocket握手Key计算
char* WebSocketHandShak(const char* key)
{
	char akey[100] = { 0 };
	char acc[165] = { 0 }, *p;
	int len;
	SHA1_CONTEXT hd;
	//
	//char* p=strstr(header, "Sec-WebSocket-Key:");//不需要查找 Upgrade:为 "websocket"
	//if (p)
	//{
		//p += 19;
		//char* p2=strst(p, "\r\n");
		strcpy(akey, key);
		strncpy(akey + strlen(key), "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);

		hash1_Reset(&hd);
		hash1_Write(&hd, (unsigned char*)akey, strlen(akey));
		hash1_Final(&hd);
		p=base64_Encode(hd.buf, strlen((char*)hd.buf));
		len=sprintf(acc, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", p);
		free(p);

		p = (char*)malloc(len+1);
		memcpy(p, acc, len);
		p[len] = 0;
		return p;
	//}
	//else
	//	return NULL;
}

typedef union  {
	unsigned short n;
	unsigned char c[2];
}u2;
typedef union  {
	unsigned long long n;
	unsigned char c[8];
}u8;

static void WebSocketDoMask(char* data, unsigned long len, char* mask)
{
	unsigned long i = 0;
	for(i=0; i<len; i++ )
		data[i] = data[i] ^ mask[i % 4];
}

//从帧中取得实际数据
long WebSocketGetData(WebSocketHandle* handle, char* data, unsigned long len)
{
	if (!handle || !data) return 0;
	handle->isEof = (char)(data[0] >> 7);//是否结束
	handle->dfExt = (data[0] & 0x70);//扩展码
	handle->type = data[0] & 0xF;//OPCode
	char hasMask = (char)(data[1] >> 7);
	char Mask[4];
	membuf_t* buf = &handle->buf;
	//Payload长度是ExtensionData长度与ApplicationData长度之和。
	//ExtensionData长度可能是0，这种情况下，Payload长度即是ApplicationData长度(默认ExtensionData长度是0)
	unsigned long tLen;//本次真实数据长度
	unsigned long Len = data[1] & 0x7f;//Payload长度
	//当前帧,第一截数据
	if (Len < 126) //如果其值在0-125，则是payload的真实长度(ApplicationData长度,ExtensionData长度为0)
	{
		if (hasMask)
		{
			if ((len - 6)>0)//防止结尾帧数据不够长度的错误
			{
				memcpy(Mask, &data[2], 4);
				tLen = len - 6;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[6], Len);
				WebSocketDoMask(buf->data + buf->size - Len, Len, Mask);
			}
		}
		else //没用掩码
			if ((len - 2)>0)
			{
				tLen = len - 2;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[2], Len);
			}
	}
	else if (Len == 126)//如果值是126，则后面2个字节形成的16位无符号整型数(unsigned short)的值是payload的真实长度，掩码就紧更着后面
	{
		u2 u; memcpy(u.c, &data[2], 2);
		Len = ntohs(u.n);//网络字节转换

		if (hasMask)
		{
			if ((len - 8)>0)
			{
				memcpy(Mask, &data[4], 4);//防止结尾帧数据不够长度的错误
				tLen = len - 8;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[8], Len);
				WebSocketDoMask(buf->data + buf->size - Len, Len, Mask);
			}
		}
		else //没用掩码
			if ((len - 4)>0)
			{
				tLen = len - 4;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[4], tLen);
			}
	}
	else if (Len == 127)//如果值是127，则后面8个字节形成的64位无符号整型数(unsigned int64)的值是payload的真实长度，掩码就紧更着后面
	{
		//u8 u; memcpy(u.c, &data[2], 8);
		//Len = ntohll(u.n);//网络字节转换
		//u_int64 ui64 = data[2]*0x100000000000000ULL+data[3]*0x1000000000000ULL+data[4]*0x10000000000ULL+data[5]*0x100000000ULL
		//				+data[6]*0x1000000ULL+data[7]*0x10000ULL+data[8]*0x100ULL+data[9]*0x1ULL;//逐字节转换
		Len = data[2] * 0x100000000000000ULL + data[3] * 0x1000000000000ULL + data[4] * 0x10000000000ULL + data[5] * 0x100000000ULL
				+data[6]*0x1000000ULL+data[7]*0x10000ULL+data[8]*0x100ULL+data[9]*0x1ULL;//逐字节转换
		if (hasMask)
		{
			if ((len - 14)>0)//防止结尾帧数据不够长度的错误
			{
				memcpy(Mask, &data[10], 4);
				tLen = len - 14;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[14], Len);
				WebSocketDoMask(buf->data + buf->size - Len, Len, Mask);
			}
		}
		else //没用掩码
			if ((len - 10)>0)
			{
				tLen = len - 10;
				Len = Len > tLen ? tLen : Len;
				membuf_append_data(buf, &data[10], Len);
			}
	}
	return Len;
}
//转换为一个WebSocket帧
char* WebSocketMakeFrame(const char* data, unsigned int dlen, char* framedata, unsigned int* flen)
{
	char bMask = 0;
	char Mask[4] = { 0 };
	if (data == NULL)
		return *flen = 0;
	//第一byte,10000000, fin = 1, rsv1 rsv2 rsv3均为0, opcode = 0x01,即数据为文本帧
	framedata[0] = 129;//0x81 最后一个包 |(无扩展协议)| 控制码(0x1表示文本帧)
	char len;
	unsigned int pos = 0;//newData使用了几位(newData[pos])
	if (dlen <= 125)
	{
		len = dlen;
		//数据长度
		framedata[1] = (bMask ? 0x80 : 0x00) | dlen;
		pos = 2;
	}
	else if (dlen <= 65535)
	{
		len = 126;
		framedata[1] = (bMask ? 0x80 : 0x00) | 0x7E;
		//数据长度
		framedata[2] = (dlen >> 8) & 255;
		framedata[3] = (dlen) & 255;
		pos = 4;
	}
	else
	{
		len = 127;
		framedata[1] = (bMask ? 0x80 : 0x00) | 0x7F;
		//数据长度
		framedata[2] = (dlen >> 56) & 255;
		framedata[3] = (dlen >> 48) & 255;
		framedata[4] = (dlen >> 40) & 255;
		framedata[5] = (dlen >> 32) & 255;
		framedata[6] = (dlen >> 24) & 255;
		framedata[7] = (dlen >> 16) & 255;
		framedata[8] = (dlen >> 8) & 255;
		framedata[9] = (dlen) & 255;
		pos = 10;
	}
	//是否有掩码
	if (bMask)
	{
		memcpy(framedata + pos, Mask, 4);
		pos += 4;
		for (unsigned int i = pos; i<dlen; i++)
			framedata[i] = data[i] ^ Mask[i % 4];
	}
	else
		memcpy(framedata + pos, data, dlen);
	flen = pos + dlen;
	return framedata;
}

#pragma endregion


//-----------------------------------------------------------------------------------工具/杂项  win/linux
#pragma region 工具/杂项

inline int day_of_year(int y, int m, int d)
{
	int k, leap, s;
	int days[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
	leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
	s = d;
	for (k = 1; k<m; k++)
	{
		s += days[k];
	}
	if (leap == 1 && m>2)
		s += 1;
	return s;
}

#ifdef _MSC_VER
//获取当前时间信息
tm_u GetLocaTime()
{
	//struct timeval  tv;
	SYSTEMTIME st;
	tm_u     tmu;

	//gettimeofday(&tv, NULL);
	GetLocalTime(&st);

	tmu.tm_sec = st.wSecond;
	tmu.tm_min = st.wMinute;
	tmu.tm_hour = st.wHour;
	tmu.tm_mday = st.wDay;
	tmu.tm_mon = st.wMonth;
	tmu.tm_year = st.wYear;
	tmu.tm_wday = st.wDayOfWeek;//(st.wDay + 2 * st.wMonth + 3 * (st.wMonth + 1) / 5 + st.wYear + st.wYear / 4 - st.wYear / 100 + st.wYear / 400 + 1) % 7;//基姆拉尔森计算公式
	tmu.tm_yday = day_of_year(st.wYear, st.wMonth, st.wDay);
	tmu.tm_isdst = 0;
	tmu.tm_vsec = GetTickCount();//tv.tv_sec;
	tmu.tm_usec = st.wMilliseconds * 1000;//tv.tv_usec;//

	return tmu;
}
//获取当天已逝去的秒数
inline unsigned int GetDaySecond()
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	return (st.wHour * 3600 + st.wMinute * 60 + st.wSecond);
}

#else
//获取当前时间信息
tm_u GetLocaTime()
{
	struct timeval  tv; //timespec
	struct tm       *p;
	tm_u     tmu;

	gettimeofday(&tv, NULL);
	p = localtime(&tv.tv_sec);

	tmu.tm_sec = p->tm_sec;
	tmu.tm_min = p->tm_min;
	tmu.tm_hour = p->tm_hour;
	tmu.tm_mday = p->tm_mday;
	tmu.tm_mon = p->tm_mon + 1;
	tmu.tm_year = p->tm_year + 1900;
	tmu.tm_wday = p->tm_wday;
	tmu.tm_yday = p->tm_yday;
	tmu.tm_isdst = p->tm_isdst;
	tmu.tm_vsec = tv.tv_sec;
	tmu.tm_usec = tv.tv_usec;
	return tmu;
}
//获取当天已逝去的秒数
inline unsigned int GetDaySecond()
{
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec%86400);
}

#endif // _MSC_VER

//字符串转换成时间戳(毫秒),字符串格式为:"2016-08-03 06:56:36"
inline long long str2stmp(const char *strTime)
{
	struct tm sTime;
	if (strTime != NULL)
#ifdef __GNUC__
		strptime(strTime, "%Y-%m-%d %H:%M:%S", &sTime);
#else
		sscanf(strTime, "%d-%d-%d %d:%d:%d", &sTime.tm_year, &sTime.tm_mon, &sTime.tm_mday, &sTime.tm_hour, &sTime.tm_min, &sTime.tm_sec);
#endif
	else
	{
		time_t timep;
		timep = time(0);
		sTime = *localtime(&timep);
	}
	sTime.tm_year -= 1900;
	sTime.tm_mon -= 1;
	long long ft = mktime(&sTime);
	return ft * 1000;
}
//时间戳(毫秒)转换成字符串,字符串格式为:"2016-08-03 06:56:36"
inline char* stmp2str(time_t t, char* str, int strlen)
{
	if (t>1000)
		t = t / 1000;
	else
	{
		t = time(0);
	}
	struct tm *sTime = localtime(&t);
	if (sTime)
		strftime(str, strlen, "%Y-%m-%d %H:%M:%S", sTime);
	return str;
}

#ifdef _MSC_VER

//#include <WinSock2.h>
#include <Iphlpapi.h>
#pragma comment(lib,"Iphlpapi.lib") //需要添加Iphlpapi.lib库
//获取IPv4地址 (第一个IPv4)
const char* GetIPv4()
{//详情见：http://www.cnblogs.com/lzpong/p/6137652.html
	static char CurIP[17] = { 0 };
	unsigned long stSize = sizeof(IP_ADAPTER_INFO);
	//PIP_ADAPTER_INFO结构体指针存储本机网卡信息
	PIP_ADAPTER_INFO pIpAdapterInfo = (PIP_ADAPTER_INFO)malloc(stSize);
	//得到结构体大小,用于GetAdaptersInfo参数
	//调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量;其中stSize参数既是一个输入量也是一个输出量
	int nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);

	if (ERROR_BUFFER_OVERFLOW == nRel)
	{
		//如果函数返回的是ERROR_BUFFER_OVERFLOW
		//则说明GetAdaptersInfo参数传递的内存空间不够,同时其传出stSize,表示需要的空间大小,stSize既是一个输入量也是一个输出量
		//释放原来的内存空间
		free(pIpAdapterInfo);
		//重新申请内存空间用来存储所有网卡信息
		pIpAdapterInfo = (PIP_ADAPTER_INFO)malloc(stSize);
		//再次调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量
		nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);
	}
	if (ERROR_SUCCESS == nRel)
	{
		//可能有多网卡,因此通过循环去判断
		while (pIpAdapterInfo)
		{
			if (MIB_IF_TYPE_ETHERNET == pIpAdapterInfo->Type)
			{
				IP_ADDR_STRING *pIpAddrString = &(pIpAdapterInfo->IpAddressList);
				strncpy(CurIP, pIpAddrString->IpAddress.String, 16);
				break;
			}
			pIpAdapterInfo = pIpAdapterInfo->Next;
		}
	}
	//释放内存空间
	if (pIpAdapterInfo)
		free(pIpAdapterInfo);

	return CurIP;
}
//获取IPv6地址 (第一个IPv6)
const char* GetIPv6()
{
	static char CurIP[50] = { 0 };

	return CurIP;
}

#else

#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
//#include <sys/socket.h>
//获取IP地址
int GetIP_v4_and_v6_linux(int family, char *address)
{
	struct ifaddrs *ifap0, *ifap;
	char buf[101];
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	int ret;
	if (NULL == address)
		return -1;

	if (getifaddrs(&ifap0))
		return -1;
	for (ifap = ifap0; ifap != NULL; ifap = ifap->ifa_next)
	{
		//if(strcmp(interface,ifap->ifa_name)!=0) continue; 
		if (ifap->ifa_addr == NULL) continue;
		if ((ifap->ifa_flags & IFF_UP) == 0) continue;
		if (family != ifap->ifa_addr->sa_family) continue;

		if (AF_INET == ifap->ifa_addr->sa_family)
		{
			addr4 = (struct sockaddr_in *)ifap->ifa_addr;
			if (NULL != inet_ntop(ifap->ifa_addr->sa_family, (void *)&(addr4->sin_addr), buf, 100))
			{
				//printf("IP4 family=%d address=%s\n",ifap->ifa_addr->sa_family,address);
				if (strcmp(buf, "127.0.0.1") == 0)
					continue;
				strcpy(address, buf);
				if (ifap0) { freeifaddrs(ifap0); ifap0 = NULL; }
				return 0;
			}
			//else
			//    break;
		}
		else if (AF_INET6 == ifap->ifa_addr->sa_family)
		{
			addr6 = (struct sockaddr_in6*) ifap->ifa_addr;
			if (IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr)) {/*printf("IN6_IS_ADDR_MULTICAST\n");*/ continue; }
			//if(IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)){/*本地IPv6地址*/printf("IN6_IS_ADDR_LINKLOCAL\n"); /*continue;*/}
			if (IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr)) {/*::1*/continue; }
			if (IN6_IS_ADDR_UNSPECIFIED(&addr6->sin6_addr)) {/*printf("IN6_IS_ADDR_UNSPECIFIED\n");*/ continue; }
			if (IN6_IS_ADDR_SITELOCAL(&addr6->sin6_addr)) {/*printf("IN6_IS_ADDR_SITELOCAL\n");*/ continue; }

			if (NULL != inet_ntop(ifap->ifa_addr->sa_family, (void *)&(addr6->sin6_addr), buf, 100))
			{
				//printf("IP6 family=%d address=%s\n",ifap->ifa_addr->sa_family,buf);
				//if(strcmp(buf,"::1")==0) //上面已经过滤了
				//	continue;
				strcpy(address, buf);
				if (ifap0) { freeifaddrs(ifap0); ifap0 = NULL; }
				return 0;
			}
			//else
			//   break; 
		}
	}
	if (ifap0) { freeifaddrs(ifap0); ifap0 = NULL; }
	return -1;
}


//获取IPv4地址 (第一个IPv4)
const char* GetIPv4()
{
	static char CurIP[17] = { 0 };
	GetIP_v4_and_v6_linux(AF_INET, CurIP);
	return CurIP;
}
//获取IPv6地址 (第一个IPv6)
const char* GetIPv6()
{
	static char CurIP[50] = { 0 };
	GetIP_v4_and_v6_linux(AF_INET6, CurIP);
	return CurIP;
}

#endif // _MSC_VER

#pragma endregion