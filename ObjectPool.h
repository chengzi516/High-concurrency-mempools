#include "Common.h"

#ifdef _WIN32
#include <windows.h>
#else
// 
#endif

// 定长内存池模板类
template<class T>
class ObjectPool
{
public:
	// 创建一个对象
	T* New()
	{
		T* obj = nullptr;

		// 如果自由链表不为空，优先从自由链表中获取对象
		if (_freeList) //自由链表存储暂时空闲的内存
		{
			void* next = *((void**)_freeList);  //通过 *((void**)_freeList)，获取 _freeList 指向的对象中存储的指针值。这个值就是下一个对象的地址。
			obj = (T*)_freeList;                //重要！x86下指针为8字节，x64指针为4字节。使用void作为通用指针类型是常用操作。
			_freeList = next;
		}
		else
		{
			// 如果剩余内存不足以分配一个对象，则重新申请大块内存
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 128 * 1024; // 每次申请128KB内存
				_memory = (char*)SystemAlloc(_remainBytes >> 13); // 使用SystemAlloc按页申请内存 ，右移13位等于除8192，8192字节就是一页的大小，这里是为了计算所需多少页
				if (_memory == nullptr)                            //使用char也是为了方便步长的计算
				{
					throw std::bad_alloc(); // 如果申请失败，抛出异常
				}
			}

			// 从大块内存中分配一个对象
			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T); // 确保对象大小对齐
			_memory += objSize; // 更新内存指针
			_remainBytes -= objSize; // 更新剩余字节数
		}

		// 调用对象的构造函数进行初始化
		new(obj) T;

		return obj;
	}

	// 删除一个对象
	void Delete(T* obj)
	{
		// 调用对象的析构函数清理对象
		obj->~T();

		// 将对象头插回自由链表
		*(void**)obj = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr; // 指向大块内存的指针
	size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数

	void* _freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};

// 测试用的树节点结构
struct TreeNode
{
	int _val; // 节点值
	TreeNode* _left; // 左子节点
	TreeNode* _right; // 右子节点

	// 构造函数
	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

// 测试ObjectPool的性能
void TestObjectPool()
{
	// 申请释放的轮次
	const size_t Rounds = 5;

	// 每轮申请释放多少次
	const size_t N = 100000;

	std::vector<TreeNode*> v1; // 用于存储普通new分配的对象
	v1.reserve(N);

	// 记录普通new分配的时间
	size_t begin1 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v1.push_back(new TreeNode); // 使用普通new分配对象
		}
		for (int i = 0; i < N; ++i)
		{
			delete v1[i]; // 释放对象
		}
		v1.clear();
	}

	size_t end1 = clock();

	// 使用ObjectPool分配对象
	std::vector<TreeNode*> v2; // 用于存储ObjectPool分配的对象
	v2.reserve(N);

	// 创建一个ObjectPool实例
	ObjectPool<TreeNode> TNPool;
	size_t begin2 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v2.push_back(TNPool.New()); // 使用ObjectPool分配对象
		}
		for (int i = 0; i < N; ++i)
		{
			TNPool.Delete(v2[i]); // 释放对象
		}
		v2.clear();
	}
	size_t end2 = clock();

	// 输出测试结果
	cout << "new cost time:" << end1 - begin1 << endl;
	cout << "object pool cost time:" << end2 - begin2 << endl;
}