/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to a hash table once a given number of
 * elements is reached.
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 *
 * <zmlen> is 1 byte length that holds the current size of the zipmap.
 * When the zipmap length is greater than or equal to 254, this value
 * is not used and the zipmap needs to be traversed to find out the length.
 *
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 253, it's a single-byte length. If it is 254 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value of 255 is used to
 * signal the end of the hash.
 *
 * <free> is the number of free unused bytes after the string, resulting
 * from modification of values associated to a key. For instance if "foo"
 * is set to "bar", and later "foo" will be set to "hi", it will have a
 * free byte to use if the value will enlarge again later, or even in
 * order to add a key/value pair if it fits.
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, the zipmap will be
 * reallocated to make sure it is as small as possible.
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * Note that because keys and values are prefixed length "objects",
 * the lookup will take O(N) where N is the number of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

#include <stdio.h>
#include <string.h>
#include "zmalloc.h"
#include "endianconv.h"

//当压缩字典的键值对的值大于253时设置为254,长度用随后的4个字节表示
#define ZIPMAP_BIGLEN 254
//结束字段标识
#define ZIPMAP_END 255

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
//最大空闲字节
#define ZIPMAP_VALUE_MAX_FREE 4

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
//根据长度大小返回长度字段字节数
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)

/* Create a new empty zipmap. */
//创建压缩字典
unsigned char *zipmapNew(void) {
		//初始化时只有2个字节,第1个字节表示zipmap保存的key-value对的个数，第2个字节为结尾符
    unsigned char *zm = zmalloc(2);
		//当前保存的键值对个数为0
    zm[0] = 0; /* Length */
    zm[1] = ZIPMAP_END;
    return zm;
}

/* Decode the encoded length pointed by 'p' */
//返回键或值记录的长度
static unsigned int zipmapDecodeLength(unsigned char *p) {
    unsigned int len = *p;

		//如果少于254则第一个字节就是长度
    if (len < ZIPMAP_BIGLEN) return len;
		//如果大于253则后面4个字节记录的才是长度
    memcpy(&len,p+1,sizeof(unsigned int));
    memrev32ifbe(&len);
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
//若形参p为NULL则返回记录长度所占的字节数,若不为空则记录长度且返回记录长度所占的字节数
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {
    if (p == NULL) {
				//如果没有传入指针则根据传入长度返回记录长度的字段所占字节数
        return ZIPMAP_LEN_BYTES(len);
    } else {
        if (len < ZIPMAP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIPMAP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return 1+sizeof(len);
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries. */
//在压缩字典中查找对应的key,若totlen不为空则totlen存入压缩字典的长度
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen) {
    unsigned char *p = zm+1, *k = NULL;
    unsigned int l,llen;
		//查找压缩字典里是否含有对应的key
    while(*p != ZIPMAP_END) {
        unsigned char free;

        /* Match or skip the key */
				//获取键的长度
        l = zipmapDecodeLength(p);
				//根据键的长度获取记录键长度字段所占的字节数
        llen = zipmapEncodeLength(NULL,l);
				//当压缩字典中含有对应的key时,返回此键值对的首地址
        if (key != NULL && k == NULL && l == klen && !memcmp(p+llen,key,l)) {
            /* Only return when the user doesn't care
             * for the total length of the zipmap. */
						//当totlen形参不为NULL时说明还需要返回压缩字典的长度
            if (totlen != NULL) {
								//k保存以查找到的key的地址
                k = p;
            } else {
                return p;
            }
        }
				//跳过键和记录键长度的字段
        p += llen+l;
        /* Skip the value as well */
				//获取value的长度
        l = zipmapDecodeLength(p);
				//跳过记录value长度的字段长度
        p += zipmapEncodeLength(NULL,l);
				//获取value中的空闲空间
        free = p[0];
				//跳过value长度和空闲长度
        p += l+1+free; /* +1 to skip the free byte */
    }
		//如果totlen不为NULL则记录压缩字典的长度
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;
    return k;
}

//返回键值对初始长度
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;

		//key+value长度+keylength( 初始为1 )+valuelength( 初始为1 )+free的长度
    l = klen+vlen+3;
		//如果keylength的长度大于253
    if (klen >= ZIPMAP_BIGLEN) l += 4;
		//如果valuelength的长度大于253
    if (vlen >= ZIPMAP_BIGLEN) l += 4;
    return l;
}

/* Return the total amount used by a key (encoded length + payload) */
//获取key的长度( 包括记录key长度的字段所占的字节数 )
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    unsigned int l = zipmapDecodeLength(p);
    return zipmapEncodeLength(NULL,l) + l;
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) */
//返回value的长度( 包括记录value长度字段的长度和free长度 )
static unsigned int zipmapRawValueLength(unsigned char *p) {
		//获取value的长度
    unsigned int l = zipmapDecodeLength(p);
    unsigned int used;
		//获取记录value长度的字节数
    used = zipmapEncodeLength(NULL,l);
		//记录value的长度( 包括记录value长度字段的长度和free长度 )
    used += p[used] + 1 + l;
    return used;
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). */
//返回整个键值对的长度
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    unsigned int l = zipmapRawKeyLength(p);
    return l + zipmapRawValueLength(p+l);
}

//给压缩字典重新分配空间
static inline unsigned char *zipmapResize(unsigned char *zm, unsigned int len) {
    zm = zrealloc(zm, len);
    zm[len-1] = ZIPMAP_END;
    return zm;
}

/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. */
//插入或更新键值对
unsigned char *zipmapSet(unsigned char *zm,unsigned char *key,unsigned int klen,unsigned char *val,unsigned int vlen,int *update) {
    unsigned int zmlen, offset;
    unsigned int freelen, reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;

    freelen = reqlen;
    if (update) *update = 0;
		//查找是否有对应的key且获取压缩字典的长度保存在zmlen变量中
    p = zipmapLookupRaw(zm,key,klen,&zmlen);
		//如果压缩字典不存在对应的key时
    if (p == NULL) {
        /* Key not found: enlarge */
				//增加压缩字典的内存空间
        zm = zipmapResize(zm, zmlen+reqlen);
				//从压缩字典的最后一个键值对后加新的键值对
        p = zm+zmlen-1;
				//重新计算压缩字典的长度
        zmlen = zmlen+reqlen;

        /* Increase zipmap length (this is an insert) */
				//增加键值对记录数
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]++;
    } else {
				//如果存在对应的key则更新值
        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
				//将更新标识设置为1
        if (update) *update = 1;
				//获取原来整个键值对的长度
        freelen = zipmapRawEntryLength(p);
				//如果更新后的键值对长度要比原来键值对的长度要长
        if (freelen < reqlen) {
            /* Store the offset of this key within the current zipmap, so
             * it can be resized. Then, move the tail backwards so this
             * pair fits at the current position. */
						//记录原来的键值对和压缩字典首地址的偏移地址
            offset = p-zm;
						//重新分配压缩字典的空间
            zm = zipmapResize(zm, zmlen-freelen+reqlen);
            //重新获取需要更新的key的首地址
						p = zm+offset;

            /* The +1 in the number of bytes to be moved is caused by the
             * end-of-zipmap byte. Note: the *original* zmlen is used. */
						//移动内容,空出更新后的键值对需要的内存空间
            memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
            zmlen = zmlen-freelen+reqlen;
            freelen = reqlen;
        }
    }

    /* We now have a suitable block where the key/value entry can
     * be written. If there is too much free space, move the tail
     * of the zipmap a few bytes to the front and shrink the zipmap,
     * as we want zipmaps to be very space efficient. */
		//记录free字段的大小
    empty = freelen-reqlen;
		//如果空闲字节大于等于预设值则消除空闲字节
    if (empty >= ZIPMAP_VALUE_MAX_FREE) {
        /* First, move the tail <empty> bytes to the front, then resize
         * the zipmap to be <empty> bytes smaller. */
				//记录键值对的首地址
        offset = p-zm;
				//消除空闲内存
        memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
        //重新计算压缩字典的长度
				zmlen -= empty;
				//重新分配压缩字典的内存
        zm = zipmapResize(zm, zmlen);
				//重新获取键值对的首地址
        p = zm+offset;
				//记录空闲字节
        vempty = 0;
    } else {
				//记录空闲字节
        vempty = empty;
    }

    /* Just write the key + value and we are done. */
    /* Key: */
		//记录key的长度并移动指针p( 跳过记录长度字段的字节数 )
    p += zipmapEncodeLength(p,klen);
		//更新key值
    memcpy(p,key,klen);
		//跳过key
    p += klen;
    /* Value: */
		//记录value的长度并移动指针p(  跳过记录长度字段的字节数 )
    p += zipmapEncodeLength(p,vlen);
		//记录空闲字节
    *p++ = vempty;
		//保存value值
    memcpy(p,val,vlen);
    return zm;
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
//删除指定键值对返回是否有删除元素记录到deleted形参
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {
    unsigned int zmlen, freelen;
		//获取指定的键值对和压缩字典的长度
    unsigned char *p = zipmapLookupRaw(zm,key,klen,&zmlen);
		//如果存在指定的键值对
    if (p) {
				//获取键值对的长度
        freelen = zipmapRawEntryLength(p);
				//删除键值对
        memmove(p, p+freelen, zmlen-((p-zm)+freelen+1));
        //重新分配压缩字典的空间
				zm = zipmapResize(zm, zmlen-freelen);

        /* Decrease zipmap length */
				//计算压缩字典的键值对数量
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]--;

        if (deleted) *deleted = 1;
    } else {
        if (deleted) *deleted = 0;
    }
    return zm;
}

/* Call before iterating through elements via zipmapNext() */
//获取第一个键值对首地址
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}

/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 *
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 */
//获取下一个键值对
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {
    if (zm[0] == ZIPMAP_END) return NULL;
    if (key) {
        *key = zm;
        *klen = zipmapDecodeLength(zm);
        *key += ZIPMAP_LEN_BYTES(*klen);
    }
    zm += zipmapRawKeyLength(zm);
    if (value) {
        *value = zm+1;
        *vlen = zipmapDecodeLength(zm);
        *value += ZIPMAP_LEN_BYTES(*vlen);
    }
    zm += zipmapRawValueLength(zm);
    return zm;
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. */
//根据key的首地址和长度获取对应的value值和长度
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    if ((p = zipmapLookupRaw(zm,key,klen,NULL)) == NULL) return 0;
    p += zipmapRawKeyLength(p);
    *vlen = zipmapDecodeLength(p);
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. */
//判断是否存在对应的键值对
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL) != NULL;
}

/* Return the number of entries inside a zipmap */
//返回压缩字典的真正长度
unsigned int zipmapLen(unsigned char *zm) {
    unsigned int len = 0;
    if (zm[0] < ZIPMAP_BIGLEN) {
        len = zm[0];
    } else {
        unsigned char *p = zipmapRewind(zm);
        while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;

        /* Re-store length if small enough */
        if (len < ZIPMAP_BIGLEN) zm[0] = len;
    }
    return len;
}

/* Return the raw size in bytes of a zipmap, so that we can serialize
 * the zipmap on disk (or everywhere is needed) just writing the returned
 * amount of bytes of the C array starting at the zipmap pointer. */
//返回压缩字典的真正长度
size_t zipmapBlobLen(unsigned char *zm) {
    unsigned int totlen;
    zipmapLookupRaw(zm,NULL,0,&totlen);
    return totlen;
}

#ifdef REDIS_TEST
static void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#define UNUSED(x) (void)(x)
int zipmapTest(int argc, char *argv[]) {
    unsigned char *zm;

    UNUSED(argc);
    UNUSED(argv);

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);

    printf("\nLook up large key:\n");
    {
        unsigned char buf[512];
        unsigned char *value;
        unsigned int vlen, i;
        for (i = 0; i < 512; i++) buf[i] = 'a';

        zm = zipmapSet(zm,buf,512,(unsigned char*) "long",4,NULL);
        if (zipmapGet(zm,buf,512,&value,&vlen)) {
            printf("  <long key> is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }

    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate through elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
