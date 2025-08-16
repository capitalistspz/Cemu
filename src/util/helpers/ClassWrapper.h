#pragma once

#include <mutex>
#include <memory>

template<typename T>
class SingletonClass
{
public:
	static T* getInstance()
	{
		static T instance;
		return &instance;
	}

protected:
	SingletonClass() = default;
};

template<typename T>
class SingletonRef
{
public:
	static std::shared_ptr<T> getInstance()
	{
		static std::atomic<std::weak_ptr<T>> s_instance;
		std::shared_ptr<T> result;
		s_instance.compare_exchange_weak(result, std::make_shared<T>());
		return result;
	}

	SingletonRef() = delete;
};