#include "stdafx.h"

//-----------------------------------------------------------------------------
// Install()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
int Install(void)
{
	int rc=0;
	SC_HANDLE schSCManager=NULL;
    SC_HANDLE schService=NULL;
    char szPath[_MAX_PATH];

	TRACE((TRACE_ENTER,_F_,""));
	if (!GetModuleFileName(NULL,szPath,_MAX_PATH))
    {
        TRACE((TRACE_ERROR,_F_,"GetModuleFileName()=%d",GetLastError()));
        goto end;
    }

    schSCManager = OpenSCManager(NULL,NULL, SC_MANAGER_ALL_ACCESS); 
	if (schSCManager==NULL)
    {
        TRACE((TRACE_ERROR,_F_,"OpenSCManager()=%d", GetLastError()));
        goto end;
    }

    schService = CreateService( 
        schSCManager,              // SCM database 
        "swSSOSvc",                   // name of service 
        "swSSO service",                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_AUTO_START,			// start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 
 
    if (schService == NULL) 
    {
        TRACE((TRACE_ERROR,_F_,"CreateService()=%d", GetLastError()));
        goto end;
    }
    
end:

    if (schService!=NULL) CloseServiceHandle(schService); 
    if (schSCManager!=NULL) CloseServiceHandle(schSCManager);
	TRACE((TRACE_LEAVE,_F_,"rc=0x%08lx",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// Uninstall()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
int Uninstall(void)
{
	int rc=0;
	TRACE((TRACE_ENTER,_F_,""));
	SC_HANDLE schSCManager=NULL;
    SC_HANDLE schService=NULL;
	TRACE((TRACE_ENTER,_F_,""));
	
    schSCManager=OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);
	if (schSCManager==NULL)
    {
        TRACE((TRACE_ERROR,_F_,"OpenSCManager()=%d",GetLastError()));
        goto end;
    }
	schService = OpenService(schSCManager, "swSSOSvc", SERVICE_STOP | DELETE);
    if (schService==NULL)
	{
        TRACE((TRACE_ERROR,_F_,"OpenService()=%d",GetLastError()));
        goto end;
	}

	SERVICE_STATUS status;
    ControlService(schService, SERVICE_CONTROL_STOP, &status);
	if (!DeleteService(schService))
	{
		TRACE((TRACE_ERROR,_F_,"DeleteService()=%d",GetLastError()));
		goto end;
	}
	 
end:	
	TRACE((TRACE_LEAVE,_F_,"rc=0x%08lx",rc));
	return rc;
}

//-----------------------------------------------------------------------------
// IsInstalled()
//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool IsInstalled(void)
{
	bool rc=false;
	SC_HANDLE schSCManager=NULL;
    SC_HANDLE schService=NULL;
	TRACE((TRACE_ENTER,_F_,""));
	
    schSCManager=OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);
	if (schSCManager==NULL)
    {
        TRACE((TRACE_ERROR,_F_,"OpenSCManager()=%d",GetLastError()));
        goto end;
    }
	schService=OpenService(schSCManager, "swSSOSvc", SERVICE_QUERY_CONFIG);
    if (schService!=NULL) rc=true;
    
end:
    if (schService!=NULL) CloseServiceHandle(schService); 
    if (schSCManager!=NULL) CloseServiceHandle(schSCManager);
	TRACE((TRACE_LEAVE,_F_,"rc=0x%08lx",rc));
	return rc;
}
