/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SDS_H
#define __SDS_H

//最大可分配内存 1024*1024bytes = 1M
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

//类型别名
typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */

//__attribute__ (( __packed__))这条语句是GCC的对C的拓展,作用为取消结构在编译过程中的优化对齐，按照实际占用字节数进行对齐，是GCC特有
//的语这个功能是跟操作系统没关系，跟编译器有关，gcc编译器不是紧凑模式的:
//struct my{ char ch; int a;} sizeof(int)=4;sizeof(my)=8;（非紧凑模式）
//struct my{ char ch; int a;}__attrubte__ ((packed)) sizeof(int)=4;sizeof(my)=5（紧凑模式,char类型占1字节）
//若数据按对齐模式写入,如int为4字节,但实际上只占3字节,那么多出来的1字节将会被填充,在对数据进行移位操作时将会得出数据不正确的情况

/*
 * 以下程序涉及类型的字节位运算,附上Linux系统的类型长度:
 * 
 * Linux系统32位与64位GCC编译器基本数据类型长度对照表
 * 
 * GCC 32位
 * sizeof(char)=1
 * sizeof(double)=8
 * sizeof(float)=4
 * sizeof(int)=4
 * sizeof(short)=2
 * sizeof(long)=4
 * sizeof(long long)=8
 * sizeof(long double)=12
 * sizeof(complex long double)=24
 * 
 * GCC 64位
 * sizeof(char)=1
 * sizeof(double)=8
 * sizeof(float)=4
 * sizeof(int)=4
 * sizeof(short)=2
 * sizeof(long)=8
 * sizeof(long long)=8
 * sizeof(long double)=16
 * sizeof(complex long double)=32
 *
 */

//sdshdr5与其它几个header结构不同，它不包含alloc字段，而长度使用flags的高5位来存储。
//因此，它不能为字符串分配空余空间。如果字符串需要动态增长，那么它就必然要重新分配内存才行。
//所以说，这种类型的sds字符串更适合存储静态的短字符串（长度小于32）。
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */	//最大值:0xF8
		//柔性数组,柔性数组成员只作为一个符号地址存在，而且必须是结构体的最后一个成员，sizeof 返回的这种结构大小不包括柔性数组的内存
		//在使用malloc分配内存时需要分配多余的空间给予柔性数组使用,如: 
		//struct sdshdr5 *p = ( struct sdshdr5 *)malloc( sizeof( sdshdr5) + 50 ),多出来的50即是柔性数组的长度范围,即char buf[50]
    char buf[];
};

//结构体字段说明 len:现存字符串长度 alloc:字符串最大容量 flags:标志位 buf:字符串存放位置
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used */
    uint8_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */	//0x01
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */	//0x02
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */ //0x03
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */ //0x04
    char buf[];
};

//header类型
#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
//类型掩码
#define SDS_TYPE_MASK 7
//偏移位
#define SDS_TYPE_BITS 3
//函数式宏定义
//返回指定类型结构体指针
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
//返回指定类型结构体指针首地址
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
//返回sdshdr5结构体buf长度
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

//计算字符串长度
//inline 内联函数关键字,使用在函数声明处，表示程序员请求编译器在此函数的被调用处将此函数实现插入，而不是像普通函数那样生成调用代码
static inline size_t sdslen(const sds s) {
		//这里传进来的形参s是上述结构体中的一种.由于每个结构体都取消对齐,因此在内存中结构体的数据是这样的
		//len | alloc | flags | buf
		//因为取消的了内存对齐,所以每个数据之前不会因为字节对齐而有字节填充.
		//因此s[-1]获取的是结构体的上一个成员,也就是flags字段的数据
    unsigned char flags = s[-1];
		//根据flags标志和类型掩码的取余获取是哪个结构体
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
						//由sdshdr5结构体定义可知字节长度信息包含在flags中的高5位,因此根据宏定义可知字符串长度等于flags右移3位
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
						//结构体首地址计算解析:
						//因为传进去的是指针字符串s的首地址,根据上面的宏定义可以算出:
						//s( 数据的存储从低地址向高地址,因此字符串s的首地址为高 )-结构体数据的大小得到结构体的首地址
						//注:sizeof( struct sdshdr8 )因为最后的buf是柔性数组,因此算出的结构体大小不包括buf的大小
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

//获取字符串剩余空间
static inline size_t sdsavail(const sds s) {
    //如上
		unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
						//sdshdr5无法得知字符串剩余容量
            return 0;
        }
        case SDS_TYPE_8: {
						//根据宏定义获取的结构体指针变量为sh
            SDS_HDR_VAR(8,s);
						//返回字符串容器剩余容量
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

//设置结构体buf长度
static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
								//如s[-1],可以这样看这条语句 ( unsigned char *) fp = ( ( unsigned char *)s ) - 1
								//注:( unsigned char * )s只是强制转换类型,并不是操作*s指针
                unsigned char *fp = ((unsigned char*)s)-1;
								//改变flags的值 
								//因为size_t为4字节长度( 32位 ),但是flags为char *类型,因为flags只有一字节( 8位 ),
								//高5位存字符串长度,低3位用来与类型掩码相与或者结构体类型,因此新长度的数值所占位数最多为5位( 即32位 ),
								//与0或运算后得到新的flags,如:
								//
								//newlen能设置的最大值
								//0000 0000 0000 0000 0000 0000 0001 1111
								//左移3位
								//0000 0000 0000 0000 0000 0000 1111 1000
								//与0进行或运算
								//0000 0000 0000 0000 0000 0000 1111 1000
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

//增加字符串指针对应的结构体的len长度
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
								//获取sdshdr5结构体的flags
                unsigned char *fp = ((unsigned char*)s)-1;
								//计算结构体先有的长度并增加
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
								//设置sdshdr5结构体的buf长度
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

//返回结构体的buf总容量
/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

//设置结构体的alloc
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
//判断是否在GNUC环境编译程序
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
		// __attribute__((format(printf, 2, 3)));
		// 这句主要作用是提示编译器，对这个函数的调用需要像printf一样，用对应的format字符串来check可变参数的数据类型。
		// format ( printf, 2, 3)告诉编译器，fmt相当于printf的format，而可变参数是从sdscatprintf的第3个参数开始
		//  __attribute__((format(printf, m, n)));
		// m：第几个参数为格式化字符串（format string）；
		// n：参数集合中的第一个，即参数“…”里的第一个参数在函数参数总数排在第几
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

//判断是否为测试环境
#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
