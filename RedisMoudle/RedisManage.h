#ifndef __CREDISMANAGE__H
#define __CREDISMANAGE__H

//#ifdef __REDIS_MANAGE_DLL
//#define __REDIS_MANAGE_DLL_API __declspec(dllexport)
//#else
//#define __REDIS_MANAGE_DLL_API __declspec(dllimport)
//#endif

#define __REDIS_MANAGE_DLL_API 

#include "redis/hiredis.h"
#include <iostream>

class  __REDIS_MANAGE_DLL_API CRedisManage
{
public:
	CRedisManage();
	~CRedisManage();

public:
	bool Init(const char* strIP, int nPort); // ��ʼ��

public:
	const char* GetErrorMsg();
	redisContext* GetRedis()
	{
		return m_hDBLogin;
	}

public:
	bool m_bInit; // ��ʼ��
	bool m_bRun;  // ������

private:
	redisContext* m_hDBLogin; // ����redis���ݿ�
	std::string   m_strErrorMsg; // ������Ϣ
};

#endif