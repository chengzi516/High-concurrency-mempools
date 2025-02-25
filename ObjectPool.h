#include "Common.h"

#ifdef _WIN32
#include <windows.h>
#else
// 
#endif

// �����ڴ��ģ����
template<class T>
class ObjectPool
{
public:
	// ����һ������
	T* New()
	{
		T* obj = nullptr;

		// �����������Ϊ�գ����ȴ����������л�ȡ����
		if (_freeList) //��������洢��ʱ���е��ڴ�
		{
			void* next = *((void**)_freeList);  //ͨ�� *((void**)_freeList)����ȡ _freeList ָ��Ķ����д洢��ָ��ֵ�����ֵ������һ������ĵ�ַ��
			obj = (T*)_freeList;                //��Ҫ��x86��ָ��Ϊ8�ֽڣ�x64ָ��Ϊ4�ֽڡ�ʹ��void��Ϊͨ��ָ�������ǳ��ò�����
			_freeList = next;
		}
		else
		{
			// ���ʣ���ڴ治���Է���һ�������������������ڴ�
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 128 * 1024; // ÿ������128KB�ڴ�
				_memory = (char*)SystemAlloc(_remainBytes >> 13); // ʹ��SystemAlloc��ҳ�����ڴ� ������13λ���ڳ�8192��8192�ֽھ���һҳ�Ĵ�С��������Ϊ�˼����������ҳ
				if (_memory == nullptr)                            //ʹ��charҲ��Ϊ�˷��㲽���ļ���
				{
					throw std::bad_alloc(); // �������ʧ�ܣ��׳��쳣
				}
			}

			// �Ӵ���ڴ��з���һ������
			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T); // ȷ�������С����
			_memory += objSize; // �����ڴ�ָ��
			_remainBytes -= objSize; // ����ʣ���ֽ���
		}

		// ���ö���Ĺ��캯�����г�ʼ��
		new(obj) T;

		return obj;
	}

	// ɾ��һ������
	void Delete(T* obj)
	{
		// ���ö�������������������
		obj->~T();

		// ������ͷ�����������
		*(void**)obj = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr; // ָ�����ڴ��ָ��
	size_t _remainBytes = 0; // ����ڴ����зֹ�����ʣ���ֽ���

	void* _freeList = nullptr; // ���������������ӵ����������ͷָ��
};

// �����õ����ڵ�ṹ
struct TreeNode
{
	int _val; // �ڵ�ֵ
	TreeNode* _left; // ���ӽڵ�
	TreeNode* _right; // ���ӽڵ�

	// ���캯��
	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

// ����ObjectPool������
void TestObjectPool()
{
	// �����ͷŵ��ִ�
	const size_t Rounds = 5;

	// ÿ�������ͷŶ��ٴ�
	const size_t N = 100000;

	std::vector<TreeNode*> v1; // ���ڴ洢��ͨnew����Ķ���
	v1.reserve(N);

	// ��¼��ͨnew�����ʱ��
	size_t begin1 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v1.push_back(new TreeNode); // ʹ����ͨnew�������
		}
		for (int i = 0; i < N; ++i)
		{
			delete v1[i]; // �ͷŶ���
		}
		v1.clear();
	}

	size_t end1 = clock();

	// ʹ��ObjectPool�������
	std::vector<TreeNode*> v2; // ���ڴ洢ObjectPool����Ķ���
	v2.reserve(N);

	// ����һ��ObjectPoolʵ��
	ObjectPool<TreeNode> TNPool;
	size_t begin2 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v2.push_back(TNPool.New()); // ʹ��ObjectPool�������
		}
		for (int i = 0; i < N; ++i)
		{
			TNPool.Delete(v2[i]); // �ͷŶ���
		}
		v2.clear();
	}
	size_t end2 = clock();

	// ������Խ��
	cout << "new cost time:" << end1 - begin1 << endl;
	cout << "object pool cost time:" << end2 - begin2 << endl;
}