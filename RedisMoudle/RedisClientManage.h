#ifndef __CREDISCLIENTMANAGE__H
#define __CREDISCLIENTMANAGE__H
#include "ParseSql.h"
#include "RedisManage.h"
#include <iostream>
#include "Cursor.h"

class CRedisClientManage
{
public:
	CRedisClientManage();
	~CRedisClientManage();

	bool Init(const char* redisServerIP, int nPort);
	bool Execute(void** hReord, const char* cmd, int nLen); // ִ������ select create�ȵ�
	bool EndOfRecord(void* hRecord);
	void MoveNext(void* hRecord);
	void GetValue(void* hRecord, const char* strField, char* pValue, int nLen);
	void CloseRecord(void* hRecord);
	void OpenRecord(void* hRecord);
	const char* GetErrorMsg()
	{
		return m_strErrorMsg.c_str();
	}

private:
	CRedisManage g_redisManage;
	CParseSql    g_parseSql;
	CCursor      m_cursor; // ��¼�α�
	std::string  m_strErrorMsg;
};


#endif