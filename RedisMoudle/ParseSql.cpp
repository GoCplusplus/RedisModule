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

	}
	DoSelect(vectFields, tree, tableName);
	// ��ѯ����delete true����ڴ�

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
	return RCommand == RedisCommand::SET_COMMAND || RCommand == RedisCommand::SET_SADD || RCommand == RedisCommand::HSET_COMMAND;
}

bool CParseSql::DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName)
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

				}
			}
		}
	}


}