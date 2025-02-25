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

// ��������
// ���ɷ����ڴ��С��256KB��
static const size_t MAX_BYTES = 256 * 1024;
// ��������������208��Ͱ����Ӧ��ͬ��С�Ķ���
static const size_t NFREELIST = 208;
// ҳ��������129��ҳ�棬���ڹ������ڴ棩
static const size_t NPAGES = 129;
// ҳ���С��λ�ƣ�1 << 13 = 8KB��
static const size_t PAGE_SHIFT = 13;

#ifdef _WIN64
typedef unsigned long long PAGE_ID; // 64λϵͳ�£�ҳ��ID����Ϊunsigned long long
#elif _WIN32
typedef size_t PAGE_ID; // 32λϵͳ�£�ҳ��ID����Ϊsize_t
#else
// linux
#endif

// ֱ��ȥ���ϰ�ҳ����ռ�
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	// ��Windowsϵͳ�£�ʹ��VirtualAlloc�����ڴ�
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// Linuxϵͳ�¿���ʹ��brk��mmap�ȷ�ʽ�����ڴ�
#endif

	if (ptr == nullptr)
		throw std::bad_alloc(); // �������ʧ�ܣ��׳��쳣

	return ptr;
}

// ��ȡ�������һ������ָ��
static void*& NextObj(void* obj)  //����ָ������ã���Сϵͳ����
{
	return *(void**)obj;
}

// �����зֺõ�С�������������
class FreeList
{
public:
	// ������������������ͷ�巨��
	void Push(void* obj)
	{
		assert(obj); // ���Զ���ָ�벻Ϊ��

		// ͷ��
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

	// ��һ������������������
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	// �����������е���һ������
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n <= _size); // ȷ����������������������С
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

	// �����������е���һ������
	void* Pop()
	{
		assert(_freeList); // ȷ������Ϊ��

		// ͷɾ
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;

		return obj;
	}

	// �ж����������Ƿ�Ϊ��
	bool Empty()
	{
		return _freeList == nullptr;
	}

	// ��ȡ����������������
	size_t& MaxSize()
	{
		return _maxSize;
	}

	// ��ȡ��������ĵ�ǰ��С
	size_t Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr; // ���������ͷָ��
	size_t _maxSize = 1;       // ����������������
	size_t _size = 0;          // ��������ĵ�ǰ��С
};

// ��������С�Ķ���ӳ�����
class SizeClass
{
public:
	// �����С���㣨���϶��룩
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));   //��ͬ��return ((bytes + alignNum - 1) / alignNum) * alignNum; 
	}

	// �����С����
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8); // [1, 128]��8�ֽڶ���
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16); // [128+1, 1024]��16�ֽڶ���
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128); // [1024+1, 8KB]��128�ֽڶ���
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024); // [8KB+1, 64KB]��1024�ֽڶ���
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024); // [64KB+1, 256KB]��8KB����
		}
		else
		{
			assert(false); // ����֧�ַ�Χ
			return -1;
		}
	}

	// ��������С��Ӧ��������������
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ��������С��Ӧ����������Ͱ����
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES); // ȷ�������С��֧�ַ�Χ��

		static int group_array[4] = { 16, 56, 56, 56 }; // ÿ���������������            
		  /*  group_array ��һ����̬���飬���ڼ�¼ÿ����С������������������磺
			[1, 128] ������ 16 ������
			[128 + 1, 1024] ������ 56 ������              
			[1024 + 1, 8KB] ������ 56 ������
			[8KB + 1, 64KB] ������ 56 ������
			[64KB + 1, 256KB] ������ 56 ������*/
		if (bytes <= 128) {
			return _Index(bytes, 3); // [1, 128]��8�ֽڶ���
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0]; // [128+1, 1024]��16�ֽڶ���
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0]; // [1024+1, 8KB]��128�ֽڶ���
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0]; // [8KB+1, 64KB]��1024�ֽڶ���
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0]; // [64KB+1, 256KB]��8KB����
		}
		else {
			assert(false); // ����֧�ַ�Χ
		}

		return -1;
	}

	// ����һ�δ����Ļ����ƶ��Ķ�������
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0); // ȷ�������С����0

		// ����һ�������ƶ��Ķ�����������
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2; // ��СֵΪ2

		if (num > 512)
			num = 512; // ���ֵΪ512

		return num;
	}

	// ����һ����ϵͳ�����ҳ������
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size); // ����һ�������ƶ��Ķ�������
		size_t npage = num * size; // �������ֽ���

		npage >>= PAGE_SHIFT; // ת��Ϊҳ������
		if (npage == 0)
			npage = 1; // ��СΪ1

		return npage;
	}
};

// ����������ҳ����ڴ��Ƚṹ
struct Span
{
	PAGE_ID _pageId = 0; // ����ڴ���ʼҳ��ҳ��
	size_t  _n = 0;      // ҳ������

	Span* _next = nullptr; // ˫���������һ��ָ��
	Span* _prev = nullptr; // ˫���������һ��ָ��

	size_t _useCount = 0; // �к�С���ڴ棬�������thread cache�ļ���
	void* _freeList = nullptr; // �кõ�С���ڴ����������

	bool _isUse = false; // �Ƿ��ڱ�ʹ��
};

// ��ͷ˫��ѭ������
class SpanList
{
public:
	// ���캯������ʼ����ͷ˫��ѭ������
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	// ��ȡ����ĵ�һ��Ԫ��
	Span* Begin()
	{
		return _head->_next;
	}

	// ��ȡ�����β��������ڵ㣩
	Span* End()
	{
		return _head;
	}

	// �ж������Ƿ�Ϊ��
	bool Empty()
	{
		return _head->_next == _head;
	}

	// ������ͷ������һ��span
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	// ������ͷ������һ��span
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// ��ָ��λ�ò���һ��span
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos); // ȷ������λ����Ч
		assert(newSpan); // ȷ�������span��Ч

		Span* prev = pos->_prev;
		// �������
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	// ��������ɾ��һ��span
	void Erase(Span* pos)
	{
		assert(pos); // ȷ��ɾ��λ����Ч
		assert(pos != _head); // ȷ������ͷ�ڵ�

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		// ɾ������
		prev->_next = next;
		next->_prev = prev;
	}

private:
	Span* _head; // ͷ�ڵ�
public:
	std::mutex _mtx; // Ͱ���������̰߳�ȫ����
};