// SQLConnect_ref.cpp  
// compile with: odbc32.lib  
#include <windows.h>  
#include <sqlext.h>  
#include <stdio.h> 
#include <locale.h> // ★ 로케일 설정을 위해 추가

/************************************************************************
/* HandleDiagnosticRecord : display error/warning information
/*
/* Parameters:
/* hHandle ODBC handle
/* hType Type of handle (SQL_HANDLE_STMT, SQL_HANDLE_ENV, SQL_HANDLE_DBC)
/* RetCode Return code of failing command
/************************************************************************/
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
    SQLSMALLINT iRec = 0;
    SQLINTEGER iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
    if (RetCode == SQL_INVALID_HANDLE) {
        fwprintf(stderr, L"Invalid handle!\n");
        return;
    }
    while (SQLGetDiagRecW(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
        (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
        // Hide data truncated..
        if (wcsncmp(wszState, L"01004", 5)) {
            fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}

int main() {    
    setlocale(LC_ALL, "korean");

    SQLHENV henv;
    SQLHDBC hdbc;
    SQLHSTMT hstmt;
    SQLRETURN retcode;

    // Allocate environment handle  
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    // Set the ODBC version environment attribute  
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

        // Allocate connection handle  
        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            // Set login timeout to 5 seconds  
            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // Connect to data source  
                retcode = SQLConnectA(hdbc, (SQLCHAR*)"2026_Game_Test", SQL_NTS, (SQLCHAR*)NULL, 0, NULL, 0);

                // 연결 성공 시
                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                    wprintf(L"DB 연결 성공!\n");

                    // Allocate statement handle  
                    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

                    // Process data  
                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
                        // TODO: 쿼리 실행 등의 작업 진행
                        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    }

                    SQLDisconnect(hdbc);
                }
                // 연결 실패 시 에러 출력
                else {
                    wprintf(L"DB 연결 실패...\n");
                    HandleDiagnosticRecord(hdbc, SQL_HANDLE_DBC, retcode);
                }

                SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
            }
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    return 0;
}

