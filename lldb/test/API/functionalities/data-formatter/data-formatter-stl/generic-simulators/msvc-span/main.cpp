// The layout of MSVC STL's std::span. A static extent is not stored:
// _Span_extent_type<T, N> holds only _Mydata and declares _Mysize as a static
// constexpr member.
//
// Nothing may reference _Mysize (e.g. by calling size()). Clang instantiates
// the initializer of a class template's inline static data member only when
// it is referenced, and without it no constant is emitted into the debug info,
// so the formatter has to take the extent from the type.
namespace std {
using size_t = decltype(sizeof(0));

inline constexpr size_t dynamic_extent = static_cast<size_t>(-1);

template <class _Ty, size_t _Extent> struct _Span_extent_type {
  using pointer = _Ty *;

  constexpr _Span_extent_type() noexcept = default;

  constexpr explicit _Span_extent_type(const pointer _Data, size_t) noexcept
      : _Mydata{_Data} {}

  pointer _Mydata{nullptr};
  static constexpr size_t _Mysize = _Extent;
};

template <class _Ty> struct _Span_extent_type<_Ty, dynamic_extent> {
  using pointer = _Ty *;

  constexpr _Span_extent_type() noexcept = default;

  constexpr explicit _Span_extent_type(const pointer _Data,
                                       const size_t _Size) noexcept
      : _Mydata{_Data}, _Mysize{_Size} {}

  pointer _Mydata{nullptr};
  size_t _Mysize{0};
};

template <class _Ty, size_t _Extent = dynamic_extent>
class span : private _Span_extent_type<_Ty, _Extent> {
private:
  using _Mybase = _Span_extent_type<_Ty, _Extent>;
  using _Mybase::_Mydata;
  using _Mybase::_Mysize;

public:
  constexpr span() noexcept = default;

  template <size_t _Size>
  constexpr span(_Ty (&_Arr)[_Size]) noexcept : _Mybase(_Arr, _Size) {}
};
} // namespace std

using S5 = std::span<int, 5>;

int g_array[] = {1, 12, 123, 1234, 12345};
std::span<int, 5> g_static_span(g_array);

int main() {
  int array[] = {1, 12, 123, 1234, 12345};
  std::span<int, 5> static_span(array);
  S5 typedef_span(array);
  std::span<int, 0> static_zero_span;
  std::span<int> dynamic_span(array);

  return 0; // break here
}
