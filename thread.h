/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#ifdef MAILESTD_MT
#include <pthread.h>
#define _thread_t		pthread_t
#define _thread_mutex_t		pthread_mutex_t
#define _thread_spinlock_t	pthread_spinlock_t
#define _thread_self		pthread_self
#define _thread_join		pthread_join
#define _thread_create		pthread_create
#define _thread_mutex_init	pthread_mutex_init
#define _thread_mutex_destroy	pthread_mutex_destroy
#define _thread_mutex_lock	pthread_mutex_lock
#define _thread_mutex_unlock	pthread_mutex_unlock
#define _thread_spin_init	pthread_spin_init
#define _thread_spin_lock	pthread_spin_lock
#define _thread_spin_unlock	pthread_spin_unlock
#define _thread_spin_destroy	pthread_spin_destroy
#else
typedef void * _thread_t;
typedef void * _thread_mutex_t;
typedef void * _thread_spinlock_t;
static inline void *_thread_empty()	{ return (void *)0xdeadbeaf; }
#define _thread_self			_thread_empty
#define _thread_join(_a,_b)		abort()
#define _thread_create(_a,_b,_c,_d)	abort()
#define _thread_mutex_init		_thread_empty
#define _thread_mutex_destroy		_thread_empty
#define _thread_mutex_lock		_thread_empty
#define _thread_mutex_unlock		_thread_empty
#define _thread_spin_init		_thread_empty
#define _thread_spin_lock		_thread_empty
#define _thread_spin_unlock		_thread_empty
#define _thread_spin_destroy		_thread_empty
#endif
