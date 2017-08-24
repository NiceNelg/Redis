/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */

//创建空链表
list *listCreate(void) {
    struct list *list;

    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */

//释放传入的链表
void listRelease(list *list) {
    unsigned long len;
    listNode *current, *next;

		//获取链表的头节点
    current = list->head;
		//获取链表长度
    len = list->len;
    while(len--) {
				//获取下一个节点
        next = current->next;
				//如果链表的释放节点函数已注册 则使用注册函数释放此节点的value值
        if (list->free) list->free(current->value);
				//释放节点
        zfree(current);
				//设置下一个节点
        current = next;
    }
		//释放链表
    zfree(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */

//新建一个值为value的节点，并且置为链表的头节点
list *listAddNodeHead(list *list, void *value) {
		//初始化链表节点
    listNode *node;

		//分配节点内存空间
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
		//设置节点值
    node->value = value;
		//如果是第一个节点
    if (list->len == 0) {
				//因为是第一个节点,因此链表的头节点和尾节点的字段值都为这个节点 
        list->head = list->tail = node;
				//因为没有其他节点 因此节点的头尾都没有值
        node->prev = node->next = NULL;
    } else {
				//如果链表中已经含有头节点 
				//因为是插入链表头节点 因此节点的prev为空
        node->prev = NULL;
				//因为链表已经有头节点了 因此插入的新头节点的下一个节点就是链表本身的旧头节点
        node->next = list->head;
				//因为不能直接查找链表的旧头节点 因此使用链表结构体记录的头结点就是旧头节点 设置旧头结点的prev为新头结点
        list->head->prev = node;
				//将新头节点覆盖旧头节点
        list->head = node;
    }
		//链表的节点数加一
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */

//新建一个值为value的节点，并且置为链表的尾节点
list *listAddNodeTail(list *list, void *value) {
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

//在链表( 链表不为空时 )随意一处插入节点,after代表插入的节点在指定节点的前或后
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
		//after为真时代表新节点插入在指定节点的后面,否则是插在前面( 老司机别污 -.- )
    if (after) {
				//新节点的前一个节点为旧节点
        node->prev = old_node;
				//新节点的后一个节点为旧节点的后一个节点
        node->next = old_node->next;
				//如果旧节点刚好为链表结尾时设置新节点为链表结尾
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        node->next = old_node;
        node->prev = old_node->prev;
        if (list->head == old_node) {
            list->head = node;
        }
    }
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    if (node->next != NULL) {
        node->next->prev = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */

//删除指定节点
void listDelNode(list *list, listNode *node)
{
		//如果存在前节点时
    if (node->prev)
				//前节点的下一个节点等于此节点的下一个节点
        node->prev->next = node->next;
    else
				//如果不存在前节点的时候表名此节点就是链表头
        list->head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;
		//释放节点内存
    if (list->free) list->free(node->value);
    zfree(node);
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */

//获取链表迭代器
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;

    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
		//设置迭代器方向,从头开始
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
		//设置迭代器方向,从尾开始
        iter->next = list->tail;
		//保存迭代器方向
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */

//销毁迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */

//将迭代器指针重新设置为链表头节点
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

//将迭代器指针重新设置为链表尾节点
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */

//返回迭代器指向的一个节点,并将迭代器指向指定方向的下一个链表节点
listNode *listNext(listIter *iter) {
    listNode *current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */

//复制链表到新链表,并反转新链表返回
list *listDup(list *orig) {
    list *copy;
    listIter iter;
    listNode *node;

		//创建空链表
    if ((copy = listCreate()) == NULL)
        return NULL;

		//复制链表数据
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
		//重置旧链表迭代器,方向从头开始
    listRewind(orig, &iter);
    while((node = listNext(&iter)) != NULL) {
        void *value;

				//如果链表的函数指针dup已定义
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                return NULL;
            }
        } else
            value = node->value;
				//从尾添加节点
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */

//根据节点的值搜索链表,并返回节点
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;

    listRewind(list, &iter);
    while((node = listNext(&iter)) != NULL) {
        if (list->match) {
            if (list->match(node->value, key)) {
                return node;
            }
        } else {
            if (key == node->value) {
                return node;
            }
        }
    }
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */

//返回指定顺序的节点,若index是负数则从尾开始检索
listNode *listIndex(list *list, long index) {
    listNode *n;

    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
//去掉尾节点,并将尾节点设置成头节点
void listRotate(list *list) {
    listNode *tail = list->tail;
		
		//判断链表长度
    if (listLength(list) <= 1) return;

    /* Detach current tail */
    list->tail = tail->prev;
    list->tail->next = NULL;
    /* Move it as head */
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}
