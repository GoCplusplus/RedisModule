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
	case token_truncate: // truncate table //δ����ع�
	{
		return TruncateTable();
	}
	case token_drop: // drop table ɾ���� //δ����ع�
	{
		return DropTable();
	}
	case token_delete: // delete table // δ����ع�
	{
		return DeleteTable();
	}
	case token_update: // update table // δ����ع�
	{
		return UpdateTable();
	}
	default:
		break;
	}
}

bool CParseSql::CreateTable()
{
	char tableName[128] = { 0x00 };
	std::vector<std::string> vectFields, vectIndex;
	std::vector<struct RollBack> vectRollBack; // ����ʧ�ܵĻع�
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
	struct RollBack roll;
	char szRollBack[128] = { 0x00 };
	sprintf_s(szRollBack, sizeof(szRollBack), "del %s_table", tableName);
	roll.cmd = RedisCommand::DEL_COMMAND;
	roll.strDes = szRollBack;
	vectRollBack.emplace_back(std::move(roll));
	if (!bRet)
	{
		m_strError = "set table error";
		BackUp(vectRollBack);
		return false;
	}
	// ������ļ�¼����ֵ
	bRet = ExecuteRedisCommand(RedisCommand::SET_COMMAND, nullptr, n, "set %s_table_id %d", tableName, 0); // ��0��ʼ
	memset(szRollBack, 0, sizeof(szRollBack));
	sprintf_s(szRollBack, sizeof(szRollBack), "del %s_table_id", tableName);
	roll.cmd = RedisCommand::DEL_COMMAND;
	roll.strDes = szRollBack;
	vectRollBack.emplace_back(std::move(roll));
	if (!bRet)
	{
		m_strError = "set table record id error";
		BackUp(vectRollBack);
		return false;
	}
	// ��������
	for (auto& field : vectFields)
	{
		bRet = ExecuteRedisCommand(RedisCommand::SET_SADD, nullptr, n, "sadd %s_fields %s", tableName, field.c_str()); // ������
		memset(szRollBack, 0, sizeof(szRollBack));
		sprintf_s(szRollBack, sizeof(szRollBack), "srem %s_fields %s", tableName, field.c_str());
		roll.cmd = RedisCommand::SREM_COMMAND;
		roll.strDes = szRollBack;
		vectRollBack.emplace_back(std::move(roll));
		if (!bRet)
		{
			m_strError = "set table field error";
			BackUp(vectRollBack);
			return false;
		}
	}
	// ��������
	for (auto& filedIndex : vectIndex)
	{
		bRet = ExecuteRedisCommand(RedisCommand::SET_SADD, nullptr, n, "sadd %s_index %s", tableName, filedIndex.c_str()); // ��������
		memset(szRollBack, 0, sizeof(szRollBack));
		sprintf_s(szRollBack, sizeof(szRollBack), "srem %s_index %s", tableName, filedIndex.c_str());
		roll.cmd = RedisCommand::SREM_COMMAND;
		roll.strDes = szRollBack;
		vectRollBack.emplace_back(std::move(roll));
		if (!bRet)
		{
			m_strError = "set table field error";
			BackUp(vectRollBack);
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
	std::vector<struct RollBack> vectRollBack;
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
	
	struct RollBack roll;
	char szRollBack[128] = { 0x00 };
	sprintf_s(szRollBack, sizeof(szRollBack), "decr %s_table_id", tableName);
	roll.cmd = RedisCommand::DECR_COMMAND;
	roll.strDes = szRollBack;
	vectRollBack.emplace_back(std::move(roll));

	if (!bRet)
	{
		m_strError = "insert error can't increase record";
		BackUp(vectRollBack);
		return false;
	}
	// ��ü�¼id�� ���ȸ���������¼ // Ψһ���� �����������Ψһ����
	for (int i = 0; i < vectFields.size(); ++i)
	{
		bRet = CheckKeyExists(vectFields[i].c_str(), RedisExistKey::IS_INDEX, bExists, tableName);
		if (bExists) // ������ ������������¼
		{
			bRet = ExecuteRedisCommand(RedisCommand::HSET_COMMAND, nullptr, n, "hset %s_%s_index %s %I64d", tableName, vectFields[i].c_str(), vectValues[i].c_str(), *pID);
			//ss << "hdel " << tableName << "_" << vectFields[i].c_str() << "_index " << vectValues[i].c_str();
			memset(szRollBack, 0, sizeof(szRollBack));
			sprintf_s(szRollBack, sizeof(szRollBack), "hdel %s_%s_index %s", tableName, vectFields[i].c_str(), vectValues[i].c_str());
			roll.cmd = RedisCommand::HDEL_COMMAND;
			roll.strDes = szRollBack;
			vectRollBack.emplace_back(std::move(roll));
			if (!bRet)
			{
				m_strError = "set index record error";
				BackUp(vectRollBack);
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
	//ss << "hdel " << tableName << "_record " << szRecord;
	memset(szRollBack, 0, sizeof(szRollBack));
	roll.cmd = RedisCommand::HDEL_COMMAND;
	sprintf_s(szRollBack, "hdel %s_record %s", tableName, szRecord);
	roll.strDes = szRollBack;
	vectRollBack.emplace_back(std::move(roll));
	if (!bRet)
	{
		m_strError = "insert record error";
		BackUp(vectRollBack);
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
	return TruncateTable(tableName);
}

bool CParseSql::TruncateTable(const char* tableName)
{
	// ��ɾ��������¼ ɾ��ʧ����ع� �ݲ�����

	//�Ȼ�ȡ�����ֶ�
	char* pIndexFields = new char[1024 * 1024];
	int nLen = 0;
	std::vector<std::string> vectIndexField;
	memset(pIndexFields, 0, 1024 * 1024);
	bool bRet;
	bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pIndexFields, nLen, "smembers %s_index", tableName);
	char* sBegin = (char*)pIndexFields;
	char* pBegin = (char*)pIndexFields;
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

	pIndexFields = pBegin;
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
					nBuffLen += sprintf_s(szBuff + nBuffLen, 1024 * 1024 - nBuffLen, "%s ", indexValue.c_str());
				//szBuff[nBuffLen - 1] = 0;
				bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, szBuff);
				if (!bRet)
				{
					m_strError = "delete index record error";
					delete[] szBuff;
					szBuff = nullptr;
					return false;
				}
				delete[] szBuff;
				szBuff = nullptr;
			}
		}

	}
	pIndexFields = pBegin;
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
	delete[] szBuff;
	szBuff = nullptr;

	delete[] pBegin;

	// ���Ѽ�¼ֵ���˵�0
	bRet = ExecuteRedisCommand(RedisCommand::SET_COMMAND, nullptr, nLen, "set %s_table_id %d", tableName, 0); // ��0��ʼ
	if (!bRet)
	{
		m_strError = "set table record id error";
		return false;
	}
	return true;
}

bool CParseSql::DropTable()
{
	char tableName[128] = { 0x00 };
	int nWordToken = 0;
	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_table)
	{
		m_strError = "invalid symbol, need table";
		return false;
	}
	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_word)
	{
		m_strError = "invalid symbol, need tablename";
		return false;
	}
	// �����Ƿ����
	strcpy_s(tableName, 128, m_Scanner.GetWord());
	bool bExists = false;
	bool bRet = CheckKeyExists(tableName, RedisExistKey::IS_TABLE, bExists);
	if (!bExists)
	{
		m_strError = "no such table"; // û�иñ�
		return false;
	}
	bRet = TruncateTable(tableName);
	if (!bRet)
	{
		m_strError = "truncate table error";
		return false;
	}

	// ɾ�������ֶ�
	char* pIndexFields = new char[1024 * 1024];
	int nLen = 0;
	std::vector<std::string> vectIndexField;
	memset(pIndexFields, 0, 1024 * 1024);
	bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pIndexFields, nLen, "smembers %s_index", tableName);
	char* sBegin = (char*)pIndexFields;
	char* pBegin = pIndexFields;
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

	pIndexFields = pBegin;
	for (auto& indexField : vectIndexField)
	{
		bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_%s_index", tableName, indexField.c_str());
		if (!bRet)
		{
			m_strError = "delete index field error";
			return false;
		}
	}
	bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_index", tableName);
	if (!bRet)
	{
		m_strError = "delete index error";
		return false;
	}

	// ɾ���ֶ�
	std::vector<std::string> vectFields;
	memset(pIndexFields, 0, 1024 * 1024);
	bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pIndexFields, nLen, "smembers %s_fields", tableName);
	sBegin = (char*)pIndexFields;
	while (*pIndexFields)
	{
		if (*pIndexFields == ',')
		{
			char Field[128] = { 0x00 };
			memcpy(Field, sBegin, pIndexFields - sBegin);
			vectFields.push_back(Field);
			sBegin = pIndexFields + 1;
		}
		pIndexFields = pIndexFields + 1;
	}
	if (*sBegin)
		vectFields.push_back(sBegin);

	pIndexFields = pBegin;
	for (auto& Field : vectFields)
	{
		bRet = ExecuteRedisCommand(RedisCommand::SREM_COMMAND, nullptr, nLen, "srem %s_fields %s", tableName, Field.c_str());
		if (!bRet)
		{
			m_strError = "delete field error";
			return false;
		}
	}
	bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_fields", tableName);
	if (!bRet)
	{
		m_strError = "delete index error";
		return false;
	}

	// ɾ����¼id�ֶ�
	bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_table_id", tableName);
	if (!bRet)
	{
		m_strError = "delete table record id error";
		return false;
	}

	// ɾ����¼�ֶ�
	bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_record", tableName);
	if (!bRet)
	{
		m_strError = "delete table record field error";
		return false;
	}

	// ���ɾ����
	bRet = ExecuteRedisCommand(RedisCommand::DEL_COMMAND, nullptr, nLen, "del %s_table", tableName);
	if (!bRet)
	{
		m_strError = "delete table error";
		return false;
	}
	return true;
}

bool CParseSql::DeleteTable() // delete
{
	QueryTree* tree = nullptr; // ��ѯ��
	std::vector<std::string> vectFields;
	bool bRet = false;
	DataRecord** pRecord = nullptr;
	int nRecords = 0;
	std::vector<std::string> getFields;
	std::vector<std::string> getValues;
	int nWordToken = m_Scanner.Scan();
	if (nWordToken != token_from)
	{
		m_strError = "invalid input, need from";
		return false;
	}
	char tableName[128] = { 0x00 };
	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_word)
	{
		m_strError = "invalid input, need tablename";
		return false;
	}
	strcpy_s(tableName, 128, m_Scanner.GetWord());

	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_where)
	{
		m_strError = "invalid input, need from";
		return false;
	}
	m_Scanner.ReadCondition(tree);
	if (tree)
	{
		bRet = DoSelect(vectFields, tree, tableName, pRecord, nRecords, true);
		if (!bRet)
		{
			m_strError = "select records error";
			return false;
		}
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

bool CParseSql::UpdateTable()
{
	char tableName[128] = { 0x00 };
	int nWordToken = 0;
	QueryTree* tree = nullptr;
	std::vector<std::string> vectFields;
	std::vector<std::string> vectValue;
	nWordToken = m_Scanner.Scan();
	bool bRet = false;
	if (nWordToken != token_word)
	{
		m_strError = "invalid input, need tablename";
		return false;
	}
	memcpy(tableName, m_Scanner.GetWord(), strlen(m_Scanner.GetWord()));

	nWordToken = m_Scanner.Scan();
	if (nWordToken != token_set)
	{
		m_strError = "invalid input, need set";
		return false;
	}
	
	while (1) // ��ȡ����Ҫ���õ��ֶ�
	{
		nWordToken = m_Scanner.Scan();
		if (nWordToken == token_where)
			break;
		if (nWordToken == EOF)
			break;
		if (nWordToken == token_word)
		{
			vectFields.push_back(m_Scanner.GetWord());
			continue;
		}
		if (nWordToken == token_eq)
		{
			nWordToken = m_Scanner.Scan();
			if (nWordToken == token_word)
			{
				vectValue.push_back(m_Scanner.GetWord());
				continue;
			}
		}
		if (nWordToken == token_idot)
			continue;
	}

	DataRecord** pRecord = nullptr;
	int nRecords = 0;
	std::vector<int> recordNums;
	m_Scanner.ReadCondition(tree);
	bRet = DoSelect(vectFields, tree, tableName, pRecord, nRecords, recordNums);
	if (!bRet)
	{
		m_strError = "select records error";
		return false;
	}
	// ��������
	if (pRecord)
	{
		std::vector<std::string> getValues;
		std::vector<std::string> getFields;
		for (int i = 0; i < nRecords; ++i)
		{
			Json::Reader jsonReader;
			Json::Value  jsonValue;
			jsonReader.parse((const char*)(pRecord[i]->pData), (const char*)(pRecord[i]->pData) + pRecord[i]->nLen, jsonValue);
			Json::Value::Members jsonMembers = jsonValue.getMemberNames();
			for (auto iter = jsonMembers.begin(); iter != jsonMembers.end(); iter++)
			{
				getValues.emplace_back(std::move((jsonValue[*iter]).asString()));
				getFields.emplace_back(std::move(*iter));
			}

			bool bError = false;
			// �ҵ���Ӧ���ֶ�
			for (int j = 0; j < vectFields.size(); ++j)
			{
				// ���ж�Ҫ���µ��ֶ��ǲ�������
				auto iter = std::find_if(getFields.begin(), getFields.end(), [vectFields, j](std::string& fi) { return fi == vectFields[j]; });
				if (iter == getFields.end())
				{
					m_strError = "invalid record";
					bError = true;
					break;
				}
				else
				{
					bool bExists = false;
					CheckKeyExists((vectFields[j]).c_str(), RedisExistKey::IS_INDEX, bExists, tableName); // �ж��Ƿ��������ֶ�
					if (bExists)
					{
						// ��ɾ������
						int nLen = 0;
						bool bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, "hdel %s_%s_index %s", tableName, vectFields[j].c_str(), getValues.at(iter - getFields.begin()).c_str());
						if (!bRet)
						{
							m_strError = "delete index record error";
							bError = true;
							break;
						}
					}
					getValues.at(iter - getFields.begin()) = vectValue[j];
					if (bExists)
					{
						// �����µ�����
						std::stringstream is;
						is << recordNums[i];
						char szRecordNum[20] = { 0x00 };
						is >> szRecordNum;
						int nLen = 0;
						char szBuff[2048] = { 0x00 };
						sprintf_s(szBuff, 2048, "hset %s_%s_index %s %s", tableName, vectFields[j].c_str(), getValues.at(iter - getFields.begin()).c_str(), szRecordNum);
						bool bRet = ExecuteRedisCommand(RedisCommand::HSET_COMMAND, nullptr, nLen, szBuff);
						if (!bRet)
						{
							m_strError = "delete index record error";
							bError = true;
							break;
						}
					}
				}
			}

			if (!bError)
			{
				// �������
				Json::Value root;
				for (int k = 0; k < getFields.size(); ++k)
					root[getFields[k].c_str()] = getValues[k];
				Json::FastWriter writer;
				std::string jsonStr = writer.write(root);
				int nLen = 0;
				char szRecord[20] = { 0x00 };
				std::stringstream is;
				is << recordNums[i];
				is >> szRecord;

				char szBuff[2048] = { 0x00 };
				sprintf_s(szBuff, 2048, "hset %s_record %s %s", tableName, szRecord, jsonStr.c_str());

				bRet = ExecuteRedisCommand(RedisCommand::HSET_COMMAND, nullptr, nLen, szBuff);
				if (!bRet)
				{
					m_strError = "update record error";
					break;
				}
			}
			else
				break;
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
	return RCommand == RedisCommand::SET_COMMAND || RCommand == RedisCommand::SET_SADD || RCommand == RedisCommand::HSET_COMMAND || RCommand == RedisCommand::HDEL_COMMAND
		|| RCommand == RedisCommand::SREM_COMMAND || RCommand == RedisCommand::DEL_COMMAND;
}

bool CParseSql::DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName , DataRecord**& pRecords, int& nReords, bool bDelete) // ��delete��Ҫ��¼��Ӧ��id
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
		char* pBegin = nullptr;
		bool bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pFiled, nLen, "smembers %s_fields", tableName);
		if (!bRet)
		{
			m_strError = "get field error";
			return false;
		}
		else
		{
			char* sBegin = (char*)pFiled;
			pBegin = (char*)pFiled;
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
		pFiled = pBegin;
		delete[] pFiled;
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
					char pRecordNum[128] = { 0x00 };
					memcpy(pRecordNum, pValue, 128);
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
							if (!bDelete)
							{
								pRecords = new DataRecord*[1];
								pRecords[0] = new DataRecord;
								pRecords[0]->pData = new char[nLen];
								pRecords[0]->nLen = nLen;
								memcpy(pRecords[0]->pData, pValue, nLen);
								nReords = 1;
							}
							else // ɾ���ü�¼
							{
								// ��ɾ������
								bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, "hdel %s_%s_index %s", tableName, indexField[i].c_str(), indexValue[i].fieldValue);
								if (!bRet)
									m_strError = "delete index record error";
								bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, "hdel %s_record %s", tableName, pRecordNum);
								if (!bRet)
									m_strError = "delete record error";
							}
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
						if (!bDelete)
						{
							pRecords[nSatisfy] = new DataRecord;
							pRecords[nSatisfy]->pData = new char[nLen];
							pRecords[nSatisfy]->nLen = nLen;
							memcpy(pRecords[nSatisfy]->pData, pValue, nLen);
							nSatisfy++;
							getFields.clear();
							getValues.clear();
						}
						else
						{
							// ��ɾ��������¼
							{
								//�Ȼ�ȡ�����ֶ�
								char* pIndexFields = new char[1024 * 1024];
								int nLen = 0;
								std::vector<std::string> vectIndexField;
								memset(pIndexFields, 0, 1024 * 1024);
								bool bRet;
								bRet = ExecuteRedisCommand(RedisCommand::SMEMBERS_COMMAND, pIndexFields, nLen, "smembers %s_index", tableName);
								char* sBegin = (char*)pIndexFields;
								char* pBegin = (char*)pIndexFields;
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
								pIndexFields = pBegin;

								for (auto& field : vectIndexField) // ��ȡ������Ӧ�ļ�¼ Ϊ�����ҵ��������ٽ�һ��value key�������ݲ����� ʹ��ѭ������
								{
									std::string strRecordIndex = "";
									std::stringstream is;
									is << i;
									is >> strRecordIndex;
									bRet = ExecuteRedisCommand(RedisCommand::HGETALL_COMMAND, pIndexFields, nLen, "hgetall %s_%s_index", tableName, field.c_str());
									if (!bRet)
									{
										m_strError = "select indexfield error";
									}
									else
									{
										std::vector<std::string> vectAll;
										char* sBegin = (char*)pIndexFields;
										while (*pIndexFields)
										{
											if (*pIndexFields == ',')
											{
												char Field[128] = { 0x00 };
												memcpy(Field, sBegin, pIndexFields - sBegin);
												vectAll.push_back(Field);
												sBegin = pIndexFields + 1;
											}
											pIndexFields = pIndexFields + 1;
										}
										if (*sBegin)
											vectAll.push_back(sBegin);
										auto iter = std::find_if(vectAll.begin(), vectAll.end(), [strRecordIndex](std::string& str) { return strRecordIndex == str; });
										if (iter != vectAll.end())
										{
											bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, "hdel %s_%s_index %s", tableName, field.c_str(), (*(--iter)).c_str());
											if (!bRet)
											{
												m_strError = "delete index record error";
											}
										}
										pIndexFields = pBegin;
									}
								}

								delete[] pIndexFields;

							}


							// ֱ��ɾ���ü�¼
							bRet = ExecuteRedisCommand(RedisCommand::HDEL_COMMAND, nullptr, nLen, "hdel %s_record %I64d", tableName, i);
							if (!bRet)
								m_strError = "delete record error";
						}
					}
					getValues.clear();
					getFields.clear();
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

bool CParseSql::DoSelect(std::vector<std::string>& vectFields, QueryTree* pTree, const char* tableName, DataRecord**& pRecords, int& nReords, std::vector<int>& vectNums)
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
			char* pBegin = (char*)pFiled;
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
		delete[] pFiled;
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
					char pRecordNum[128] = { 0x00 };
					memcpy(pRecordNum, pValue, 128);
					bRet = ExecuteRedisCommand(RedisCommand::HGET_COMMAND, pValue, nLen, "hget %s_record %s", tableName, (char*)pValue); // ��ȡ��¼��Ϣ
					if (!bRet)
						break;
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
						bRet = IsSatisfyRecord(getFields, getValues, pTree);
						if (bRet) // ������¼���ϲ�������
						{
							pRecords = new DataRecord*[1];
							pRecords[0] = new DataRecord;
							pRecords[0]->pData = new char[nLen];
							pRecords[0]->nLen = nLen;
							memcpy(pRecords[0]->pData, pValue, nLen);
							nReords = 1;
							std::stringstream is;
							is << pRecordNum;
							int iRecord = 0;
							is >> iRecord;
							vectNums.push_back(iRecord);
						}
						getValues.clear();
						getFields.clear();
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
						vectNums.push_back(i);
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
	{
		bool bRet = bSatisfy;
		if (pTree->lTree)
			bRet = bRet && bSatisfyLeft;
		if (pTree->rTree)
			bRet = bRet && bSatisfyRight;
		return bRet;
	}
	if (pTree->pData->nToken == token_or)
	{
		bool bRet = bSatisfy;
		if (pTree->lTree)
			bRet = bRet || bSatisfyLeft;
		if (pTree->rTree)
			bRet = bRet || bSatisfyRight;
		return bRet;
	}
}

bool CParseSql::BackUp(std::vector<struct RollBack>& vectBackUp) //����
{
	int nLen = 0;
	for (auto& roll : vectBackUp)
		ExecuteRedisCommand(roll.cmd, nullptr, nLen, (char*)roll.strDes.c_str());
	return true;
}