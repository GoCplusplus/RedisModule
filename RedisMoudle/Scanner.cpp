#include "Scanner.h"
#include <cstring>
#include <stdio.h>
#include <ctype.h>
#include "comm.h"
#include "ParseSql.h"
#include <queue>

CScanner::CScanner()
{
	m_nPos = 0;
	m_pBuff = new char[nScanSize];
	m_pWord = new char[1024];
}

CScanner::~CScanner()
{
	if (m_pBuff)
		delete[] m_pBuff;
	if (m_pWord)
		delete[] m_pWord;
}

void CScanner::LoadContent(const char* strContent, int nLen)
{
	if (nLen > nScanSize)
	{
		delete[] m_pBuff;
		m_pBuff = new char[nLen];
		memcpy(m_pBuff, strContent, nLen);
	}
	else
	{
		memset(m_pBuff, 0, nScanSize);
		memcpy(m_pBuff, strContent, nLen);
	}
	m_nPos = 0;
	m_nTotal = nLen;
}

int CScanner::Scan()
{
	memset(m_pWord, 0, 1024);
	char ch;
	do {
		ch = get();
		if (ch == EOF)
			return EOF;
	} while (isspace(ch)); // �����հ׷�
	switch (ch)
	{
	case '*':
		return token_all;
	case '=':
		return token_eq;
	case '<':
	{
		ch = get();
		if (ch == '=')
			return token_le; // С�ڵ���
		else
		{
			unget();
			return token_ls;
		}
	}
	case '>':
	{
		ch = get();
		if (ch == '=')
			return token_ge; // ���ڵ���
		else
		{
			unget();
			return token_gt;
		}
	}
	case '(':
		return token_lpair;
	case ')':
		return token_rpair;
	case ',':
		return token_idot;
	default:
		if (isalnum(ch) || ch == '_') // ����ĸ�����ֻ���'_'
		{
			int i = 0;
			do 
			{
				m_pWord[i++] = ch;
				ch = get();
				if (ch == ',')
				{
					unget();
					break;
				}
				if (ch == ')')
				{
					unget();
					break;
				}
				if (ch == EOF || ch == 0)
					break;
			} while (!isspace(ch));
			int nWord = 0;
			TokensTable::FindToken(m_pWord, nWord);
			if (nWord == 0)
				return token_word;
			else
				return nWord;
		}
	}
}

char CScanner::get()
{
	if (m_nPos > m_nTotal)
		return EOF;
	return m_pBuff[m_nPos++];
}

void CScanner::unget()
{
	m_nPos--;
}

bool CScanner::ReadCondition(QueryTree*& tree) // ��������
{
	int nWordToken = 0;
	//QueryTree* root = tree; //���Һ��ʵĴ�ŵ� 
	do 
	{
		nWordToken = Scan();
		if (nWordToken == EOF)
			break;
		if (nWordToken != token_word)
			return false;
		else
		{
			QueryCondition* pNew = new QueryCondition;
			strcpy_s(pNew->fieldName, sizeof(pNew->fieldName),GetWord());
			nWordToken = Scan();
			pNew->nToken = nWordToken;
			nWordToken = Scan();
			if (nWordToken != token_word)
				return false;
			else if (nWordToken == token_lpair) // ������������� ���������1��select���������һ������ 2��һ������ ��������ģʽ
			{
				nWordToken = Scan();
				if (nWordToken == token_word) // �ǵڶ����������һ������ ���ҳ�һ������
				{
					ReadSet(pNew->vectValueSet, const_cast<char*>(GetWord())); // ��ȡ����
				}
				else if (nWordToken == token_select) // ��һ����� select ����һ���µ�parseʵ��ȥ���� �õ������Ϊ����
				{
					
				}
				else // �����symbol
					return false;
			}
			else if (nWordToken == token_word)
				strcpy_s(pNew->fieldValue, sizeof(pNew->fieldValue), GetWord());

			// ���Ҵ��condition�Ľڵ�
			QueryTree* pFind;
			QueryTreeAssist pFindParent;
			QueryTreeAssist obj;
				// ������Ȳ��ҿ��нڵ�
			std::queue<QueryTree*> queueTrees;
			std::queue<QueryTreeAssist> queueParent; // ���ڵ�
			queueTrees.push(tree);
			queueParent.push(obj); // ���ڵ�û�и��ڵ�
			while (!queueTrees.empty())
			{
				pFind = queueTrees.front();
				if (!queueParent.empty())
					pFindParent = queueParent.front();
				if (!pFind) // ���ҵ���
					break;
				else
				{
					queueTrees.push(pFind->lTree);
					QueryTreeAssist obj;
					obj.pData = pFind;
					obj.nLeft = 1; // left
					queueParent.push(obj);
					queueTrees.push(pFind->rTree);
					obj.pData = pFind;
					obj.nLeft = 2;
					queueParent.push(obj);
				}
				queueTrees.pop();
				queueParent.pop();
			}

			pFind = new QueryTree;
			pFind->pData = new QueryNode;
			pFind->pData->condition = pNew;
			nWordToken = Scan();
			if (nWordToken == EOF)
				pFind->pData->nToken = token_normalend;
			else
				pFind->pData->nToken = nWordToken;
			if (!tree)
				tree = pFind;
			else
			{
				if (pFindParent.nLeft == 1)
					pFindParent.pData->lTree = pFind;
				else
					pFindParent.pData->rTree = pFind;
			}
		}
	} while (nWordToken != EOF);

	return true;
}

bool CScanner::ReadSet(std::vector<std::string>& vectSetValue, char* pFirst)
{
	vectSetValue.push_back(pFirst);
	int nWordToken = 0;
	do 
	{
		nWordToken = Scan();
		if (nWordToken == EOF)
			break;
		else if (nWordToken == token_idot)
			continue;
		else if (nWordToken == token_word)
			vectSetValue.push_back(GetWord());
	} while (nWordToken != token_rpair);

	return true;
}