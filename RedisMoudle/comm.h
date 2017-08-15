#ifndef __COMM__H
#define __COMM__H
#include <iostream>
#include <vector>

// ���ݽṹ�����ļ�

enum class RedisCommand
{
	SET_COMMAND,
	GET_COMMAND,
	SET_SADD,    // �Լ��ϵ�add����
	SIS_MEMBER,  // �Ƿ��ǳ�Ա
	INCR_COMMAND, // �����ֶ�
	HSET_COMMAND, // hash�ֶ�����
	HGET_COMMAND, // hget
	SMEMBERS_COMMAND, // smembers
	HKEYS_COMMAND, //hkeys //��ȡ����key
	HDEL_COMMAND, //hdel ɾ��
};

enum class RedisExistKey
{
	IS_TABLE, // ��
	IS_FIELD, // �ֶ�
	IS_INDEX  // ����
};

enum expression_token // �ؼ��ֶ���
{
	token_create = 1,
	token_table,
	token_where,
	token_eq, // ����
	token_ls, // С��
	token_le, // С�ڵ���
	token_gt, // ����
	token_ge, // ���ڵ���
	token_and,
	token_or,
	token_in,
	token_commit,
	token_delete,
	token_drop,
	token_alter,
	token_update,
	token_values,
	token_select,
	token_from,
	token_set,
	token_all,
	token_lpair, // ������
	token_rpair,  // ������
	token_idot, // ����
	token_word,  // ��ͨ����
	token_normalend, // �������� ����������ѯ����
	token_index, // ������� ���ڿ��ٲ���
	token_insert, // ��������
	token_into, // ��������
	token_truncate, // truncate table
};

struct keyword {
	int eToken;
	char             express[20];
};

static struct keyword KeyWords[] = {
	{
		token_create, "create"
	},
	{
		token_table, "table",
	},
	{
		token_where, "where"
	},
	{
		token_eq, "="
	},
	{
		token_ls, "<"
	},
	{
		token_le, "<="
	},
	{
		token_gt, ">"
	},
	{
		token_ge, ">="
	},
	{
		token_and, "and"
	},
	{
		token_or, "or"
    },
	{
		token_in, "in"
	},
	{
		token_commit, "commit"
	},
	{
		token_delete, "delete"
	},
	{
		token_drop, "drop"
	},
	{
		token_alter, "alter"
	},
	{
		token_update, "update"
	},
	{
		token_values, "values"
	},
	{
		token_select, "select"
	},
	{
		token_from, "from"
	},
	{
		token_set, "set"
	},
	{
		token_index, "index"
	},
	{
		token_insert, "insert"
	},
	{
		token_into, "into"
	},
	{
		token_truncate, "truncate"
	}
};

struct QueryCondition // ��ѯ����
{
	int nToken; // ���� С�ڻ����
	char fieldName[128]; // ����
	char fieldValue[128]; // ֵ
	std::vector<std::string> vectValueSet;
	QueryCondition()
	{
		nToken = 0;
		memset(fieldName, 0, sizeof(fieldName));
		memset(fieldValue, 0, sizeof(fieldValue));
		vectValueSet.clear();
	}
};

struct QueryNode // ��ѯ�ڵ�
{
	int nToken; // and or in��
	struct QueryCondition* condition;
	QueryNode()
	{
		nToken = 0;
		condition = nullptr;
	}
};

struct QueryTree // ��ѯ��
{
	QueryNode* pData;
	QueryTree* lTree;
	QueryTree* rTree;
	QueryTree()
	{
		this->pData = nullptr;
		this->lTree = nullptr;
		this->rTree = nullptr;
	}
};

struct QueryTreeAssist // ����
{
	QueryTree* pData;
	int        nLeft; //1left 2right
};

struct IndexValue // ������ֵ
{
	int nToken; // ���� С�ڻ����
	char fieldValue[128]; // ֵ
	IndexValue()
	{
		nToken = 0;
		memset(this->fieldValue, 0, sizeof(this->fieldValue));
	}
};

struct DataRecord // ע��õ��ļ�¼��Ҫ�Լ��ͷ��ڴ�
{
	void* pData; // json��ʽ�ļ�¼
	int   nLen;
	DataRecord()
	{
		pData = nullptr;
		nLen = 0;
	}
};

#endif
