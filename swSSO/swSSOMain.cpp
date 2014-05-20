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
// swSSOMain.cpp
//-----------------------------------------------------------------------------
// Point d'entr�e + boucle de recherche de fen�tre � SSOiser
//-----------------------------------------------------------------------------

#include "stdafx.h"
#include "ISimpleDOMNode_i.c"
#include "ISimpleDOMDocument_i.c"

// Un peu de globales...
const char gcszCurrentVersion[]="100";	// 082 = 0.82
const char gcszCurrentBeta[]="0000";	// 0851 = 085 beta 1, 0000 pas de beta

static HWND gwMain=NULL;

HINSTANCE ghInstance;
HRESULT   ghrCoIni=E_FAIL;	 // code retour CoInitialize()
bool gbSSOActif=TRUE;	 // Etat swSSO : actif / d�sactiv�	
int giPwdProtection; // Protection des mots de passe : PP_ENCRYPTED | PP_WINDOWS

T_ACTION *gptActions;  // tableau d'actions
int giNbActions;		// nb d'actions dans le tableau

int giBadPwdCount;		// nb de saisies erron�es de mdp cons�cutives
HWND gwAskPwd=NULL ;       // anti r�-entrance fen�tre saisie pwd

HCRYPTKEY ghKey1=NULL;
HCRYPTKEY ghKey2=NULL;

// Icones et pointeur souris
HICON ghIconAltTab=NULL;
HICON ghIconSystrayActive=NULL;
HICON ghIconSystrayInactive=NULL;
HICON ghIconLoupe=NULL;
HANDLE ghLogo=NULL;
HANDLE ghLogoFondBlanc50=NULL;
HANDLE ghLogoFondBlanc90=NULL;
HCURSOR ghCursorHand=NULL; 
HCURSOR ghCursorWait=NULL;
HIMAGELIST ghImageList=NULL;

HFONT ghBoldFont=NULL;

// Compteurs pour les stats
UINT guiNbWEBSSO;
UINT guiNbWINSSO;
UINT guiNbPOPSSO;
UINT guiNbWindows;
UINT guiNbVisibleWindows;

// 0.76
BOOL gbRememberOnThisComputer=FALSE;
BOOL gbRecoveryRunning=FALSE;

BOOL gbRegisterSessionNotification=FALSE;
UINT guiLaunchAppMsg;
UINT guiConnectAppMsg;

int giTimer=0;
static int giRegisterSessionNotificationTimer=0;
static int giNbRegisterSessionNotificationTries=0;
static int giRefreshTimer=10;

int giOSVersion=OS_WINDOWS_OTHER;
int giOSBits=OS_32;

SID *gpSid=NULL;
char *gpszRDN=NULL;
char gszComputerName[MAX_COMPUTERNAME_LENGTH+1]="";
char gszUserName[UNLEN+1]="";


char szPwdMigration093[LEN_PWD+1]=""; // stockage temporaire du mot de passe pour migration 0.93, effac� tout de suite apr�s.

// oblig� de changer aussi le mot de passe statique pour l'encodage simple car il �tait trop long d'un caract�re !!!
static const char gcszStaticPwd092[]="1;*$pmo�_-'-�(-�e+==�&&*/}epaw&�1KijahBv15*��%?./�q"; 
static const char gcszStaticPwd093[]="*�Al43HJj8]_3za;?,!��AHI3le!ma!/sw\aw+==,;/�A6YhPM"; 

// 0.91 : pour choix de config (fen�tre ChooseConfig)
typedef struct
{
	int iNbConfigs;
	int tabConfigs[NB_MAX_APPLICATIONS];
	int iConfig;
} T_CHOOSE_CONFIG;

T_LAST_DETECT gTabLastDetect[MAX_NB_LAST_DETECT]; // 0.93 liste des fen�tres d�tect�es sur cette action

HANDLE ghPwdChangeEvent=NULL; // 0.96

int giLastApplication=-1;
SYSTEMTIME gLastLoginTime; // ISSUE#106

//*****************************************************************************
//                             FONCTIONS PRIVEES
//*****************************************************************************

static int CALLBACK EnumWindowsProc(HWND w, LPARAM lp);

//-----------------------------------------------------------------------------
// TimerProc()
//-----------------------------------------------------------------------------
// L'appel � cette fonction est d�clench� toutes les 500 ms par le timer.
// C'est cette fonction qui lance l'�num�ration des fen�tres
//-----------------------------------------------------------------------------
static void CALLBACK TimerProc(HWND w,UINT msg,UINT idEvent,DWORD dwTime)
{
	UNREFERENCED_PARAMETER(dwTime);
	UNREFERENCED_PARAMETER(idEvent);
	UNREFERENCED_PARAMETER(msg);
	UNREFERENCED_PARAMETER(w);

	// TODO : � d�placer dans un autre timer pour le faire moins souvent ?
	DWORD dw=WaitForSingleObject(ghPwdChangeEvent,0);
	TRACE((TRACE_DEBUG,_F_,"WaitForSingleObject=0x%08lx",dw));
	if (dw==WAIT_OBJECT_0)
	{
		TRACE((TRACE_INFO,_F_,"WaitForSingleObject : swsso-pwdchange event received"));
		if (ChangeWindowsPwd()==0)
			MessageBox(w,GetString(IDS_CHANGE_PWD_OK),"swSSO",MB_OK | MB_ICONINFORMATION);
		else
			MessageBox(w,GetString(IDS_CHANGE_PWD_FAILED),"swSSO",MB_OK | MB_ICONEXCLAMATION);
	}

	if (gbSSOActif) 
	{
		guiNbWindows=0;
		guiNbVisibleWindows=0;
		// 0.93 : avant de commencer l'�num�ration, d�taggue toutes les fen�tres
		LastDetect_UntagAllWindows();
		// enum�ration des fen�tres
		EnumWindows(EnumWindowsProc,0);
		// 0.93 : apr�s l'�num�ration, efface toutes les fen�tres non taggu�s
		//        cela permet de supprimer de la liste des derniers SSO r�alis�s les fen�tres 
		//        qui ne sont plus � l'�cran
		LastDetect_RemoveUntaggedWindows();
		// 0.82 : r�armement du timer (si d�sarm�) une fois que l'�num�ration est termin�e 
		//        (plut�t que dans la WindowsProc -> au moins on est s�r que c'est toujours fait)
		LaunchTimer();
	}
}

//-----------------------------------------------------------------------------
// RegisterSessionNotificationTimerProc()
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void CALLBACK RegisterSessionNotificationTimerProc(HWND w,UINT msg,UINT idEvent,DWORD dwTime)
{
	UNREFERENCED_PARAMETER(dwTime);
	UNREFERENCED_PARAMETER(idEvent);
	UNREFERENCED_PARAMETER(msg);
	UNREFERENCED_PARAMETER(w);
	
	if (!gbRegisterSessionNotification) 
	{
		gbRegisterSessionNotification=WTSRegisterSessionNotification(gwMain,NOTIFY_FOR_THIS_SESSION);
		if (gbRegisterSessionNotification) // c'est bon, enfin !
		{
			TRACE((TRACE_ERROR,_F_,"WTSRegisterSessionNotification() -> OK, au bout de %d tentatives !",giNbRegisterSessionNotificationTries));
			KillTimer(NULL,giRegisterSessionNotificationTimer);
			giRegisterSessionNotificationTimer=0;
		}
		else
		{
			TRACE((TRACE_ERROR,_F_,"WTSRegisterSessionNotification()=%ld [REESSAI DANS 15 SECONDES]",GetLastError()));
			giNbRegisterSessionNotificationTries++;
			if (giNbRegisterSessionNotificationTries>20) // 20 fois 15 secondes = 5 minutes, on arr�te, tant pis !
			{
				TRACE((TRACE_ERROR,_F_,"WTSRegisterSessionNotification n'a pas reussi : PLUS DE REESSAI"));
				KillTimer(NULL,giRegisterSessionNotificationTimer);
				giRegisterSessionNotificationTimer=0;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// LauchTimer()
//-----------------------------------------------------------------------------
// Lance le timer si pas d�j� lanc�
//-----------------------------------------------------------------------------
int LaunchTimer(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;

	if (giTimer==0) 
	{
		giTimer=SetTimer(NULL,0,500,TimerProc);
		if (giTimer==0) 
		{
#ifdef TRACE_ACTIVEES
			DWORD err=GetLastError();
			TRACE((TRACE_ERROR,_F_,"SetTimer() : %ld (0x%08lx)",err,err));
#endif
			goto end;
		}
	}
	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// KBSimSSO()
//-----------------------------------------------------------------------------
// R�alisation du SSO de l'action pass�e en param�tre par simulation de frappe clavier
//-----------------------------------------------------------------------------
int KBSimSSO(HWND w, int iAction)
{
	TRACE((TRACE_ENTER,_F_, "iAction=%d",iAction));
	int rc=-1;
	char szDecryptedPassword[LEN_PWD+1];

	// d�chiffrement du champ mot de passe
	if ((*gptActions[iAction].szPwdEncryptedValue!=0)) // TODO -> CODE A REVOIR PLUS TARD (PAS BEAU SUITE A ISSUE#83)
	{
		char *pszPassword=swCryptDecryptString(gptActions[iAction].szPwdEncryptedValue,ghKey1);
		if (pszPassword!=NULL) 
		{
			strcpy_s(szDecryptedPassword,sizeof(szDecryptedPassword),pszPassword);
			SecureZeroMemory(pszPassword,strlen(pszPassword));
			free(pszPassword);
		}
	}
	else
	{
		strcpy_s(szDecryptedPassword,sizeof(szDecryptedPassword),gptActions[iAction].szPwdEncryptedValue);
	}

	// analyse et ex�cution de la simulation de frappe clavier - on passe tous les param�tres, KBSimEx se d�brouille.
	// nouveau en 0.91 : flag NOFOCUS permet de ne pas mettre le focus syst�matiquement sur la fen�tre
	// (n�cessaire avec Terminal Server, sinon perte du focus sur les champs login/pwd)
	if (_strnicmp(gptActions[iAction].szKBSim,"[NOFOCUS]",strlen("[NOFOCUS]"))==0) w=NULL;
	KBSimEx(w,gptActions[iAction].szKBSim,gptActions[iAction].szId1Value,
										  gptActions[iAction].szId2Value,
										  gptActions[iAction].szId3Value,
										  gptActions[iAction].szId4Value,
										  szDecryptedPassword);

	SecureZeroMemory(szDecryptedPassword,sizeof(szDecryptedPassword));

	rc=0;
//end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// ChooseConfigInitDialog()
//-----------------------------------------------------------------------------
// InitDialog DialogProc de la fen�tre de choix de config en cas de multi-comptes
//-----------------------------------------------------------------------------
void ChooseConfigInitDialog(HWND w,LPARAM lp)
{
	TRACE((TRACE_ENTER,_F_, ""));

	T_CHOOSE_CONFIG *lpConfigs=(T_CHOOSE_CONFIG*)lp;
	if (lpConfigs==NULL) goto end;
	HWND wLV;
	LVCOLUMN lvc;
	LVITEM   lvi;
	int i,pos;

	// conserve le lp pour la suite
	SetWindowLong(w,DWL_USER,lp);

	// icone ALT-TAB
	SendMessage(w,WM_SETICON,ICON_BIG,(LPARAM)ghIconAltTab);
	SendMessage(w,WM_SETICON,ICON_SMALL,(LPARAM)ghIconSystrayActive); 

	// init de la listview
	// listview
	wLV=GetDlgItem(w,LV_CONFIGS);
	// listview - cr�ation colonnes 
	lvc.mask=LVCF_WIDTH | LVCF_TEXT ;
	lvc.cx=218;
	lvc.pszText="Application";
	ListView_InsertColumn(wLV,0,&lvc);

	lvc.mask=LVCF_WIDTH | LVCF_TEXT;
	lvc.cx=200;
	lvc.pszText="Identifiant";
	ListView_InsertColumn(wLV,1,&lvc);

	// listview - styles
	ListView_SetExtendedListViewStyle(wLV,ListView_GetExtendedListViewStyle(wLV)|LVS_EX_FULLROWSELECT);

	// listview - remplissage
	for (i=0;i<lpConfigs->iNbConfigs;i++)
	{
		lvi.mask=LVIF_TEXT | LVIF_PARAM;
		lvi.iItem=i;
		lvi.iSubItem=0;
		lvi.pszText=gptActions[lpConfigs->tabConfigs[i]].szApplication;
		lvi.lParam=lpConfigs->tabConfigs[i];		// index de la config dans table g�n�rale gptActions
		pos=ListView_InsertItem(GetDlgItem(w,LV_CONFIGS),&lvi);
		
		if (pos!=-1)
		{
			lvi.mask=LVIF_TEXT;
			lvi.iItem=pos;
			lvi.iSubItem=1;
			lvi.pszText=GetComputedValue(gptActions[lpConfigs->tabConfigs[i]].szId1Value);
			ListView_SetItem(GetDlgItem(w,LV_CONFIGS),&lvi);
		}
	}
	// s�lection par d�faut
	SendMessage(GetDlgItem(w,CB_TYPE),CB_SETCURSEL,0,0);
	ListView_SetItemState(GetDlgItem(w,LV_CONFIGS),0,LVIS_SELECTED | LVIS_FOCUSED , LVIS_SELECTED | LVIS_FOCUSED);
	
	// titre en gras
	SetTextBold(w,TX_FRAME);
	// centrage
	SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE | SWP_NOMOVE);
	MACRO_SET_SEPARATOR;
	// magouille supr�me : pour g�rer les cas rares dans lesquels la peinture du bandeau & logo se fait mal
	// on active un timer d'une seconde qui ex�cutera un invalidaterect pour forcer la peinture
	if (giRefreshTimer==giTimer) giRefreshTimer=11;
	SetTimer(w,giRefreshTimer,200,NULL);

end:
	TRACE((TRACE_LEAVE,_F_, ""));
}
//-----------------------------------------------------------------------------
// ChooseConfigOnOK()
//-----------------------------------------------------------------------------
// Appel�e lorsque l'utilisateur clique sur OK ou double-clique sur un item de la listview
//-----------------------------------------------------------------------------
int ChooseConfigOnOK(HWND w)
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;
	int iSelectedItem=ListView_GetNextItem(GetDlgItem(w,LV_CONFIGS),-1,LVNI_SELECTED);

	if (iSelectedItem==-1) goto end;
	// R�cup�re le lparam de l'item s�lectionn�. 
	// Le lparam contient l'index de la config dans la table g�n�rale gptActions : lpConfigs->tabConfigs[i]
	LVITEM lvi;
	lvi.mask=LVIF_PARAM ;
	lvi.iItem=iSelectedItem;
	ListView_GetItem(GetDlgItem(w,LV_CONFIGS),&lvi);
	// R�cup�re le lparam de la fen�tre qui contient le pointeur vers la structure configs
	T_CHOOSE_CONFIG *lpConfigs=(T_CHOOSE_CONFIG *)GetWindowLong(w,DWL_USER);
	// Stocke l'index de la configuration choisie par l'utilisateur
	lpConfigs->iConfig=lvi.lParam;
	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// ChooseConfigDialogProc()
//-----------------------------------------------------------------------------
// DialogProc de la fen�tre de choix de config en cas de multi-comptes
//-----------------------------------------------------------------------------
static int CALLBACK ChooseConfigDialogProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
	int rc=FALSE;
	switch (msg)
	{
		case WM_INITDIALOG:
			TRACE((TRACE_DEBUG,_F_, "WM_INITDIALOG"));
			ChooseConfigInitDialog(w,lp);
			break;
		case WM_TIMER:
			TRACE((TRACE_INFO,_F_,"WM_TIMER (refresh)"));
			if (giRefreshTimer==(int)wp) 
			{
				KillTimer(w,giRefreshTimer);
				InvalidateRect(w,NULL,FALSE);
				SetForegroundWindow(w); 
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wp))
			{
				case IDOK:
					if (ChooseConfigOnOK(w)==0) EndDialog(w,IDOK);
					break;
				case IDCANCEL:
					EndDialog(w,IDCANCEL);
					break;
			}
			break;
		case WM_NOTIFY:
			switch (((NMHDR FAR *)lp)->code) 
			{
				case NM_DBLCLK: 
					if (ChooseConfigOnOK(w)==0) EndDialog(w,IDOK);
					break;
			}
			break;
		case WM_CTLCOLORSTATIC:
			int ctrlID;
			ctrlID=GetDlgCtrlID((HWND)lp);
			switch(ctrlID)
			{
				case TX_FRAME:
					SetBkMode((HDC)wp,TRANSPARENT);
					rc=(int)GetStockObject(HOLLOW_BRUSH);
					break;
			}
			break;
		case WM_HELP:
			Help();
			break;
		case WM_PAINT:
			DrawLogoBar(w,50,ghLogoFondBlanc50);
			rc=TRUE;
			break;
		case WM_ACTIVATE:
			InvalidateRect(w,NULL,FALSE);
			break;
	}
	return rc;
}

//-----------------------------------------------------------------------------
// ChooseConfig()
//-----------------------------------------------------------------------------
// Propose une fen�tre de choix de la configuration � utiliser si plusieurs
// matchent (=multi-comptes !)
//-----------------------------------------------------------------------------
int ChooseConfig(HWND w,int *piAction)
{
	TRACE((TRACE_ENTER,_F_, "iAction=%d",*piAction));
	int rc=-1;
	int i,j;
	T_CHOOSE_CONFIG config;
	config.iNbConfigs=1;			// nombre de configs qui matchent
	config.tabConfigs[0]=*piAction;	// premi�re config 
	config.iConfig=*piAction;		// premi�re config

	// Cherche si d'autres configurations ACTIVES ont les m�mes caract�ristiques :
	// - iType
	// - szTitle
	// - szURL
	// - szId1Name...szId4Name
	// - id2Type...id4Type
	// - szPwdName
	for (i=0;i<giNbActions;i++)
	{
		if (i==*piAction) continue;
		if ((gptActions[i].bActive) &&
			(gptActions[*piAction].iType==gptActions[i].iType) &&
			(gptActions[*piAction].id2Type==gptActions[i].id2Type) &&
			(gptActions[*piAction].id3Type==gptActions[i].id3Type) &&
			(gptActions[*piAction].id4Type==gptActions[i].id4Type) &&
			(strcmp(gptActions[*piAction].szTitle,gptActions[i].szTitle)==0) &&
			(strcmp(gptActions[*piAction].szURL,gptActions[i].szURL)==0) &&
			(strcmp(gptActions[*piAction].szId1Name,gptActions[i].szId1Name)==0) &&
			(strcmp(gptActions[*piAction].szId2Name,gptActions[i].szId2Name)==0) &&
			(strcmp(gptActions[*piAction].szId3Name,gptActions[i].szId3Name)==0) &&
			(strcmp(gptActions[*piAction].szId4Name,gptActions[i].szId4Name)==0) &&
			(strcmp(gptActions[*piAction].szPwdName,gptActions[i].szPwdName)==0))
		{
			config.tabConfigs[config.iNbConfigs]=i;
			config.iNbConfigs++;
		}
	}
#ifdef TRACES_ACTIVEES
	TRACE((TRACE_INFO,_F_,"Liste des configurations possibles :"));
	for (i=0;i<config.iNbConfigs;i++) 
	{
		TRACE((TRACE_INFO,_F_,"%d : %d",i,config.tabConfigs[i]));
	}
#endif
	// si aucune config trouv�e autre que celle initiale, on sort et on fait le SSO avec cette config
	if (config.iNbConfigs==1) { giLaunchedApp=-1; rc=0; goto end; }

	// avant d'afficher la fen�tre de choix des configs, on va regarder si l'une des configs trouv�es
	// correspond � une application qui vient d'�tre lanc�e par LaunchSelectedApp(). 
	// si c'est le cas, inutile de proposer � l'utilisateur de choisir, on choisit pour lui ! (�a c'est vraiment g�nial)
	TRACE((TRACE_DEBUG,_F_,"giLaunchedApp=%d",giLaunchedApp));
	if (giLaunchedApp!=-1)
	{
		for (i=0;i<config.iNbConfigs;i++) 
		{
			if (config.tabConfigs[i]==giLaunchedApp) // trouv�, on utilisera celle-l�, on sort.
			{
				TRACE((TRACE_INFO,_F_,"Lanc� depuis LaunchPad action %d",config.tabConfigs[i]));
				*piAction=config.tabConfigs[i];
				giLaunchedApp=-1;
				rc=0;
				// repositionne tLastSSO et wLastSSO des actions qui ne seront pas trait�es
				// l'action trait�e sera mise � jour au moment du SSO
				for (j=0;j<config.iNbConfigs;j++)
				{
					if (config.tabConfigs[j]==*piAction) continue; // tout sauf celle choisie par l'utilisateur
					time(&gptActions[config.tabConfigs[j]].tLastSSO);
					gptActions[config.tabConfigs[j]].wLastSSO=w;
				}
				goto end;
			}
		}
	}
	// pas trouv�, on oublie, c'�tait une mauvaise piste
	giLaunchedApp=-1;

	// affiche la fen�tre de choix des configs
	if (DialogBoxParam(ghInstance,MAKEINTRESOURCE(IDD_CHOOSE_CONFIG),w,ChooseConfigDialogProc,LPARAM(&config))!=IDOK) 
	{
		// l'utilisateur a annul�, on marque tout le monde en WAIT_ONE_MINUTE comme �a on ne fait pas le SSO tout de suite
		// repositionne tLastDetect et wLastDetect 
		for (i=0;i<config.iNbConfigs;i++) 
		{
			//gptActions[config.tabConfigs[i]].wLastDetect=w;
			//time(&gptActions[config.tabConfigs[i]].tLastDetect);
			time(&gptActions[config.tabConfigs[i]].tLastSSO);
			gptActions[config.tabConfigs[i]].wLastSSO=w;
			gptActions[config.tabConfigs[i]].iWaitFor=WAIT_ONE_MINUTE;
			TRACE((TRACE_DEBUG,_F_,"gptActions(%d).iWaitFor=WAIT_ONE_MINUTE",config.tabConfigs[i]));
		}
		goto end;
	}
	// retourne l'action qui a �t� choisie par l'utilisateur
	TRACE((TRACE_INFO,_F_,"Choix de l'utilisateur : action %d",config.iConfig));
	*piAction=config.iConfig;
	// repositionne tLastSSO et wLastSSO des actions qui ne seront pas trait�es
	// l'action trait�e sera mise � jour au moment du SSO
	for (i=0;i<config.iNbConfigs;i++)
	{
		if (config.tabConfigs[i]==*piAction) continue; // tout sauf celle choisie par l'utilisateur
		time(&gptActions[config.tabConfigs[i]].tLastSSO);
		gptActions[config.tabConfigs[i]].wLastSSO=w;
	}
	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d iAction=%d",rc,*piAction));
	return rc;
}

//-----------------------------------------------------------------------------
// EnumWindowsProc()
//-----------------------------------------------------------------------------
// Callback d'�num�ration de fen�tres pr�sentes sur le bureau et d�clenchement
// du SSO le cas �ch�ant
//-----------------------------------------------------------------------------
// [rc] : toujours TRUE (continuer l'�num�ration)
//-----------------------------------------------------------------------------
static int CALLBACK EnumWindowsProc(HWND w, LPARAM lp)
{
	UNREFERENCED_PARAMETER(lp);
	int i;
	time_t tNow,tLastSSOOnThisWindow;
	bool bDoSSO;
	char *pszURL=NULL;
	char *pszURL2=NULL;
	char *pszURLBar=NULL;
	int iPopupType=POPUP_NONE;
	char szClassName[128+1]; // pour stockage nom de classe de la fen�tre
	char szTitre[255+1];	  // pour stockage titre de fen�tre
	int rc;
	char szMsg[512+1];
	int lenTitle;
	int iBrowser=BROWSER_NONE;
	HWND wChromePopup=NULL;
	HWND wReal=NULL; // bidouille pour Chrome
	
	guiNbWindows++;
	// 0.93B4 : fen�tres �ventuellement exclues 
	if (IsExcluded(w)) goto end;
	// lecture du titre de la fen�tre
	GetWindowText(w,szTitre,sizeof(szTitre));
	if (*szTitre==0) goto end; // si fen�tre sans titre, on passe ! <optim>
	if (!IsWindowVisible(w)) goto end; // fen�tre cach�e, on passe ! <optim+compteur>
	guiNbVisibleWindows++;

	// 0.93 : marque la fen�tre comme toujours pr�sente � l'�cran dans liste des derniers SSO r�alis�s
	LastDetect_TagWindow(w); 

	// lecture de la classe de la fen�tre (pour reconnaitre IE et Firefox ensuite)
	GetClassName(w,szClassName,sizeof(szClassName));
	
	TRACE((TRACE_DEBUG,_F_,"szTitre=%s",szTitre));

	// boucle dans la liste d'action pour voir si la fen�tre correspond � une config connue
    for (i=0;i<giNbActions;i++)
    {
    	if (!gptActions[i].bActive) goto suite; // action d�sactiv�e
		if (!gptActions[i].bSaved) { TRACE((TRACE_INFO,_F_,"action %d non enregistr�e => SSO non ex�cut�",i)); goto suite; } // 0.93B6 ISSUE#55
		if (gptActions[i].iType==UNK) goto suite; // 0.85 : ne traite pas si type inconnu
		
		// ESSAI POPUP AUTHENT CHROME
		// Avant de comparer le titre, v�rifier si ce n'est pas une popup Chrome
		// En effet, avec Chrome, le titre est soit vide, soit correspond au site visit� pr�cedemment, donc non repr�sentatif
		wChromePopup=NULL;
		// ISSUE#77 : Chrome 20+ : Chrome_WidgetWin_0 -> Chrome_WidgetWin_
		if (gptActions[i].iType==POPSSO && strncmp(szClassName,"Chrome_WidgetWin_",17)==0) wChromePopup=GetChromePopupHandle(w,i);
		if (wChromePopup!=NULL)
		{
			iPopupType=POPUP_CHROME;
			TRACE((TRACE_DEBUG,_F_,"POPUP_CHROME !"));
		}
		else
		{
			lenTitle=strlen(gptActions[i].szTitle);
			if (lenTitle==0) goto suite; // 0.85 : ne traite pas si pas de titre (sauf pour popup chrome)
    		// 0.80 : on compare sur le d�but du titre et non plus sur une partie du titre 
			// if (strstr(szTitre,gptActions[i].szTitle)==NULL) goto suite; // fen�tre inconnue...
			// 0.92B3 : �volution de la comparaison du titre pour prendre en compte le joker *
			// if (_strnicmp(szTitre,gptActions[i].szTitle,lenTitle)!=0) goto suite; // fen�tre inconnue...
			if (!swStringMatch(szTitre,gptActions[i].szTitle)) goto suite;
		}
		// A ce stade, on a associ� le titre (uniquement le titre, c'est � dire qu'on n'a pas encore v�rifi�
		// que l'URL �tait correcte) � une action.
		TRACE((TRACE_INFO,_F_,"======= Fen�tre handle 0x%08lx titre connu (%s) classe (%s) action (%d) type (%d) � v�rifier maintenant",w,szTitre,szClassName,i,gptActions[i].iType));
    	if (gptActions[i].iType==POPSSO) 
    	{
			if (strcmp(szClassName,gcszMozillaDialogClassName)==0) // POPUP FIREFOX
			{
				pszURL=GetFirefoxPopupURL(w);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL popup firefox non trouvee")); goto suite; }
				iPopupType=POPUP_FIREFOX;
			}
			else if (strcmp(szTitre,"S�curit� de Windows")==0 ||
					 strcmp(szTitre,"Windows Security")==0) // POPUP IE8 SUR W7 [ISSUE#5] (FR et US uniquement... pas beau)
			{
				pszURL=GetW7PopupURL(w);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL popup W7 non trouvee")); goto suite; }
				iPopupType=POPUP_W7;
			}
			else if (iPopupType==POPUP_CHROME)
			{
				pszURL=GetChromeURL(w);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL popup Chrome non trouvee")); goto suite; }
			}
			else
			{
				iPopupType=POPUP_XP;
			}
			// il faut v�rifier que l'URL matche pour FIREFOX, W7 et CHROME car elles ont toutes le meme titre !
			// Popup IE sous XP, pas la peine, titre distinctif
			if (iPopupType==POPUP_FIREFOX || iPopupType==POPUP_W7 || iPopupType==POPUP_CHROME)
			{
				TRACE((TRACE_INFO,_F_,"URL trouvee  = %s",pszURL));
				TRACE((TRACE_INFO,_F_,"URL attendue = %s",gptActions[i].szURL));
				// ISSUE#87 : l'URL dans la config peut avoir la forme : le site www demande*|http://www
				int len=strlen(gptActions[i].szURL);
				pszURL2=(char*)malloc(len+1);
				if (*pszURL2==NULL) { TRACE((TRACE_ERROR,_F_,"malloc(%ld)",len+1)); goto end; }
				memcpy(pszURL2,gptActions[i].szURL,len+1);
				char *p=strchr(pszURL2,'|');
				if (p!=NULL)
				{
					*p=0;
					p++;
					if (swCheckBrowserURL(iPopupType,p)!=0)
					{
						TRACE((TRACE_INFO,_F_,"Impossible de v�rifier l'URL de la barre d'URL... on ne fait pas le SSO !"));
						goto suite;// URL popup authentification inconnue
					}
				}
				// si p!=null c'est qu'on a trouv� un | et une URL derri�re, il faut la v�rifier
				// ce test ne change pas, sauf qu'il porte sur pszURL2 pour couvrir les 2 cas (avec ou sans |)
				// 0.92B6 : utilise le swStringMatch, permet donc d'utiliser * en d�but de cha�ne
				if (!swStringMatch(pszURL,pszURL2))
				{
					TRACE((TRACE_DEBUG,_F_,"Titre connu, mais URL ne matche pas, on passe !"));
					goto suite;// URL popup authentification inconnue
				}
			}
		}
		else if (gptActions[i].iType==WINSSO && *(gptActions[i].szURL)!=0) // fen�tre Windows avec URL, il faut v�rifier que l'URL matche
		{
			if (!CheckURL(w,i))
			{
				TRACE((TRACE_DEBUG,_F_,"Titre connu, mais URL ne matche pas, on passe !"));
				goto suite;// URL popup authentification inconnue
			}
		}
		else if (gptActions[i].iType==WEBSSO || gptActions[i].iType==WEBPWD || gptActions[i].iType==XEBSSO) // action WEB, il faut v�rifier que l'URL matche
		{
			if (strcmp(szClassName,"IEFrame")==0 || // IE
				strcmp(szClassName,"#32770")==0 ||  // Network Connect
				strcmp(szClassName,"rctrl_renwnd32")==0 || // Outlook 97 � 2003 (au moins, � v�rifier pour 2007)
				strcmp(szClassName,"OpusApp")==0 || // Word 97 � 2003 (au moins, � v�rifier pour 2007)
				strcmp(szClassName,"ExploreWClass")==0 || strcmp(szClassName,"CabinetWClass")==0) // Explorateur Windows
			{
				iBrowser=BROWSER_IE;
				pszURL=GetIEURL(w,TRUE);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL IE non trouvee : on passe !")); goto suite; }
			}
			else if (strcmp(szClassName,gcszMozillaUIClassName)==0) // FF3
			{
				iBrowser=BROWSER_FIREFOX3;
				pszURL=GetFirefoxURL(w,FALSE,NULL,BROWSER_FIREFOX3,TRUE);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL Firefox 3- non trouvee : on passe !")); goto suite; }
			}
			else if (strcmp(szClassName,gcszMozillaClassName)==0) // FF4
			{
				iBrowser=BROWSER_FIREFOX4;
				pszURL=GetFirefoxURL(w,FALSE,NULL,BROWSER_FIREFOX4,TRUE);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL Firefox 4+ non trouvee : on passe !")); goto suite; }
			}
			else if (strcmp(szClassName,"Maxthon2_Frame")==0) // Maxthon
			{
				iBrowser=BROWSER_MAXTHON;
				if (gptActions[i].iType==XEBSSO) 
				{
					TRACE((TRACE_ERROR,_F_,"Nouvelle methode de configuration non supportee avec Maxthon"));
					goto suite;
				}
				pszURL=GetMaxthonURL();
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL Maxthon non trouvee : on passe !")); goto suite; }
			}
			else if (strncmp(szClassName,"Chrome_WidgetWin_",17)==0) // ISSUE#77 : Chrome 20+ : Chrome_WidgetWin_0 -> Chrome_WidgetWin_
			{
				iBrowser=BROWSER_CHROME;
				/*if (gptActions[i].iType!=XEBSSO) 
				{
					TRACE((TRACE_ERROR,_F_,"Ancienne methode de configuration non supportee avec Chrome"));
					goto suite;
				}*/
				pszURL=GetChromeURL(w);
				if (pszURL==NULL) { TRACE((TRACE_ERROR,_F_,"URL Chrome non trouvee : on passe !")); goto suite; }
			}
			else // autre ??
			{
				TRACE((TRACE_ERROR,_F_,"Unknown class : %s !",szClassName)); goto suite; 
			}
			TRACE((TRACE_INFO,_F_,"URL trouvee  = %s",pszURL));
			TRACE((TRACE_INFO,_F_,"URL attendue = %s",gptActions[i].szURL));

			// 0.92B6 : utilise le swStringMatch, permet donc d'utiliser * en d�but de cha�ne
			// if (!swStringMatch(pszURL,gptActions[i].szURL))
			if (!swURLMatch(pszURL,gptActions[i].szURL))
			{
				TRACE((TRACE_DEBUG,_F_,"Titre connu, mais URL ne matche pas, on passe !"));
				goto suite;// URL popup authentification inconnue
			}
		}
		// ARRIVE ICI, ON SAIT QUE LA FENETRE EST BIEN ASSOCIEE A L'ACTION i ET QU'IL FAUT FAIRE LE SSO...
		// ... SAUF SI DEJA FAIT RECEMMENT !
		TRACE((TRACE_INFO,_F_,"======= Fen�tre v�rif�e OK, on v�rifie si elle a �t� trait�e r�cemment"));
		TRACE((TRACE_DEBUG,_F_,"Fenetre      ='%s'",szTitre));
		TRACE((TRACE_DEBUG,_F_,"Application  ='%s'",gptActions[i].szApplication));
		TRACE((TRACE_DEBUG,_F_,"Type         =%d",gptActions[i].iType));

		// ISSUE#107 : conserve l'id de la derni�re application reconnue pour l'accompagnement du changement de mot de passe
		// ISSUE#108 : conserve l'id de la derni�re application reconnue pour la s�lectionner par d�faut dans la fen�tre de gestion des sites
		giLastApplication=i;

		// 0.93
		//TRACE((TRACE_DEBUG,_F_,"now-tLastDetect=%ld",t-gptActions[i].tLastDetect));
		//TRACE((TRACE_DEBUG,_F_,"wLastDetect    =0x%08lx",gptActions[i].wLastDetect));
		time(&tNow);
		TRACE((TRACE_DEBUG,_F_,"tNow-tLastSSO=%ld (nb secondes depuis dernier SSO sur cette action)",tNow-gptActions[i].tLastSSO));
		TRACE((TRACE_DEBUG,_F_,"wLastSSO     =0x%08lx",gptActions[i].wLastSSO));

		bDoSSO=true;

		// Cas particulier de Chrome : remaplace le w de la fen�tre ppale par le w de la popup
		// permet de g�rer le comportement en cas d'�chec d'authent exactement pareil qu'avec les autres
		// navigateurs o� la popup est une vraie fenetre...
		if (gptActions[i].iType==POPSSO && iPopupType==POPUP_CHROME) { wReal=w; w=wChromePopup; }

		// on n'ex�cute pas (une action utilisateur est n�cessaire)
		if (gptActions[i].bWaitForUserAction) 
		{
			TRACE((TRACE_INFO,_F_,"SSO en attente d'action utilisateur"));
			goto suite;
		}
		
		// D�tection d'un SSO d�j� fait r�cemment sur la fen�tre afin de pr�venir les essais multiples 
		// avec mauvais mots de passe qui pourraient bloquer le compte 
		tLastSSOOnThisWindow=LastDetect_GetTime(w); // date de dernier SSO sur cette fen�tre (toutes actions confondues)
		TRACE((TRACE_DEBUG,_F_,"tNow-tLastSSOOnThisWindow	=%ld (nb secondes depuis dernier SSO sur cette fen�tre)",tNow-tLastSSOOnThisWindow));

		if (gptActions[i].iType==WEBSSO || gptActions[i].iType==WEBPWD || gptActions[i].iType==XEBSSO)
		{
			// si tLastSSOOnThisWindow==1 => handle inconnu donc jamais trait� => on fait le SSO, sinon :
			if (tLastSSOOnThisWindow!=-1) // on a d�j� fait un SSO sur cette m�me fenetre (cette action ou une autre, peu importe, par exemple une autre action � cause du multi-comptes)
			{
				// c'est du Web, rien de choquant (le navigateur a toujours le meme handle quel que soit le site acc�d� !
				// Par contre, il faut dans les cas suivants ne pas r�essayer imm�diatement, mais laisser passer
				// un d�lai avant le prochain essai (pr�cis� en face de chaque cas) :
				// - Eviter de griller un compte avec X saisies de mauvais mots de passe de suite (iWaitFor=WAIT_IF_SSO_OK)
				// - Ne pas bouffer de CPU en cherchant tous les 500ms des champs qu'on n'a pas trouv� dans la page (iWaitFor=WAIT_IF_SSO_KO)
				// - Ne pas bouffer de CPU quand l'URL ne correspond pas (alors que le titre correspond) (iWaitFor=WAIT_IF_BAD_URL)
				if ((tNow-gptActions[i].tLastSSO)<gptActions[i].iWaitFor) 
				{
					TRACE((TRACE_INFO,_F_,"(tNow-gptActions[i].tLastSSO)<gptActions[i].iWaitFor"));
					bDoSSO=false;
				}
			}
		}
		else // WINSSO ou POPUP
		{
			if (tLastSSOOnThisWindow!=-1)
			{
 				// fen�tre trait�e pr�c�demment par cette action OU PAR UNE AUTRE (cause multi-comptes)
				// elle est toujours l�, donc c'est l'authentification qui rame, pas la peine de r�essayer, 
				// elle disparaitra d'elle m�me au  bout d'un moment...
   				TRACE((TRACE_INFO,_F_,"Fenetre %s handle identique deja traitee il y a %d secondes, on ne fait rien",gptActions[i].szTitle,tNow-tLastSSOOnThisWindow));
				bDoSSO=false;
			}	
			else  
			{
				// fen�tre inconnue au bataillon
				TRACE((TRACE_INFO,_F_,"Fenetre %s handle diff�rent",gptActions[i].szTitle));
				if (gptActions[i].iWaitFor==WAIT_ONE_MINUTE && (tNow-gptActions[i].tLastSSO)<gptActions[i].iWaitFor)
				{
					TRACE((TRACE_DEBUG,_F_,"WAIT_ONE_MINUTE"));
					bDoSSO=false;
				}
				else
				{
					if ((tNow-gptActions[i].tLastSSO)<5) // 0.86 : passage de 3 � 5 secondes pour applis qui rament...
					{
						TRACE((TRACE_DEBUG,_F_,"Le SSO sur cette action a �t� r�alis� il y a moins de 5 secondes"));
						// le SSO sur cette action a �t� r�alis� il y a moins de 5 secondes et cette fen�tre est nouvelle
						// => 2 possibilit�s :
						// 1) c'est une r�apparition suite � un �chec d'authentification
						// 2) c'est vraiment une nouvelle fen�tre ouverte dans les 5 secondes
						// pour diff�rencier, il faut voir si la fen�tre sur laquelle le SSO a �t� fait pr�cedemment
						// est toujours � l'�cran ou pas. Si elle est toujours l�, c'est bien une nouvelle fen�tre
						// Si elle n'est plus l�, on est sans doute dans le cas de l'�chec d'authent
						if(gptActions[i].wLastSSO!=NULL && IsWindow(gptActions[i].wLastSSO))
						{
							TRACE((TRACE_DEBUG,_F_,"La fen�tre pr�c�demment SSOis�e est toujours l�, celle-ci est donc nouvelle"));
							// fen�tre toujours l� ==> nouvelle fen�tre, on fait le SSO
							gptActions[i].iWaitFor=WAIT_IF_SSO_OK;
    						bDoSSO=true;
						}
						else // fen�tre plus l�, sans doute un �chec d'authentification 
						{
							TRACE((TRACE_DEBUG,_F_,"La fen�tre pr�c�demment SSOis�e n'est plus l�, sans doute un retour cause �chec authentification"));
							// 0.85 : on sugg�re donc � l'utilisateur de changer son mot de passe.
							if (gptActions[i].bAutoLock) // 0.66 ne suspend pas si l'utilisateur a mis autoLock=NO (le SSO sera donc fait)
							{
								char szSubTitle[256];
								KillTimer(NULL,giTimer); giTimer=0;
								TRACE((TRACE_INFO,_F_,"Fenetre %s handle different deja traitee il y a %d secondes !",gptActions[i].szTitle,tNow-gptActions[i].tLastSSO));
								T_MESSAGEBOX3B_PARAMS params;
								params.szIcone=IDI_EXCLAMATION;
								params.iB1String=IDS_DESACTIVATE_B1; // r�essayer
								params.iB2String=IDS_DESACTIVATE_B2; // changer le mdp
								params.iB3String=IDS_DESACTIVATE_B3; // d�sactiver
								params.wParent=w;
								params.iTitleString=IDS_MESSAGEBOX_TITLE;
								wsprintf(szSubTitle,GetString(IDS_DESACTIVATE_SUBTITLE),gptActions[i].szApplication);
								params.szSubTitle=szSubTitle;
								strcpy_s(szMsg,sizeof(szMsg),GetString(IDS_DESACTIVATE_MESSAGE));
								params.szMessage=szMsg;
								//if (MessageBox(w,szMsg,"swSSO", MB_YESNO | MB_ICONQUESTION)==IDYES)
								int reponse=MessageBox3B(&params);
								if (reponse==B1) // r�essayer
								{
									gptActions[i].iWaitFor=0;
									// rien � faire, �a va repartir tout seul :-)
								}
								else if (reponse==B2) // changer le mdp
								{
									ChangeApplicationPassword(w,i);
								}
								else // B3 : d�sactiver
								{
									bDoSSO=false;
									// 0.86 sauvegarde la d�sactivation dans le .INI !
									// 0.90A2 : on ne sauvegarde plus (risque d'�crire dans une section renomm�e)
									// WritePrivateProfileString(gptActions[i].szApplication,"active","NO",gszCfgFile);
									
									//gptActions[i].bActive=false;
									// 0.90B1 : finalement on ne d�sactive plus, on suspend pendant 1 minute (#107)
									gptActions[i].iWaitFor=WAIT_ONE_MINUTE;
								}
							}
    					}
					}
				}
			}
		}
		//------------------------------------------------------------------------------------------------------
		if (bDoSSO) // on a d�termin� que le SSO doit �tre tent�
		//------------------------------------------------------------------------------------------------------
		{
			TRACE((TRACE_INFO,_F_,"======= Fen�tre v�rifi�e OK et pas trait�e r�cemment, on lance le SSO !"));
			//0.80 : tue le timer le temps de faire le SSO, le r�arme ensuite (cas des pages lourdes � parser avec Firefox...)
			KillTimer(NULL,giTimer); giTimer=0;

			//0.91 : fait choisir l'appli � l'utilisateur si plusieurs matchent (gestion du multicomptes)
			if (ChooseConfig(w,&i)!=0) goto end;
			
			//0.91 : v�rifie que chaque champ identifiant et mot de passe d�clar� a bien une valeur associ�e
			//       sinon demande les valeurs manquantes � l'utilisateur !
			//0.92 : correction ISSUE#7 : le cas des popup qui ont toujours id et pwd mais pas de chaque champ 
			//		 identifiant et mot de passe d�clar� n'�tait pas trait� !
			gbDontAskId=TRUE;
			gbDontAskId2=TRUE;
			gbDontAskId3=TRUE;
			gbDontAskId4=TRUE;
			gbDontAskPwd=TRUE;

			//0.93 : logs
			swLogEvent(EVENTLOG_INFORMATION_TYPE,MSG_SECONDARY_LOGIN_SUCCESS,gptActions[i].szApplication,gptActions[i].szId1Value,NULL,i);

			if (gptActions[i].bKBSim && gptActions[i].szKBSim[0]!=0) // simulation de frappe clavier
			{
				if (strnistr(gptActions[i].szKBSim,"[ID]",-1)!=NULL &&  *gptActions[i].szId1Value==0) gbDontAskId=FALSE;
				if (strnistr(gptActions[i].szKBSim,"[ID2]",-1)!=NULL && *gptActions[i].szId2Value==0) gbDontAskId2=FALSE;
				if (strnistr(gptActions[i].szKBSim,"[ID3]",-1)!=NULL && *gptActions[i].szId3Value==0) gbDontAskId3=FALSE;
				if (strnistr(gptActions[i].szKBSim,"[ID4]",-1)!=NULL && *gptActions[i].szId4Value==0) gbDontAskId4=FALSE;
				if (strnistr(gptActions[i].szKBSim,"[PWD]",-1)!=NULL && *gptActions[i].szPwdEncryptedValue==0) gbDontAskPwd=FALSE;
			}
			else // SSO normal
			{
				if (*gptActions[i].szId1Name!=0 && *gptActions[i].szId1Value==0) gbDontAskId=FALSE;
				if (*gptActions[i].szId2Name!=0 && *gptActions[i].szId2Value==0) gbDontAskId2=FALSE;
				if (*gptActions[i].szId3Name!=0 && *gptActions[i].szId3Value==0) gbDontAskId3=FALSE;
				if (*gptActions[i].szId4Name!=0 && *gptActions[i].szId4Value==0) gbDontAskId4=FALSE;
				if (*gptActions[i].szPwdName!=0 && *gptActions[i].szPwdEncryptedValue==0) gbDontAskPwd=FALSE;
			}
			// cas des popups (0.92 - ISSUE#7)
			if (gptActions[i].iType==POPSSO) 
			{
				if (*gptActions[i].szId1Value==0) gbDontAskId=FALSE;
				if (*gptActions[i].szPwdEncryptedValue==0) gbDontAskPwd=FALSE;
			}
			
			// s'il y a au moins un champ non renseign�, afficher la fen�tre de saisie
			//if (!gbDontAskId || !gbDontAskId2 || !gbDontAskId3 || !gbDontAskId4 || !gbDontAskPwd)
			if ((gptActions[i].iType!=WEBPWD) && (!gbDontAskId || !gbDontAskId2 || !gbDontAskId3 || !gbDontAskId4 || !gbDontAskPwd))
			{
				T_IDANDPWDDIALOG params;
				params.bCenter=TRUE;
				params.iAction=i;
				params.iTitle=IDS_IDANDPWDTITLE_MISSING;
				wsprintf(params.szText,GetString(IDS_IDANDPWDTEXT_MISSING),gptActions[i].szApplication);
					
				if (DialogBoxParam(ghInstance,MAKEINTRESOURCE(IDD_ID_AND_PWD),HWND_DESKTOP,IdAndPwdDialogProc,(LPARAM)&params)==IDOK) 
				{
					SaveApplications();
				}
				else
				{
					// l'utilisateur a annul�, on marque la config en WAIT_ONE_MINUTE comme �a on ne fait pas 
					// le SSO tout de suite -> repositionne tLastDetect et wLastDetect 
					//time(&gptActions[i].tLastDetect);
					//gptActions[i].wLastDetect=w;
					time(&gptActions[i].tLastSSO);
					gptActions[i].wLastSSO=w;
					LastDetect_AddOrUpdateWindow(w);
					gptActions[i].iWaitFor=WAIT_ONE_MINUTE;
					TRACE((TRACE_DEBUG,_F_,"gptActions(%d).iWaitFor=WAIT_ONE_MINUTE",i));
					goto end;
				}
			}
			
			//0.89 : ind�pendamment du type (WIN, POP ou WEB), si c'est de la simulation de frappe clavier, on y va !
			//       et ensuite on termine sans uploader la config (comme on n'a aucun moyen de savoir
			//       si le SSO a fonctionn�, c'est pr�f�rable de laisser l'utilisateur juger et remonter
			//		 manuellement la configuration le cas �ch�ant
			// ISSUE#61 / 0.93 : on ne traite les popup W7 en simulation de frappe, marche pas avec IE9 ou W7 SP1 ?
			// if (iPopupType==POPUP_W7 || iPopupType==POPUP_CHROME) 
			if (iPopupType==POPUP_CHROME) // 0.92 : on traite les popup W7 en simulation de frappe �a marche tr�s bien
			{
				gptActions[i].bKBSim=TRUE;
				strcpy_s(gptActions[i].szKBSim,LEN_KBSIM+1,"[40][ID][40][TAB][40][PWD][40][ENTER]");
			}
			if (gptActions[i].bKBSim && gptActions[i].szKBSim[0]!=0)
			{
				TRACE((TRACE_INFO,_F_,"SSO en mode simulation de frappe clavier"));
				if (gptActions[i].iType==POPSSO && iPopupType==POPUP_CHROME)
					KBSimSSO(wReal,i);
				else
					KBSimSSO(w,i);
				// repositionne tLastDetect et wLastDetect
				//time(&gptActions[i].tLastDetect);
				//gptActions[i].wLastDetect=w;
				time(&gptActions[i].tLastSSO);
				gptActions[i].wLastSSO=w;
				LastDetect_AddOrUpdateWindow(w);
				if (_strnicmp(gptActions[i].szKBSim,"[WAIT]",strlen("[WAIT]"))==0) gptActions[i].bWaitForUserAction=TRUE;
				//goto suite; // 0.90 XXXXXXXXX ICI XXXXXXXXXXXXX
				// ISSUE#61 / 0.93 : on ne traite les popup W7 en simulation de frappe, marche pas avec IE9 ou W7 SP1 ?
				// if (iPopupType==POPUP_W7 || iPopupType==POPUP_CHROME) { gptActions[i].bKBSim=FALSE; *(gptActions[i].szKBSim)=0; }
				if (iPopupType==POPUP_CHROME) { gptActions[i].bKBSim=FALSE; *(gptActions[i].szKBSim)=0; }
				switch (gptActions[i].iType)
				{
					case POPSSO: guiNbPOPSSO++; break;
					case WINSSO: guiNbWINSSO++; break;
					case WEBSSO: guiNbWEBSSO++; break;
					case XEBSSO: guiNbWEBSSO++; break;
				}
				goto end;
			}
			switch(gptActions[i].iType)
			{
				case WINSSO: 
				case POPSSO: 
					SSOWindows(w,i,iPopupType);
					// repositionne tLastDetect et wLastDetect
					//time(&gptActions[i].tLastDetect);
					//gptActions[i].wLastDetect=w;
					time(&gptActions[i].tLastSSO);
					gptActions[i].wLastSSO=w;
					LastDetect_AddOrUpdateWindow(w);
					break;
				case WEBSSO: 
					// pas de break, c'est volontaire !
				case XEBSSO: 
					// pas de break, c'est volontaire !
				case WEBPWD:
					switch (iBrowser)
					{
						case BROWSER_IE:
							if (gptActions[i].iType==XEBSSO)
								rc=SSOWebAccessible(w,i,BROWSER_IE);
							else
								rc=SSOWeb(w,i,w); 
							break;
							break;
						case BROWSER_FIREFOX3:
						case BROWSER_FIREFOX4:
							if (gptActions[i].iType==XEBSSO)
								rc=SSOWebAccessible(w,i,iBrowser);
							else
							{
								// ISSUE#60 : en attendant d'avoir une r�ponse de Mozilla, on n'ex�cute pas les anciennes config 
								//            avec Firefox sous Windows 7 pour �viter le plantage !
								// ISSUE#60 modifi� en 0.94B2 : Vista et 64 bits seulement
								if (giOSBits==OS_64) 
								{
									TRACE((TRACE_INFO,_F_,"Ancienne configuration Firefox + Windows 64 bits : on n'ex�cute pas !"));
									rc=0;
								}
								else rc=SSOFirefox(w,i,iBrowser); 
							}
							break;
						case BROWSER_MAXTHON:
							rc=SSOMaxthon(w,i); 
							break;
						case BROWSER_CHROME:
							if (gptActions[i].iType==XEBSSO)
								rc=SSOWebAccessible(w,i,iBrowser);
							else
							{
								if (giOSBits==OS_64) 
								{
									TRACE((TRACE_INFO,_F_,"Ancienne configuration Chrome + Windows 64 bits : on n'ex�cute pas !"));
									rc=0;
								}
								else rc=SSOFirefox(w,i,iBrowser); // ISSUE#66 0.94 : chrome a impl�ment� ISimpleDOM comme Firefox !
							}
							break;
						default:
							rc=-1;
					}

					// repositionne tLastDetect et wLastDetect (�cras� par la suite dans un cas, cf. plus bas)
					//time(&gptActions[i].tLastDetect);
					//gptActions[i].wLastDetect=w;
					time(&gptActions[i].tLastSSO);
					gptActions[i].wLastSSO=w;
					LastDetect_AddOrUpdateWindow(w);

					if (rc==0) // SSO r�ussi
					{
						TRACE((TRACE_INFO,_F_,"SSO r�ussi, on ne le retente pas avant %d secondes",WAIT_IF_SSO_OK));
						// on ne r�essaie pas si le SSO est r�ussi mais on ne peut pas cramer d�finitivement
						// ce SSO sinon un utilisateur qui se d�connecte ne pourra pas se reconnecter
						// tant qu'il n'aura pas ferm� / r�ouvert son navigateur (handle diff�rent) !
						gptActions[i].iNbEssais=0;
						gptActions[i].iWaitFor=WAIT_IF_SSO_OK; 
					}
					else if (rc==-2) // SSO abandonn� car l'URL ne correspond pas
					{
						// Si l'URL est diff�rente, c'est probablement que le SSO a �t� fait pr�c�demment
						// et que l'utilisateur est sur une autre page du site.
						// Il n'est pas utile de r�essayer imm�diatement, n�anmoins il faut essayer r�guli�rement
						// pour que le SSO fonctionne quand la page attendue arrivera (par exemple si la page
						// visit�e �tait une page pr�c�dant celle sur laquelle l'utilisateur va faire le SSO...)
						// Le r�-essai ne coute presque rien en CPU, on peut le faire toutes les 5 secondes
						gptActions[i].iNbEssais=0;
						gptActions[i].iWaitFor=WAIT_IF_BAD_URL;
						TRACE((TRACE_INFO,_F_,"URL diff�rente de celle attendue, prochain essai dans %d secondes",WAIT_IF_BAD_URL));
					}
					else // SSO rat�, erreur inattendue ou plus probablement champs non trouv�s
					{
						// Deux cas de figure... comment les diff�rencier ???
						// 1) La page n'est pas encore compl�tement arriv�e, il faut donc r�essayer un petit peu plus tard
						// 2) Le titre et l'URL ne permettent pas de distinguer la page courante de la page de login...
						// Solution : on retente quelques fois relativement rapidement pour traiter le cas 1 et au bout de 
						// quelques essais on espace les tentatives pour traiter plut�t le cas 2 (car parsing complet gourmant
						// en CPU, on ne peut pas le faire toutes les 2 secondes ind�finiment)
						TRACE((TRACE_INFO,_F_,"Echec SSOWeb application %s (essai %d)",gptActions[i].szApplication,gptActions[i].iNbEssais));
						gptActions[i].iNbEssais++;
						if (gptActions[i].iNbEssais<21) // les 20 premi�res fois, on retente imm�diatement
						{
							//gptActions[i].wLastDetect=NULL;
							//gptActions[i].tLastDetect=-1;
							gptActions[i].tLastSSO=-1;
							gptActions[i].wLastSSO=NULL;
							TRACE((TRACE_INFO,_F_,"Essai %d immediat",gptActions[i].iNbEssais));
						}
						else if (gptActions[i].iNbEssais<121) // puis 100 fois toutes les 2 secondes
						{
							gptActions[i].iWaitFor=WAIT_IF_SSO_PAGE_NOT_COMPLETE; 
							TRACE((TRACE_INFO,_F_,"Essai %d dans %d secondes",gptActions[i].iNbEssais,WAIT_IF_SSO_PAGE_NOT_COMPLETE));
						}
						else 
						{
							// bon, �a fait un paquet de fois qu'on r�essaie... c'est surement un cas 
							// ou titre+URL ne permettent pas de diff�rencier la page de login des autres
							// Du coup, inutile de s'acharner, on retente + tard
							// gptActions[i].iNbEssais=0; // r�arme le compteur pour la prochaine tentative
							// A VOIR : je pense qu'il vaut mieux ne pas r�armer le compteur
							//         comme �a on continue sur un rythme d'une fois tous les pas souvent
							gptActions[i].iWaitFor=WAIT_IF_SSO_NOK; 
							TRACE((TRACE_INFO,_F_,"Prochain essai dans %d secondes",WAIT_IF_SSO_NOK));
						}
					}
					break;
				default: ;
			} // switch
		}
		// 0.80 : update la config sur le serveur si autoris� par l'utilisateur
		// 0.82 : le timer doit plut�t �tre r�arm� quand on a fini l'�num�ration des fen�tres -> voir TimerProc()
		//        en plus ici c'�tait dangereux : un goto suite ou end passait outre le r�armement !!!
		//        et je me demande si �a ne pouvait pas cr�er des cas de r�entrance qui auraient eu pour cons�quence
		//        que le timer ne soit jamais r�arm�...
		// giTimer=SetTimer(NULL,0,500,TimerProc);
suite:
		// eh oui, il faut lib�rer pszURL... Sinon, vous croyez vraiment que 
		// j'aurais fait ce "goto suite", alors que continue me tendait les bras ?
		if (pszURL!=NULL) { free(pszURL); pszURL=NULL; }
		if (pszURL2!=NULL) { free(pszURL2); pszURL2=NULL; }
		if (pszURLBar!=NULL) { free(pszURLBar); pszURLBar=NULL; }
	}
end:
	// nouveau en 0.90...
	if (pszURL!=NULL)
	{ 
		free(pszURL); 
		pszURL=NULL;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------
// LoadIcons()
//-----------------------------------------------------------------------------
// Chargement de toutes les icones et pointeurs de souris pour l'IHM
//-----------------------------------------------------------------------------
// [rc] : 0 si OK
//-----------------------------------------------------------------------------
static int LoadIcons(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;
	ghIconAltTab=(HICON)LoadImage(ghInstance, 
					MAKEINTRESOURCE(IDI_LOGO),
					IMAGE_ICON,
					0,
					0,
					LR_DEFAULTSIZE);
	if (ghIconAltTab==NULL) goto end;
	ghIconSystrayActive=(HICON)LoadImage(ghInstance, 
					MAKEINTRESOURCE(IDI_SYSTRAY_ACTIVE),
					IMAGE_ICON,
					GetSystemMetrics(SM_CXSMICON),
					GetSystemMetrics(SM_CYSMICON),
					LR_DEFAULTCOLOR);
	if (ghIconSystrayActive==NULL) goto end;
	ghIconSystrayInactive=(HICON)LoadImage(ghInstance, 
					MAKEINTRESOURCE(IDI_SYSTRAY_INACTIVE), 
					IMAGE_ICON,
					GetSystemMetrics(SM_CXSMICON),
					GetSystemMetrics(SM_CYSMICON),
					LR_DEFAULTCOLOR);
	if (ghIconSystrayInactive==NULL) goto end;
	ghIconLoupe=(HICON)LoadImage(ghInstance, 
					MAKEINTRESOURCE(IDI_LOUPE), 
					IMAGE_ICON,
					GetSystemMetrics(SM_CXSMICON),
					GetSystemMetrics(SM_CYSMICON),
					LR_LOADTRANSPARENT);
	if (ghIconLoupe==NULL) goto end;
	ghLogo=(HICON)LoadImage(ghInstance,MAKEINTRESOURCE(IDB_LOGO),IMAGE_BITMAP,0,0,LR_DEFAULTCOLOR);
	if (ghLogo==NULL) goto end;
	ghLogoFondBlanc50=(HICON)LoadImage(ghInstance,MAKEINTRESOURCE(IDB_LOGO_FONDBLANC50),IMAGE_BITMAP,0,0,LR_DEFAULTCOLOR);
	if (ghLogoFondBlanc50==NULL) goto end;
	ghLogoFondBlanc90=(HICON)LoadImage(ghInstance,MAKEINTRESOURCE(IDB_LOGO_FONDBLANC90),IMAGE_BITMAP,0,0,LR_DEFAULTCOLOR);
	if (ghLogoFondBlanc90==NULL) goto end;
	ghCursorHand=(HCURSOR)LoadImage(ghInstance,
					MAKEINTRESOURCE(IDC_CURSOR_HAND),
					IMAGE_CURSOR,
					0,
					0,
					LR_DEFAULTSIZE);
	if (ghCursorHand==NULL) goto end;
	ghCursorWait=LoadCursor(NULL, IDC_WAIT); 
	if (ghCursorWait==NULL) goto end;
	ghImageList=ImageList_LoadBitmap(ghInstance,MAKEINTRESOURCE(IDB_TVIMAGES),16,4,RGB(255,0,255));
	if (ghImageList==NULL) goto end;
	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// UnloadIcons()
//-----------------------------------------------------------------------------
// D�chargement de toutes les icones et pointeurs de souris
//-----------------------------------------------------------------------------
static void UnloadIcons(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	if (ghIconAltTab!=NULL) { DestroyIcon(ghIconAltTab); ghIconAltTab=NULL; }
	if (ghIconSystrayActive!=NULL) { DestroyIcon(ghIconSystrayActive); ghIconSystrayActive=NULL; }
	if (ghIconSystrayInactive!=NULL) { DestroyIcon(ghIconSystrayInactive); ghIconSystrayInactive=NULL; }
	if (ghIconLoupe!=NULL) { DestroyIcon(ghIconLoupe); ghIconLoupe=NULL; }
	if (ghLogo!=NULL) { DeleteObject(ghLogo); ghLogo=NULL; }
	if (ghLogoFondBlanc50!=NULL) { DeleteObject(ghLogoFondBlanc50); ghLogoFondBlanc50=NULL; }
	if (ghLogoFondBlanc90!=NULL) { DeleteObject(ghLogoFondBlanc90); ghLogoFondBlanc90=NULL; }
	if (ghCursorHand!=NULL) { DestroyCursor(ghCursorHand); ghCursorHand=NULL; }
	if (ghCursorWait!=NULL) { DestroyCursor(ghCursorWait); ghCursorWait=NULL; }
	if (ghImageList!=NULL) ImageList_Destroy(ghImageList);
	TRACE((TRACE_LEAVE,_F_, ""));
}

//-----------------------------------------------------------------------------
// SimplePwdChoiceDialogProc()
//-----------------------------------------------------------------------------
// DialogProc de la boite de choix simplifi�e de strat�gie de mot de passe
// (fen�tre ouverte lorsqu'aucun fichier de config n'est trouv�)
//-----------------------------------------------------------------------------
static int CALLBACK SimplePwdChoiceDialogProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
	int rc=FALSE;
	switch (msg)
	{
		case WM_INITDIALOG:
			TRACE((TRACE_DEBUG,_F_, "WM_INITDIALOG"));
			// icone ALT-TAB
			SendMessage(w,WM_SETICON,ICON_BIG,(LPARAM)ghIconAltTab); 
			SendMessage(w,WM_SETICON,ICON_SMALL,(LPARAM)ghIconSystrayActive); 
			// init champs de saisie
			//SendMessage(GetDlgItem(w,TB_PWD1),EM_SETPASSWORDCHAR,(WPARAM)'*',0);
			//SendMessage(GetDlgItem(w,TB_PWD2),EM_SETPASSWORDCHAR,(WPARAM)'*',0);
			SendMessage(GetDlgItem(w,TB_PWD1),EM_LIMITTEXT,LEN_PWD,0);
			SendMessage(GetDlgItem(w,TB_PWD2),EM_LIMITTEXT,LEN_PWD,0);
			// titres en gras
			SetTextBold(w,TX_FRAME);
			// policies
			if (!gbEnableOption_SavePassword) EnableWindow(GetDlgItem(w,CK_SAVE),FALSE);
			MACRO_SET_SEPARATOR;
			// magouille supr�me : pour g�rer les cas rares dans lesquels la peinture du bandeau & logo se fait mal
			// on active un timer d'une seconde qui ex�cutera un invalidaterect pour forcer la peinture
			if (giRefreshTimer==giTimer) giRefreshTimer=11;
			SetTimer(w,giRefreshTimer,200,NULL);
			break;
		case WM_TIMER:
			TRACE((TRACE_INFO,_F_,"WM_TIMER (refresh)"));
			if (giRefreshTimer==(int)wp) 
			{
				KillTimer(w,giRefreshTimer);
				InvalidateRect(w,NULL,FALSE);
				SetForegroundWindow(w); 
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wp))
			{
				case IDOK:
					char szPwd1[LEN_PWD+1];
					GetDlgItemText(w,TB_PWD1,szPwd1,sizeof(szPwd1));
					// Password Policy
					if (!IsPasswordPolicyCompliant(szPwd1))
					{
						MessageBox(w,gszPwdPolicy_Message,"swSSO",MB_OK | MB_ICONEXCLAMATION);
					}
					else
					{
						BYTE AESKeyData[AES256_KEY_LEN];
						giPwdProtection=PP_ENCRYPTED;
						// g�n�re le sel qui sera pris en compte pour la d�rivation de la cl� AES et le stockage du mot de passe
						swGenPBKDF2Salt();
						swCryptDeriveKey(szPwd1,&ghKey1,AESKeyData);
						StoreMasterPwd(szPwd1);
						RecoveryChangeAESKeyData(AESKeyData);
						// inscrit la date de dernier changement de mot de passe dans le .ini
						// cette valeur est chiffr� par le (nouveau) mot de passe et �crite seulement si politique mdp d�finie
						SaveMasterPwdLastChange();
						if (IsDlgButtonChecked(w,CK_SAVE)==BST_CHECKED)
						{
							gbRememberOnThisComputer=TRUE;
							DPAPIStoreMasterPwd(szPwd1);
						}
						SecureZeroMemory(szPwd1,strlen(szPwd1));
						SaveConfigHeader();
						EndDialog(w,IDOK);
					}
					break;
				case IDCANCEL:
					EndDialog(w,IDCANCEL);
					break;
				case TB_PWD1:
				case TB_PWD2:
				{
					char szPwd1[LEN_PWD+1];
					char szPwd2[LEN_PWD+1];
					int len1,len2;
					if (HIWORD(wp)==EN_CHANGE)
					{
						len1=GetDlgItemText(w,TB_PWD1,szPwd1,sizeof(szPwd1));
						len2=GetDlgItemText(w,TB_PWD2,szPwd2,sizeof(szPwd2));
						if (len1==len2 && len1!=0 && strcmp(szPwd1,szPwd2)==0)
							EnableWindow(GetDlgItem(w,IDOK),TRUE);
						else
							EnableWindow(GetDlgItem(w,IDOK),FALSE);
					}
					break;
				}
			}
			break;
		case WM_CTLCOLORSTATIC:
			int ctrlID;
			ctrlID=GetDlgCtrlID((HWND)lp);
			switch(ctrlID)
			{
				case TX_FRAME:
					SetBkMode((HDC)wp,TRANSPARENT);
					rc=(int)GetStockObject(HOLLOW_BRUSH);
					break;
			}
			break;
		case WM_HELP:
			Help();
			break;
		case WM_PAINT:
			DrawLogoBar(w,50,ghLogoFondBlanc50);
			rc=TRUE;
			break;
	}
	return rc;
}

//-------------------------------------------------------------------------------------
// Hook pour changer les libell�s de la message box d'erreur de mot de passe
// qui propose de lancer la proc�dure de recouvrement (boutons : r�essayer / oubli� ?)
//-------------------------------------------------------------------------------------
WNDPROC gOldProc;
HHOOK  ghHook;
LRESULT CALLBACK HookWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
    LRESULT rc = CallWindowProc(gOldProc,w,msg,wp,lp);
    if (msg==WM_INITDIALOG)
    {
		TRACE((TRACE_DEBUG,_F_,"WM_INITDIALOG"));
		SetDlgItemText(w,IDCANCEL,GetString(IDS_BTN_FORGOT));
		SetDlgItemText(w,IDRETRY,GetString(IDS_BTN_RETRY));
    }
    if (msg==WM_NCDESTROY) 
	{
		TRACE((TRACE_DEBUG,_F_,"WM_NCDESTROY"));
		UnhookWindowsHookEx(ghHook);
	}
	return rc;
}
LRESULT CALLBACK SetHook(int nCode,WPARAM wp,LPARAM lp)
{
	if (nCode==HC_ACTION)
	{
		
		CWPSTRUCT* pwp = (CWPSTRUCT*)lp;
		if (pwp->message==WM_INITDIALOG) 
		{ 
			TRACE((TRACE_DEBUG,_F_,"WM_INITDIALOG"));
			gOldProc=(WNDPROC)SetWindowLong(pwp->hwnd,GWL_WNDPROC,(LONG)HookWndProc); 
		}
	}
	return CallNextHookEx(ghHook,nCode,wp,lp);
}

//-----------------------------------------------------------------------------
// PwdDialogProc()
//-----------------------------------------------------------------------------
// DialogProc de la boite de saisie du mot de passe maitre
//-----------------------------------------------------------------------------
static int CALLBACK PwdDialogProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
	int rc=FALSE;
	switch (msg)
	{
		case WM_INITDIALOG:
			TRACE((TRACE_DEBUG,_F_, "WM_INITDIALOG"));
			BOOL bUseDPAPI;
			// Modifie le texte pour demander le mot de passe WIndows
			if (giPwdProtection==PP_WINDOWS) SetDlgItemText(w,TX_FRAME,GetString(IDS_PLEASE_ENTER_WINDOWS_PASSWORD));
			// icone ALT-TAB
			SendMessage(w,WM_SETICON,ICON_BIG,(LPARAM)ghIconAltTab);
			SendMessage(w,WM_SETICON,ICON_SMALL,(LPARAM)ghIconSystrayActive); 
			gwAskPwd=w;
			// init champ de saisie
			//SendMessage(GetDlgItem(w,TB_PWD),EM_SETPASSWORDCHAR,(WPARAM)'*',0);
			SendMessage(GetDlgItem(w,TB_PWD),EM_LIMITTEXT,LEN_PWD,0);
			// titre en gras
			SetTextBold(w,TX_FRAME);
			// policies
			bUseDPAPI=(BOOL)lp;
			if (!gbEnableOption_SavePassword || !bUseDPAPI || giPwdProtection==PP_WINDOWS) ShowWindow(GetDlgItem(w,CK_SAVE),SW_HIDE);
			// 0.81 : centrage si parent!=NULL
			if (GetParent(w)!=NULL)
			{
				int cx;
				int cy;
				RECT rect,rectParent;
				cx = GetSystemMetrics( SM_CXSCREEN );
				cy = GetSystemMetrics( SM_CYSCREEN );
				GetWindowRect(w,&rect);
				GetWindowRect(GetParent(w),&rectParent);
				SetWindowPos(w,NULL,rectParent.left+((rectParent.right-rectParent.left)-(rect.right-rect.left))/2,
									rectParent.top+ ((rectParent.bottom-rectParent.top)-(rect.bottom-rect.top))/2,
									0,0,SWP_NOSIZE | SWP_NOZORDER);
			}
			else
			{
				SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE | SWP_NOMOVE);
			}
			MACRO_SET_SEPARATOR;
			// magouille supr�me : pour g�rer les cas rares dans lesquels la peinture du bandeau & logo se fait mal
			// on active un timer d'une seconde qui ex�cutera un invalidaterect pour forcer la peinture
			if (giRefreshTimer==giTimer) giRefreshTimer=11;
			SetTimer(w,giRefreshTimer,200,NULL);
			break;
		case WM_TIMER:
			TRACE((TRACE_INFO,_F_,"WM_TIMER (refresh)"));
			if (giRefreshTimer==(int)wp) 
			{
				KillTimer(w,giRefreshTimer);
				InvalidateRect(w,NULL,FALSE);
				SetForegroundWindow(w); 
				//SetFocus(w); ATTENTION, REMET LE FOCUS SUR LE MDP ET FOUT LA MERDE SI SAISIE DEJA COMMENCEE !
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wp))
			{
				case IDOK:
				{
					char szPwd[LEN_PWD+1];
					GetDlgItemText(w,TB_PWD,szPwd,sizeof(szPwd));
					if (giPwdProtection==PP_WINDOWS && ghKey1!=NULL) // Cas de la demande de mot de passe "loupe" (TogglePasswordField) ou du d�verrouillage
					{
						BYTE AESKeyData[AES256_KEY_LEN];
						swCryptDeriveKey(szPwd,&ghKey2,AESKeyData);
						SecureZeroMemory(szPwd,strlen(szPwd));
						if (ReadVerifyCheckSynchroValue(ghKey2)==0)
						{
							EndDialog(w,IDOK);
						}
						else
						{
							MessageBox(w,GetString(IDS_BADPWD),"swSSO",MB_OK | MB_ICONEXCLAMATION);
							if (giBadPwdCount>5) PostQuitMessage(-1);
						}
					}
					else if (CheckMasterPwd(szPwd)==0)
					{
						if (IsDlgButtonChecked(w,CK_SAVE)==BST_CHECKED)
						{
							gbRememberOnThisComputer=TRUE;
							DPAPIStoreMasterPwd(szPwd);
						}
						// 0.90 : si une cl� de recouvrement existe et les infos de recouvrement n'ont pas encore
						//        �t� enregistr�es dans le .ini (cas de la premi�re utilisation apr�s d�ploiement de la cl�
						BYTE AESKeyData[AES256_KEY_LEN];
						swCryptDeriveKey(szPwd,&ghKey1,AESKeyData);
						SecureZeroMemory(szPwd,strlen(szPwd));
						RecoveryFirstUse(w,AESKeyData);
						EndDialog(w,IDOK);
					}
					else
					{
						SecureZeroMemory(szPwd,strlen(szPwd));
						// 0.93B1 : log authentification primaire �chou�e
						if (gbSSOActif)
							swLogEvent(EVENTLOG_WARNING_TYPE,MSG_PRIMARY_LOGIN_ERROR,NULL,NULL,NULL,0);
						else
							swLogEvent(EVENTLOG_WARNING_TYPE,MSG_UNLOCK_BAD_PWD,NULL,NULL,NULL,0);

						SendDlgItemMessage(w,TB_PWD,EM_SETSEL,0,-1);

						if (gpRecoveryKeyValue==NULL || *gszRecoveryInfos==0)
						{
							MessageBox(w,GetString(IDS_BADPWD),"swSSO",MB_OK | MB_ICONEXCLAMATION);
						}
						else // une cl� de recouvrement existe et que les recoveryInfos ont d�j� �t� stock�es
							 // on propose de r�initialiser le mot de passe
						{
							ghHook=SetWindowsHookEx (WH_CALLWNDPROC,(HOOKPROC)SetHook,NULL,GetCurrentThreadId());
							if (MessageBox(w,GetString(IDS_BADPWD2),"swSSO",MB_RETRYCANCEL | MB_ICONEXCLAMATION)==IDCANCEL)
							{
								RecoveryChallenge(w);
								EndDialog(w,IDCANCEL);
								PostQuitMessage(-1);
							}
						}
						if (giBadPwdCount>5) PostQuitMessage(-1);
					}
					break;
				}
				case IDCANCEL:
					EndDialog(w,IDCANCEL);
					break;
				case TB_PWD:
				{
					if (HIWORD(wp)==EN_CHANGE)
					{
						char szPwd[LEN_PWD+1];
						int len;
						len=GetDlgItemText(w,TB_PWD,szPwd,sizeof(szPwd));
						EnableWindow(GetDlgItem(w,IDOK),len==0 ? FALSE : TRUE);
					}
					break;
				}
			}
			break;
		case WM_CTLCOLORSTATIC:
			int ctrlID;
			ctrlID=GetDlgCtrlID((HWND)lp);
			switch(ctrlID)
			{
				case TX_FRAME:
					SetBkMode((HDC)wp,TRANSPARENT);
					rc=(int)GetStockObject(HOLLOW_BRUSH);
					break;
			}
			break;
		case WM_HELP:
			Help();
			break;
		case WM_PAINT:
			DrawLogoBar(w,50,ghLogoFondBlanc50);
			rc=TRUE;
			break;
		case WM_ACTIVATE:
			InvalidateRect(w,NULL,FALSE);
			break;
	}
	return rc;
}


//*****************************************************************************
//                             FONCTIONS PUBLIQUES
//*****************************************************************************

//-----------------------------------------------------------------------------
// AskPwd()
//-----------------------------------------------------------------------------
// Demande et v�rifie le mot de passe de l'utilisateur.
//-----------------------------------------------------------------------------
// [rc] : 0 si OK
//-----------------------------------------------------------------------------
int AskPwd(HWND wParent,BOOL bUseDPAPI)
{
	TRACE((TRACE_ENTER,_F_, ""));

	int ret=-1;
	int rc;
	char szPwd[LEN_PWD+1];

	if (giBadPwdCount>5) 
	{
		PostQuitMessage(-1);
		goto end;
	}

	// 0.65 : anti r�-entrance
	if (gwAskPwd!=NULL) 
	{
		SetForegroundWindow(gwAskPwd);
		goto end;
	}

	//0.76 : DPAPI
	if (bUseDPAPI)
	{
		rc=DPAPIGetMasterPwd(szPwd);
		if (rc==0)
		{
			if (CheckMasterPwd(szPwd)==0)
			{
				// 0.90 : si une cl� de recouvrement existe et les infos de recouvrement n'ont pas encore
				//        �t� enregistr�e dans le .ini (cas de la premi�re utilisation apr�s d�ploiement de la cl�
				BYTE AESKeyData[AES256_KEY_LEN];
				swCryptDeriveKey(szPwd,&ghKey1,AESKeyData);
				RecoveryFirstUse(wParent,AESKeyData);
				// 0.85B9 : remplacement de ZeroMemory(szPwd,sizeof(szPwd));
				SecureZeroMemory(szPwd,strlen(szPwd));
				gbRememberOnThisComputer=TRUE;
				ret=0;
				goto end;
			}
		}
	}
	rc=DialogBoxParam(ghInstance,MAKEINTRESOURCE(IDD_PWD),wParent,PwdDialogProc,(LPARAM)bUseDPAPI);
	gwAskPwd=NULL;
	if (rc!=IDOK) goto end;
	
	giBadPwdCount=0;
	ret=0;
end:
	TRACE((TRACE_LEAVE,_F_, "ret=%d",ret));
	return ret;
}

typedef struct 
{
	int iDomainId;
	char szDomainLabel[LEN_DOMAIN+1];
}
T_DOMAIN;

//-----------------------------------------------------------------------------
// SelectDomainDialogProc()
//-----------------------------------------------------------------------------
// DialogProc de la boite de choix de domaine
//-----------------------------------------------------------------------------
static int CALLBACK SelectDomainDialogProc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
	int rc=FALSE;
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			TRACE((TRACE_DEBUG,_F_, "WM_INITDIALOG"));
			// icone ALT-TAB
			SendMessage(w,WM_SETICON,ICON_BIG,(LPARAM)ghIconAltTab);
			SendMessage(w,WM_SETICON,ICON_SMALL,(LPARAM)ghIconSystrayActive); 
			// titre en gras
			SetTextBold(w,TX_FRAME);
			SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE | SWP_NOMOVE);
			MACRO_SET_SEPARATOR;
			// remplissage combo
			T_DOMAIN *ptDomains=(T_DOMAIN*)lp;
			int i=1;
			while (ptDomains[i].iDomainId!=-1) 
			{ 
				int index=SendMessage(GetDlgItem(w,CB_DOMAINS),CB_ADDSTRING,0,(LPARAM)ptDomains[i].szDomainLabel); 
				SendMessage(GetDlgItem(w,CB_DOMAINS),CB_SETITEMDATA,index,(LPARAM)ptDomains[i].iDomainId); 
				i++; 
			}
			SendMessage(GetDlgItem(w,CB_DOMAINS),CB_SETCURSEL,0,0);
			// magouille supr�me : pour g�rer les cas rares dans lesquels la peinture du bandeau & logo se fait mal
			// on active un timer d'une seconde qui ex�cutera un invalidaterect pour forcer la peinture
			if (giRefreshTimer==giTimer) giRefreshTimer=11;
			SetTimer(w,giRefreshTimer,200,NULL);
			break;
		}
		case WM_TIMER:
			TRACE((TRACE_INFO,_F_,"WM_TIMER (refresh)"));
			if (giRefreshTimer==(int)wp) 
			{
				KillTimer(w,giRefreshTimer);
				InvalidateRect(w,NULL,FALSE);
				SetForegroundWindow(w); 
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wp))
			{
				case IDOK:
				{
					int index=SendMessage(GetDlgItem(w,CB_DOMAINS),CB_GETCURSEL,0,0);
					giDomainId=SendMessage(GetDlgItem(w,CB_DOMAINS),CB_GETITEMDATA,index,0);
					SendMessage(GetDlgItem(w,CB_DOMAINS),CB_GETLBTEXT,index,(LPARAM)gszDomainLabel);
					EndDialog(w,IDOK);
					break;
				}
				case IDCANCEL:
					EndDialog(w,IDCANCEL);
					break;
			}
			break;
		case WM_CTLCOLORSTATIC:
			int ctrlID;
			ctrlID=GetDlgCtrlID((HWND)lp);
			switch(ctrlID)
			{
				case TX_FRAME:
					SetBkMode((HDC)wp,TRANSPARENT);
					rc=(int)GetStockObject(HOLLOW_BRUSH);
					break;
			}
			break;
		case WM_HELP:
			Help();
			break;
		case WM_PAINT:
			DrawLogoBar(w,50,ghLogoFondBlanc50);
			rc=TRUE;
			break;
		case WM_ACTIVATE:
			InvalidateRect(w,NULL,FALSE);
			break;
	}
	return rc;
}

//-----------------------------------------------------------------------------
// SelectDomain()
//-----------------------------------------------------------------------------
// R�cup�re la liste des domaines disponibles sur le serveur et s'il y en 
// a plus d'un propose le choix � l'utilisateur
// rc :  0 - OK, l'utilisateur a choisi, le domaine est renseign� dans giDomainId et gszDomainLabel
//    :  1 - Il n'y avait qu'un seul domaine, l'utilisateur n'a rien vu mais le domaine est bien renseign�
//    :  2 - L'utilisateur a annul�
//    : -1 - Erreur (serveur non disponible, ...)
//-----------------------------------------------------------------------------
int SelectDomain(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;
	char szRequest[255+1];
	char *pszResult=NULL;
	BSTR bstrXML=NULL;
	HRESULT hr;
	IXMLDOMDocument *pDoc=NULL;
	IXMLDOMNode		*pRoot=NULL;
	IXMLDOMNode		*pNode=NULL;
	IXMLDOMNode		*pChildApp=NULL;
	IXMLDOMNode		*pChildElement=NULL;
	IXMLDOMNode		*pNextChildApp=NULL;
	IXMLDOMNode		*pNextChildElement=NULL;
	VARIANT_BOOL	vbXMLLoaded=VARIANT_FALSE;
	BSTR bstrNodeName=NULL;
	T_DOMAIN tDomains[50];
	char tmp[10];
	int iNbDomains=0;
	int ret;

	// requete le serveur pour obtenir la liste des domaines
	sprintf_s(szRequest,sizeof(szRequest),"%s?action=getdomains",gszWebServiceAddress);
	TRACE((TRACE_INFO,_F_,"Requete HTTP : %s",szRequest));
	pszResult=HTTPRequest(szRequest,8,NULL);
	if (pszResult==NULL) { TRACE((TRACE_ERROR,_F_,"HTTPRequest(%s)=NULL",szRequest)); goto end; }
	bstrXML=GetBSTRFromSZ(pszResult);
	if (bstrXML==NULL) goto end;

	// analyse le contenu XML retourn�
	hr = CoCreateInstance(CLSID_DOMDocument30, NULL, CLSCTX_INPROC_SERVER, IID_IXMLDOMDocument,(void**)&pDoc);
	if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"CoCreateInstance(IID_IXMLDOMDocument)=0x%08lx",hr)); goto end; }
	hr = pDoc->loadXML(bstrXML,&vbXMLLoaded);
	if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"pXMLDoc->loadXML()=0x%08lx",hr)); goto end; }
	if (vbXMLLoaded==VARIANT_FALSE) { TRACE((TRACE_ERROR,_F_,"pXMLDoc->loadXML() returned FALSE")); goto end; }
	hr = pDoc->QueryInterface(IID_IXMLDOMNode, (void **)&pRoot);
	if (FAILED(hr))	{ TRACE((TRACE_ERROR,_F_,"pXMLDoc->QueryInterface(IID_IXMLDOMNode)=0x%08lx",hr)); goto end;	}
	hr=pRoot->get_firstChild(&pNode);
	if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"pRoot->get_firstChild(&pNode)")); goto end; }
	hr=pNode->get_firstChild(&pChildApp);
	if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"pNode->get_firstChild(&pChildApp)")); goto end; }
	while (pChildApp!=NULL) 
	{
		TRACE((TRACE_DEBUG,_F_,"<domain>"));
		hr=pChildApp->get_firstChild(&pChildElement);
		if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"pNode->get_firstChild(&pChildElement)")); goto end; }
		while (pChildElement!=NULL) 
		{
			hr=pChildElement->get_nodeName(&bstrNodeName);
			if (FAILED(hr)) { TRACE((TRACE_ERROR,_F_,"pChild->get_nodeName()")); goto end; }
			TRACE((TRACE_DEBUG,_F_,"<%S>",bstrNodeName));
			
			if (CompareBSTRtoSZ(bstrNodeName,"id")) 
			{
				StoreNodeValue(tmp,sizeof(tmp),pChildElement);
				tDomains[iNbDomains].iDomainId=atoi(tmp);
			}
			else if (CompareBSTRtoSZ(bstrNodeName,"label")) 
			{
				StoreNodeValue(tDomains[iNbDomains].szDomainLabel,sizeof(tDomains[iNbDomains].szDomainLabel),pChildElement);
			}
			// rechercher ses fr�res et soeurs
			pChildElement->get_nextSibling(&pNextChildElement);
			pChildElement->Release();
			pChildElement=pNextChildElement;
		} // while(pChild!=NULL)
		// rechercher ses fr�res et soeurs
		pChildApp->get_nextSibling(&pNextChildApp);
		pChildApp->Release();
		pChildApp=pNextChildApp;
		TRACE((TRACE_DEBUG,_F_,"</domain>"));
		iNbDomains++;
	} // while(pNode!=NULL)
	tDomains[iNbDomains].iDomainId=-1;

#ifdef TRACES_ACTIVEES
	int trace_i;
	for (trace_i=0;trace_i<iNbDomains;trace_i++)
	{
		TRACE((TRACE_INFO,_F_,"Domaine %d : id=%d label=%s",trace_i,tDomains[trace_i].iDomainId,tDomains[trace_i].szDomainLabel));
	}
#endif

	if (iNbDomains==0) // aucun domaine
	{
		giDomainId=1; *gszDomainLabel=0;
		rc=1; goto end;
	}
	else if (iNbDomains==1) // domaine commun -> renseigne le domaine commun
	{
		giDomainId=tDomains[0].iDomainId;
		strcpy_s(gszDomainLabel,sizeof(gszDomainLabel),tDomains[0].szDomainLabel);
		rc=1; goto end;
	}
	else if (iNbDomains==2) // domaine commun + 1 domaine sp�cifique -> renseigne le domaine sp�cifique
	{
		giDomainId=tDomains[1].iDomainId;
		strcpy_s(gszDomainLabel,sizeof(gszDomainLabel),tDomains[1].szDomainLabel);
		rc=1; goto end;
	}
	else // plus de 2 domaines, demande � l'utilisateur de choisir
	{
		ret=DialogBoxParam(ghInstance,MAKEINTRESOURCE(IDD_SELECT_DOMAIN),NULL,SelectDomainDialogProc,(LPARAM)tDomains);
		if (ret==IDCANCEL) { rc=2; goto end; }
	}
	rc=0;
end:
	SaveConfigHeader();
	if (bstrXML!=NULL) SysFreeString(bstrXML);
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// WinMain()
//-----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow)
{
	UNREFERENCED_PARAMETER(nCmdShow);
	UNREFERENCED_PARAMETER(hPrevInstance);

	int rc;
	int iError=0; // v0.88 : message d'erreur au d�marrage
	MSG msg;
	int len;
	int rcSystray=-1;
    HANDLE hMutex=NULL;
	BOOL bLaunchApp=FALSE;
	BOOL bConnectApp=FALSE;
	BOOL bAlreadyLaunched=FALSE;
	OSVERSIONINFO osvi;
	BOOL b64=false;
	BOOL bMigrationWindowsSSO=FALSE;
	
	// init des traces
	TRACE_OPEN();
	TRACE((TRACE_ENTER,_F_, ""));
	
	// init de toutes les globales
	ghInstance=hInstance;
	gptActions=NULL;
	giBadPwdCount=0;
	gbSSOActif=true;
	ghIconSystrayActive=NULL;
	ghIconSystrayInactive=NULL; 
	ghIconLoupe=NULL;
	ghCursorHand=NULL;
	ghLogo=NULL;
	ghImageList=NULL;
	guiNbWEBSSO=0;
	guiNbWINSSO=0;
	guiNbPOPSSO=0;
	giaccChildCountErrors=0;
	giaccChildErrors=0;
	giBadPwdCount=0;
	gwAskPwd=NULL; // 0.65 anti r�-entrance fen�tre saisie pwd
	ghKey1=NULL;
	ghKey2=NULL;
	time_t tNow,tLastPwdChange;
	gbRecoveryRunning=FALSE;
	gpSid=NULL;
	gpszRDN=NULL;

	
	gSalts.bPBKDF2PwdSaltReady=FALSE;
	gSalts.bPBKDF2KeySaltReady=FALSE;

	if (strlen(lpCmdLine)>_MAX_PATH) { iError=-1; goto end; } // j'aime pas les petits malins ;-)

	TRACE((TRACE_INFO,_F_,"lpCmdLine=%s",lpCmdLine));

	guiLaunchAppMsg=RegisterWindowMessage("swsso-launchapp");
	if (guiLaunchAppMsg==0)
	{
		TRACE((TRACE_ERROR,_F_,"RegisterWindowMessage(swsso-launchapp)=%d",GetLastError()));
		// peut-�tre pas la peine d'emp�cher swsso de d�marrer pour �a...
	}
	else
	{
		TRACE((TRACE_INFO,_F_,"RegisterWindowMessage(swsso-launchapp) OK -> msg=0x%08lx",guiLaunchAppMsg));
	}
	guiConnectAppMsg=RegisterWindowMessage("swsso-connectapp");
	if (guiConnectAppMsg==0)
	{
		TRACE((TRACE_ERROR,_F_,"RegisterWindowMessage(swsso-connectapp)=%d",GetLastError()));
		// peut-�tre pas la peine d'emp�cher swsso de d�marrer pour �a...
	}
	else
	{
		TRACE((TRACE_INFO,_F_,"RegisterWindowMessage(swsso-connectapp) OK -> msg=0x%08lx",guiConnectAppMsg));
	}
	// 0.92 : r�cup�ration version OS pour traitements sp�cifiques Vista et/ou Seven
	// Remarque : pas tout � fait juste, mais convient pour les postes de travail. A revoir pour serveurs.
	ZeroMemory(&osvi,sizeof(OSVERSIONINFO));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (GetVersionEx(&osvi))
	{
		TRACE((TRACE_DEBUG,_F_,"dwMajorVersion=%d dwMinorVersion=%d",osvi.dwMajorVersion,osvi.dwMinorVersion));
		if (osvi.dwMajorVersion==6 && osvi.dwMinorVersion==2) giOSVersion=OS_WINDOWS_8;
		else if (osvi.dwMajorVersion==6 && osvi.dwMinorVersion==1) giOSVersion=OS_WINDOWS_7;
		else if (osvi.dwMajorVersion==6 && osvi.dwMinorVersion==0) giOSVersion=OS_WINDOWS_VISTA;
		else if (osvi.dwMajorVersion==5 && osvi.dwMinorVersion==1) giOSVersion=OS_WINDOWS_XP;
	}
	IsWow64Process(GetCurrentProcess(), &b64) ;
	giOSBits=b64?OS_64:OS_32;
	TRACE((TRACE_INFO,_F_,"giOSVersion=%d giOSBits=%d",giOSVersion,giOSBits));

	// 0.91 : si la ligne de commande contient le param�tre -launchapp, ouvre la fen�tre de lancement d'appli
	//        soit en postant � message � swsso si d�j� lanc�, soit par appel � ShowAppNsites en fin de WinMain()
	if (strnistr(lpCmdLine,"-launchapp",-1)!=NULL && guiLaunchAppMsg!=0) 
	{
		bLaunchApp=TRUE;
		// supprime le param�tre -launchapp de la ligne de commande 
		// pour traitement du path �ventuellement sp�cifi� pour le .ini
		lpCmdLine+=strlen("-launchapp");
		if (*lpCmdLine==' ') lpCmdLine++;
		TRACE((TRACE_INFO,_F_,"lpCmdLine=%s",lpCmdLine));
	}
	// 0.97 : si la ligne de commande contient le param�tre -connectapp, ouvre la fen�tre de lancement d'appli
	//        soit en postant � message � swsso si d�j� lanc�, soit par appel � ShowAppNsites en fin de WinMain()
	if (strnistr(lpCmdLine,"-connectapp",-1)!=NULL && guiConnectAppMsg!=0) 
	{
		bConnectApp=TRUE;
		// supprime le param�tre -connectapp de la ligne de commande 
		// pour traitement du path �ventuellement sp�cifi� pour le .ini
		lpCmdLine+=strlen("-connectapp");
		if (*lpCmdLine==' ') lpCmdLine++;
		TRACE((TRACE_INFO,_F_,"lpCmdLine=%s",lpCmdLine));
	}


	// 0.42 v�rif pas d�j� lanc�
	hMutex=CreateMutex(NULL,TRUE,"swSSO.exe");
	bAlreadyLaunched=(GetLastError()==ERROR_ALREADY_EXISTS);
	if (bAlreadyLaunched)
	{
		TRACE((TRACE_INFO,_F_,"Une instance est deja lancee"));
		if (bLaunchApp)
		{
			TRACE((TRACE_INFO,_F_,"Demande � l'instance pr�c�dente d'ouvrir la fenetre de lancement d'applications"));
			PostMessage(HWND_BROADCAST,guiLaunchAppMsg,0,0);
		}
		if (bConnectApp)
		{
			TRACE((TRACE_INFO,_F_,"Demande � l'instance pr�c�dente de connecter l'application en avant plan"));
			PostMessage(HWND_BROADCAST,guiConnectAppMsg,0,0);
		}

		goto end;
	}
	
	// si pas de .ini pass� en param�tre, on ch das le r�p courant
	if (*lpCmdLine==0) 
	{
		len=GetCurrentDirectory(_MAX_PATH-10,gszCfgFile);
		if (len==0) { iError=-1; goto end; }
		if (gszCfgFile[len-1]!='\\')
		{
			gszCfgFile[len]='\\';
			len++;
		}
		strcpy_s(gszCfgFile+len,_MAX_PATH+1,"swSSO.ini");
	}
	else 
	{
		// ISSUE#104 et ISSUE#109
		ExpandFileName(lpCmdLine,gszCfgFile,_MAX_PATH+1);
		//strcpy_s(gszCfgFile,_MAX_PATH+1,lpCmdLine);
	}
	// inits Window et COM
	InitCommonControls();
	ghrCoIni=CoInitialize(NULL);
	if (FAILED(ghrCoIni)) 
	{
		TRACE((TRACE_ERROR,_F_,"CoInitialize hr=0x%08lx",ghrCoIni));
		iError=-1;
		goto end;
	}
	// r�cup�re username, computername, SID et domaine
	if (GetUserDomainAndComputer()!=0) { iError=-1; goto end; }

	// chargement ressources
	if (LoadIcons()!=0) { iError=-1; goto end; }

	// initialisation du module crypto
	if (swCryptInit()!=0) { iError=-1; goto end; }
	
	// chargement des policies (password, global et enterprise)
	LoadPolicies();

	// lecture du header de la config (=lecture section swSSO = version et protection)
	if (GetConfigHeader()!=0) 
	{
		iError=-2;
		goto end;
	}

	// bienvenue (>0.51)
	if (*gszCfgVersion==0) // version <0.50 ou premier lancement...
	{
		strcpy_s(gszCfgVersion,4,gcszCfgVersion);
		// affichage boite de choix de protection mots de passe
		if (gbPasswordChoiceLevel==PP_WINDOWS)
		{
			if (InitWindowsSSO()) goto end;
			giPwdProtection=PP_WINDOWS;
			SaveConfigHeader();
		}
		else
		{
			if (DialogBox(ghInstance,MAKEINTRESOURCE(IDD_SIMPLE_PWD_CHOICE),NULL,SimplePwdChoiceDialogProc)!=IDOK) goto end;
		}
	}
	else // 
	{
		// force la migration en SSO Windows si configur� en base de registre
		if (gbPasswordChoiceLevel==PP_WINDOWS)
		{
			TRACE((TRACE_DEBUG,_F_,"PP_WINDOWS demand� en base de registre, on force la migration"));
			giPwdProtection=PP_WINDOWS;
		}

		if (giPwdProtection==PP_ENCRYPTED)
		{
			// regarde s'il y a une r�init de mdp en cours
			int ret=RecoveryResponse(NULL);
			if (ret==0) // il y a eu une r�init et �a a bien march� :-)
			{ 
				// transchiffrement plus tard une fois que les configs sont charg�es en m�moire
				gbRecoveryRunning=TRUE;
			}
			else if (ret==-2)  // pas de r�init
			{
				if (AskPwd(NULL,TRUE)!=0) goto end;
			}
			else if (ret==-5)  // l'utilisateur a demand� de reg�n�rer le challenge (ISSUE#121)
			{
				RecoveryChallenge(NULL);
				goto end;
			}
			else // il y a eu une r�init et �a n'a pas march� :-(
			{
				goto end;
			}
		}
		else if (giPwdProtection==PP_WINDOWS) // couplage mot de passe Windows
		{
			char szConfigHashedPwd[SALT_LEN*2+HASH_LEN*2+1];
			int len;
			
			// Regarde si l'utilisateur utilisait un mot de passe avant de demander le couplage Windows
			GetPrivateProfileString("swSSO","pwdValue","",szConfigHashedPwd,sizeof(szConfigHashedPwd),gszCfgFile);
			TRACE((TRACE_DEBUG,_F_,"pwdValue=%s",szConfigHashedPwd));
			len=strlen(szConfigHashedPwd);
			if (len==PBKDF2_PWD_LEN*2)
			{
				char szPwd[LEN_PWD+1];
				int ret=DPAPIGetMasterPwd(szPwd);
				SecureZeroMemory(szPwd,LEN_PWD+1); // pas besoin du mdp, c'�tait juste pour savoir si la cl� �tait pr�sente et valide
				if (ret!=0)
				{
					// N'affiche pas le message qui indique que le mot de passe maitre va �tre demand� une derni�re fois
					// si jamais l'utilisateur avait enregistr� son mot de passe
					MessageBox(NULL,GetString(IDS_INFO_WINDOWS_SSO_MIGRATION),"swSSO",MB_OK | MB_ICONINFORMATION);
				}
				giPwdProtection=PP_ENCRYPTED; // bidouille pour avoir le bon message dans la fen�tre AskPwd...
				if (AskPwd(NULL,TRUE)!=0) goto end;
				giPwdProtection=PP_WINDOWS;
				bMigrationWindowsSSO=TRUE; // On note de faire la migration (se fait plus tard une fois les configs charg�es)
			}
			else if (len==0) // L'utilisateur est d�j� en mode PP_WINDOWS
			{
				// regarde s'il y a une r�init de mdp en cours
				int ret=RecoveryResponse(NULL);
				if (ret==0) // il y a eu une r�init et �a a bien march� :-)
				{ 
					// transchiffrement plus tard une fois que les configs sont charg�es en m�moire
					gbRecoveryRunning=TRUE;
				}
				else if (ret==-2) // pas de r�init
				{
					if (CheckWindowsPwd(&bMigrationWindowsSSO)!=0) goto end;
				}
				else // il y a eu une r�init et �a n'a pas march� :-(
				{
					goto end;
				}
			}
			else // L'utilisateur a une vieille version de swSSO ou a un probl�me avec son .ini...
			{
				TRACE((TRACE_ERROR,_F_,"len(pwdValue)=%d",len));
				MessageBox(NULL,GetString(IDS_ERROR_WINDOWS_SSO_VER),"swSSO",MB_OK | MB_ICONSTOP);
				goto end;
			}
		}
		// 0.93B1 : log authentification primaire r�ussie
		swLogEvent(EVENTLOG_INFORMATION_TYPE,MSG_PRIMARY_LOGIN_SUCCESS,NULL,NULL,NULL,0);
	}

	// 0.80B9 : lit la config proxy pour ce poste de travail.
	// Remarque : n'est pas fait dans GetConfigHeader car on a besoin de la cl�
	//			  d�riv�e du mot de passe maitre pour d�chiffrement le mdp proxy
	GetProxyConfig(gszComputerName,&gbInternetUseProxy,gszProxyURL,gszProxyUser,gszProxyPwd);

	// allocation du tableau d'actions
	gptActions=(T_ACTION*)malloc(NB_MAX_APPLICATIONS*sizeof(T_ACTION));
	TRACE((TRACE_DEBUG,_F_,"malloc (%d)",NB_MAX_APPLICATIONS*sizeof(T_ACTION)));
	if (gptActions==NULL)
	{
		TRACE((TRACE_ERROR,_F_,"malloc (%d)",NB_MAX_APPLICATIONS*sizeof(T_ACTION)));
		iError=-1;
		goto end;
	}
	// 0.92B5 : pour corriger bug cat�gories perdues en 0.92B3, LoadApplications passe APRES LoadCategories
	// lecture des cat�gories
	if (LoadCategories()!=0) { iError=-2; goto end; }
	// lecture des applications configur�es
	if (LoadApplications()==-1) { iError=-2; goto end; }
	
	// v�rifie la date de dernier changement de mot de passe
	// attention, comme il y a transchiffrement des id&pwd et des mdp proxy, il 
	// faut bien que ces infos aient �t� lues avant un �ventuel changement de mot de passe impos� !
	if (giPwdProtection==PP_ENCRYPTED)
	{
		if (giPwdPolicy_MaxAge!=0)
		{
			time(&tNow);
			tLastPwdChange=GetMasterPwdLastChange();
			TRACE((TRACE_INFO,_F_,"tNow              =%ld",tNow));
			TRACE((TRACE_INFO,_F_,"tLastPwdChange    =%ld",tLastPwdChange));
			TRACE((TRACE_INFO,_F_,"diff              =%ld",tNow-tLastPwdChange));
			TRACE((TRACE_INFO,_F_,"giPwdPolicy_MaxAge=%ld",giPwdPolicy_MaxAge));
			if ((tNow-tLastPwdChange)>(giPwdPolicy_MaxAge*86400))
			{
				// impose le changement de mot de passe
				if (WindowChangeMasterPwd(TRUE)!=0) goto end;
			}
		}
	}
	// 0.91 : propose � l'utilisateur de r�cup�rer toutes les configurations disponibles sur le serveur
	TRACE((TRACE_DEBUG,_F_,"giNbActions=%d gbGetAllConfigsAtFirstStart=%d giDomainId=%d",giNbActions,gbGetAllConfigsAtFirstStart,giDomainId));
	if (giNbActions==0 && gbGetAllConfigsAtFirstStart) 
	{
		// 0.94 : gestion des domaines
		if (giDomainId==1) // domaine non renseign� dans le .ini 
		{
			int ret= SelectDomain();
			// ret:  0 - OK, l'utilisateur a choisi, le domaine est renseign� dans giDomainId et gszDomainLabel
			//    :  1 - Il n'y avait qu'un seul domaine, l'utilisateur n'a rien vu mais le domaine est bien renseign�
			//    :  2 - L'utilisateur a annul�
			//    : -1 - Erreur (serveur non disponible, ...)
			if (ret==0 || ret==1) GetAllConfigsFromServer();
			else if (ret==2) goto end;
			else if (ret==-1) { MessageBox(NULL,GetString(IDS_GET_ALL_CONFIGS_ERROR),"swSSO",MB_OK | MB_ICONEXCLAMATION); }
		}
		// 0.92 / ISSUE#26 : n'affiche pas la demande si gbDisplayConfigsNotifications=FALSE
		else if (!gbDisplayConfigsNotifications || MessageBox(NULL,GetString(IDS_GET_ALL_CONFIGS),"swSSO",MB_YESNO | MB_ICONQUESTION)==IDYES) 
		{
			GetAllConfigsFromServer();
		}
	}
	else
	{
		// 0.91 : si demand�, r�cup�re les nouvelles configurations et/ou les configurations modifi�es
		if (gbGetNewConfigsAtStart || gbGetModifiedConfigsAtStart)
		{
			GetNewOrModifiedConfigsFromServer();
		}
	}
	// ISSUE#59 : ce code �tait avant dans LoadCategories().
	// D�plac� dans winmain pour ne pas l'ex�cuter si des cat�gories ont �t� r�cup�r�es depuis le serveur
	if (giNbCategories==0) // si aucune cat�gorie, cr�e la cat�gorie "non class�"
	{
		strcpy_s(gptCategories[0].szLabel,LEN_CATEGORY_LABEL+1,GetString(IDS_NON_CLASSE));
		gptCategories[0].id=0;
		gptCategories[0].bExpanded=TRUE;
		giNbCategories=1;
		giNextCategId=1;
		WritePrivateProfileString("swSSO-Categories","0",gptCategories[0].szLabel,gszCfgFile);
	}

	// initialisation SSO Web (IE)
	if (SSOWebInit()!=0) { iError=-1; goto end; }
	
	// cr�ation fen�tre technique (r�ception des messages)
	gwMain=CreateMainWindow();
	if (gwMain==NULL) { iError=-1; goto end; }
	
	// inscription pour r�ception des notifs de verrouillage de session
	gbRegisterSessionNotification=WTSRegisterSessionNotification(gwMain,NOTIFY_FOR_THIS_SESSION);
	TRACE((TRACE_DEBUG,_F_,"WTSRegisterSessionNotification() -> OK"));
	if (!gbRegisterSessionNotification)
	{
		// cause possible de l'�chec : "If this function is called before the dependent services 
		// of Terminal Services have started, an RPC_S_INVALID_BINDING error code may be returned"
		// Du coup l'id�e est de r�essayer plus tard (1 minute) avec un timer
		TRACE((TRACE_ERROR,_F_,"WTSRegisterSessionNotification()=%ld [REESSAI DANS 15 SECONDES]",GetLastError()));
		giRegisterSessionNotificationTimer=SetTimer(NULL,0,15000,RegisterSessionNotificationTimerProc);
		giNbRegisterSessionNotificationTries++;
	}

	// cr�ation icone systray
	rcSystray=CreateSystray(gwMain);
	// 0.71 - modif pour CMH : on ne sort plus en erreur
	// si la cr�ation du systray �choue.
	// if (rcSystray!=0) goto end;

	// 0.80 si demand�, v�rification des mises � jour sur internet
	if (gbInternetCheckVersion) InternetCheckVersion();

	// 0.42 premiere utilisation (fichier vide) => affichage fen�tre config
	// 0.80 -> n'est plus n�cessaire... on peut configurer sans ouvrir cette fen�tre ("ajouter cette application")
	// if (giNbActions==0) ShowConfig(0);

	// 0.93B4 : si gbParseWindowsOnStart=FALSE, ajoute toutes les fen�tres ouvertes et visibles dans la liste des fen�tres
	if (!gbParseWindowsOnStart) ExcludeOpenWindows();

	if (*szPwdMigration093!=0) 
	{
		rc=Migration093(NULL,szPwdMigration093);
		SecureZeroMemory(szPwdMigration093,sizeof(szPwdMigration093));
		if (rc!=0) goto end;
	}

	if (bMigrationWindowsSSO)
	{
		rc=MigrationWindowsSSO();
		if (rc!=0) goto end;
		MessageBox(NULL,GetString(IDS_SYNCHRO_PWD_OK),"swSSO",MB_OK | MB_ICONINFORMATION);
	}

	if (gbRecoveryRunning)
	{
		// demande le nouveau mot de passe
		if (giPwdProtection==PP_ENCRYPTED)
		{
			//ChangeMasterPwd("new");
			//RecoverySetNewMasterPwd();
			if (WindowChangeMasterPwd(TRUE)!=0) goto end;
		}
		else // PP_WINDOWS
		{
			if (ChangeWindowsPwd()!=0) goto end;
		}
		gbRecoveryRunning=FALSE;
		// supprime le recovery running
		WritePrivateProfileString("swSSO","recoveryRunning","",gszCfgFile);
		// 
		MessageBox(NULL,GetString(giPwdProtection==PP_ENCRYPTED?IDS_RECOVERY_ENCRYPTED_OK:IDS_RECOVERY_WINDOWS_OK),"swSSO",MB_ICONINFORMATION | MB_OK);
		swLogEvent(EVENTLOG_INFORMATION_TYPE,MSG_RECOVERY_SUCCESS,NULL,NULL,NULL,0);
	}

	if (giPwdProtection==PP_WINDOWS)
	{
		char szEventName[1024];
		sprintf_s(szEventName,"Global\\swsso-pwdchange-%s-%s",gpszRDN,gszUserName);
		ghPwdChangeEvent=CreateEvent(NULL,FALSE,FALSE,szEventName);
		if (ghPwdChangeEvent==NULL)
		{
			TRACE((TRACE_ERROR,_F_,"CreateEvent(swsso-pwdchange)=%d",GetLastError()));
				iError=-1;
				goto end;
		}
	}

	if (LaunchTimer()!=0)
	{
		iError=-1;
		goto end;
	}

	// Si -launchapp, ouvre la fen�tre ShowAppNsites
	if (bLaunchApp) ShowLaunchApp();

	// Ici on peut consid�rer que swSSO est bien d�marr� et que l'utilisateur est connect�
	// Prise de la date de login pour les stats
	GetLocalTime(&gLastLoginTime);

	// d�clenchement du timer pour enum�ration de fen�tres toutes les 500ms
	// boucle de message, dont on ne sortira que par un PostQuitMessage()
	while((rc=GetMessage(&msg,NULL,0,0))!=0)
    { 
		if (rc!=-1)
	    {
			if (msg.message==guiLaunchAppMsg) 
			{
				TRACE((TRACE_INFO,_F_,"Message recu : swsso-launchapp (0x%08lx)",guiLaunchAppMsg));
				PostMessage(gwMain,WM_COMMAND,MAKEWORD(TRAY_MENU_LAUNCH_APP,0),0);
			}
			else if (msg.message==guiConnectAppMsg) 
			{
				TRACE((TRACE_INFO,_F_,"Message recu : swsso-connectapp (0x%08lx)",guiConnectAppMsg));
				PostMessage(gwMain,WM_COMMAND,MAKEWORD(TRAY_MENU_SSO_NOW,0),0);
			} 
			else
			{
		        TranslateMessage(&msg); 
		        DispatchMessage(&msg); 
			}
	    }
	}
	iError=0;
end:
	if (iError==-1)
	{
		swLogEvent(EVENTLOG_ERROR_TYPE,MSG_GENERIC_START_ERROR,NULL,NULL,NULL,0);
		char szErrMsg[1024+1];
		strcpy_s(szErrMsg,sizeof(szErrMsg),GetString(IDS_GENERIC_STARTING_ERROR));
		MessageBox(NULL,szErrMsg,GetString(IDS_MESSAGEBOX_TITLE),MB_OK | MB_ICONSTOP);
	}
	else if (iError==-2) 
	{
		swLogEvent(EVENTLOG_ERROR_TYPE,MSG_SWSSO_INI_CORRUPTED,gszCfgFile,NULL,NULL,0);
		MessageBox(NULL,gszErrorMessageIniFile,GetString(IDS_MESSAGEBOX_TITLE),MB_OK | MB_ICONSTOP);
	}
	else
	{
		swLogEvent(EVENTLOG_INFORMATION_TYPE,MSG_QUIT,NULL,NULL,NULL,0);
		if (gbStat) swStat(); // 0.99 - ISSUE#106
	}

	// on lib�re tout avant de terminer
	swCryptDestroyKey(ghKey1);
	swCryptDestroyKey(ghKey2);
	swCryptTerm();
	SSOWebTerm();
	UnloadIcons();
	if (giTimer!=0) KillTimer(NULL,giTimer);
	if (ghPwdChangeEvent!=NULL) CloseHandle(ghPwdChangeEvent);
	if (giRegisterSessionNotificationTimer!=0) KillTimer(NULL,giRegisterSessionNotificationTimer);
	if (gbRegisterSessionNotification) WTSUnRegisterSessionNotification(gwMain);
	if (rcSystray==0) DestroySystray(gwMain);
	if (ghBoldFont!=NULL) DeleteObject(ghBoldFont);
	// 0.65 : suppression : UnregisterClass("swSSOClass",ghInstance);
	// Inutile (source MSDN) :
	// "All window classes that an application registers are unregistered when it terminates."
	if (gwMain!=NULL) DestroyWindow(gwMain); // 0.90 : par contre c'est bien de d�truire gwMain (peut-�tre � l'origine du bug de non verrouillage JMA ?)
	if (gptActions!=NULL) free(gptActions);
	if (gptCategories!=NULL) free(gptCategories); 
	if (ghrCoIni==S_OK) CoUninitialize();
	if (hMutex!=NULL) ReleaseMutex(hMutex);
	if (gpRecoveryKeyValue!=NULL) free(gpRecoveryKeyValue);
	if (gpSid!=NULL) free(gpSid);
	if (gpszRDN!=NULL) free(gpszRDN);

	TRACE((TRACE_LEAVE,_F_, ""));
	TRACE_CLOSE();
	return 0; 
}
