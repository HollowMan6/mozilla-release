/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include "mozmemory_wrap.h"
#include "mozilla/Types.h"

/* Declare malloc implementation functions with the right return and
 * argument types. */
#define MALLOC_DECL(name, return_type, ...) \
  MOZ_MEMORY_API return_type name ## _impl(__VA_ARGS__);
#include "malloc_decls.h"

#ifdef MOZ_WRAP_NEW_DELETE
/* operator new(unsigned int) */
MOZ_MEMORY_API void *
mozmem_malloc_impl(_Znwj)(unsigned int size)
{
  return malloc_impl(size);
}
/* operator new[](unsigned int) */
MOZ_MEMORY_API void *
mozmem_malloc_impl(_Znaj)(unsigned int size)
{
  return malloc_impl(size);
}
/* operator delete(void*) */
MOZ_MEMORY_API void
mozmem_malloc_impl(_ZdlPv)(void *ptr)
{
  free_impl(ptr);
}
/* operator delete[](void*) */
MOZ_MEMORY_API void
mozmem_malloc_impl(_ZdaPv)(void *ptr)
{
  free_impl(ptr);
}
/*operator new(unsigned int, std::nothrow_t const&)*/
MOZ_MEMORY_API void *
mozmem_malloc_impl(_ZnwjRKSt9nothrow_t)(unsigned int size)
{
  return malloc_impl(size);
}
/*operator new[](unsigned int, std::nothrow_t const&)*/
MOZ_MEMORY_API void *
mozmem_malloc_impl(_ZnajRKSt9nothrow_t)(unsigned int size)
{
  return malloc_impl(size);
}
/* operator delete(void*, std::nothrow_t const&) */
MOZ_MEMORY_API void
mozmem_malloc_impl(_ZdlPvRKSt9nothrow_t)(void *ptr)
{
  free_impl(ptr);
}
/* operator delete[](void*, std::nothrow_t const&) */
MOZ_MEMORY_API void
mozmem_malloc_impl(_ZdaPvRKSt9nothrow_t)(void *ptr)
{
  free_impl(ptr);
}
#endif

/* strndup and strdup may be defined as macros in string.h, which would
 * clash with the definitions below. */
#undef strndup
#undef strdup

#ifndef XP_DARWIN
MOZ_MEMORY_API char *
strndup_impl(const char *src, size_t len)
{
  char* dst = (char*) malloc_impl(len + 1);
  if (dst)
    strncpy(dst, src, len + 1);
  return dst;
}

MOZ_MEMORY_API char *
strdup_impl(const char *src)
{
  size_t len = strlen(src);
  return strndup_impl(src, len);
}
#endif /* XP_DARWIN */

#ifdef XP_WIN
/*
 *  There's a fun allocator mismatch in (at least) the VS 2010 CRT
 *  (see the giant comment in $(topsrcdir)/mozglue/build/Makefile.in)
 *  that gets redirected here to avoid a crash on shutdown.
 */
void
dumb_free_thunk(void *ptr)
{
  return; /* shutdown leaks that we don't care about */
}

#include <wchar.h>

/*
 *  We also need to provide our own impl of wcsdup so that we don't ask
 *  the CRT for memory from its heap (which will then be unfreeable).
 */
wchar_t *
wcsdup_impl(const wchar_t *src)
{
  size_t len = wcslen(src);
  wchar_t *dst = (wchar_t*) malloc_impl((len + 1) * sizeof(wchar_t));
  if (dst)
    wcsncpy(dst, src, len + 1);
  return dst;
}
#endif /* XP_WIN */
