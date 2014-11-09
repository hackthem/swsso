//-----------------------------------------------------------------------------
//
//                                  swSSO
//
//       SSO Windows et Web avec Internet Explorer, Firefox, Mozilla...
//
//                Copyright (C) 2004-2014 - Sylvain WERDEFROY
//
//							 http://www.swsso.fr
//                   
//                             sylvain@swsso.fr
//
//-----------------------------------------------------------------------------
// 
//  This file is part of swSSO.
//  
//  swSSO is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  swSSO is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with swSSO.  If not, see <http://www.gnu.org/licenses/>.
// 
//-----------------------------------------------------------------------------
// swSSOSelectAccount.cpp
//-----------------------------------------------------------------------------

#include "stdafx.h"

HWND gwSelectAccount=NULL;
static int giRefreshTimer=10;
static int giSetForegroundTimer=20;
int giSelectedAccount=-1; // index de l'application s�lectionn�e pour r�utilisation du compte

//-----------------------------------------------------------------------------
// MoveControls()
//-----------------------------------------------------------------------------
// Repositionne les contr�les suite � redimensionnement de la fen�tre
//-----------------------------------------------------------------------------
static void MoveControls(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	
	RECT rect;
	GetClientRect(w,&rect);
	
	SetWindowPos(GetDlgItem(w,TV_APPLICATIONS),NULL,10,10,rect.right-20,rect.bottom-48,SWP_NOZORDER);
	SetWindowPos(GetDlgItem(w,IDOK),NULL,rect.right-162,rect.bottom-30,0,0,SWP_NOSIZE | SWP_NOZORDER);
	SetWindowPos(GetDlgItem(w,IDCANCEL),NULL,rect.right-82,rect.bottom-30,0,0,SWP_NOSIZE | SWP_NOZORDER);
		
	InvalidateRect(w,NULL,FALSE);
	TRACE((TRACE_LEAVE,_F_, ""));
}

//-----------------------------------------------------------------------------
// SaveWindowPos()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
static void SaveWindowPos(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	RECT rect;
	GetWindowRect(w,&rect);
	char s[8+1];

	if (IsIconic(w)) goto end;

	gx4=rect.left;
	gy4=rect.top;
	gcx4=rect.right-rect.left;
	gcy4=rect.bottom-rect.top;
	wsprintf(s,"%d",gx4);  WritePrivateProfileString("swSSO","x4",s,gszCfgFile);
	wsprintf(s,"%d",gy4);  WritePrivateProfileString("swSSO","y4",s,gszCfgFile);
	wsprintf(s,"%d",gcx4); WritePrivateProfileString("swSSO","cx4",s,gszCfgFile);
	wsprintf(s,"%d",gcy4); WritePrivateProfileString("swSSO","cy4",s,gszCfgFile);
	StoreIniEncryptedHash(); // ISSUE#164
end:
	TRACE((TRACE_LEAVE,_F_, ""));
}

//-----------------------------------------------------------------------------
// SelectAccountOnInitDialog()
//-----------------------------------------------------------------------------
void SelectAccountOnInitDialog(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	
	int cx;
	int cy;
	RECT rect;
	
	gwSelectAccount=w;

	// Positionnement et dimensionnement de la fen�tre
	if (gx4!=-1 && gy4!=-1 && gcx4!=-1 && gcy4!=-1)
	{
		SetWindowPos(w,NULL,gx4,gy4,gcx4,gcy4,SWP_NOZORDER);
	}
	else // position par d�faut
	{
		cx = GetSystemMetrics( SM_CXSCREEN );
		cy = GetSystemMetrics( SM_CYSCREEN );
		GetWindowRect(w,&rect);
		SetWindowPos(w,NULL,cx-(rect.right-rect.left)-50,cy-(rect.bottom-rect.top)-70,0,0,SWP_NOSIZE | SWP_NOZORDER);
	}

	// icone ALT-TAB
	SendMessage(w,WM_SETICON,ICON_BIG,(LPARAM)ghIconAltTab); 
	SendMessage(w,WM_SETICON,ICON_SMALL,(LPARAM)ghIconSystrayActive); 
	
	// Chargement de l'image list
	TreeView_SetImageList(GetDlgItem(w,TV_APPLICATIONS),ghImageList,TVSIL_STATE);

	// Remplissage de la treeview
	FillTreeView(w,TRUE);

	// timer de refresh
	if (giSetForegroundTimer==giTimer) giSetForegroundTimer=21;
	SetTimer(w,giSetForegroundTimer,100,NULL);

	TRACE((TRACE_LEAVE,_F_, ""));
}

//-----------------------------------------------------------------------------
// SelectAccountDialogProc()
//-----------------------------------------------------------------------------
// DialogProc de la fen�tre de lancement d'application 
//-----------------------------------------------------------------------------
static int CALLBACK SelectAccountDialogProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
	UNREFERENCED_PARAMETER(lp);
//	TRACE((TRACE_DEBUG,_F_,"msg=0x%08lx LOWORD(wp)=0x%04x HIWORD(wp)=%d lp=%d",msg,LOWORD(wp),HIWORD(wp),lp));

	int rc=FALSE;
	switch (msg)
	{
		case WM_INITDIALOG:		// ------------------------------------------------------- WM_INITDIALOG
			SelectAccountOnInitDialog(w);
			MoveControls(w); 
			break;
		case WM_DESTROY:
			//
			break;
		case WM_HELP:
			Help();
			break;
		case WM_TIMER:
			TRACE((TRACE_INFO,_F_,"WM_TIMER (SetForeground)"));
			if (giSetForegroundTimer==(int)wp) 
			{ 
				KillTimer(w,giSetForegroundTimer);
				SetForegroundWindow(w); 
				SetFocus(w); 
			}
			break;
		case WM_COMMAND:		// ------------------------------------------------------- WM_COMMAND
			//TRACE((TRACE_DEBUG,_F_,"WM_COMMAND LOWORD(wp)=0x%04x HIWORD(wp)=%d lp=%d",LOWORD(wp),HIWORD(wp),lp));
			switch (LOWORD(wp))
			{
				case IDOK:
					if (HIWORD(wp)==0) // les notifications autres que "from a control" ne nous int�ressent pas !
					{
						HTREEITEM hItem,hParentItem;
						int iAction;
						hItem=TreeView_GetSelection(GetDlgItem(w,TV_APPLICATIONS));
						hParentItem=TreeView_GetParent(GetDlgItem(w,TV_APPLICATIONS),hItem);
						if (hParentItem!=NULL) // sinon c'est une cat�gorie, �a ne nous int�resse pas
						{
							iAction=TVItemGetLParam(w,hItem); 
							SaveWindowPos(w);
							EndDialog(w,iAction);
						}
					}
					break;
				case IDCANCEL:
					if (HIWORD(wp)==0) // les notifications autres que "from a control" ne nous int�ressent pas !
					{
						SaveWindowPos(w);
						EndDialog(w,-1);
					}
					break;
			}
			break;
		case WM_SIZE:			// ------------------------------------------------------- WM_SIZE
			MoveControls(w); 
			break;
		case WM_SIZING:
			{
				RECT *pRectNewSize=(RECT*)lp;
				TRACE((TRACE_DEBUG,_F_,"RectNewSize=%d,%d,%d,%d",pRectNewSize->top,pRectNewSize->left,pRectNewSize->bottom,pRectNewSize->right));
				if (pRectNewSize->right-pRectNewSize->left < 391)  pRectNewSize->right=pRectNewSize->left+391;
				if (pRectNewSize->bottom-pRectNewSize->top < 553)  pRectNewSize->bottom=pRectNewSize->top+553;
				rc=TRUE;
			}
			break;
		case WM_NOTIFY:			// ------------------------------------------------------- WM_NOTIFY
			switch (((NMHDR FAR *)lp)->code) 
			{
				case NM_DBLCLK:
					if (wp==TV_APPLICATIONS) 
					{
						HTREEITEM hItem,hParentItem;
						int iAction;
						hItem=TreeView_GetSelection(GetDlgItem(w,TV_APPLICATIONS));
						hParentItem=TreeView_GetParent(GetDlgItem(w,TV_APPLICATIONS),hItem);
						if (hParentItem!=NULL) // sinon c'est une cat�gorie, �a ne nous int�resse pas
						{
							iAction=TVItemGetLParam(w,hItem); 
							SaveWindowPos(w);
							EndDialog(w,iAction);
						}
					}
					break;
			}
			break;
	}
	return rc;
}

// ----------------------------------------------------------------------------------
// ShowSelectAccount()
// ----------------------------------------------------------------------------------
// Fen�tre de s�l�ction d'un compte existant
// ----------------------------------------------------------------------------------
int ShowSelectAccount(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;
	
	// si fen�tre d�j� affich�e, la replace au premier plan
	if (gwSelectAccount!=NULL)
	{
		ShowWindow(gwSelectAccount,SW_SHOW);
		SetForegroundWindow(gwSelectAccount);
		goto end;
	}

	rc=DialogBox(ghInstance,MAKEINTRESOURCE(IDD_SELECT_ACCOUNT),HWND_DESKTOP,SelectAccountDialogProc);
	gwSelectAccount=NULL;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}