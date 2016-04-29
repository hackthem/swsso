
//
//                                  swSSO
//
//       SSO Windows et Web avec Internet Explorer, Firefox, Mozilla...
//
//                Copyright (C) 2004-2016 - Sylvain WERDEFROY
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

#include "stdafx.h"
HINSTANCE ghInstance;

SID *gpSid=NULL;
char *gpszRDN=NULL;
char gszUserName[UNLEN+1]="";
char gszCfgFile[_MAX_PATH+1];
UINT guiContinueMsg=0;
T_SALT gSalts;
HCRYPTKEY ghKey1=NULL;
char gBufPassword[256];

//-----------------------------------------------------------------------------
// GetUserAndDomain() 
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
int GetUserAndDomain(void)
{
	TRACE((TRACE_ENTER,_F_,""));

	DWORD cbRDN,cbSid;
	SID_NAME_USE eUse;
	DWORD lenUserName;
	int rc=-1;

	// UserName
	lenUserName=sizeof(gszUserName); 
	if (!GetUserName(gszUserName,&lenUserName))
	{
		TRACE((TRACE_ERROR,_F_,"GetUserName(%d)",GetLastError())); goto end;
	}

	// d�termine le SID de l'utilisateur courant et r�cup�re le nom de domaine
	cbSid=0;
	cbRDN=0;
	LookupAccountName(NULL,gszUserName,NULL,&cbSid,NULL,&cbRDN,&eUse); // pas de test d'erreur, car la fonction �choue forc�ment
	if (GetLastError()!=ERROR_INSUFFICIENT_BUFFER)
	{
		TRACE((TRACE_ERROR,_F_,"LookupAccountName[1](%s)=%d",gszUserName,GetLastError()));
		goto end;
	}
	gpSid=(SID *)malloc(cbSid); if (gpSid==NULL) { TRACE((TRACE_ERROR,_F_,"malloc(%d)",cbSid)); goto end; }
	gpszRDN=(char *)malloc(cbRDN); if (gpszRDN==NULL) { TRACE((TRACE_ERROR,_F_,"malloc(%d)",cbRDN)); goto end; }
	if(!LookupAccountName(NULL,gszUserName,gpSid,&cbSid,gpszRDN,&cbRDN,&eUse))
	{
		TRACE((TRACE_ERROR,_F_,"LookupAccountName[2](%s)=%d",gszUserName,GetLastError()));
		goto end;
	}
	TRACE((TRACE_INFO,_F_,"gszUserName=%s gpszRDN=%s",gszUserName,gpszRDN));

	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_,"rc=%d",rc));
	return rc;

}

//-----------------------------------------------------------------------------
// MainWindowProc()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
static LRESULT CALLBACK MainWindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) 
{
	return DefWindowProc(w,msg,wp,lp);
}

//-----------------------------------------------------------------------------
// CreateMainWindow()
//-----------------------------------------------------------------------------
// Cr�ation de la fen�tre technique qui recevra tous les messages du Systray
// et les notifications de verrouillage de session Windows
//-----------------------------------------------------------------------------
HWND CreateMainWindow(void)
{
	TRACE((TRACE_ENTER,_F_, ""));
	HWND wMain=NULL;
	ATOM atom=0;
	WNDCLASS wndClass;
	
	ZeroMemory(&wndClass,sizeof(WNDCLASS));
	wndClass.style=0;
	wndClass.lpfnWndProc=MainWindowProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = ghInstance;
	wndClass.hCursor = NULL;
	wndClass.lpszMenuName = 0;
	wndClass.hbrBackground = NULL;
	wndClass.hIcon = NULL;
	wndClass.lpszClassName = "swSSOMigrationClass";
	
	atom=RegisterClass(&wndClass);
	if (atom==0) goto end;
	
	wMain=CreateWindow("swSSOMigrationClass","swSSOMigrationWindow",0,0,0,0,0,NULL,NULL,ghInstance,0);
	if (wMain==NULL) goto end;

end:
	TRACE((TRACE_LEAVE,_F_, "wMain=0x%08lx",wMain));
	return wMain;
}

//-----------------------------------------------------------------------------
// swSSOMigrationPart1() 
//-----------------------------------------------------------------------------
// r�cup�ration user et domaine
// lecture des sels dans le .ini
// envoi des sels � swSSOSVC (PUTPSKS)
// demande les cl�s swSSOSVC (GETPHKD)
// demande du mot de passe � swSSOSVC (GETPASS)
// d�chiffrement du mot de passe
// rechiffrement du mot de passe (CRYPTPROTECTMEMORY_CROSS_PROCESS)
//-----------------------------------------------------------------------------
int swSSOMigrationPart1()
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;
	char bufRequest[1024];
	char bufResponse[1024];
	DWORD dwLenResponse;
	BYTE AESKeyData[AES256_KEY_LEN];
	char *pszPassword=NULL;

	// r�cup�re user et domaine
	if (GetUserAndDomain()!=0) goto end;
	// lecture des sels dans le .ini
	if (swReadPBKDF2Salt()!=0) goto end;
	// Envoie les sels � swSSOSVC : V02:PUTPSKS:domain(256octets)username(256octets)PwdSalt(64octets)KeySalt(64octets)
	SecureZeroMemory(bufRequest,sizeof(bufRequest));
	memcpy(bufRequest,"V02:PUTPSKS:",12);
	memcpy(bufRequest+12,gpszRDN,strlen(gpszRDN)+1);
	memcpy(bufRequest+12+DOMAIN_LEN,gszUserName,strlen(gszUserName)+1);
	memcpy(bufRequest+12+DOMAIN_LEN+USER_LEN,gSalts.bufPBKDF2PwdSalt,PBKDF2_SALT_LEN);
	memcpy(bufRequest+12+DOMAIN_LEN+USER_LEN+PBKDF2_SALT_LEN,gSalts.bufPBKDF2KeySalt,PBKDF2_SALT_LEN);
	if (swPipeWrite(bufRequest,12+DOMAIN_LEN+USER_LEN+PBKDF2_SALT_LEN*2,bufResponse,sizeof(bufResponse),&dwLenResponse)!=0) 
	{
		TRACE((TRACE_ERROR,_F_,"Erreur swPipeWrite()")); goto end;
	}
	// Demande le keydata � swSSOSVC : V02:GETPHKD:CUR:domain(256octets)username(256octets)
	SecureZeroMemory(bufRequest,sizeof(bufRequest));
	memcpy(bufRequest,"V02:GETPHKD:CUR:",16);
	memcpy(bufRequest+16,gpszRDN,strlen(gpszRDN)+1);
	memcpy(bufRequest+16+DOMAIN_LEN,gszUserName,strlen(gszUserName)+1);
	if (swPipeWrite(bufRequest,16+DOMAIN_LEN+USER_LEN,bufResponse,sizeof(bufResponse),&dwLenResponse)!=0) 
	{
		TRACE((TRACE_ERROR,_F_,"Erreur swPipeWrite()")); goto end;
	}
	if (dwLenResponse!=PBKDF2_PWD_LEN+AES256_KEY_LEN)
	{
		TRACE((TRACE_ERROR,_F_,"dwLenResponse=%ld (attendu=%d)",dwLenResponse,PBKDF2_PWD_LEN+AES256_KEY_LEN)); goto end;
	}
	// Cr�e la cl� de chiffrement des mots de passe secondaires
	memcpy(AESKeyData,bufResponse+PBKDF2_PWD_LEN,AES256_KEY_LEN);
	swCreateAESKeyFromKeyData(AESKeyData,&ghKey1);
	// Demande le mot de passe � swSSOSVC : V02:GETPASS:domain(256octets)username(256octets)
	SecureZeroMemory(bufRequest,sizeof(bufRequest));
	memcpy(bufRequest,"V02:GETPASS:",12);
	memcpy(bufRequest+12,gpszRDN,strlen(gpszRDN)+1);
	memcpy(bufRequest+12+DOMAIN_LEN,gszUserName,strlen(gszUserName)+1);
	if (swPipeWrite(bufRequest,12+DOMAIN_LEN+USER_LEN,bufResponse,sizeof(bufResponse),&dwLenResponse)!=0) 
	{
		TRACE((TRACE_ERROR,_F_,"Erreur swPipeWrite()")); goto end;
	}
	// en retour, on a re�u le mot de passe chiffr� par la cl� d�riv�e du mot de passe (si, si)
	if (dwLenResponse!=LEN_ENCRYPTED_AES256)
	{
		TRACE((TRACE_ERROR,_F_,"Longueur reponse attendue=LEN_ENCRYPTED_AES256=%d, recue=%d",LEN_ENCRYPTED_AES256,dwLenResponse)); goto end;
	}
	bufResponse[dwLenResponse]=0;
	// d�chiffre le mot de passe
	pszPassword=swCryptDecryptString(bufResponse,ghKey1);
	if (pszPassword==NULL) goto end;
	SecureZeroMemory(gBufPassword,sizeof(gBufPassword));
	strcpy_s(gBufPassword,sizeof(gBufPassword),pszPassword);
	SecureZeroMemory(pszPassword,strlen(pszPassword));
	//TRACE_BUFFER((TRACE_PWD,_F_,(unsigned char*)gBufPassword,sizeof(gBufPassword),"gBufPassword"));
	// rechiffre le mot de passe pour le repasser � SVC dans l'�tape 2
	if (swProtectMemory(gBufPassword,sizeof(gBufPassword),CRYPTPROTECTMEMORY_CROSS_PROCESS)!=0) goto end;
	TRACE_BUFFER((TRACE_DEBUG,_F_,(unsigned char*)gBufPassword,sizeof(gBufPassword),"gBufPassword"));
	
	rc=0;
end:
	if (pszPassword!=NULL) free(pszPassword);
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// swSSOMigrationPart2() 
//-----------------------------------------------------------------------------
// envoie PUTPASS (domaine, user, password) � swSSOSVC
//-----------------------------------------------------------------------------
int swSSOMigrationPart2()
{
	TRACE((TRACE_ENTER,_F_, ""));
	int rc=-1;

	char bufRequest[1024];
	char bufResponse[1024];
	DWORD dwLenResponse;

	SecureZeroMemory(bufRequest,sizeof(bufRequest));
	memcpy(bufRequest,"V02:PUTPASS:",12);
	memcpy(bufRequest+12,gpszRDN,strlen(gpszRDN)+1);
	memcpy(bufRequest+12+DOMAIN_LEN,gszUserName,strlen(gszUserName)+1);
	memcpy(bufRequest+12+DOMAIN_LEN+USER_LEN,gBufPassword,sizeof(gBufPassword));
	
	// Envoie la requ�te
	if (swPipeWrite(bufRequest,12+DOMAIN_LEN+USER_LEN+sizeof(gBufPassword),bufResponse,sizeof(bufResponse),&dwLenResponse)!=0) 
	{
		TRACE((TRACE_ERROR,_F_,"Erreur swPipeWrite()")); goto end;
	}

	rc=0;
end:
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// WinMain() 
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow)
{
	UNREFERENCED_PARAMETER(nCmdShow);
	UNREFERENCED_PARAMETER(hPrevInstance);
	TRACE_OPEN();
	TRACE((TRACE_ENTER,_F_, ""));
	
	int rc=-1;
	int lenCmdLine;
	HANDLE hMutex=NULL;
	MSG msg;
	HWND gwMain=NULL;

	ghInstance=hInstance;
	gpSid=NULL;
	gpszRDN=NULL;

	puts("Demarrage de swSSOMigration");

	// ligne de commande
	lenCmdLine=strlen(lpCmdLine);
	if (lenCmdLine==0 || lenCmdLine>_MAX_PATH) { TRACE((TRACE_ERROR,_F_,"lenCmdLine=%d",lenCmdLine)); goto end; }
	TRACE((TRACE_INFO,_F_,"lenCmdLine=%d lpCmdLine=%s",lenCmdLine,lpCmdLine));

	// enregistrement des messages pour r�ception de param�tres en ligne de commande quand d�j�
	guiContinueMsg=RegisterWindowMessage("swssomigration-continue");
	if (guiContinueMsg==0)	{ TRACE((TRACE_ERROR,_F_,"RegisterWindowMessage(swssomigration-continue)=%d",GetLastError())); }
	TRACE((TRACE_INFO,_F_,"RegisterWindowMessage(swssomigration-continue) OK -> msg=0x%08lx",guiContinueMsg));

	// v�rif pas d�j� lanc�
	hMutex=CreateMutex(NULL,TRUE,"swSSOMigration.exe");
	if (GetLastError()==ERROR_ALREADY_EXISTS)
	{
		TRACE((TRACE_INFO,_F_,"Demande � l'instance pr�c�dente de continuer"));
		PostMessage(HWND_BROADCAST,guiContinueMsg,0,0);
		goto end;
	}
	
	// r�cup�re le nom du .ini en param�tre
	ExpandFileName(lpCmdLine,gszCfgFile,_MAX_PATH+1);
	TRACE((TRACE_INFO,_F_,"gszCfgFile=%s",gszCfgFile));
	
	// initalise la crypto
	if (swCryptInit()!=0) goto end;
	if (swProtectMemoryInit()!=0) goto end;

	// cr�ation fen�tre technique pour r�ception des messages
	gwMain=CreateMainWindow();

	puts("Debut migration partie 1/2");

	// premi�re partie de la migration
	rc=swSSOMigrationPart1();
	if (rc!=0) { puts("Fin migration partie 1/2 -- ERREUR"); goto end; }
	TRACE((TRACE_INFO,_F_,"swSSOMigrationPart1 termine, se met en attente"));
	puts("Fin migration partie 1/2 -- OK");
	puts("En attente debut migration partie 2/2");

	// boucle de message pour attendre le signal pour la 2nde partie de la migration
	while((rc=GetMessage(&msg,NULL,0,0))!=0)
    { 
		if (rc!=-1)
	    {
			if (msg.message==guiContinueMsg) 
			{
				TRACE((TRACE_INFO,_F_,"Message recu : swssomigration-continue (0x%08lx)",guiContinueMsg));
				puts("Debut migration partie 2/2");
				rc=swSSOMigrationPart2();
				if (rc==0)
					puts("Fin migration partie 2/2 -- OK");
				else
					puts("Fin migration partie 2/2 -- ERREUR"); 
				goto end;
			}
			else
			{
		        TranslateMessage(&msg); 
		        DispatchMessage(&msg); 
			}
	    }
	}

	rc=0;
end:
	if (rc==0)
		puts("Arret de swSSOMigration -- OK");
	else
		puts("Arret de swSSOMigration -- ERREUR");
	swCryptDestroyKey(ghKey1);
	swCryptTerm();
	swProtectMemoryTerm();
	if (gwMain!=NULL) DestroyWindow(gwMain); 
	if (gpSid!=NULL) free(gpSid);
	if (gpszRDN!=NULL) free(gpszRDN);
	if (hMutex!=NULL) ReleaseMutex(hMutex);
	TRACE((TRACE_LEAVE,_F_, "rc=%d",rc));
	TRACE_CLOSE();
	return rc; 
}