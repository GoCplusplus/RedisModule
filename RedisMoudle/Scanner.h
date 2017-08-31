#ifndef __CSCANNER__H
#define __CSCANNER__H

#include "comm.h"
#include <iostream>
#include <vector>

const int nScanSize = 2048; // ��ʼ����������С

class CScanner
{
public:
	CScanner();
	~CScanner();

public:
	void LoadContent(const char* strContent, int nLen); // ����Ҫɨ�������
	int Scan();
	const char* GetWord()
	{
		return m_pWord;
	}
	bool ReadCondition(QueryTree*& tree); // ��������

private:
	char get();
	void unget();
	bool ReadSet(std::vector<std::string>& vectSetValue, char* pFirst); // ֵ�ļ���
private:
	int m_nPos; // ��ǰɨ�赽��λ��
	int m_nTotal; // ���ݵ����ֽ���
	char* m_pBuff; // Ҫɨ�������
	char* m_pWord; // ɨ�赽�ĵ���
};

#endif