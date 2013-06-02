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
// swSSOIETools.cpp
//-----------------------------------------------------------------------------
// Fonctions utilitaires sp�cifiques IE
//-----------------------------------------------------------------------------

#include "stdafx.h"

static HWND gwW7PopupChild;

//***********************************************************************************
//                             FONCTIONS PRIVEES
//***********************************************************************************

//-----------------------------------------------------------------------------
// IEEnumChildProc()
//-----------------------------------------------------------------------------
// Enum�ration des fils de la fen�tre navigateur � la recherche 
// de la barre d'adresse pour v�rifier l'URL -> chang� en 0.83, voir dans swSSOWeb.cpp
//-----------------------------------------------------------------------------
// [in] lp = &psz
//-----------------------------------------------------------------------------
static int CALLBACK IEEnumChildProc(HWND w, LPARAM lp)
{
	UNREFERENCED_PARAMETER(lp);
	int rc=true; // true=continuer l'�num�ration
	char szClassName[128+1];
	DWORD dw;
	HRESULT hr;
	IHTMLDocument2 *pHTMLDocument2=NULL;
	BSTR bstrURL=NULL;
	BSTR bstrState=NULL;

	T_GETURL *ptGetURL=(T_GETURL *)lp;

	// 0.85B6 : je supprime la comparaison du titre car ne fonctionne pas quand le titre est trop long !
	//          (exemple copaindavant -> titre fen�tre windows tronqu� vs titre html entier, incomparable donc)

	GetClassName(w,szClassName,sizeof(szClassName));
	TRACE((TRACE_DEBUG,_F_,"w=0x%08lx class=%s",w,szClassName));
	if (strcmp(szClassName,"Internet Explorer_Server")!=0) goto end;

	// r�cup�ration pointeur sur le document HTML (interface IHTMLDocument2)
	SendMessageTimeout(w,guiHTMLGetObjectMsg,0L,0L,SMTO_ABORTIFHUNG,1000,&dw);
	hr=(*gpfObjectFromLresult)(dw,IID_IHTMLDocument2,0,(void**)&pHTMLDocument2);
	if (FAILED(hr)) 
	{
		TRACE((TRACE_ERROR,_F_,"gpfObjectFromLresult(%d,IID_IHTMLDocument2)=0x%08lx",dw,hr));
		goto end;
	}
   	TRACE((TRACE_DEBUG,_F_,"gpfObjectFromLresult(IID_IHTMLDocument2)=%08lx pHTMLDocument2=%08lx",hr,pHTMLDocument2));

	// 0.90 : ne commence pas tant que le document n'est pas charg�
	//        (uniquement dans le cas d'une simulation de frappe clavier ?)
	hr=pHTMLDocument2->get_readyState(&bstrState);
	if (FAILED(hr)) 
	{ 
		TRACE((TRACE_ERROR,_F_,"get_readyState=0x%08lx",hr)); 
		// ca n'a pas march�, pas grave, on continue quand m�me
	}
	else
	{
		TRACE((TRACE_INFO,_F_,"readyState=%S",bstrState)); 
		if (!CompareBSTRtoSZ(bstrState,"complete")) { rc=false; goto end; } // pas fini de charger, on arr�te
	}

	hr=pHTMLDocument2->get_URL(&bstrURL);
	if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"get_URL()=0x%08lx",hr)); goto end; }
	TRACE((TRACE_DEBUG,_F_,"get_URL()=%S",bstrURL));

	ptGetURL->pszURL=(char*)malloc(SysStringLen(bstrURL)+1);
	if (ptGetURL->pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"malloc(%d)=NULL",SysStringLen(bstrURL)+1)); goto end; }

	wsprintf(ptGetURL->pszURL,"%S",bstrURL);
	rc=false; // trouv� l'URL, on arrete l'enum

end:
	if (bstrState!=NULL) SysFreeString(bstrState);
	if (pHTMLDocument2!=NULL) pHTMLDocument2->Release();
	if (bstrURL!=NULL) SysFreeString(bstrURL);
	return rc;
}

//***********************************************************************************
//                             FONCTIONS PUBLIQUES
//***********************************************************************************

// ----------------------------------------------------------------------------------
// GetIEURL()
// ----------------------------------------------------------------------------------
// Retourne l'URL courante de la fen�tre IE
// ----------------------------------------------------------------------------------
// [out] pszURL (� lib�rer par l'appelant) ou NULL si erreur 
// ----------------------------------------------------------------------------------

char *GetIEURL(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	
	T_GETURL tGetURL;
	tGetURL.pszURL=NULL;

	EnumChildWindows(w,IEEnumChildProc,(LPARAM)&tGetURL);

	TRACE((TRACE_LEAVE,_F_,"pszURL=0x%08lx",tGetURL.pszURL));
	return tGetURL.pszURL;
}

// ----------------------------------------------------------------------------------
// GetMaxthonURL()
// ----------------------------------------------------------------------------------
// Retourne l'URL courante de la fen�tre MAXTHON
// ----------------------------------------------------------------------------------
// [out] pszURL (� lib�rer par l'appelant) ou NULL si erreur 
// ----------------------------------------------------------------------------------
char *GetMaxthonURL(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	HWND wMaxthonView;
	char *pszURL=NULL;
	
	// c'est un peu compliqu�, parce que la fen�tre de navigation (Internet Explorer_Server)
	// n'est pas fille de la fen�tre ppale de maxthon ! Il faut donc commencer trouver
	// la fen�tre m�re (facile elle a la classe Maxthon_View) puis appeler GetIEURL() avec 
	// le handle de cette fen�tre au lieu de celle re�ue en param�tre.
	wMaxthonView=FindWindow("Maxthon2_View",NULL);
	if (wMaxthonView==NULL)
	{
		TRACE((TRACE_ERROR,_F_,"Fen�tre de classe Maxthon2_View non trouv�e"));
		goto end;
	}
	pszURL=GetIEURL(wMaxthonView);
end:
	TRACE((TRACE_LEAVE,_F_,"pszURL=0x%08lx",pszURL));
	return pszURL;
}

// ----------------------------------------------------------------------------------
// GetIEWindowTitle()
// ----------------------------------------------------------------------------------
// Retourne le Window Title de IE (lecture en base de registre)
// La valeur retourn�e commence par espace-tiret-espace
// ----------------------------------------------------------------------------------
// NULL si non trouv�.
// ----------------------------------------------------------------------------------

char *GetIEWindowTitle(void)
{
	TRACE((TRACE_ENTER,_F_, ""));

	char *pszTitle=NULL;
	HKEY hKey=NULL;
	int rc;
	DWORD dwValueSize,dwValueType;

	rc=RegOpenKeyEx(HKEY_CURRENT_USER,"Software\\Microsoft\\Internet Explorer\\Main",0,KEY_READ,&hKey);
	if (rc!=ERROR_SUCCESS) { TRACE((TRACE_ERROR,_F_,"RegOpenKeyEx(UKCU)")); goto end;}

	pszTitle=(char*)malloc(256);
	if (pszTitle==NULL) goto end;
	pszTitle[0]=' ';
	pszTitle[1]='-';
	pszTitle[2]=' ';
	dwValueType=REG_SZ;
	dwValueSize=250;
	rc=RegQueryValueEx(hKey,"Window Title",NULL,&dwValueType,(LPBYTE)pszTitle+3,&dwValueSize);
	if (rc!=ERROR_SUCCESS) { free(pszTitle) ; pszTitle=NULL; goto end; }
end:
	if (hKey!=NULL) RegCloseKey(hKey);
	TRACE((TRACE_LEAVE,_F_,"pszTitle=0x%08lx",pszTitle));
	return pszTitle;
}

// ----------------------------------------------------------------------------------
// W7PopupEnumChildProc()
// ----------------------------------------------------------------------------------
// Enum�ration des fils de la popup W7 � la recherche de la fen�tre de classe
// DirectUIHWND qui contient les champs accessibles avec IAccessible
// ----------------------------------------------------------------------------------
static int CALLBACK W7PopupEnumChildProc(HWND w, LPARAM lp) 
{
	UNREFERENCED_PARAMETER(lp);
	TRACE((TRACE_ENTER,_F_, ""));

	char szClassName[128+1];
	int rc=TRUE;
	
	// v�rification de la classe. Si c'est pas lui, on continue l'enum
	GetClassName(w,szClassName,sizeof(szClassName));
	TRACE((TRACE_DEBUG,_F_,"Classe='%s'",szClassName));
	if (strcmp(szClassName,"DirectUIHWND")!=0) goto end;
	gwW7PopupChild=w;
	// si trouv�, pas la peine de continuer l'enum
	rc=FALSE;
end:
	TRACE((TRACE_LEAVE,_F_, ""));
	return rc; 
}

//-----------------------------------------------------------------------------
// GetW7PopupIAccessible()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
IAccessible *GetW7PopupIAccessible(HWND w)
{
	TRACE((TRACE_ENTER,_F_, "w=0x%08lx",w));
	HRESULT hr;
	IAccessible *pAccessible=NULL;

	gwW7PopupChild=NULL;
	EnumChildWindows(w,W7PopupEnumChildProc,0);
	if (gwW7PopupChild==NULL) goto end;
	TRACE((TRACE_DEBUG,_F_,"w=0x%08lx gwW7PopupChild=0x%08lx",w,gwW7PopupChild));

	// r�cup�re pointeur IAccessible
	hr=AccessibleObjectFromWindow(gwW7PopupChild,(DWORD)OBJID_CLIENT,IID_IAccessible,(void**)&pAccessible);
	if (FAILED(hr))
	{
		TRACE((TRACE_ERROR,_F_,"AccessibleObjectFromWindow()=0x%08lx",hr));
		goto end;
	}
end:
	TRACE((TRACE_LEAVE,_F_, "pAccessible=0x%08lx",pAccessible));
	return pAccessible;
}

// ----------------------------------------------------------------------------------
// GetW7PopupURL()
// ----------------------------------------------------------------------------------
// lecture de l'URL dans les popups IE dans W7
// ----------------------------------------------------------------------------------
// [rc] pointeur vers la chaine, � lib�rer par l'appelant. NULL si erreur
// ----------------------------------------------------------------------------------
char *GetW7PopupURL(HWND w) 
{
	TRACE((TRACE_ENTER,_F_, ""));

	HRESULT hr;
	IAccessible *pAccessible=NULL;
	VARIANT index;
	BSTR bstrName=NULL;
	int  bstrLen;
	char *pszURL=NULL;
	IAccessible *pChild=NULL;
	IDispatch *pIDispatch=NULL;
	long lCount,lURLChildPos;
	
	// r�cup pAccessible
	pAccessible=GetW7PopupIAccessible(w);
	if (pAccessible==NULL) { TRACE((TRACE_ERROR,_F_,"Impossible de trouver un pointeur iAccessible sur cette popup")); goto end; }

	// [ISSUE#78] compte les childs pour diff�rencier W7 et W8
	hr=pAccessible->get_accChildCount(&lCount);
	if (FAILED(hr))	{ TRACE((TRACE_ERROR,_F_,"get_accChildCount()=0x%08lx",hr)); goto end; }
	TRACE((TRACE_INFO,_F_,"get_accChildCount()=%ld",lCount));
	if (lCount==4) // l'URL est dans le 1er child
		lURLChildPos=1;
	else // l'URL est dans le 2�me child
		lURLChildPos=2;
	
	// R�cup pointeur sur le 1er child qui contient l'URL
	index.vt=VT_I4;
	index.lVal=lURLChildPos; // et oui, 1 et pas 0, car le 1er est 1 (le 0 veut dire "self") !
	hr=pAccessible->get_accChild(index,&pIDispatch);
	TRACE((TRACE_DEBUG,_F_,"pAccessible->get_accChild(%ld)=0x%08lx",index.lVal,hr));
	if (FAILED(hr)) goto end;
	hr =pIDispatch->QueryInterface(IID_IAccessible, (void**) &pChild);
	TRACE((TRACE_DEBUG,_F_,"QueryInterface(IID_IAccessible)=0x%08lx",hr));
	if (FAILED(hr)) goto end;

	// Lecture du contenu du child
	index.vt=VT_I4;
	index.lVal=0; // cette fois, 0, oui, car c'est le nom de l'objet lui-m�me que l'on cherche.
	hr=pChild->get_accName(index,&bstrName);
	if (hr!=S_OK) 
	{
		TRACE((TRACE_ERROR,_F_,"pChild->get_accName(%ld)=0x%08lx",index.lVal,hr));
		goto end;
	}
	TRACE((TRACE_DEBUG,_F_,"pChild->get_accName(%ld)='%S'",index.lVal,bstrName));

	bstrLen=SysStringLen(bstrName);
	pszURL=(char*)malloc(bstrLen+1);
	if (pszURL==NULL) 
	{
		TRACE((TRACE_ERROR,_F_,"malloc(%ld)",bstrLen+1));
		goto end; 
	}
	sprintf_s(pszURL,bstrLen+1,"%S",bstrName);
	TRACE((TRACE_DEBUG,_F_,"pszURL='%s'",pszURL));
		
end:
	if (pIDispatch!=NULL) { pIDispatch->Release(); pIDispatch=NULL; }
	if (pChild!=NULL) { pChild->Release(); pChild=NULL; }

	SysFreeString(bstrName);
	if (pAccessible!=NULL) pAccessible->Release();
	TRACE((TRACE_LEAVE,_F_,"pszURL=0x%08lx",pszURL));
	return pszURL;
}

