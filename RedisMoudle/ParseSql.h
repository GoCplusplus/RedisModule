#ifndef __CPARSESQL__H
#define __CPARSESQL__H
#include "Scanner.h"
#include "redis/hiredis.h"
#include <iostream>
#include <list>

const int TOKEN_ITEM_SIZE = 120;

class TokensTable
{

private:
	struct HashNode
	{
		keyword token;
		HashNode* pNext;
		HashNode()
		{
			token.eToken = -1;
			memset(token.express, 0, sizeof(token.express));
			pNext = nullptr;
		}
	};

private:
	TokensTable() {}
public:
	~TokensTable()
	{
		for (int i = 0; i < TOKEN_ITEM_SIZE; i++)
		{
			HashNode hashNode = hashTable[i];
			DeleteHashNode(hashNode.pNext);
		}
	}
private:
	void DeleteHashNode(HashNode*& pNode)
	{
		if (!pNode)
			return;
		DeleteHashNode(pNode->pNext);
		delete pNode;
		pNode = nullptr;
	}
public:
	static int AddTokens(keyword token);
	static bool FindToken(const char* pWord, int& nToken);
private:
	static int GetHashCode(const char* pWord, int tablesize);
private:
	static HashNode hashTable[TOKEN_ITEM_SIZE];
};

void AddAllTokens();

class CParseSql // ����sql���ģ��
{
public:
	CParseSql();
	~CParseSql();

public:
	static void LoadToken();
	bool Parse(const char* strContent, int nLen); //����sql���
	const char* GetErrorMsg()
	{
		return m_strError.c_str();
	}
	void SetRedisConnect(redisContext* hDB)
	{
		m_hDBLogin = hDB;
	}

private:
	bool CreateTable(); // ������
	bool SelectFromTable(); // ѡ���
	bool InsertIntoTable(); // ��������
	bool CheckKeyExists(const char* pKey, RedisExistKey, bool& bExist,const char* tablename = nullptr); //����ֶ��Ƿ���� ����ֶ�ʱ��Ҫtablename
	bool ExecuteRedisCommand(RedisCommand RCommand, void* pGetValue, int& nLen,char* command, ...); // �Է���ֵ������Ȥ��pGetValueΪnullptr
	bool SetCommand(RedisCommand RCommand);
	bool DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName, DataRecord**& pRecords, int& nReords, bool bDelete = false); // ���в���
	bool DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName, DataRecord**& pRecords, int& nReords, std::vector<int>& vectNums); // ���в��� vectNumsΪ���Ӧ�ļ�¼

	bool IsSatisfyRecord(std::vector<std::string>& vectFields, std::vector<std::string>& vectValues, QueryTree* pTree); // �Ƿ�Ϊ�����ѯ�����ļ�¼
	bool TruncateTable(); // truncate table
	bool TruncateTable(const char* pTableName);
	bool DropTable(); // drop table
	bool DeleteTable(); // delete table
	bool UpdateTable(); // update table
private:
	CScanner m_Scanner;
	std::string m_strError;
	redisContext* m_hDBLogin;
};

#endif