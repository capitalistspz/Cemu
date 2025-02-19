#pragma once

#include <type_traits>

namespace Latte
{
	class LATTEREG;
};

template<class T, std::size_t... N>
constexpr T bswap_impl(T i, std::index_sequence<N...>)
{
	return ((((i >> (N * CHAR_BIT)) & (T)(uint8_t)(-1)) << ((sizeof(T) - 1 - N) * CHAR_BIT)) | ...);
}

template<class T, class U = std::make_unsigned_t<T>>
constexpr T bswap(T i)
{
	return (T)bswap_impl<U>((U)i, std::make_index_sequence<sizeof(T)>{});
}

template <typename T>
constexpr T SwapEndian(T value)
{
	if constexpr (std::is_integral_v<T>)
	{
#ifdef _MSC_VER
		if constexpr (sizeof(T) == sizeof(uint32_t))
		{
			return (T)_byteswap_ulong(value);
		}
#endif

		return (T)bswap((std::make_unsigned_t<T>)value);
	}
	else if constexpr (std::is_floating_point_v<T>)
	{
		if constexpr (sizeof(T) == sizeof(uint32_t))
		{
			const auto tmp = bswap(std::bit_cast<uint32_t>(value));
			return std::bit_cast<float>(tmp);
		}
		if constexpr (sizeof(T) == sizeof(uint64_t))
		{
			const auto tmp = bswap(std::bit_cast<uint64_t>(value));
			return std::bit_cast<double>(tmp);
		}
	}
	else if constexpr (std::is_enum_v<T>)
	{
		return (T)SwapEndian((std::underlying_type_t<T>)value);
	}
	else if constexpr (std::is_base_of_v<Latte::LATTEREG, T>)
	{
		const auto tmp = bswap<uint32_t>(std::bit_cast<uint32_t>(value));
		return std::bit_cast<T>(tmp);
	}
    else
    {
        static_assert(std::is_integral_v<T>, "unsupported betype specialization!");
    }

	return value;
}

// swap if native isn't big endian
template <typename T>
constexpr T _BE(T value)
{
	return SwapEndian(value);
}

// swap if native isn't little endian
template <typename T>
constexpr T _LE(T value)
{
	return value;
}

template <typename T>
class betype
{
public:
	constexpr betype() = default;

	// copy
	constexpr explicit(false) betype(T value)
		: m_value(SwapEndian(value)) {}

	// required for trivially_copyable
	constexpr betype(const betype& value) = default;

	template <typename U>
	constexpr explicit(false) betype(const betype<U>& value)
		: betype(static_cast<T>(value.value())) {}

	// assigns
	constexpr static betype from_bevalue(T value)
	{
		betype result;
		result.m_value = value;
		return result;
	}

	// returns LE value
	[[nodiscard]] constexpr T value() const { return SwapEndian<T>(m_value); }

	// returns BE value
	[[nodiscard]] constexpr T bevalue() const { return m_value; }

	constexpr operator T() const { return value(); }

	constexpr betype& operator+=(const betype<T>& v)
	{
		m_value = SwapEndian(T(value() + v.value()));
		return *this;
	}

	constexpr betype& operator+=(const T& v) requires std::integral<T>
	{
		m_value = SwapEndian(T(value() + v));
		return *this;
	}

	constexpr betype& operator-=(const betype& v)
	{
		m_value = SwapEndian(T(value() - v.value()));
		return *this;
	}

	constexpr betype& operator*=(const betype& v)
	{
		m_value = SwapEndian(T(value() * v.value()));
		return *this;
	}

	constexpr betype& operator/=(const betype& v)
	{
		m_value = SwapEndian(T(value() / v.value()));
		return *this;
	}

	constexpr betype& operator&=(const betype& v) requires (requires (T& x, const T& y) { x &= y; })
	{
		m_value &= v.m_value;
		return *this;
	}

	constexpr betype& operator|=(const betype& v) requires (requires (T& x, const T& y) { x |= y; })
	{
		m_value |= v.m_value;
		return *this;
	}

	constexpr betype& operator^=(const betype& v) requires (requires (T& x, const T& y) { x ^= y; })
	{
		m_value ^= v.m_value;
		return *this;
	}

	constexpr betype& operator>>=(std::size_t idx) requires std::integral<T>
	{
		m_value = SwapEndian(T(value() >> idx));
		return *this;
	}

	constexpr betype& operator<<=(std::size_t idx) requires std::integral<T>
	{
		m_value = SwapEndian(T(value() << idx));
		return *this;
	}

	constexpr betype operator~() const requires std::integral<T>
	{
		return from_bevalue(T(~m_value));
	}

	// pre-increment
	constexpr betype& operator++() requires std::integral<T>
	{
		m_value = SwapEndian(T(value() + 1));
		return *this;
	}

	// post-increment
	constexpr betype operator++(int) requires std::integral<T>
	{
		betype<T> tmp(*this);
		m_value = SwapEndian(T(value() + 1));
		return tmp;
	}

	// pre-decrement
	constexpr betype& operator--() requires std::integral<T>
	{
		m_value = SwapEndian(T(value() - 1));
		return *this;
	}

	// post-decrement
	constexpr betype operator--(int) requires std::integral<T>
	{
		betype tmp(*this);
		m_value = SwapEndian(T(value() - 1));
		return tmp;
	}

private:
	//T m_value{}; // before 1.26.2
	T m_value;
};

using uint64be = betype<uint64>;
using uint32be = betype<uint32>;
using uint16be = betype<uint16>;
using uint8be = betype<uint8>;

using sint64be = betype<sint64>;
using sint32be = betype<sint32>;
using sint16be = betype<sint16>;
using sint8be = betype<sint8>;

using float32be = betype<float>;
using float64be = betype<double>;

static_assert(sizeof(betype<uint64>) == sizeof(uint64));
static_assert(sizeof(betype<uint32>) == sizeof(uint32));
static_assert(sizeof(betype<uint16>) == sizeof(uint16));
static_assert(sizeof(betype<uint8>) == sizeof(uint8));
static_assert(sizeof(betype<float>) == sizeof(float));
static_assert(sizeof(betype<double>) == sizeof(double));

static_assert(std::is_trivially_copyable_v<uint32be>);
static_assert(std::is_trivially_constructible_v<uint32be>);
static_assert(std::is_copy_constructible_v<uint32be>);
static_assert(std::is_move_constructible_v<uint32be>);
static_assert(std::is_copy_assignable_v<uint32be>);
static_assert(std::is_move_assignable_v<uint32be>);
