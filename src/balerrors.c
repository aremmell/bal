/*
 * balerrors.c
 *
 * Author:    Ryan M. Lederman <lederman@gmail.com>
 * Copyright: Copyright (c) 2004-2023
 * Version:   0.2.0
 * License:   The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "bal.h"
#include "bal/errors.h"
#include "bal/internal.h"

static _bal_thread_local bal_error_info _error_info = {
    0, BAL_UNKNOWN, BAL_UNKNOWN, 0U, false
};

#if defined(__WIN__)
# pragma comment(lib, "shlwapi.lib")
#endif

int _bal_getlasterror(const bal_socket* s, bal_error* err)
{
    int retval = 0;

    if (_bal_validptr(err)) {
        memset(err, 0, sizeof(bal_error));
        bool resolved = false;

        if (NULL != s) {
            err->code = bal_geterror(s);
            if (0 != err->code)
                resolved = true;
        }

        if (!resolved)
            err->code = _error_info.code;

        retval = err->code;
        _bal_formaterrormsg(err->code, err->desc, (NULL != s) ? false : _error_info.gai);
    }

    return retval;
}

bool __bal_setlasterror(int code, const char* func, const char* file,
    uint32_t line, bool gai)
{
    _error_info.code = code;
    _error_info.func = func;
    _error_info.file = file;
    _error_info.line = line;
    _error_info.gai  = gai;

    return false;
}

void _bal_formaterrormsg(int err, char buf[BAL_MAXERROR], bool gai)
{
    buf[0] = '\0';

#if defined(__WIN__)
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
                  FORMAT_MESSAGE_MAX_WIDTH_MASK;
    DWORD fmt = FormatMessageA(flags, NULL, (DWORD)err, 0UL, buf, BAL_MAXERROR, NULL);
    assert(0UL != fmt);

    if (fmt > 0UL) {
        if (buf[fmt - 1] == '\n' || buf[fmt - 1] == ' ')
            buf[fmt - 1] = '\0';
    }

    BAL_UNUSED(gai);
#else
    if (gai) {
        const char* tmp = gai_strerror(err);
        _bal_strcpy(buf, BAL_MAXERROR, tmp, strnlen(tmp, BAL_MAXERROR));
    } else {
     int finderr = -1;
#if defined(__HAVE_XSI_STRERROR_R__)
        finderr = strerror_r(err, buf, BAL_MAXERROR);
# if defined(__HAVE_XSI_STRERROR_R_ERRNO__)
        if (finderr == -1)
            finderr = errno;
# endif
#elif defined(__HAVE_GNU_STRERROR_R__)
        char* tmp = strerror_r(err, buf, BAL_MAXERROR);
        if (tmp != buf)
            _bal_strcpy(buf, BAL_MAXERROR, tmp, strnlen(tmp, BAL_MAXERROR));
#elif defined(__HAVE_STRERROR_S__)
        finderr = (int)strerror_s(buf, BAL_MAXERROR, err);
#else
        char* tmp = strerror(err);
        _bal_strcpy(buf, BAL_MAXERROR, tmp, strnlen(tmp, BAL_MAXERROR));
#endif
#if defined(__HAVE_XSI_STRERROR_R__) || defined(__HAVE_STRERROR_S__)
        assert(0 == finderr);
#endif
        BAL_UNUSED(finderr);
    }
#endif
}

#if defined(BAL_DBGLOG)
void __bal_dbglog(const char* func, const char* file, uint32_t line,
    const char* format, ...)
{
    va_list args;
    va_list args2;
    va_start(args, format);
    va_copy(args2, args);

    int prnt_len = vsnprintf(NULL, 0, format, args);

    va_end(args);
    BAL_ASSERT(prnt_len > 0);

    char* buf = calloc(prnt_len + 1, sizeof(char));
    BAL_ASSERT(NULL != buf);

    if (buf) {
        char prefix[256] = {0};
        int len = snprintf(prefix, 256, "["BAL_TID_SPEC"] %s (%s:%"PRIu32"): ",
            _bal_gettid(), func, file, line);
        BAL_ASSERT_UNUSED(len, len > 0 && len < 256);

        (void)vsnprintf(buf, prnt_len + 1, format, args2);
        va_end(args2);

        const char* color = "0";
# if defined(__WIN__)
        if (NULL != StrStrIA(buf, "error") || NULL != StrStrIA(buf, "assert"))
            color = "91";
        else if (NULL != StrStrIA(buf, "warn"))
            color = "33";
# else
        if (NULL != strcasestr(buf, "error") || NULL != strcasestr(buf, "assert"))
            color = "91";
        else if (NULL != strcasestr(buf, "warn"))
            color = "33";
# endif
        printf("\x1b[%sm%s%s\x1b[0m\n", color, prefix, buf);

        _bal_safefree(&buf);
    }
}
#endif
