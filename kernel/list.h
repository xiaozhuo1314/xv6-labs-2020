#ifndef __LIST_H__
#define __LIST_H__

#ifndef NULL
#define NULL ((void *)0)
#endif

#define _inline inline __attribute__((always_inline))

/*
 * 通过结构体中的某一个成员获得该结构体的起始位置的指针
 * ptr为结构体中某一个成员X的指针,注意这个X是一种链表类型,且在结构体中存储的是对象,而不是指向它的指针
 * type为结构体的类型
 * member为结构中成员X的变量名
 * 表达式当中的(char *)ptr是结构体成员的地址，然后((char *)&((type *)0)->member)是把struct Data放到内存为0处时计算出的member的偏移量，所以相减就可以得到这个struct Data的地址
 * for example:
 * struct data
 * {
 *     int a;
 *     int X;
 *     int c
 * };
 * struct data *p;
 * 调用struct_entry( &(p->X), struct data, X )就可以获得指针p
 */ 
#define struct_entry(ptr, type, member) ((type *)((char *)ptr - ((char *)&((type *)0)->member)))

// 双向链表
struct linked_list
{
    struct linked_list *prev, *next;
};

// 链表初始化
static _inline void list_init(struct linked_list *head)
{
    head->prev = head->next = head;
}

// 添加节点,将list添加到prev和next之间
static _inline void _list_add(struct linked_list *list, struct linked_list *prev, struct linked_list *next)
{
    list->prev = prev;
    list->next = next;
    prev->next = list;
    next->prev = list;
}

// 将节点加入到头部,head只是用来标志头部,并未使用,类似于一个哨兵节点
static _inline void list_add(struct linked_list *list, struct linked_list *head)
{
    _list_add(list, head, head->next);
}

// 将节点添加到尾部
static _inline void list_add_tail(struct linked_list *list, struct linked_list *head)
{
    _list_add(list, head->prev, head);
}

// 删除节点,仅将节点从链表中拿下来
static _inline void _list_del(struct linked_list *list)
{
    list->prev->next = list->next;
    list->next->prev = list->prev;
}

// 删除节点,将节点前后都设为null
static _inline void list_del(struct linked_list *list)
{
    _list_del(list);
    list->prev = (struct linked_list *)NULL;
    list->next = (struct linked_list *)NULL;
}

// 节点从链表中删除下来后,将节点初始化
static _inline void list_del_and_init(struct linked_list *list)
{
    _list_del(list);
    list_init(list);
}

// 判断list是否为空,因为arg可能是个表达式,所以需要括号括起来
#define list_empty(arg) ((arg) == (arg)->next)

// 链表中的节点都是某一种结构体,list_entry指的是找到该节点结构体的起始内存位置
#define list_entry(ptr, type, member) struct_entry(ptr, type, member)

// 链表中第一个节点结构体的起始内存位置,由于head头节点是一个哨兵节点,所以第一个节点应该是(head)->next
#define list_first_entry(head, type, member) list_entry((head)->next, type, member)

// 链表中尾节点结构体的起始内存位置,由于head头节点是一个哨兵节点,所以尾节点应该是(head)->prev
#define list_last_entry(head, type, member) list_entry((head)->prev, type, member)

// 链表中当前节点的下一个节点所代表的结构体的起始内存位置
#define list_next_entry(l, type, member) list_entry((l)->next, type, member)

// 链表中当前节点的上一个节点所代表的结构体的起始内存位置
#define list_prev_entry(l, type, member) list_entry((l)->prev, type, member)

/*
 * 遍历链表的for循环头,entry未初始化
 * entry为结构体指针
 * head为结构体中链表成员的哨兵头节点
 * member为结构中链表成员的变量名
 */
#define list_for_each_entry(entry, head, member) \
    for(entry = list_first_entry(head, typeof(*entry), member); \
        &(entry->member) != (head); \
        entry = list_next_entry(&(entry->member), typeof(*entry), member))

/*
 * 遍历链表的for循环头,entry已经初始化了
 * entry为结构体指针
 * head为结构体中链表成员的哨兵头节点
 * member为结构中链表成员的变量名
 */
#define list_for_each_entry_continue(entry, head, member) \
     for(; &(entry->member) != (head); \
           entry = list_next_entry(&(entry->member), typeof(*entry), member))

/*
 * 遍历链表的for循环头,entry未初始化,且加上了一个next_entry用于在for循环中使用
 * entry为结构体指针
 * head为结构体中链表成员的哨兵头节点
 * member为结构中链表成员的变量名
 */
#define list_for_each_entry_safe(entry, next_entry, head, member) \
    for(entry = list_first_entry(head, typeof(*entry), member),   \
        next_entry = list_next_entry(&(entry->member), typeof(*entry), member); \
        &(entry->member) != (head); \
        entry = next, next_entry = list_next_entry(&(entry->member), typeof(*entry), member))

/*
 * 遍历链表的for循环头,entry已经初始化了,且加上了一个next_entry用于在for循环中使用
 * entry为结构体指针
 * head为结构体中链表成员的哨兵头节点
 * member为结构中链表成员的变量名
 */
#define list_for_each_entry_safe_continue(entry, next_entry, head, member) \
    for(next_entry = list_next_entry(&(entry->member), typeof(*entry), member); \
        &(entry->member) != (head); \
        entry = next, next_entry = list_next_entry(&(entry->member), typeof(*entry), member))

/*
 * 反着遍历链表的for循环头,entry未初始化
 * entry为结构体指针
 * head为结构体中链表成员的哨兵头节点
 * member为结构中链表成员的变量名
 */
#define list_for_each_entry_reverse(entry, head, member) \
    for(entry = list_last_entry(head, type(*entry), member); \
        &(entry->member) != (head); \
        entry = list_prev_entry(&(entry->member), typeof(*entry), member))

#endif