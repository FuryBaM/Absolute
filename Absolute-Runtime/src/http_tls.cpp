#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "scheduler_io.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace {
    struct HttpTlsState {
        std::string headers;
#if defined(_WIN32)
        HINTERNET session = nullptr;
        HINTERNET connection = nullptr;
        HINTERNET request = nullptr;
#else
        std::string body;
        std::size_t offset = 0;
#endif
    };

    thread_local std::string lastTlsError;

    template <class Result, class Operation>
    Result RunTlsIo(Operation&& operation) {
        Result result{};
        std::string error;
        Absolute::RuntimeDetail::RunBlockingIo([&] {
            result = operation();
            error = lastTlsError;
        });
        lastTlsError = std::move(error);
        return result;
    }

    void CloseState(HttpTlsState* state) {
        if (!state) return;
#if defined(_WIN32)
        if (state->request) WinHttpCloseHandle(state->request);
        if (state->connection) WinHttpCloseHandle(state->connection);
        if (state->session) WinHttpCloseHandle(state->session);
#endif
        delete state;
    }

#if defined(_WIN32)
    std::wstring Utf8ToWide(const char* value) {
        if (!value || !*value) return {};
        const int length = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
        if (length <= 1) return {};
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
            result.data(), length);
        result.pop_back();
        return result;
    }

    std::string WideToUtf8(
        const wchar_t* value, std::size_t length) {
        if (!value || length == 0) return {};
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value, static_cast<int>(length),
            nullptr, 0, nullptr, nullptr);
        if (required <= 0) return {};
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value, static_cast<int>(length),
            result.data(), required, nullptr, nullptr);
        return result;
    }

    void CaptureWinHttpError(const char* operation) {
        const DWORD code = GetLastError();
        wchar_t* message = nullptr;
        const DWORD flags =
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageW(
            flags, nullptr, code, 0,
            reinterpret_cast<wchar_t*>(&message), 0, nullptr);
        lastTlsError = std::string(operation) +
            " failed with WinHTTP error " + std::to_string(code);
        if (length > 0 && message) {
            std::string detail = WideToUtf8(message, length);
            while (!detail.empty() &&
                (detail.back() == '\r' || detail.back() == '\n' ||
                    detail.back() == ' ')) {
                detail.pop_back();
            }
            if (!detail.empty()) {
                lastTlsError += ": " + detail;
            }
        }
        if (message) LocalFree(message);
    }

    HttpTlsState* OpenWindows(
        const char* method, const char* url, const char* headers,
        const char* body, std::int32_t timeoutMilliseconds,
        std::int32_t maximumResponseBytes) {
        const std::wstring wideUrl = Utf8ToWide(url);
        const std::wstring wideMethod = Utf8ToWide(method);
        if (wideUrl.empty() || wideMethod.empty()) {
            lastTlsError = "HTTPS method or URL is not valid UTF-8";
            return nullptr;
        }

        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(
                wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()),
                0, &components)) {
            CaptureWinHttpError("parse HTTPS URL");
            return nullptr;
        }
        if (components.nScheme != INTERNET_SCHEME_HTTPS ||
            components.dwHostNameLength == 0) {
            lastTlsError = "TLS transport requires an https:// URL";
            return nullptr;
        }

        std::wstring host(
            components.lpszHostName, components.dwHostNameLength);
        std::wstring target;
        if (components.dwUrlPathLength > 0) {
            target.append(
                components.lpszUrlPath, components.dwUrlPathLength);
        } else {
            target = L"/";
        }
        if (components.dwExtraInfoLength > 0) {
            target.append(
                components.lpszExtraInfo, components.dwExtraInfoLength);
        }

        HttpTlsState* state = new (std::nothrow) HttpTlsState();
        if (!state) {
            lastTlsError = "HTTPS state allocation failed";
            return nullptr;
        }
        state->session = WinHttpOpen(
            L"Absolute/0.8",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!state->session) {
            CaptureWinHttpError("open WinHTTP session");
            CloseState(state);
            return nullptr;
        }
        const int timeout = timeoutMilliseconds > 0
            ? timeoutMilliseconds : 30000;
        if (!WinHttpSetTimeouts(
                state->session, timeout, timeout, timeout, timeout)) {
            CaptureWinHttpError("configure HTTPS timeout");
            CloseState(state);
            return nullptr;
        }

        state->connection = WinHttpConnect(
            state->session, host.c_str(), components.nPort, 0);
        if (!state->connection) {
            CaptureWinHttpError("connect HTTPS host");
            CloseState(state);
            return nullptr;
        }
        state->request = WinHttpOpenRequest(
            state->connection, wideMethod.c_str(), target.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!state->request) {
            CaptureWinHttpError("open HTTPS request");
            CloseState(state);
            return nullptr;
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(
                state->request, WINHTTP_OPTION_REDIRECT_POLICY,
                &redirectPolicy, sizeof(redirectPolicy))) {
            CaptureWinHttpError("disable WinHTTP redirects");
            CloseState(state);
            return nullptr;
        }

        const std::wstring wideHeaders = Utf8ToWide(headers);
        const char* requestBody = body ? body : "";
        const std::size_t requestBodySize = std::strlen(requestBody);
        if (requestBodySize >
            static_cast<std::size_t>(
                (std::numeric_limits<DWORD>::max)())) {
            lastTlsError = "HTTPS request body is too large";
            CloseState(state);
            return nullptr;
        }
        if (!WinHttpSendRequest(
                state->request,
                wideHeaders.empty()
                    ? WINHTTP_NO_ADDITIONAL_HEADERS
                    : wideHeaders.c_str(),
                wideHeaders.empty()
                    ? 0 : static_cast<DWORD>(wideHeaders.size()),
                requestBodySize == 0
                    ? WINHTTP_NO_REQUEST_DATA
                    : const_cast<char*>(requestBody),
                static_cast<DWORD>(requestBodySize),
                static_cast<DWORD>(requestBodySize),
                0)) {
            CaptureWinHttpError("send HTTPS request");
            CloseState(state);
            return nullptr;
        }
        if (!WinHttpReceiveResponse(state->request, nullptr)) {
            CaptureWinHttpError("receive HTTPS response");
            CloseState(state);
            return nullptr;
        }

        DWORD headerBytes = 0;
        WinHttpQueryHeaders(
            state->request,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX,
            nullptr, &headerBytes, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            headerBytes < sizeof(wchar_t)) {
            CaptureWinHttpError("query HTTPS response headers");
            CloseState(state);
            return nullptr;
        }
        std::wstring rawHeaders(
            headerBytes / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(
                state->request,
                WINHTTP_QUERY_RAW_HEADERS_CRLF,
                WINHTTP_HEADER_NAME_BY_INDEX,
                rawHeaders.data(), &headerBytes,
                WINHTTP_NO_HEADER_INDEX)) {
            CaptureWinHttpError("read HTTPS response headers");
            CloseState(state);
            return nullptr;
        }
        std::size_t headerLength =
            headerBytes / sizeof(wchar_t);
        while (headerLength > 0 &&
            rawHeaders[headerLength - 1] == L'\0') {
            --headerLength;
        }
        state->headers =
            WideToUtf8(rawHeaders.data(), headerLength);
        if (maximumResponseBytes > 0 &&
            state->headers.size() >
                static_cast<std::size_t>(maximumResponseBytes)) {
            lastTlsError = "HTTPS response headers exceed response limit";
            CloseState(state);
            return nullptr;
        }
        lastTlsError.clear();
        return state;
    }
#else
    using CurlEasyInit = void* (*)();
    using CurlEasySetopt = int (*)(void*, int, ...);
    using CurlEasyPerform = int (*)(void*);
    using CurlEasyCleanup = void (*)(void*);
    using CurlEasyStrerror = const char* (*)(int);
    using CurlSlistAppend = void* (*)(void*, const char*);
    using CurlSlistFreeAll = void (*)(void*);
    using CurlGlobalInit = int (*)(long);

    struct CurlApi {
        void* library = nullptr;
        CurlEasyInit easyInit = nullptr;
        CurlEasySetopt easySetopt = nullptr;
        CurlEasyPerform easyPerform = nullptr;
        CurlEasyCleanup easyCleanup = nullptr;
        CurlEasyStrerror easyStrerror = nullptr;
        CurlSlistAppend slistAppend = nullptr;
        CurlSlistFreeAll slistFreeAll = nullptr;
        CurlGlobalInit globalInit = nullptr;
        std::string error;
        bool ready = false;
    };

    CurlApi& GetCurl() {
        static CurlApi api;
        static std::once_flag once;
        std::call_once(once, [&] {
#if defined(__APPLE__)
            const char* names[] = {
                "libcurl.4.dylib", "libcurl.dylib", nullptr};
#else
            const char* names[] = {
                "libcurl.so.4", "libcurl.so", nullptr};
#endif
            for (const char** name = names; *name && !api.library; ++name)
                api.library = dlopen(*name, RTLD_NOW | RTLD_LOCAL);
            if (!api.library) {
                api.error =
                    "libcurl is required for HTTPS but was not found";
                return;
            }
#define ABSOLUTE_CURL_SYMBOL(field, name) \
            api.field = reinterpret_cast<decltype(api.field)>( \
                dlsym(api.library, name))
            ABSOLUTE_CURL_SYMBOL(easyInit, "curl_easy_init");
            ABSOLUTE_CURL_SYMBOL(easySetopt, "curl_easy_setopt");
            ABSOLUTE_CURL_SYMBOL(easyPerform, "curl_easy_perform");
            ABSOLUTE_CURL_SYMBOL(easyCleanup, "curl_easy_cleanup");
            ABSOLUTE_CURL_SYMBOL(easyStrerror, "curl_easy_strerror");
            ABSOLUTE_CURL_SYMBOL(slistAppend, "curl_slist_append");
            ABSOLUTE_CURL_SYMBOL(slistFreeAll, "curl_slist_free_all");
            ABSOLUTE_CURL_SYMBOL(globalInit, "curl_global_init");
#undef ABSOLUTE_CURL_SYMBOL
            if (!api.easyInit || !api.easySetopt || !api.easyPerform ||
                !api.easyCleanup || !api.easyStrerror ||
                !api.slistAppend || !api.slistFreeAll ||
                !api.globalInit) {
                api.error = "installed libcurl has an incomplete ABI";
                return;
            }
            if (api.globalInit(3L) != 0) {
                api.error = "curl_global_init failed";
                return;
            }
            api.ready = true;
        });
        return api;
    }

    struct CurlCapture {
        HttpTlsState* state = nullptr;
        std::size_t maximum = 0;
        bool exceeded = false;
    };

    std::size_t CurlWrite(
        char* data, std::size_t size, std::size_t count, void* opaque) {
        CurlCapture* capture = static_cast<CurlCapture*>(opaque);
        const std::size_t bytes = size * count;
        if (bytes > capture->maximum - capture->state->body.size()) {
            capture->exceeded = true;
            return 0;
        }
        capture->state->body.append(data, bytes);
        return bytes;
    }

    std::size_t CurlHeader(
        char* data, std::size_t size, std::size_t count, void* opaque) {
        CurlCapture* capture = static_cast<CurlCapture*>(opaque);
        const std::size_t bytes = size * count;
        if (bytes >= 5 && std::memcmp(data, "HTTP/", 5) == 0)
            capture->state->headers.clear();
        if (bytes > capture->maximum -
                capture->state->headers.size() -
                capture->state->body.size()) {
            capture->exceeded = true;
            return 0;
        }
        capture->state->headers.append(data, bytes);
        return bytes;
    }

    HttpTlsState* OpenCurl(
        const char* method, const char* url, const char* headers,
        const char* body, std::int32_t timeoutMilliseconds,
        std::int32_t maximumResponseBytes) {
        CurlApi& api = GetCurl();
        if (!api.ready) {
            lastTlsError = api.error;
            return nullptr;
        }
        HttpTlsState* state = new (std::nothrow) HttpTlsState();
        if (!state) {
            lastTlsError = "HTTPS state allocation failed";
            return nullptr;
        }
        void* easy = api.easyInit();
        if (!easy) {
            lastTlsError = "curl_easy_init failed";
            delete state;
            return nullptr;
        }

        void* headerList = nullptr;
        std::string headerText = headers ? headers : "";
        std::size_t start = 0;
        while (start < headerText.size()) {
            const std::size_t end = headerText.find("\r\n", start);
            const std::size_t length =
                end == std::string::npos
                    ? headerText.size() - start : end - start;
            if (length > 0) {
                const std::string line =
                    headerText.substr(start, length);
                headerList = api.slistAppend(
                    headerList, line.c_str());
            }
            if (end == std::string::npos) break;
            start = end + 2;
        }

        const std::size_t maximum = maximumResponseBytes > 0
            ? static_cast<std::size_t>(maximumResponseBytes)
            : static_cast<std::size_t>(16 * 1024 * 1024);
        CurlCapture capture{state, maximum, false};
        char errorBuffer[256]{};
        constexpr int Url = 10002;
        constexpr int WriteData = 10001;
        constexpr int WriteFunction = 20011;
        constexpr int HeaderData = 10029;
        constexpr int HeaderFunction = 20079;
        constexpr int CustomRequest = 10036;
        constexpr int HttpHeader = 10023;
        constexpr int PostFields = 10015;
        constexpr int PostFieldSizeLarge = 30120;
        constexpr int TimeoutMilliseconds = 155;
        constexpr int NoSignal = 99;
        constexpr int VerifyPeer = 64;
        constexpr int VerifyHost = 81;
        constexpr int ErrorBuffer = 10010;

        api.easySetopt(easy, Url, url);
        api.easySetopt(easy, CustomRequest, method);
        api.easySetopt(easy, HttpHeader, headerList);
        api.easySetopt(easy, WriteFunction, &CurlWrite);
        api.easySetopt(easy, WriteData, &capture);
        api.easySetopt(easy, HeaderFunction, &CurlHeader);
        api.easySetopt(easy, HeaderData, &capture);
        api.easySetopt(easy, TimeoutMilliseconds,
            static_cast<long>(
                timeoutMilliseconds > 0 ? timeoutMilliseconds : 30000));
        api.easySetopt(easy, NoSignal, 1L);
        api.easySetopt(easy, VerifyPeer, 1L);
        api.easySetopt(easy, VerifyHost, 2L);
        api.easySetopt(easy, ErrorBuffer, errorBuffer);
        const char* requestBody = body ? body : "";
        if (*requestBody) {
            api.easySetopt(easy, PostFields, requestBody);
            api.easySetopt(
                easy, PostFieldSizeLarge,
                static_cast<std::int64_t>(std::strlen(requestBody)));
        }

        const int result = api.easyPerform(easy);
        if (headerList) api.slistFreeAll(headerList);
        api.easyCleanup(easy);
        if (result != 0 || capture.exceeded) {
            if (capture.exceeded) {
                lastTlsError = "HTTPS response exceeds response limit";
            } else if (*errorBuffer) {
                lastTlsError = errorBuffer;
            } else {
                lastTlsError = api.easyStrerror(result);
            }
            delete state;
            return nullptr;
        }
        lastTlsError.clear();
        return state;
    }
#endif
}

extern "C" void* absolute_http_tls_open(
    const char* method, const char* url, const char* headers,
    const char* body, std::int32_t timeoutMilliseconds,
    std::int32_t maximumResponseBytes) {
    if (!method || !*method || !url || !*url) {
        lastTlsError = "HTTPS method and URL are required";
        return nullptr;
    }
    return RunTlsIo<HttpTlsState*>([&] {
#if defined(_WIN32)
        return OpenWindows(
            method, url, headers, body, timeoutMilliseconds,
            maximumResponseBytes);
#else
        return OpenCurl(
            method, url, headers, body, timeoutMilliseconds,
            maximumResponseBytes);
#endif
    });
}

extern "C" const char* absolute_http_tls_headers(void* handle) {
    HttpTlsState* state = static_cast<HttpTlsState*>(handle);
    if (!state) {
        lastTlsError = "HTTPS response is closed";
        return "";
    }
    lastTlsError.clear();
    return state->headers.c_str();
}

extern "C" const char* absolute_http_tls_receive(
    void* handle, std::int32_t maximumBytes) {
    HttpTlsState* state = static_cast<HttpTlsState*>(handle);
    if (!state || maximumBytes <= 0) {
        if (!state) lastTlsError = "HTTPS response is closed";
        return "";
    }
    return RunTlsIo<const char*>([&]() -> const char* {
        std::string receivedText;
#if defined(_WIN32)
        receivedText.resize(
            static_cast<std::size_t>(maximumBytes));
        DWORD received = 0;
        if (!WinHttpReadData(
                state->request, receivedText.data(),
                static_cast<DWORD>(maximumBytes), &received)) {
            CaptureWinHttpError("read HTTPS response body");
            return nullptr;
        }
        receivedText.resize(received);
#else
        const std::size_t remaining =
            state->body.size() - state->offset;
        const std::size_t count = (std::min)(
            remaining, static_cast<std::size_t>(maximumBytes));
        receivedText.assign(
            state->body.data() + state->offset, count);
        state->offset += count;
#endif
        const std::size_t durableSize = receivedText.size() + 1;
        char* durable =
            static_cast<char*>(std::malloc(durableSize));
        if (!durable) {
            lastTlsError = "HTTPS receive allocation failed";
            return nullptr;
        }
        std::memcpy(
            durable, receivedText.c_str(), durableSize);
        lastTlsError.clear();
        return durable;
    });
}

extern "C" void absolute_http_tls_close(void* handle) {
    CloseState(static_cast<HttpTlsState*>(handle));
}

extern "C" const char* absolute_http_tls_error() {
    return lastTlsError.c_str();
}
