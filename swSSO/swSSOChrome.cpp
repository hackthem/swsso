//-----------------------------------------------------------------------------
//
//                                  swSSO
//
//       SSO Windows et Web avec Internet Explorer, Firefox, Mozilla...
//
//                Copyright (C) 2004-2013 - Sylvain WERDEFROY
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
// swSSOChrome.cpp
//-----------------------------------------------------------------------------

#include "stdafx.h"

//-----------------------------------------------------------------------------
// GetChromeURL()
//-----------------------------------------------------------------------------
// Retourne l'URL courante de la fen�tre Chrome
// ----------------------------------------------------------------------------------
// [in] w = handle de la fen�tre
// [rc] pszURL (� lib�rer par l'appelant) ou NULL si erreur 
// ----------------------------------------------------------------------------------
char *GetChromeURL(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	HWND wURL;
	char *pszURL=NULL;
	int len;

	// recherche la fen�tre fille de classe "Chrome_AutocompleteEditView"
	// son texte contient l'URL de l'onglet en cours
	wURL=FindWindowEx(w,NULL,"Chrome_AutocompleteEditView",NULL);
	// 0.94 pour Chrome 13+, la fen�tre est de classe Chrome_OmniboxView !!
	// if (wURL==NULL) { TRACE((TRACE_ERROR,_F_,"FindWindowEx(Chrome_AutocompleteEditView)=NULL")); goto end; }
	if (wURL==NULL) 
	{ 
		TRACE((TRACE_INFO,_F_,"FindWindowEx(Chrome_AutocompleteEditView)=NULL"));
		wURL=FindWindowEx(w,NULL,"Chrome_OmniboxView",NULL);
		if (wURL==NULL) { TRACE((TRACE_ERROR,_F_,"FindWindowEx(Chrome_OmniboxView)=NULL")); goto end; }
	}
	TRACE((TRACE_DEBUG,_F_,"wURL=0x%08lx",wURL));
	// lecture taille URL (=longueur texte de la fen�tre trouv�e)
	len=SendMessage(wURL,WM_GETTEXTLENGTH,0,0);
	TRACE((TRACE_DEBUG,_F_,"lenURL=%d",len));
	if (len==0) { TRACE((TRACE_ERROR,_F_,"URL vide")); goto end; }
	// allocation pszURL
	pszURL=(char*)malloc(len+1);
	if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"malloc(%d)",len+1)); goto end; }
	// lecture texte fen�tre
	SendMessage(wURL,WM_GETTEXT,len+1,(LPARAM)pszURL);
	TRACE((TRACE_DEBUG,_F_,"URL=%s",pszURL));
end:
	TRACE((TRACE_LEAVE,_F_,"pszURL=0x%08lx",pszURL));
	return pszURL;
}

typedef struct 
{
	int iAction;
	HWND w;
} T_CHROMEFINDPOPUP;


//-----------------------------------------------------------------------------
// ChromeFindPopupProc()
//-----------------------------------------------------------------------------
// Enum�ration des fils de la fen�tre principale de chrome � la recherche
// d'une popup d'authentification
//-----------------------------------------------------------------------------
static int CALLBACK ChromeFindPopupProc(HWND w, LPARAM lp)
{
	int rc=TRUE;
	char szClassName[128+1]; 
	char szTitle[512+1];
	T_CHROMEFINDPOPUP *ptChromeFindPopup;

	ptChromeFindPopup=(T_CHROMEFINDPOPUP *)lp;

	GetClassName(w,szClassName,sizeof(szClassName));
	// ISSUE#77 : Chrome 20+ : Chrome_WidgetWin_0 -> Chrome_WidgetWin_
	if (strncmp(szClassName,"Chrome_WidgetWin_",17)==0)
	{
		GetWindowText(w,szTitle,sizeof(szTitle));
		TRACE((TRACE_DEBUG,_F_,"szTitle=%s iAction=%d",szTitle,ptChromeFindPopup->iAction));
		if (ptChromeFindPopup->iAction==-1) 
		{
			if (strcmp(szTitle,"Authentification requise")==0) rc=FALSE; // trouv�, on arr�te l'�num
		}
		else
		{
			if (swStringMatch(szTitle,gptActions[ptChromeFindPopup->iAction].szTitle)) rc=FALSE; // trouv�, on arr�te l'�num
		}
		if (!rc) 
		{ 
			TRACE((TRACE_DEBUG,_F_,"Popup trouvee w=0x%08lx",w));
			ptChromeFindPopup->w=w;
		}
	}
	return rc;
}

//-----------------------------------------------------------------------------
// GetChromePopupHandle()
//-----------------------------------------------------------------------------
// [in] w = handle de la fen�tre
// [in] iAction = config SSO concern�e, si -1, utilise les titres de popup en dur
// [rc] TRUE si la fen�tre h�berge une popup d'authentification Chrome 
//-----------------------------------------------------------------------------
HWND GetChromePopupHandle(HWND w,int iAction)
{
	TRACE((TRACE_ENTER,_F_, "w=0x%08lx iAction=%d",w,iAction));
	HWND rc=NULL;

	T_CHROMEFINDPOPUP tChromeFindPopup;
	tChromeFindPopup.iAction=iAction;
	tChromeFindPopup.w=NULL;
	
	EnumChildWindows(w,ChromeFindPopupProc,(LPARAM)&tChromeFindPopup);

	rc=tChromeFindPopup.w;

	TRACE((TRACE_LEAVE,_F_, "rc=0x%08lx",rc));
	return rc;
}	

