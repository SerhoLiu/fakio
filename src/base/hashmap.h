/*
 * Copyright (c) 2013 Serho Liu. All rights reserved.
 *
 * Use of this source code is governed by a MIT-style license that can be
 * found in the MIT-LICENSE file.
 *
 * HashMap: a hashtable
 */

#ifndef _LIBAKIO_HASHMAP_H_
#define _LIBAKIO_HASHMAP_H_

  
typedef struct hashmap hashmap;


/**
 * 自定义比较两个 Key 值的函数
 *
 * Return value: = 0 相同，< 0 小于, > 0 大于
 */
typedef int compare_key(const void *desc, int dlen,
                        const void *src, int slen);


/**
 * 自定义对 Key,Value 进行 map 的函数，允许对 Value 进行修改
 */
typedef void hashmap_map_func(const void *key, void **value,
                              const void *other);

/**
 * 创建 hashmap
 * @size: 初始大小，应该指定为 2^n，若等于0则使用默认值 512
 */
hashmap *hashmap_new(unsigned long size, compare_key *compare);

/**
 * 释放 hashmap，不会释放 Key,Value，请自己管理
 */
void hashmap_free(hashmap *map);

/**
 * 将 Key/Value 映射添加到 Hashmap 中
 *
 * Return value: 0 添加失败, 1 添加成功
 */
int hashmap_put(hashmap *map, const void *key, int len, void *value);


/**
 * 根据 Key 获取相应的 Value
 *
 * Return value: Value
 */
void *hashmap_get(const hashmap *map, const void *key, int len);

/**
 * 根据 Key 删除相应的 Key/Value 映射
 *
 * Return value: Value
 */
void *hashmap_delete(hashmap *map, const void *key, int len);

/**
 * 将 func 使用到 Hashmap 中的 Key/Value 对上
 *
 * Return value: 0 失败, 1 成功
 */
int hashmap_map(const hashmap *map, hashmap_map_func *func,
                const void *other);

#endif
