﻿
/*
Copyright (c) 2009-2015 Maximus5
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define HIDE_USE_EXCEPTION_INFO

#define SHOWDEBUGSTR
#define DEBUGSTRCMD(s) //DEBUGSTR(s)

#include "Header.h"
#include "../common/PipeServer.h"

#include "ConEmu.h"
#include "DefaultTerm.h"
#include "GuiServer.h"
#include "Macro.h"
#include "OptionsClass.h"
#include "RealConsole.h"
#include "VConGroup.h"
#include "VConRelease.h"
#include "VirtualConsole.h"

#ifdef USEPIPELOG
namespace PipeServerLogger
{
	Event g_events[BUFFER_SIZE];
	LONG g_pos = -1;
}
#endif

#ifdef _DEBUG
//	#define ALLOW_WINE_MSG
#else
	#undef ALLOW_WINE_MSG
#endif

CGuiServer::CGuiServer()
{
	//mn_GuiServerThreadId = 0; mh_GuiServerThread = NULL; mh_GuiServerThreadTerminate = NULL;
	mp_GuiServer = (PipeServer<CESERVER_REQ>*)calloc(1, sizeof(*mp_GuiServer));
	mp_GuiServer->SetMaxCount(2);
	ms_ServerPipe[0] = 0;
	mp_GuiServerPID = (PipeServer<CESERVER_REQ>*)calloc(1, sizeof(*mp_GuiServerPID));
	mp_GuiServerPID->SetMaxCount(2);
	ms_ServerPipePID[0] = 0;
	mb_GuiForcedTermination = false;
}

CGuiServer::~CGuiServer()
{
	Stop(true);
}

bool CGuiServer::Start()
{
	// Запустить серверную нить
	// 120122 - теперь через PipeServer
	_wsprintf(ms_ServerPipe, SKIPLEN(countof(ms_ServerPipe)) CEGUIPIPENAME, L".", (DWORD)ghWnd); //-V205

	mp_GuiServer->SetOverlapped(true);
	mp_GuiServer->SetLoopCommands(false);
	mp_GuiServer->SetDummyAnswerSize(sizeof(CESERVER_REQ_HDR));

	// Issue 1828: FindWindowEx may fails in some cases, only PID will be working...
	_wsprintf(ms_ServerPipePID, SKIPLEN(countof(ms_ServerPipePID)) CESERVERPIPENAME, L".", GetCurrentProcessId());

	mp_GuiServerPID->SetOverlapped(true);
	mp_GuiServerPID->SetLoopCommands(false);
	mp_GuiServerPID->SetDummyAnswerSize(sizeof(CESERVER_REQ_HDR));

	PipeServer<CESERVER_REQ>::PipeServerConnected_t lpfnOnConnected = NULL;
	#ifdef _DEBUG
	lpfnOnConnected = CGuiServer::OnGuiServerConnected;
	#endif

	if (!mp_GuiServer->StartPipeServer(false, ms_ServerPipe, (LPARAM)this, LocalSecurity(), GuiServerCommand, GuiServerFree, lpfnOnConnected, NULL))
	{
		// Ошибка уже показана
		return false;
	}

	mp_GuiServerPID->StartPipeServer(false, ms_ServerPipePID, (LPARAM)this, LocalSecurity(), GuiServerCommand, GuiServerFree, lpfnOnConnected, NULL);

	return true;
}

void CGuiServer::Stop(bool abDeinitialize/*=false*/)
{
	//120122 - Теперь через PipeServer
	if (mp_GuiServer)
	{
		ShutdownGuiStep(L"mp_GuiServer->StopPipeServer");

		mp_GuiServer->StopPipeServer(false, mb_GuiForcedTermination);

		if (abDeinitialize)
		{
			free(mp_GuiServer);
			mp_GuiServer = NULL;
		}

		ShutdownGuiStep(L"mp_GuiServer->StopPipeServer - done");
	}

	if (mp_GuiServerPID)
	{
		ShutdownGuiStep(L"mp_GuiServerPID->StopPipeServer");

		mp_GuiServerPID->StopPipeServer(false, mb_GuiForcedTermination);

		if (abDeinitialize)
		{
			free(mp_GuiServerPID);
			mp_GuiServerPID = NULL;
		}

		ShutdownGuiStep(L"mp_GuiServerPID->StopPipeServer - done");
	}
}

//// Эта функция пайп не закрывает!
//void CGuiServer::GuiServerThreadCommand(HANDLE hPipe)
BOOL CGuiServer::GuiServerCommand(LPVOID pInst, CESERVER_REQ* pIn, CESERVER_REQ* &ppReply, DWORD &pcbReplySize, DWORD &pcbMaxReplySize, LPARAM lParam)
{
	BOOL lbRc = FALSE;
	CGuiServer* pGSrv = (CGuiServer*)lParam;

	if (!pGSrv)
	{
		_ASSERTE(((CGuiServer*)lParam)!=NULL);
		pGSrv = &gpConEmu->m_GuiServer;
	}

	if (pIn->hdr.bAsync)
		pGSrv->mp_GuiServer->BreakConnection(pInst);

	gpSetCls->debugLogCommand(pIn, TRUE, timeGetTime(), 0, pGSrv ? pGSrv->ms_ServerPipe : NULL);

	#ifdef _DEBUG
	UINT nDataSize = pIn->hdr.cbSize - sizeof(CESERVER_REQ_HDR);
	#endif
	// Все данные из пайпа получены, обрабатываем команду и возвращаем (если нужно) результат

	#ifdef ALLOW_WINE_MSG
	if (gbIsWine)
	{
		wchar_t szMsg[128];
		msprintf(szMsg, countof(szMsg), L"CGuiServer::GuiServerCommand.\nGUI TID=%u\nSrcPID=%u, SrcTID=%u, Cmd=%u",
			GetCurrentThreadId(), pIn->hdr.nSrcPID, pIn->hdr.nSrcThreadId, pIn->hdr.nCmd);
		MessageBox(szMsg, MB_ICONINFORMATION);
	}
	#endif

	switch (pIn->hdr.nCmd)
	{
		case CECMD_NEWCMD:
		{
			// Приходит из другой копии ConEmu.exe, когда она запущена с ключом /single, /showhide, /showhideTSA
			DEBUGSTRCMD(L"GUI recieved CECMD_NEWCMD\n");

			LPCWSTR pszCommand = pIn->NewCmd.GetCommand();
			_ASSERTE(pszCommand!=NULL && "Must be at least empty string but NOT NULL");

			if (pIn->NewCmd.isAdvLogging && !gpSetCls->isAdvLogging)
			{
				gpSetCls->isAdvLogging = pIn->NewCmd.isAdvLogging;
				gpConEmu->CreateLog();
			}

			if (gpSetCls->isAdvLogging && (pIn->NewCmd.isAdvLogging > gpSetCls->isAdvLogging))
			{
				wchar_t szLogLevel[80];
				_wsprintf(szLogLevel, SKIPLEN(countof(szLogLevel)) L"Changing log level! Old=%u, New=%u", (UINT)gpSetCls->isAdvLogging, (UINT)pIn->NewCmd.isAdvLogging);
				gpConEmu->LogString(szLogLevel);
				gpSetCls->isAdvLogging = pIn->NewCmd.isAdvLogging;
			}

			if (gpSetCls->isAdvLogging)
			{
				size_t cchAll = 120 + _tcslen(pIn->NewCmd.szConEmu) + _tcslen(pIn->NewCmd.szCurDir) + _tcslen(pszCommand);
				wchar_t* pszInfo = (wchar_t*)malloc(cchAll*sizeof(*pszInfo));
				if (pszInfo)
				{
					_wsprintf(pszInfo, SKIPLEN(cchAll) L"CECMD_NEWCMD: Wnd=x%08X, Act=%u, ConEmu=%s, Dir=%s, Cmd=%s",
						(DWORD)(DWORD_PTR)pIn->NewCmd.hFromConWnd, pIn->NewCmd.ShowHide,
						pIn->NewCmd.szConEmu, pIn->NewCmd.szCurDir, pszCommand);
					gpConEmu->LogString(pszInfo);
					free(pszInfo);
				}
			}

			BOOL bAccepted = FALSE;

			if (pIn->NewCmd.szConEmu[0])
			{
				bAccepted = (lstrcmpi(gpConEmu->ms_ConEmuExeDir, pIn->NewCmd.szConEmu) == 0);
			}
			else
			{
				bAccepted = TRUE;
			}

			if (bAccepted)
			{
				bool bCreateTab = (pIn->NewCmd.ShowHide == sih_None || pIn->NewCmd.ShowHide == sih_StartDetached)
					// Issue 1275: When minimized into TSA (on all VCon are closed) we need to restore and run new tab
					|| (pszCommand[0] && !CVConGroup::isVConExists(0));

				RConStartArgs rTest;
				if (pszCommand[0])
				{
					rTest.pszSpecialCmd = lstrdup(pszCommand);
					rTest.ProcessNewConArg();
				}

				if (rTest.BackgroundTab != crb_On)
				{
					gpConEmu->DoMinimizeRestore(bCreateTab ? sih_SetForeground : pIn->NewCmd.ShowHide);
				}

				// Может быть пусто
				if (bCreateTab && pszCommand[0])
				{
					RConStartArgs *pArgs = new RConStartArgs;

					// New tab must be started with same credentials that calling tab if others was not specified
					{
						if (!rTest.HasInheritedArgs())
						{
							CVConGuard VCon;
							if ((pIn->NewCmd.hFromConWnd || pIn->NewCmd.hFromDcWnd)
								&& CVConGroup::GetVConByHWND(pIn->NewCmd.hFromConWnd, pIn->NewCmd.hFromDcWnd, &VCon))
							{
								const RConStartArgs& r = VCon->RCon()->GetArgs();
								if (r.HasInheritedArgs())
								{
									pArgs->AssignInheritedArgs(&r);
								}
							}
						}
					}

					pArgs->Detached = (pIn->NewCmd.ShowHide == sih_StartDetached) ? crb_On : crb_Off;
					pArgs->pszSpecialCmd = lstrdup(pszCommand);
					if (pIn->NewCmd.szCurDir[0] == 0)
					{
						_ASSERTE(pIn->NewCmd.szCurDir[0] != 0);
					}
					else
					{
						pArgs->pszStartupDir = lstrdup(pIn->NewCmd.szCurDir);
					}

					LPCWSTR pszStrings = pIn->NewCmd.GetEnvStrings();
					if (pszStrings && pIn->NewCmd.cchEnvStrings)
					{
						size_t cbBytes = pIn->NewCmd.cchEnvStrings*sizeof(*pArgs->pszEnvStrings);
						pArgs->pszEnvStrings = (wchar_t*)malloc(cbBytes);
						if (pArgs->pszEnvStrings)
						{
							memmove(pArgs->pszEnvStrings, pszStrings, cbBytes);
							pArgs->cchEnvStrings = pIn->NewCmd.cchEnvStrings;
						}
					}

					if (gpSetCls->IsMulti() || CVConGroup::isDetached())
					{
						gpConEmu->PostCreateCon(pArgs);
					}
					else
					{
						// Если хотят в одном окне - только одну консоль
						gpConEmu->CreateWnd(pArgs);
						SafeDelete(pArgs);
					}
				}
				else
				{
					_ASSERTE(pIn->NewCmd.ShowHide==sih_ShowMinimize || pIn->NewCmd.ShowHide==sih_ShowHideTSA || pIn->NewCmd.ShowHide==sih_Show);
				}
			}

			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(BYTE);
			lbRc = ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize);
			if (lbRc)
			{
				ppReply->Data[0] = bAccepted;
			}

			break;
		} //CECMD_NEWCMD

		case CECMD_TABSCMD:
		{
			// 0: спрятать/показать табы, 1: перейти на следующую, 2: перейти на предыдущую, 3: commit switch
			DEBUGSTRCMD(L"GUI recieved CECMD_TABSCMD\n");
			_ASSERTE(nDataSize>=1);
			DWORD nTabCmd = pIn->Data[0];
			gpConEmu->TabCommand((ConEmuTabCommand)nTabCmd);

			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(BYTE);
			if (ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
			{
				lbRc = TRUE;
				ppReply->Data[0] = TRUE;
			}
			break;
		} // CECMD_TABSCMD

		case CECMD_ATTACH2GUI:
		{
			// Получен запрос на Attach из сервера

			MCHKHEAP;

			CESERVER_REQ_SRVSTARTSTOPRET Ret = {};
			lbRc = CVConGroup::AttachRequested(pIn->StartStop.hWnd, &(pIn->StartStop), Ret);

			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SRVSTARTSTOPRET)+(Ret.cchEnvStrings*sizeof(wchar_t));
			if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
			{
				SafeFree(Ret.pszStrings);
				goto wrap;
			}

			if (lbRc)
			{
				_ASSERTE(sizeof(ppReply->SrvStartStopRet) == sizeof(Ret));
				memmove(&ppReply->SrvStartStopRet, &Ret, sizeof(Ret));

				// Environment strings (inherited from parent console)
				if (Ret.cchEnvStrings && Ret.pszStrings)
				{
					memmove(ppReply->SrvStartStopRet.szStrings, Ret.pszStrings, Ret.cchEnvStrings*sizeof(wchar_t));
					ppReply->SrvStartStopRet.cchEnvStrings = Ret.cchEnvStrings;
				}
				else
				{
					ppReply->SrvStartStopRet.cchEnvStrings = 0;
				}
				SafeFree(Ret.pszStrings);

				_ASSERTE((ppReply->StartStopRet.nBufferHeight == 0) || ((int)ppReply->StartStopRet.nBufferHeight > (pIn->StartStop.sbi.srWindow.Bottom-pIn->StartStop.sbi.srWindow.Top)));
			}

			MCHKHEAP;

			break;
		} // CECMD_ATTACH2GUI

		case CECMD_SRVSTARTSTOP:
		{
			MCHKHEAP;

			// SRVSTART не приходит если запускается cmd под админом
			bool lbAllocated = false;

			if (pIn->SrvStartStop.Started == srv_Started)
			{
				// Запущен процесс сервера
				HWND hConWnd = (HWND)pIn->dwData[1];
				_ASSERTE(hConWnd && IsWindow(hConWnd));

				DWORD nStartTick = timeGetTime();

				struct MsgSrvStartedArg
				{
					HWND  hConWnd;
					DWORD nSrcPID;
					DWORD dwKeybLayout;
					DWORD timeStart;
					DWORD timeRecv;
					DWORD timeFin;
					CESERVER_REQ_SRVSTARTSTOPRET Ret;

					//111002 - вернуть должен HWND окна отрисовки (дочернее окно ConEmu)
					static LRESULT OnSrvStarted(LPARAM lParam)
					{
						MsgSrvStartedArg *pArg = (MsgSrvStartedArg*)lParam;

						HWND hWndDC = NULL;

						DWORD nServerPID = pArg->nSrcPID;
						HWND  hWndCon = pArg->hConWnd;
						DWORD dwKeybLayout = pArg->dwKeybLayout;
						pArg->timeRecv = timeGetTime();

						DWORD t1, t2; int iFound = -1;

						hWndDC = CVConGroup::DoSrvCreated(nServerPID, hWndCon, dwKeybLayout, t1, t2, iFound, pArg->Ret);

						UNREFERENCED_PARAMETER(dwKeybLayout);
						UNREFERENCED_PARAMETER(hWndCon);

						pArg->timeFin = timeGetTime();
						if (hWndDC == NULL)
						{
							_ASSERTE(hWndDC!=NULL);
						}
						else
						{
							#ifdef _DEBUG
							DWORD nRecvDur = pArg->timeRecv - pArg->timeStart;
							DWORD nProcDur = pArg->timeFin - pArg->timeRecv;

							#define MSGSTARTED_TIMEOUT 10000
							if ((nRecvDur > MSGSTARTED_TIMEOUT) || (nProcDur > MSGSTARTED_TIMEOUT))
							{
								_ASSERTE((nRecvDur <= MSGSTARTED_TIMEOUT) && (nProcDur <= MSGSTARTED_TIMEOUT));
							}
							#endif
						}

						return (LRESULT)hWndDC;
					};
				} arg = {hConWnd, pIn->hdr.nSrcPID, pIn->SrvStartStop.dwKeybLayout, nStartTick};

				gpConEmu->CallMainThread(true, arg.OnSrvStarted, (LPARAM)&arg);

				HWND hWndDC = arg.Ret.Info.hWndDc;
				HWND hWndBack = arg.Ret.Info.hWndBack;
				_ASSERTE(hWndDC!=NULL);

				#ifdef _DEBUG
				DWORD dwErr = GetLastError(), nEndTick = timeGetTime(), nDelta = nEndTick - nStartTick;
				if (hWndDC && nDelta >= EXECUTE_CMD_WARN_TIMEOUT)
				{
					if (!IsDebuggerPresent())
					{
						//_ASSERTE(nDelta <= EXECUTE_CMD_WARN_TIMEOUT || (pIn->hdr.nCmd == CECMD_CMDSTARTSTOP && nDelta <= EXECUTE_CMD_WARN_TIMEOUT2));
						_ASSERTEX(nDelta <= EXECUTE_CMD_WARN_TIMEOUT);
					}
				}
				#endif

				pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SRVSTARTSTOPRET)+(arg.Ret.cchEnvStrings*sizeof(wchar_t));
				if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
				{
					SafeFree(arg.Ret.pszStrings);
					goto wrap;
				}
				lbAllocated = true;

				_ASSERTE(sizeof(ppReply->SrvStartStopRet) == sizeof(arg.Ret));
				memmove(&ppReply->SrvStartStopRet, &arg.Ret, sizeof(arg.Ret));

				// Environment strings (inherited from parent console)
				if (arg.Ret.cchEnvStrings && arg.Ret.pszStrings)
				{
					memmove(ppReply->SrvStartStopRet.szStrings, arg.Ret.pszStrings, arg.Ret.cchEnvStrings*sizeof(wchar_t));
					ppReply->SrvStartStopRet.cchEnvStrings = arg.Ret.cchEnvStrings;
				}
				else
				{
					ppReply->SrvStartStopRet.cchEnvStrings = 0;
				}
				SafeFree(arg.Ret.pszStrings);
			}
			else if (pIn->SrvStartStop.Started == srv_Stopped)
			{
				// Процесс сервера завершается
				CRealConsole* pRCon = NULL;
				CVConGuard VCon;

				for (size_t i = 0;; i++)
				{
					if (!CVConGroup::GetVCon(i, &VCon))
						break;

					pRCon = VCon->RCon();
					if (pRCon && (pRCon->GetServerPID(true) == pIn->hdr.nSrcPID || pRCon->GetServerPID(false) == pIn->hdr.nSrcPID))
					{
						break;
					}
					pRCon = NULL;
				}

				gpConEmu->mn_ShellExitCode = pIn->SrvStartStop.nShellExitCode;

				if (pRCon)
					pRCon->OnServerClosing(pIn->hdr.nSrcPID, &pIn->SrvStartStop.nShellExitCode);

				//pIn->dwData[0] = 1;
			}
			else
			{
				_ASSERTE((pIn->dwData[0] == 1) || (pIn->dwData[0] == 101));
			}

			MCHKHEAP;

			if (!lbAllocated)
			{
				pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SRVSTARTSTOPRET);
				if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
					goto wrap;
			}

			lbRc = TRUE;
			//// Отправляем
			//fSuccess = WriteFile(
			//               hPipe,        // handle to pipe
			//               pOut,         // buffer to write from
			//               pOut->hdr.cbSize,  // number of bytes to write
			//               &cbWritten,   // number of bytes written
			//               NULL);        // not overlapped I/O

			//ExecuteFreeResult(pOut);
			break;
		} // CECMD_SRVSTARTSTOP

		case CECMD_ASSERT:
		{
			DWORD nBtn = MessageBox(NULL, pIn->AssertInfo.szDebugInfo, pIn->AssertInfo.szTitle, pIn->AssertInfo.nBtns);

			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(DWORD);
			if (ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
			{
				lbRc = TRUE;
				ppReply->dwData[0] = nBtn;
			}

			//ExecutePrepareCmd(&pIn->hdr, CECMD_ASSERT, sizeof(CESERVER_REQ_HDR) + sizeof(DWORD));
			//pIn->dwData[0] = nBtn;
			//// Отправляем
			//fSuccess = WriteFile(
			//               hPipe,        // handle to pipe
			//               pIn,         // buffer to write from
			//               pIn->hdr.cbSize,  // number of bytes to write
			//               &cbWritten,   // number of bytes written
			//               NULL);        // not overlapped I/O
			break;
		} // CECMD_ASSERT

		case CECMD_ATTACHGUIAPP:
		{
			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_ATTACHGUIAPP);
			if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
				goto wrap;
			ppReply->AttachGuiApp = pIn->AttachGuiApp;

			//CESERVER_REQ Out;
			//ExecutePrepareCmd(&Out.hdr, CECMD_ATTACHGUIAPP, sizeof(CESERVER_REQ_HDR)+sizeof(Out.AttachGuiApp));
			//Out.AttachGuiApp = pIn->AttachGuiApp;

			#ifdef SHOW_GUIATTACH_START
			if (pIn->AttachGuiApp.hWindow == NULL)
			{
				wchar_t szDbg[1024];
				_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"AttachGuiApp requested from:\n%s\nPID=%u", pIn->AttachGuiApp.sAppFilePathName, pIn->AttachGuiApp.nPID);
				//MBoxA(szDbg);
				MessageBox(NULL, szDbg, L"ConEmu", MB_SYSTEMMODAL);
			}
			#endif

			// Уведомить ожидающую вкладку
			CRealConsole* pRCon = CVConGroup::AttachRequestedGui(pIn->AttachGuiApp.nServerPID, pIn->AttachGuiApp.sAppFilePathName, pIn->AttachGuiApp.nPID);
			if (pRCon)
			{
				CVConGuard VCon(pRCon->VCon());
				RECT rcPrev = ppReply->AttachGuiApp.rcWindow;
				HWND hBack = pRCon->VCon()->GetBack();

				//// Размер должен быть независим от возможности наличия прокрутки в VCon
				//GetWindowRect(hBack, &ppReply->AttachGuiApp.rcWindow);
				//ppReply->AttachGuiApp.rcWindow.right -= ppReply->AttachGuiApp.rcWindow.left;
				//ppReply->AttachGuiApp.rcWindow.bottom -= ppReply->AttachGuiApp.rcWindow.top;
				//ppReply->AttachGuiApp.rcWindow.left = ppReply->AttachGuiApp.rcWindow.top = 0;
				////MapWindowPoints(NULL, hBack, (LPPOINT)&ppReply->AttachGuiApp.rcWindow, 2);
				//pRCon->CorrectGuiChildRect(ppReply->AttachGuiApp.nStyle, ppReply->AttachGuiApp.nStyleEx, ppReply->AttachGuiApp.rcWindow);

				// Уведомить RCon и ConEmuC, что гуй подцепился
				// Вызывается два раза. Первый (при запуске exe) ahGuiWnd==NULL, второй - после фактического создания окна
				pRCon->SetGuiMode(pIn->AttachGuiApp.nFlags, pIn->AttachGuiApp.hAppWindow, pIn->AttachGuiApp.Styles.nStyle, pIn->AttachGuiApp.Styles.nStyleEx, pIn->AttachGuiApp.sAppFilePathName, pIn->AttachGuiApp.nPID, pIn->hdr.nBits, rcPrev);

				ppReply->AttachGuiApp.nFlags = agaf_Success | (pRCon->isActive(false) ? 0 : agaf_Inactive);
				ppReply->AttachGuiApp.nServerPID = pRCon->GetServerPID();
				ppReply->AttachGuiApp.nPID = ppReply->AttachGuiApp.nServerPID;
				ppReply->AttachGuiApp.hConEmuDc = pRCon->GetView();
				ppReply->AttachGuiApp.hConEmuBack = hBack;
				ppReply->AttachGuiApp.hConEmuWnd = ghWnd;
				ppReply->AttachGuiApp.hAppWindow = pIn->AttachGuiApp.hAppWindow;
				ppReply->AttachGuiApp.hSrvConWnd = pRCon->ConWnd();
				ppReply->AttachGuiApp.hkl = (DWORD)(LONG)(LONG_PTR)GetKeyboardLayout(gpConEmu->mn_MainThreadId);
				ZeroStruct(ppReply->AttachGuiApp.Styles.Shifts);
				CRealConsole::CorrectGuiChildRect(pIn->AttachGuiApp.Styles.nStyle, pIn->AttachGuiApp.Styles.nStyleEx, ppReply->AttachGuiApp.Styles.Shifts, pIn->AttachGuiApp.sAppFilePathName);
			}
			else
			{
				ppReply->AttachGuiApp.nFlags = agaf_Fail;
				_ASSERTE(FALSE && "No one tab is waiting for ChildGui process");
			}

			lbRc = TRUE;

			//// Отправляем
			//fSuccess = WriteFile(
			//               hPipe,        // handle to pipe
			//               &Out,         // buffer to write from
			//               Out.hdr.cbSize,  // number of bytes to write
			//               &cbWritten,   // number of bytes written
			//               NULL);        // not overlapped I/O
			break;
		} // CECMD_ATTACHGUIAPP

		case CECMD_GUICLIENTSHIFT:
		{
			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(GuiStylesAndShifts);
			if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
				goto wrap;
			ppReply->GuiAppShifts = pIn->GuiAppShifts;

			ZeroStruct(ppReply->GuiAppShifts.Shifts);
			CRealConsole::CorrectGuiChildRect(pIn->GuiAppShifts.nStyle, pIn->GuiAppShifts.nStyleEx, ppReply->GuiAppShifts.Shifts, pIn->GuiAppShifts.szExeName);

			lbRc = TRUE;
			break;
		} // CECMD_GUICLIENTSHIFT

		case CECMD_GUIMACRO:
		{
			// Допустимо, если GuiMacro пытаются выполнить извне
			CVConGuard VCon;

			CVConGroup::GetActiveVCon(&VCon);

			DWORD nFarPluginPID = VCon->RCon()->GetFarPID(true);
			LPWSTR pszResult = ConEmuMacro::ExecuteMacro(pIn->GuiMacro.sMacro, VCon->RCon(), (nFarPluginPID==pIn->hdr.nSrcPID), &pIn->GuiMacro);

			int nLen = pszResult ? _tcslen(pszResult) : 0;

			pcbReplySize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_GUIMACRO)+nLen*sizeof(wchar_t);
			if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
			{
				SafeFree(pszResult);
				goto wrap;
			}

			if (pszResult)
			{
				lstrcpy(ppReply->GuiMacro.sMacro, pszResult);
				ppReply->GuiMacro.nSucceeded = 1;
				free(pszResult);
			}
			else
			{
				ppReply->GuiMacro.sMacro[0] = 0;
				ppReply->GuiMacro.nSucceeded = 0;
			}

			lbRc = TRUE;
			break;
		} // CECMD_GUIMACRO

		case CECMD_CMDSTARTSTOP:
		{
			CRealServer* pRSrv = NULL;
			CVConGuard VCon;

			DWORD nSrvPID = pIn->hdr.nSrcPID;
			DWORD nMonitorTID = (pIn->DataSize() >= sizeof(pIn->StartStop)) ? pIn->StartStop.dwAID : 0;

			if (CVConGroup::GetVConBySrvPID(nSrvPID, nMonitorTID, &VCon))
				pRSrv = &VCon->RCon()->m_RConServer;

			if (pRSrv)
			{
				CESERVER_REQ* pOut = pRSrv->cmdStartStop(pInst, pIn, pIn->DataSize());
				if (pOut)
				{
					DWORD nDataSize = pOut->DataSize();
					pcbReplySize = pOut->hdr.cbSize;
					if (ExecuteNewCmd(ppReply, pcbMaxReplySize, pOut->hdr.nCmd, pcbReplySize))
					{
						if (nDataSize > 0)
						{
							memmove(ppReply->Data, pOut->Data, nDataSize);
						}
						lbRc = TRUE;
					}
					ExecuteFreeResult(pOut);
				}
			}
			break;
		} // CECMD_CMDSTARTSTOP

		//case CECMD_DEFTERMSTARTED:
		//{
		//	if (gpConEmu->mp_DefTrm)
		//		gpConEmu->mp_DefTrm->OnDefTermStarted(pIn);

		//	pcbReplySize = sizeof(CESERVER_REQ_HDR);
		//	if (!ExecuteNewCmd(ppReply, pcbMaxReplySize, pIn->hdr.nCmd, pcbReplySize))
		//		goto wrap;
		//	lbRc = TRUE;
		//	break;
		//} // CECMD_DEFTERMSTARTED

		default:
			_ASSERTE(FALSE && "Command was not handled in CGuiServer::GuiServerCommand");
	}

	//// Освободить память
	//if (pIn && (LPVOID)pIn != (LPVOID)&in)
	//{
	//	free(pIn); pIn = NULL;
	//}
wrap:
	return lbRc;
}

void CGuiServer::GuiServerFree(CESERVER_REQ* pReply, LPARAM lParam)
{
	ExecuteFreeResult(pReply);
}

#ifdef _DEBUG
BOOL CGuiServer::OnGuiServerConnected(LPVOID pInst, LPARAM lParam)
{
	#ifdef ALLOW_WINE_MSG
	if (gbIsWine)
	{
		wchar_t szMsg[128];
		msprintf(szMsg, countof(szMsg), L"CGuiServer::OnGuiServerConnected.\nGUI TID=%u", GetCurrentThreadId());
		MsgBox(szMsg, MB_ICONINFORMATION);
	}
	#endif
	return TRUE;
}
#endif
