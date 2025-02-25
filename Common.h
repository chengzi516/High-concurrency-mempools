#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <time.h>
#include <assert.h>

#include <thread>
#include <mutex>

using std::cout;
using std::endl;

#ifdef _WIN32
#include <windows.h>
#else
// ...
#endif

// 常量定义
// 最大可分配内存大小（256KB）
static const size_t MAX_BYTES = 256 * 1024;
// 自由链表数量（208个桶，对应不同大小的对象）
static const size_t NFREELIST = 208;
// 页面数量（129个页面，用于管理大块内存）
static const size_t NPAGES = 129;
// 页面大小的位移（1 << 13 = 8KB）
static const size_t PAGE_SHIFT = 13;

#ifdef _WIN64
typedef unsigned long long PAGE_ID; // 64位系统下，页面ID类型为unsigned long long
#elif _WIN32
typedef size_t PAGE_ID; // 32位系统下，页面ID类型为size_t
#else
// linux
#endif

// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	// 在Windows系统下，使用VirtualAlloc申请内存
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// Linux系统下可以使用brk或mmap等方式申请内存
#endif

	if (ptr == nullptr)
		throw std::bad_alloc(); // 如果申请失败，抛出异常

	return ptr;
}

// 获取对象的下一个对象指针
static void*& NextObj(void* obj)  //返回指针的引用，减小系统开销
{
	return *(void**)obj;
}

// 管理切分好的小对象的自由链表
class FreeList
{
public:
	// 将对象推入自由链表（头插法）
	void Push(void* obj)
	{
		assert(obj); // 断言对象指针不为空

		// 头插
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

	// 将一批对象推入自由链表
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	// 从自由链表中弹出一批对象
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n <= _size); // 确保请求的数量不超过链表大小
		start = _freeList;
		end = start;

		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}

		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	// 从自由链表中弹出一个对象
	void* Pop()
	{
		assert(_freeList); // 确保链表不为空

		// 头删
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;

		return obj;
	}

	// 判断自由链表是否为空
	bool Empty()
	{
		return _freeList == nullptr;
	}

	// 获取自由链表的最大容量
	size_t& MaxSize()
	{
		return _maxSize;
	}

	// 获取自由链表的当前大小
	size_t Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr; // 自由链表的头指针
	size_t _maxSize = 1;       // 自由链表的最大容量
	size_t _size = 0;          // 自由链表的当前大小
};

// 计算对象大小的对齐映射规则
class SizeClass
{
public:
	// 对齐大小计算（向上对齐）
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));   //等同于return ((bytes + alignNum - 1) / alignNum) * alignNum; 
	}

	// 对象大小对齐
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8); // [1, 128]，8字节对齐
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16); // [128+1, 1024]，16字节对齐
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128); // [1024+1, 8KB]，128字节对齐
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024); // [8KB+1, 64KB]，1024字节对齐
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024); // [64KB+1, 256KB]，8KB对齐
		}
		else
		{
			assert(false); // 超出支持范围
			return -1;
		}
	}

	// 计算对象大小对应的自由链表索引
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算对象大小对应的自由链表桶索引
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES); // 确保对象大小在支持范围内

		static int group_array[4] = { 16, 56, 56, 56 }; // 每个区间的链表数量            
		  /*  group_array 是一个静态数组，用于记录每个大小区间的链表数量。例如：
			[1, 128] 区间有 16 个链表。
			[128 + 1, 1024] 区间有 56 个链表。              
			[1024 + 1, 8KB] 区间有 56 个链表。
			[8KB + 1, 64KB] 区间有 56 个链表。
			[64KB + 1, 256KB] 区间有 56 个链表。*/
		if (bytes <= 128) {
			return _Index(bytes, 3); // [1, 128]，8字节对齐
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0]; // [128+1, 1024]，16字节对齐
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0]; // [1024+1, 8KB]，128字节对齐
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0]; // [8KB+1, 64KB]，1024字节对齐
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0]; // [64KB+1, 256KB]，8KB对齐
		}
		else {
			assert(false); // 超出支持范围
		}

		return -1;
	}

	// 计算一次从中心缓存移动的对象数量
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0); // 确保对象大小大于0

		// 计算一次批量移动的对象数量上限
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2; // 最小值为2

		if (num > 512)
			num = 512; // 最大值为512

		return num;
	}

	// 计算一次向系统申请的页面数量
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size); // 计算一次批量移动的对象数量
		size_t npage = num * size; // 计算总字节数

		npage >>= PAGE_SHIFT; // 转换为页面数量
		if (npage == 0)
			npage = 1; // 最小为1

		return npage;
	}
};

// 管理多个连续页大块内存跨度结构
struct Span
{
	PAGE_ID _pageId = 0; // 大块内存起始页的页号
	size_t  _n = 0;      // 页的数量

	Span* _next = nullptr; // 双向链表的下一个指针
	Span* _prev = nullptr; // 双向链表的上一个指针

	size_t _useCount = 0; // 切好小块内存，被分配给thread cache的计数
	void* _freeList = nullptr; // 切好的小块内存的自由链表

	bool _isUse = false; // 是否在被使用
};

// 带头双向循环链表
class SpanList
{
public:
	// 构造函数，初始化带头双向循环链表
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	// 获取链表的第一个元素
	Span* Begin()
	{
		return _head->_next;
	}

	// 获取链表的尾部（虚拟节点）
	Span* End()
	{
		return _head;
	}

	// 判断链表是否为空
	bool Empty()
	{
		return _head->_next == _head;
	}

	// 在链表头部插入一个span
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	// 从链表头部弹出一个span
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// 在指定位置插入一个span
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos); // 确保插入位置有效
		assert(newSpan); // 确保插入的span有效

		Span* prev = pos->_prev;
		// 插入操作
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	// 从链表中删除一个span
	void Erase(Span* pos)
	{
		assert(pos); // 确保删除位置有效
		assert(pos != _head); // 确保不是头节点

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		// 删除操作
		prev->_next = next;
		next->_prev = prev;
	}

private:
	Span* _head; // 头节点
public:
	std::mutex _mtx; // 桶锁，用于线程安全操作
};