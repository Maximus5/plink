
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
		const char* pszSpec;
		size_t nCur = 0;
		while (nLen)
		{
			switch (*pszSrc)
			{
			case  0: pszSpec = "\\NUL"; break;
			case  1: pszSpec = "\\SOH"; break; // Start of Heading
			case  2: pszSpec = "\\STX"; break; // Start of Text
			case  3: pszSpec = "\\ETX"; break; // End of Text
			case  4: pszSpec = "\\EOT"; break; // END of Transmission
			case  5: pszSpec = "\\ENQ"; break; // Enquiry
			case  6: pszSpec = "\\ACK"; break; // Acknowledge (response to ENQ)
			case  7: pszSpec = "\\BEL"; break; // Bell
			case  8: pszSpec = "\\BS_"; break; // Backspace
			case  9: pszSpec = "\\TAB"; break; // Tab
			case 10: pszSpec = "\\LF_"; break; // Line Feed
			case 11: pszSpec = "\\VT_"; break; // Vertical Tab
			case 12: pszSpec = "\\FF_"; break; // Form Feed
			case 13: pszSpec = "\\CR_"; break; // Carriage Return
			case 14: pszSpec = "\\SO_"; break; // Shift Out
			case 15: pszSpec = "\\SI_"; break; // Shift In
			case 16: pszSpec = "\\DLE"; break; // Data Link Escape
			case 17: pszSpec = "\\DC1"; break;
			case 18: pszSpec = "\\DC2"; break;
			case 19: pszSpec = "\\DC3"; break;
			case 20: pszSpec = "\\DC4"; break;
			case 21: pszSpec = "\\NAK"; break; // Negative Acknowledge
			case 22: pszSpec = "\\SYN"; break; // Synchronous Idle
			case 23: pszSpec = "\\ETB"; break; // End of Transmission Block
			case 24: pszSpec = "\\CAN"; break; // Cancel
			case 25: pszSpec = "\\EM_"; break; // End of medium
			case 26: pszSpec = "\\SUB"; break; // Substite
			case 27: pszSpec = "\\ESC"; break; // Escape
			case 28: pszSpec = "\\FS_"; break; // File Separator
			case 29: pszSpec = "\\GS_"; break; // Group Separator
			case 30: pszSpec = "\\RS_"; break; // Record Separator
			case 31: pszSpec = "\\US_"; break; // Unit Separator
			// Some other specials
			case '\\': pszSpec = "\\\\";  break;
			case 0x7F: pszSpec = "\\DEL"; break;
			// Usual chars
			default:
				*(pszDst++) = *pszSrc; pszSpec = NULL;
			}

			if (pszSpec)
			{
				int cbLen = strlen(pszSpec);
				memmove(pszDst, pszSpec, cbLen);
				pszDst += cbLen;
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
