/*
 * Copyright (c) 2012 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef ILIAS_ILIAS_ASYNC_EXPORT_H
#define ILIAS_ILIAS_ASYNC_EXPORT_H


/* Include ilias config here, so I won't keep tripping over missing include. */
#include <ilias/config_async.h>


#if defined(WIN32)
#ifdef ilias_async_EXPORTS
#define ILIAS_ASYNC_EXPORT	__declspec(dllexport)
#define ILIAS_ASYNC_LOCAL	/* nothing */
#else
#define ILIAS_ASYNC_EXPORT	__declspec(dllimport)
#define ILIAS_ASYNC_LOCAL	/* nothing */
#endif /* ilias_async_EXPORTS */
#elif defined(__GNUC__) || defined(__clang__)
#define ILIAS_ASYNC_EXPORT	__attribute__ ((visibility ("default")))
#define ILIAS_ASYNC_LOCAL	__attribute__ ((visibility ("hidden")))
#else
#define ILIAS_ASYNC_EXPORT	/* nothing */
#define ILIAS_ASYNC_LOCAL	/* nothing */
#endif


#endif /* ILIAS_ILIAS_ASYNC_EXPORT_H */
