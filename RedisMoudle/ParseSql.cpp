#include "ParseSql.h"
#include <cstring>
#include "comm.h"
#include <vector>
#include <algorithm>
#include "RedisSetCommand.h"
#include "json.h"
#include "RedisGetCommand.h"
#include <queue>
#include <sstream>
#include "Cursor.h"

TokensTable::HashNode TokensTable::hashTable[TOKEN_ITEM_SIZE];

int TokensTable::AddTokens(keyword token)
{
	int nHashCode = GetHashCode(token.express, TOKEN_ITEM_SIZE);
	if (hashTable[nHashCode].token.eToken == -1)
		hashTable[nHashCode].token = token;
	else
	{
		if (hashTable[nHashCode].token.eToken == token.eToken)
			return token.eToken;
		else
		{
			HashNode*& pNode = hashTable[nHashCode].pNext;
			while (pNode)
			{
				if (pNode->token.eToken == token.eToken)
					return token.eToken;
				pNode = pNode->pNext;
			}
			pNode = new HashNode;
			pNode->token = token;
		}
	}
	return 0;
}

int TokensTable::GetHashCode(const char* pWord, int tablesize)
{
	int hashval = 0;
	const char* szBegin = pWord;
	while (*szBegin)
	{
		hashval = 37 * hashval + *szBegin;
		hashval %= tablesize;
		szBegin++;
	}
	if (hashval < 0)
		hashval += tablesize;
	return hashval;
}

bool TokensTable::FindToken(const char* pWord, int& nToken)
{
	nToken = 0;
	int nHashCode = GetHashCode(pWord, TOKEN_ITEM_SIZE);
	HashNode hashNode = hashTable[nHashCode];
	if (strcmp(hashNode.token.express, pWord) == 0)
	{
		nToken = hashNode.token.eToken;
		return true;
	}
	else
	{
		HashNode* pNode = hashNode.pNext;
		while (pNode)
		{
			if (strcmp(pNode->token.express, pWord) == 0)
			{
				nToken = pNode->token.eToken;
				return true;
			}
			pNode = pNode->pNext;
		}
	}
	return false;
}

void AddAllTokens()
{
	for (int i = 0; i < sizeof(KeyWords) / sizeof(KeyWords[0]); i++)
		TokensTable::AddTokens(KeyWords[i]);
}

CParseSql::CParseSql()
{
}


CParseSql::~CParseSql()
{
}

void CParseSql::LoadToken() // ���عؼ���
{
	AddAllTokens();
}

bool CParseSql::Parse(const char* strContent, int nLen)
{
	m_Scanner.LoadContent(strContent, nLen);
	int token = m_Scanner.Scan();
	if (token == 0)
		return false;
	switch (token)
	{
	case token_create: // ������
	{
		return CreateTable();
	}
	case token_select: // ѡ�����
	{
		return SelectFromTable();
	}
	case token_insert: // ��������
	{
		return InsertIntoTable();
	}
	case token_truncate: // truncate table
	{
		return TruncateTable();
	}
	default:
		break;
	}
}

bool CParseSql::CreateTable()
{
	char tableName[128] = { 0x00 };
	std::vector<std::string> vectFields, vectIndex;
	int nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_table)
	{
		m_strError = "invalid symbol need table";
		return false;
	}
	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_word)
	{
		m_strError = "invalid symbol need tablename";
		return false;
	}
	strcpy_s(tableName, sizeof(tableName), m_Scanner.GetWord()); // ��ȡ����
	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_lpair)
	{
		m_strError = "invalid symbol need (";
		return false;
	}
	nWordsToken = m_Scanner.Scan();
	while (nWordsToken != token_rpair)
	{
		if (nWordsToken == token_word) // ����
			vectFields.push_back(m_Scanner.GetWord()); // ����
		else if (nWordsToken == token_index)
		{
			nWordsToken = m_Scanner.Scan();
			if (nWordsToken == token_word)
				vectIndex.push_back(m_Scanner.GetWord()); // ������
			else
			{
				m_strError = "invalid sysmbol, need normal fields";
				return false;
			}
		}
		nWordsToken = m_Scanner.Scan();
	}

	// ��������Ƿ����ֶ������
	bool bFind = true;
	for (auto& iterIndex : vectIndex)
	{
		auto iter = std::find_if(vectIndex.begin(), vectIndex.end(), [iterIndex](std::string fieldName) { return iterIndex == fieldName; });
		if (iter == vectIndex.end())
		{
			bFind = false;
			m_strError = "not all index filed in fields";
			return false;
		}
	}

	// ������
	bool bRet = false;
	int n;
	bRet = ExecuteRedisCommand(RedisCommand::SET_COMMAND, nullptr, n, "set %s_table %s", tableName, tableName); // ���ñ���
	if (!bRet)
	{
		m_strError = "set table error";
		return false;
	}
	// ������ļ�¼����ֵ
	bRet = ExecuteRedisCommand(RedisCommand::SET_COMMAND, nullptr, n, "set %s_table_id %d", tableName, 0); // ��0��ʼ
	if (!bRet)
	{
		m_strError = "set table record id error";
		return false;
	}
	// ��������
	for (auto& field : vectFields)
	{
		bRet = ExecuteRedisCommand(RedisCommand::SET_SADD, nullptr, n, "sadd %s_fields %s", tableName, field.c_str()); // ������ �������Ӧ���ع� ��ʱδ����
		if (!bRet)
		{
			m_strError = "set table field error";
			return false;
		}
	}
	// ��������
	for (auto& filedIndex : vectIndex)
	{
		bRet = ExecuteRedisCommand(RedisCommand::SET_SADD, nullptr, n, "sadd %s_index %s", tableName, filedIndex.c_str()); // �������� �������Ӧ���ع� ��ʱδ����
		if (!bRet)
		{
			m_strError = "set table field error";
			return false;
		}
	}

	return true;
}

bool CParseSql::SelectFromTable() // ��ѯ �Խ�����ķ�ʽ����
{
	QueryTree* tree = nullptr; // ��ѯ��
	std::vector<std::string> vectFields;
	char tableName[128] = { 0x00 };
	int nWordToken = m_Scanner.Scan();
	if (nWordToken == token_all) // select *
	{
		nWordToken = m_Scanner.Scan();
		if (nWordToken != token_from)
		{
			m_strError = "invalid symbol, need from";
			return false;
		}
		else
		{
			nWordToken = m_Scanner.Scan();
			if (nWordToken != token_word)
			{
				m_strError = "invalid symbol, need tablename";
				return false;
			}
			else
			{
				strcpy_s(tableName, sizeof(tableName), m_Scanner.GetWord()); // ����
				nWordToken = m_Scanner.Scan();
				if (nWordToken == EOF) // ��������ѯ
				{

				}
				else // ��������ѯ
				{
					if (nWordToken == token_where)
						m_Scanner.ReadCondition(tree); // ��������
					else
					{
						m_strError = "invalid symbol, need where";
						return false;
					}

				}
			}
		}
	}
	else // ��ѯĳ�����ֶ� ����ѯ
	{
		do 
		{
			if (nWordToken == token_word)
			{
				vectFields.push_back(m_Scanner.GetWord());
			}
			else if (nWordToken == token_idot) // ����','
			{

			}
			else
			{
				m_strError = "invalid input, need fieldname";
				return false;
			}
			nWordToken = m_Scanner.Scan();
		} while (nWordToken != token_from);

		nWordToken = m_Scanner.Scan();
		if (nWordToken != token_word)
		{
			m_strError = "invalid symbol, need tablename";
			return false;
		}
		else
		{
			strcpy_s(tableName, sizeof(tableName), m_Scanner.GetWord()); // ����
			nWordToken = m_Scanner.Scan();
			if (nWordToken == EOF) // ��������ѯ
			{

			}
			else // ��������ѯ
			{
				if (nWordToken == token_where)
					m_Scanner.ReadCondition(tree); // ��������
				else
				{
					m_strError = "invalid symbol, need where";
					return false;
				}

			}
		}

	}

	DataRecord** pRecord = nullptr;
	int nRecords = 0;
	bool bRet = DoSelect(vectFields, tree, tableName, pRecord, nRecords);
	if (!bRet)
	{
		m_strError = "no such records"; // ��ѯʧ��
		return false;
	}

	if (pRecord)
	{
		CCursor cursor(pRecord, nRecords);
		char strValue[128] = { 0x00 };
		while (!cursor.EndOfRecord())
		{
			memset(strValue, 0, sizeof(strValue));
			for (auto& field : vectFields)
			{
				std::cout << field.c_str() << ":";
				cursor.GetValue(field, strValue, 128);
				std::cout << strValue;
				std::cout << "\t";
			}
			std::cout << std::endl;
			cursor.MoveNext();
		}
	}

	// �ͷ��ڴ�
	if (pRecord)
	{
		for (int i = 0; i < nRecords; ++i)
		{
			if (pRecord[i])
			{
				delete[](pRecord[i])->pData;
				delete (pRecord[i]);
			}
		}
		delete[] pRecord;
	}

	std::queue<QueryTree*> queueTree;
	if (tree)
	{
		queueTree.push(tree);
		while (!queueTree.empty())
		{
			QueryTree* pTree = queueTree.front();
			queueTree.pop();
			if (pTree->lTree)
				queueTree.push(pTree->lTree);
			if (pTree->rTree)
				queueTree.push(pTree->rTree);
			// delete
			delete pTree->pData->condition;
			delete pTree->pData;
			delete pTree;
		}
	}

	return true;
}

bool CParseSql::InsertIntoTable()
{
	char tableName[128] = { 0x00 };
	int nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_into)
	{
		m_strError = "invalid sysbmol, need into";
		return false;
	}

	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_word)
	{
		m_strError = "invalid input";
		return false;
	}

	strcpy_s(tableName, sizeof(tableName), m_Scanner.GetWord()); // ��ȡ����
	bool bExists = false;
	bool bRet = CheckKeyExists(tableName, RedisExistKey::IS_TABLE, bExists);
	if (!bExists)
	{
		m_strError = "no such table"; // û�иñ�
		return false;
	}

	std::vector<std::string> vectFields; // �ֶ���
	std::vector<std::string> vectValues; // ֵ
	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_lpair)
	{
		m_strError = "invalid symbol need (";
		return false;
	}
	nWordsToken = m_Scanner.Scan();
	while (nWordsToken != token_rpair)
	{
		if (nWordsToken == token_word) // ����
			vectFields.push_back(m_Scanner.GetWord()); // ����
		nWordsToken = m_Scanner.Scan();
	}

	// �����ֶ��Ƿ񶼴���
	for (auto& field : vectFields)
	{
		bool bRet = CheckKeyExists(field.c_str(), RedisExistKey::IS_FIELD, bExists, tableName);
		if (!bExists)
		{
			m_strError = "no such field";
			return false;
		}
	}

	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_values)
	{
		m_strError = "invalid symbol, nedd values";
		return false;
	}

	nWordsToken = m_Scanner.Scan();
	if (nWordsToken != token_lpair)
	{
		m_strError = "invalid symbol, nedd (";
		return false;
	}

	nWordsToken = m_Scanner.Scan();
	while (nWordsToken != token_rpair)
	{
		if (nWordsToken == token_word) // ����
			vectValues.push_back(m_Scanner.GetWord()); // ����
		nWordsToken = m_Scanner.Scan();
	}

	//�ж��Ƿ�ƥ��
	if (vectFields.size() != vectValues.size())
	{
		m_strError = "invalid input,fields and values not match";
		return false;
	}

	// �����json����
	Json::Value root;
	for (int i = 0; i < vectFields.size(); ++i)
		root[vectFields[i].c_str()] = vectValues[i];
	//std::string jsonStr = root.toStyledString();
	Json::FastWriter writer;
	std::string jsonStr = writer.write(root);

	// ��ü�¼id
	long long* pID = new long long;
	int n;
	bRet = ExecuteRedisCommand(RedisCommand::INCR_COMMAND, pID, n,"incr %s_table_id", tableName);
	if (!bRet)
	{
		m_strError = "insert error can't increase record";
		return false;
	}
	// ��ü�¼id�� ���ȸ���������¼ // Ψһ���� �����������Ψһ����
	for (int i = 0; i < vectFields.size(); ++i)
	{
		bRet = CheckKeyExists(vectFields[i].c_str(), RedisExistKey::IS_INDEX, bExists, tableName);
		if (bExists) // ������ ������������¼
		{
			bRet = ExecuteRedisCommand(RedisCommand::HSET_COMMAND, nullptr, n, "hset %s_%s_index %s %I64d", tableName, vectFields[i].c_str(), vectValues[i].c_str(), *pID);
			if (!bRet)
			{
				m_strError = "set index record error";
				return false;
			}
		}
	}

	char szRecord[20] = { 0x00 };
	sprintf_s(szRecord, 20, "%I64d", *pID);

	char szBuff[2048] = { 0x00 };
	sprintf_s(szBuff, 2048, "hset %s_record %s %s", tableName, szRecord, jsonStr.c_str());

	// �����¼
	bRet = ExecuteRedisCommand(RedisCommand::HSET_COMMAND, nullptr, n, "%s", szBuff);
	if (!bRet)
	{
		m_strError = "insert record error";
		return false;
	}
	delete pID;
	return true;
}

bool CParseSql::TruncateTable() // truncate table
{
	int nWordToken = 0;
	char tableName[128] = { 0x00 };
	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_table)
	{
		m_strError = "invalid input, need table";
		return false;
	}
	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_word)
	{
		m_strError = "invalid input, need tablename";
		return false;
	}
	strcpy_s(tableName, 128, m_Scanner.GetWord());
	bool bExists = false;
	bool bRet = CheckKeyExists(tableName, RedisExistKey::IS_TABLE, bExists);
	if (!bExists)
	{
		m_strError = "no such table"; // û�иñ�
		return false;
	}

	// ��ɾ��������¼ ɾ��ʧ����ع� �ݲ�����
	
	//�Ȼ�ȡ�����ֶ�
	char* pIndexFields = new char[1024 * 1024];
	int nLen = 0;
	std::vector<std::string> vectIndexField;
	memset(pIndexFields, 0, 1024 * 1024);
	bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pIndexFields, nLen, "smembers %s_index", tableName);
	char* sBegin = (char*)pIndexFields;
	while (*pIndexFields)
	{
		if (*pIndexFields == ',')
		{
			char Field[128] = { 0x00 };
			memcpy(Field, sBegin, pIndexFields - sBegin);
			vectIndexField.push_back(Field);
			sBegin = pIndexFields + 1;
		}
		pIndexFields = pIndexFields + 1;
	}
	if (*sBegin)
		vectIndexField.push_back(sBegin);
	
	char* szBuff = new char[1024 * 1024]; // �����ռ� ��ֹ����
	for (auto& field : vectIndexField) // ��ȡ������Ӧ�ļ�¼
	{
		memset(pIndexFields, 0, 1024 * 1024);
		bRet = ExecuteRedisCommand(RedisCommand::HKEYS_COMMAND, pIndexFields, nLen, "hkeys %s_%s_index", tableName, field.c_str());
		if (bRet) // ɾ����Ӧ��������¼
		{
			std::vector<std::string> vectIndexValue;
			char* sBegin = (char*)pIndexFields;
			while (*pIndexFields)
			{
				if (*pIndexFields == ',')
				{
					char Field[128] = { 0x00 };
					memcpy(Field, sBegin, pIndexFields - sBegin);
					vectIndexValue.push_back(Field);
					sBegin = pIndexFields + 1;
				}
				pIndexFields = pIndexFields + 1;
			}
			if (*sBegin)
				vectIndexValue.push_back(sBegin);

			if (!vectIndexValue.empty())
			{
				memset(szBuff, 0, 1024 * 1024);
				int nBuffLen = 0;
				nBuffLen = sprintf_s(szBuff, 1024 * 1024, "hdel %s_%s_index ", tableName, field.c_str());
				for (auto& indexValue : vectIndexValue)
					nBuffLen += sprintf_s(szBuff + nBuffLen, 1024 * 1024 - nBuffLen,"%s ", indexValue.c_str());
				//szBuff[nBuffLen - 1] = 0;
				bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, szBuff);
				if (!bRet)
				{
					m_strError = "delete index record error";
					delete szBuff;
					szBuff = nullptr;
					return false;
				}
				delete szBuff;
				szBuff = nullptr;
			}
		}
		
	}

	// ɾ����¼
	memset(pIndexFields, 0, 1024 * 1024);
	szBuff = new char[1024 * 1024];
	memset(szBuff, 0, 1024 * 1024);
	int nBuffLen = 0;
	nBuffLen = sprintf_s(szBuff, 1024 * 1024, "hdel %s_record ", tableName);
	bRet = ExecuteRedisCommand(RedisCommand::HKEYS_COMMAND, pIndexFields, nLen, "hkeys %s_record", tableName);
	if (!bRet)
	{
		m_strError = "delete records error";
		return false;
	}
	else
	{
		char* sBegin = (char*)pIndexFields;
		while (*pIndexFields)
		{
			if (*pIndexFields == ',')
			{
				char Field[128] = { 0x00 };
				memcpy(Field, sBegin, pIndexFields - sBegin);
				nBuffLen += sprintf_s(szBuff + nBuffLen, 1024 * 1024 - nBuffLen, "%s ", Field);
				sBegin = pIndexFields + 1;
			}
			pIndexFields = pIndexFields + 1;
		}
		if (*sBegin)
		{
			nBuffLen += sprintf_s(szBuff + nBuffLen, 1024 * 1024 - nBuffLen, "%s", sBegin);
			//szBuff[nBuffLen - 1] = 0;
			bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, szBuff);
			if (!bRet)
			{
				m_strError = "delete index record error";
				delete szBuff;
				szBuff = nullptr;
				return false;
			}
		}
	}
	delete szBuff;
	szBuff = nullptr;

	// ���Ѽ�¼ֵ���˵�0
	bRet = ExecuteRedisCommand(RedisCommand::SET_COMMAND, nullptr, nLen, "set %s_table_id %d", tableName, 0); // ��0��ʼ
	if (!bRet)
	{
		m_strError = "set table record id error";
		return false;
	}
	return true;
}

bool CParseSql::CheckKeyExists(const char* pKey, RedisExistKey keyType, bool& bExist,const char* pTableName)
{
	int n;
	if (keyType == RedisExistKey::IS_TABLE) // �����Ƿ����
		bExist = ExecuteRedisCommand(RedisCommand::GET_COMMAND, nullptr, n, "get %s_table", pKey);
	if (keyType == RedisExistKey::IS_FIELD) // ����ֶ��Ƿ����
		bExist = ExecuteRedisCommand(RedisCommand::SIS_MEMBER, nullptr, n, "sismember %s_fields %s", pTableName, pKey);
	if (keyType == RedisExistKey::IS_INDEX) // ����Ƿ�Ϊ�����ֶ�
		bExist = ExecuteRedisCommand(RedisCommand::SIS_MEMBER, nullptr, n, "sismember %s_index %s", pTableName, pKey);
	return true;
}

bool CParseSql::ExecuteRedisCommand(RedisCommand RCommand, void* pGetValue, int& nLen, char* command, ...)
{
	char* buff = new char[2048];
	memset(buff, 0, 2048);
	va_list arg;
	va_start(arg, command);
	vsprintf_s(buff, 2048, command, arg);
	bool bRet = false;
	if (SetCommand(RCommand))
	{
		CRedisSetCommand reSet;
		bRet = reSet.Execute(m_hDBLogin, RCommand, buff);
		if (SET_STATUS::STATUS_OK != reSet.GetStatus())
			bRet = false;
	}
	else
	{
		CRedisGetCommand reGet;
		bRet = reGet.Execute(m_hDBLogin, RCommand,buff);
		RedisRes* res = reGet.GetRedisRes();
		if (res)
		{
			bRet = (res->status == GET_STATUS::STATUS_OK); // ������״̬Ϊ����
			if (pGetValue)
			{
				memcpy(pGetValue, res->pData, res->nLen);
				nLen = res->nLen;
			}
		}
	}
	delete[] buff;
	return bRet;
}

bool CParseSql::SetCommand(RedisCommand RCommand)
{
	return RCommand == RedisCommand::SET_COMMAND || RCommand == RedisCommand::SET_SADD || RCommand == RedisCommand::HSET_COMMAND || RCommand == RedisCommand::HDEL_COMMAND;
}

bool CParseSql::DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName , DataRecord**& pRecords, int& nReords)
{
	// �ȼ����Ƿ����
	bool bExists = false;
	CheckKeyExists(tableName, RedisExistKey::IS_TABLE, bExists);
	if (!bExists)
	{
		m_strError = "no such table";
		return false;
	}
	if (!vectFields.empty()) // ��������ֶ��Ƿ���ȷ
	{
		for (auto& filed : vectFields)
		{
			CheckKeyExists(filed.c_str(), RedisExistKey::IS_FIELD, bExists, tableName);
			if (!bExists)
			{
				m_strError = "no such field";
				return false;
			}
		}
	}
	else // ��select * ��ȡȫ������ֶ�
	{
		void* pFiled = new char[1024];
		memset(pFiled, 0, 1024);
		int nLen = 0;
		bool bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pFiled, nLen, "smembers %s_fields", tableName);
		if (!bRet)
		{
			m_strError = "get field error";
			return false;
		}
		else
		{
			char* sBegin = (char*)pFiled;
			while (*((char*)pFiled))
			{
				if (*(char*)pFiled == ',')
				{
					char Field[128] = { 0x00 };
					memcpy(Field, sBegin, (char*)pFiled - sBegin);
					vectFields.push_back(Field);
					sBegin = (char*)pFiled + 1;
				}
				pFiled = (char*)pFiled + 1;
			}
			vectFields.push_back(sBegin);
		}
	}
	std::vector<std::string> condiField;
	std::vector<std::string> indexField;
	std::vector<IndexValue> indexValue; // �����ֶζ�Ӧ��ֵ
	std::queue<QueryTree*> queueTree;
	// �ȼ���������Ƿ��������ֶ�
	if (pTree)
	{
		queueTree.push(pTree);
		while (!queueTree.empty())
		{
			QueryTree* pFind = queueTree.front();
			queueTree.pop();
			if (pFind->pData)
			{
				std::string str = pFind->pData->condition->fieldName;
				condiField.push_back(str);
				CheckKeyExists(str.c_str(), RedisExistKey::IS_INDEX, bExists, tableName);
				if (bExists)
				{
					IndexValue value;
					value.nToken = pFind->pData->condition->nToken;
					strcpy_s(value.fieldValue, sizeof(pFind->pData->condition->fieldValue), pFind->pData->condition->fieldValue);
					indexField.push_back(str);
					indexValue.push_back(value);
				}
			}
			if (pFind->lTree)
				queueTree.push(pFind->lTree);
			if (pFind->rTree)
				queueTree.push(pFind->rTree);
		}
	}

	// ��������ֶ��Ƿ񶼴���
	for (auto& filed : condiField)
	{
		CheckKeyExists(filed.c_str(), RedisExistKey::IS_FIELD, bExists, tableName);
		if (!bExists)
		{
			m_strError = "no such field";
			return false;
		}
	}

	bool bDoIndex = false;
	void* pValue = new char[1024];
	memset(pValue, 0, 1024);
	int nLen = 0;
	std::vector<std::string> getFields;
	std::vector<std::string> getValues;
	if (!indexField.empty()) // �������ֶ�
	{
		// ���ȴ���"="�����
		for (int i = 0; i < indexValue.size(); ++i)
		{
			if (indexValue[i].nToken == token_eq) // ���
			{
				bDoIndex = true;
				bool bRet = ExecuteRedisCommand(RedisCommand::HGET_COMMAND, pValue, nLen, "hget %s_%s_index %s", tableName, indexField[i].c_str(), indexValue[i].fieldValue);
				if (!bRet)
					break;
				else
				{
					bRet = ExecuteRedisCommand(RedisCommand::HGET_COMMAND, pValue, nLen, "hget %s_record %s", tableName, (char*)pValue); // ��ȡ��¼��Ϣ
					if (!bRet)
						break;
					else // �����¼
					{
						Json::Reader jsonReader;
						Json::Value  jsonValue;
						jsonReader.parse((const char*)pValue , (const char*)pValue + nLen, jsonValue);
						Json::Value::Members jsonMembers = jsonValue.getMemberNames();
						for (auto iter = jsonMembers.begin(); iter != jsonMembers.end(); iter++)
						{
							getValues.emplace_back(std::move((jsonValue[*iter]).asString()));
							getFields.emplace_back(std::move(*iter));
						}
						// ���������������Ƿ����Ϊ������¼
						bRet = IsSatisfyRecord(getFields, getValues, pTree);
						if (bRet) // ������¼���ϲ�������
						{
							pRecords = new DataRecord*[1];
							pRecords[0] = new DataRecord;
							pRecords[0]->pData = new char[nLen];
							pRecords[0]->nLen = nLen;
							memcpy(pRecords[0]->pData, pValue, nLen);
							nReords = 1;
						}
					}
				}
				break;
			}
		}
	}
	if (!bDoIndex) // û�д������� ��������ʽ����
	{
		bool bRet = ExecuteRedisCommand(RedisCommand::GET_COMMAND, pValue, nLen, "get %s_table_id", tableName); // ���Ȼ�ȡ�ܼ�¼�ж�����
		if (bRet)
		{
			long long records;
			std::stringstream ss;
			ss << (char*)pValue;
			ss >> records;
			pRecords = new DataRecord*[records];
			for (int i = 0; i < records; i++)
				pRecords[i] = nullptr;
			int nSatisfy = 0;
			for (long long i = 1; i <= records; ++i) // ��¼��1��ʼ
			{
				memset(pValue, 0, 1024);
				bRet = ExecuteRedisCommand(RedisCommand::HGET_COMMAND, pValue, nLen, "hget %s_record %I64d", tableName, i); // ��ȡ��¼��Ϣ
				if (!bRet)
					continue;
				else // �����¼
				{
					Json::Reader jsonReader;
					Json::Value  jsonValue;
					jsonReader.parse((const char*)pValue, (const char*)pValue + nLen, jsonValue);
					Json::Value::Members jsonMembers = jsonValue.getMemberNames();
					for (auto iter = jsonMembers.begin(); iter != jsonMembers.end(); iter++)
					{
						getValues.emplace_back(std::move((jsonValue[*iter]).asString()));
						getFields.emplace_back(std::move(*iter));
					}
					// ���������������Ƿ����Ϊ������¼
					if (pTree)
						bRet = IsSatisfyRecord(getFields, getValues, pTree);
					else // �޲������� ȫ��������
						bRet = true;
					if (bRet) // ������¼���ϲ�������
					{
						pRecords[nSatisfy] = new DataRecord;
						pRecords[nSatisfy]->pData = new char[nLen];
						pRecords[nSatisfy]->nLen = nLen;
						memcpy(pRecords[nSatisfy]->pData, pValue, nLen);
						nSatisfy++;
						getFields.clear();
						getValues.clear();
					}
				}
			}
			if (nSatisfy == 0)
			{
				nReords = 0;
				delete[] pRecords;
				pRecords = nullptr;
			}
			else
			{
				nReords = nSatisfy;
			}
		}
	}
	return true;
}

bool CParseSql::IsSatisfyRecord(std::vector<std::string>& vectFields, std::vector<std::string>& vectValues, QueryTree* pTree) // float�����Ȳ�����
{
	bool bSatisfy = false;
	bool bSatisfyLeft = false;
	bool bSatisfyRight = false;
	if (pTree)
	{
		bool bFind = false;
		int nIndex = 0;
		if (pTree->pData)
		{
			//auto iter = std::find_if(vectFields.begin(), vectFields.end(), [pTree](std::string& field) { return field == std::string(pTree->pData->condition->fieldName); });
			for (int i = 0; i < vectFields.size(); i++)
			{
				if (strcmp(vectFields[i].c_str() , pTree->pData->condition->fieldName) == 0)
				{
					bFind = true;
					nIndex = i;
					break;
				}
			}
			//if (iter == vectFields.end())
			if (!bFind)
				bSatisfy = false;
			else
			{
				//int nIndex = iter - vectFields.begin();
				if (pTree->pData->condition->nToken == token_eq) // ���
				{
					bSatisfy = (strcmp(vectValues[nIndex].c_str(), pTree->pData->condition->fieldValue) == 0 ? true : false);
				}
			}
		}
	}
	if (pTree->lTree)
		bSatisfyLeft = IsSatisfyRecord(vectFields, vectValues, pTree->lTree);
	if (pTree->rTree)
		bSatisfyRight = IsSatisfyRecord(vectFields, vectValues, pTree->rTree);
	if (pTree->pData->nToken == token_normalend) // �жϽڵ���and or ���� ��������
		return bSatisfy;
	if (pTree->pData->nToken == token_and)
		return bSatisfyLeft && bSatisfyRight;
	if (pTree->pData->nToken == token_or)
		return bSatisfyLeft || bSatisfyRight;
}