
#if !defined(_DEBUG)

#define DumpSendString(buf,cchLen)

#else

static void DumpSendString(const char* buf, size_t cchLen)
{
	static int nWriteCallNo = 0;
	char   szDbg[200];
	size_t nLen = cchLen;
	size_t nStart;
	char*  pszDst;

	sprintf(szDbg, "putty-send #%u: ", ++nWriteCallNo);

	nStart = strlen(szDbg);
	pszDst = szDbg + nStart;

	if (buf && cchLen)
	{
		const char* pszSrc = buf;
		size_t nCur = 0;
		while (nLen)
		{
			switch (*pszSrc)
			{
			case '\r':
				*(pszDst++) = '\\'; *(pszDst++) = 'r';
				break;
			case '\n':
				*(pszDst++) = '\\'; *(pszDst++) = 'n';
				break;
			case '\t':
				*(pszDst++) = '\\'; *(pszDst++) = 't';
				break;
			case '\x1B':
				*(pszDst++) = '\\'; *(pszDst++) = 'e';
				break;
			case 0:
				*(pszDst++) = '\\'; *(pszDst++) = '0';
				break;
			case 7:
				*(pszDst++) = '\\'; *(pszDst++) = 'a';
				break;
			case 8:
				*(pszDst++) = '\\'; *(pszDst++) = 'b';
				break;
			case '\\':
				*(pszDst++) = '\\'; *(pszDst++) = '\\';
				break;
			default:
				*(pszDst++) = *pszSrc;
			}
			pszSrc++;
			nLen--;
			nCur++;

			if (nCur >= 80)
			{
				*(pszDst++) = '#';
				*(pszDst++) = '\n';
				*pszDst = 0;
				OutputDebugStringA(szDbg);
				memset(szDbg, ' ', nStart);
				nCur = 0;
				pszDst = szDbg + nStart;
			}
		}
	}
	else
	{
		char* psEmptyMessage = " - <empty sequence>";
		size_t nMsgLen = strlen(psEmptyMessage);

		pszDst -= 2;
		memcpy(pszDst, psEmptyMessage, nMsgLen);
		pszDst += nMsgLen;
	}

	if (pszDst > szDbg)
	{
		*(pszDst++) = '#';
		*(pszDst++) = '\n';
		*pszDst = 0;
		OutputDebugStringA(szDbg);
	}
}

#endif
