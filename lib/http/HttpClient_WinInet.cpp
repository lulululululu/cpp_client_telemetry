// Copyright (c) Microsoft. All rights reserved.

#include "HttpClient_WinInet.hpp"
#include "utils/Common.hpp"
#include <aria/Utils.hpp>
#include <WinInet.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>
#include <oacr.h>

namespace ARIASDK_NS_BEGIN {


class WinInetRequestWrapper : public PAL::RefCountedImpl<WinInetRequestWrapper>
{
  protected:
    HttpClient_WinInet&    m_parent;
    std::string            m_id;
    IHttpResponseCallback* m_appCallback;
    HINTERNET              m_hWinInetSession;
    HINTERNET              m_hWinInetRequest;
    HANDLE                 m_hDoneEvent;
    std::vector<uint8_t>   m_bodyBuffer;
    BYTE                   m_buffer[1024];
    DWORD                  m_bufferUsed;

  public:
    WinInetRequestWrapper(HttpClient_WinInet& parent, std::string const& id)
      : m_parent(parent),
        m_id(id),
        m_hWinInetSession(NULL),
        m_hWinInetRequest(NULL),
        m_hDoneEvent(::CreateEvent(NULL, TRUE, FALSE, NULL))
    {
        ARIASDK_LOG_DETAIL("%p WinInetRequestWrapper()", this);
    }

    WinInetRequestWrapper(WinInetRequestWrapper const&) = delete;
    WinInetRequestWrapper& operator=(WinInetRequestWrapper const&) = delete;

    ~WinInetRequestWrapper()
    {
        ARIASDK_LOG_DETAIL("%p ~WinInetRequestWrapper()", this);
        ::InternetCloseHandle(m_hWinInetRequest);
        ::InternetCloseHandle(m_hWinInetSession);
        ::CloseHandle(m_hDoneEvent);
    }

    void signalDone()
    {
        ::SetEvent(m_hDoneEvent);
    }

    void cancel()
    {
        ::InternetCloseHandle(m_hWinInetRequest);
        ::WaitForSingleObject(m_hDoneEvent, INFINITE);
        m_hWinInetRequest = NULL;
    }

#ifndef _DEBUG
// bResult is only used inside an assertion
#pragma warning(push)
#pragma warning(disable:4189)
#endif
    void send(SimpleHttpRequest* request, IHttpResponseCallback* callback)
    {
        m_appCallback = callback;

        {
			std::lock_guard<std::mutex> lock(m_parent.m_requestsMutex);
            m_parent.m_requests[m_id] = self();
        }

        URL_COMPONENTSA urlc;
        memset(&urlc, 0, sizeof(urlc));
        urlc.dwStructSize = sizeof(urlc);
        char hostname[256];
        urlc.lpszHostName = hostname;
        urlc.dwHostNameLength = sizeof(hostname);
        char path[1024];
        urlc.lpszUrlPath = path;
        urlc.dwUrlPathLength = sizeof(path);
        if (!::InternetCrackUrlA(request->m_url.data(), (DWORD)request->m_url.size(), 0, &urlc)) {
            DWORD dwError = ::GetLastError();
            ARIASDK_LOG_WARNING("InternetCrackUrl() failed: %d", dwError);
            onRequestComplete(dwError);
            return;
        }

        m_hWinInetSession = ::InternetConnectA(m_parent.m_hInternet, hostname, urlc.nPort,
            NULL, NULL, INTERNET_SERVICE_HTTP, 0, reinterpret_cast<DWORD_PTR>(this));
        if (m_hWinInetSession == NULL) {
            DWORD dwError = ::GetLastError();
            ARIASDK_LOG_WARNING("InternetConnect() failed: %d", dwError);
            onRequestComplete(dwError);
            return;
        }
        // TODO: Session handle for the same target should be cached across requests to enable keep-alive.

        PCSTR szAcceptTypes[] = {"*/*", NULL};
        m_hWinInetRequest = ::HttpOpenRequestA(
            m_hWinInetSession, request->m_method.c_str(), path, NULL, NULL, szAcceptTypes,
            INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_AUTH | INTERNET_FLAG_NO_CACHE_WRITE |
            INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE |
            INTERNET_FLAG_RELOAD | (urlc.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0),
            reinterpret_cast<DWORD_PTR>(this));
        if (m_hWinInetRequest == NULL) {
            DWORD dwError = ::GetLastError();
            ARIASDK_LOG_WARNING("HttpOpenRequest() failed: %d", dwError);
            onRequestComplete(dwError);
            return;
        }

        ::InternetSetStatusCallback(m_hWinInetRequest, &WinInetRequestWrapper::winInetCallback);

        std::ostringstream os;
        for (auto const& header : request->m_headers) {
            os << header.first << ": " << header.second << "\r\n";
        }
        ::HttpAddRequestHeadersA(m_hWinInetRequest, os.str().data(), static_cast<DWORD>(os.tellp()), HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);

        // Take over the body buffer ownership, it must stay alive until
        // the async operation finishes.
        m_bodyBuffer.swap(request->m_body);
        BOOL bResult = ::HttpSendRequest(m_hWinInetRequest, NULL, 0, m_bodyBuffer.data(), (DWORD)m_bodyBuffer.size());

        DWORD dwError = GetLastError();
        assert(!bResult);
        if (dwError != ERROR_IO_PENDING) {
            dwError = ::GetLastError();
            ARIASDK_LOG_WARNING("HttpSendRequest() failed: %d", dwError);
            onRequestComplete(dwError);
        }
    }
#ifndef _DEBUG
#pragma warning(pop)
#endif

    static void CALLBACK winInetCallback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength)
    {
#ifndef DEBUG
        UNREFERENCED_PARAMETER(dwStatusInformationLength);  // Only used inside an assertion
#endif
        OACR_USE_PTR(hInternet);

        WinInetRequestWrapper* self = reinterpret_cast<WinInetRequestWrapper*>(dwContext);

        ARIASDK_LOG_DETAIL("winInetCallback: hInternet %p, dwContext %p, dwInternetStatus %u", hInternet, dwContext, dwInternetStatus);
        // Are you looking at logs and need to decode dwInternetStatus values?
        // Go To Definition (F12) on INTERNET_STATUS_REQUEST_COMPLETE below to get to the right place of WinInet.h.

        switch (dwInternetStatus) {
            case INTERNET_STATUS_REQUEST_SENT: {
                assert(hInternet == self->m_hWinInetRequest);
                self->m_bodyBuffer.clear();
                self->m_bufferUsed = 0;
                return;
            }

            case INTERNET_STATUS_REQUEST_COMPLETE: {
                assert(dwStatusInformationLength >= sizeof(INTERNET_ASYNC_RESULT));
                INTERNET_ASYNC_RESULT& result = *static_cast<INTERNET_ASYNC_RESULT*>(lpvStatusInformation);
                assert(hInternet == self->m_hWinInetRequest);
                self->onRequestComplete(result.dwError);
                return;
            }

            default:
                return;
        }
    }

    void onRequestComplete(DWORD dwError)
    {
        if (dwError == ERROR_SUCCESS) {
            // If looking good so far, try to fetch the response body first.
            // It might potentially be another async operation which will
            // trigger INTERNET_STATUS_REQUEST_COMPLETE again.

            m_bodyBuffer.insert(m_bodyBuffer.end(), m_buffer, m_buffer + m_bufferUsed);
            do {
                m_bufferUsed = 0;
                BOOL bResult = ::InternetReadFile(m_hWinInetRequest, m_buffer, sizeof(m_buffer), &m_bufferUsed);
                if (!bResult) {
                    dwError = GetLastError();
                    if (dwError == ERROR_IO_PENDING) {
                        // Do not touch anything from this thread anymore.
                        // The buffer passed to InternetReadFile() and the
                        // read count will be filled asynchronously, so they
                        // must stay valid and writable until the next
                        // INTERNET_STATUS_REQUEST_COMPLETE callback comes
                        // (that's why those are member variables).
                        return;
                    }
                    ARIASDK_LOG_WARNING("InternetReadFile() failed: %d", dwError);
                    break;
                }

                m_bodyBuffer.insert(m_bodyBuffer.end(), m_buffer, m_buffer + m_bufferUsed);
            } while (m_bufferUsed == sizeof(m_buffer));
        }

        std::unique_ptr<SimpleHttpResponse> response(new SimpleHttpResponse(m_id));

        if (dwError == ERROR_SUCCESS) {
            response->m_result = HttpResult_OK;
            response->m_body.swap(m_bodyBuffer);

            uint32_t value = 0;
            DWORD dwSize = sizeof(value);
            BOOL bResult = ::HttpQueryInfoA(m_hWinInetRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &value, &dwSize, NULL);
            if (!bResult) {
                ARIASDK_LOG_WARNING("HttpQueryInfo(STATUS_CODE) failed: %d", GetLastError());
            }
            response->m_statusCode = value;

            char* pBuffer = reinterpret_cast<char*>(m_buffer);
            dwSize = sizeof(m_buffer) - 1;
            if (!HttpQueryInfoA(m_hWinInetRequest, HTTP_QUERY_RAW_HEADERS_CRLF, pBuffer, &dwSize, NULL)) {
                dwError = GetLastError();
                if (dwError != ERROR_INSUFFICIENT_BUFFER) {
                    ARIASDK_LOG_WARNING("HttpQueryInfo(RAW_HEADERS) failed: %d", dwError);
                    dwSize = 0;
                } else {
                    m_bodyBuffer.resize(dwSize + 1);
                    pBuffer = reinterpret_cast<char*>(m_bodyBuffer.data());
                    if (!HttpQueryInfoA(m_hWinInetRequest, HTTP_QUERY_RAW_HEADERS_CRLF, pBuffer, &dwSize, NULL)) {
                        ARIASDK_LOG_WARNING("HttpQueryInfo(RAW_HEADERS) failed twice: %d", dwError);
                        dwSize = 0;
                    }
                }
            }
            pBuffer[dwSize] = '\0';

            char const* ptr = pBuffer;
            while (*ptr) {
                char const* colon = strchr(ptr, ':');
                if (!colon) {
                    break;
                }
                std::string name(ptr, colon);

                ptr = colon + 1;
                while (*ptr == ' ') {
                    ptr++;
                }

                char const* eol = strstr(ptr, "\r\n");
                if (!eol) {
                    break;
                }
                std::string value1(ptr, eol);

                response->m_headers.add(toLower(name), value1);
                ptr = eol + 2;
            }

        } else {
            switch (dwError) {
                case ERROR_INTERNET_OPERATION_CANCELLED:
                    response->m_result = HttpResult_Aborted;
                    break;

                case ERROR_INTERNET_TIMEOUT:
                case ERROR_INTERNET_EXTENDED_ERROR:
                case ERROR_INTERNET_NAME_NOT_RESOLVED:
                case ERROR_INTERNET_ITEM_NOT_FOUND:
                case ERROR_INTERNET_CANNOT_CONNECT:
                case ERROR_INTERNET_CONNECTION_ABORTED:
                case ERROR_INTERNET_CONNECTION_RESET:
                case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
                case ERROR_INTERNET_SEC_CERT_CN_INVALID:
                case ERROR_INTERNET_HTTP_TO_HTTPS_ON_REDIR:
                case ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR:
                case ERROR_INTERNET_CHG_POST_IS_NON_SECURE:
                case ERROR_INTERNET_POST_IS_NON_SECURE:
                case ERROR_INTERNET_CLIENT_AUTH_CERT_NEEDED:
                case ERROR_INTERNET_INVALID_CA:
                case ERROR_INTERNET_HTTPS_HTTP_SUBMIT_REDIR:
                case ERROR_INTERNET_SEC_CERT_ERRORS:
                case ERROR_HTTP_DOWNLEVEL_SERVER:
                case ERROR_HTTP_INVALID_SERVER_RESPONSE:
                case ERROR_HTTP_REDIRECT_FAILED:
                case ERROR_HTTP_NOT_REDIRECTED:
                case ERROR_INTERNET_SEC_INVALID_CERT:
                case ERROR_INTERNET_SEC_CERT_REVOKED:
                case ERROR_INTERNET_DECODING_FAILED:
                    response->m_result = HttpResult_NetworkFailure;
                    break;

                default:
                    response->m_result = HttpResult_LocalFailure;
                    break;
            }
        }

        m_appCallback->OnHttpResponse(response.release());

        m_parent.signalDoneAndErase(m_id);
    }
};

//---

unsigned HttpClient_WinInet::s_nextRequestId = 0;

HttpClient_WinInet::HttpClient_WinInet()
{
    m_hInternet = ::InternetOpen(NULL, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, INTERNET_FLAG_ASYNC);
}

HttpClient_WinInet::~HttpClient_WinInet()
{
    bool done;

    {
		std::lock_guard<std::mutex> lock(m_requestsMutex);
        for (auto const& item : m_requests) {
            item.second->cancel();
        }
        done = m_requests.empty();
    }

    while (!done) {
        PAL::sleep(100);
        {
			std::lock_guard<std::mutex> lock(m_requestsMutex);
            done = m_requests.empty();
        }
    }

    ::InternetCloseHandle(m_hInternet);
}

void HttpClient_WinInet::signalDoneAndErase(std::string const& id)
{
	std::lock_guard<std::mutex> lock(m_requestsMutex);
    auto it = m_requests.find(id);
    if (it != m_requests.end()) {
        it->second->signalDone();
        m_requests.erase(it);
    }
}

IHttpRequest* HttpClient_WinInet::CreateRequest()
{
    std::string id = "WI-" + toString(::InterlockedIncrement(&s_nextRequestId));
    return new SimpleHttpRequest(id);
}

void HttpClient_WinInet::SendRequestAsync(IHttpRequest* request, IHttpResponseCallback* callback)
{
    SimpleHttpRequest* req = static_cast<SimpleHttpRequest*>(request);
    PAL::RefCountedPtr<WinInetRequestWrapper> wrapper(new WinInetRequestWrapper(*this, req->m_id), false);
    wrapper->send(req, callback);
    delete req;
}

void HttpClient_WinInet::CancelRequestAsync(std::string const& id)
{
    PAL::RefCountedPtr<WinInetRequestWrapper> request;
    {
		std::lock_guard<std::mutex> lock(m_requestsMutex);
        auto it = m_requests.find(id);
        if (it != m_requests.end()) {
            request = it->second;
        }
    }
    if (request) {
        request->cancel();
    }
}


} ARIASDK_NS_END
