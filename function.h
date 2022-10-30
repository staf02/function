#pragma once

#include <exception>

struct bad_function_call : std::exception {
  const char* what() const noexcept override {
    return "bad function call";
  }
};

using storage_t = std::aligned_storage_t<sizeof(void*), alignof(void*)>;

template <typename T>
static constexpr bool is_small =
    sizeof(T) <= sizeof(void*) && std::is_nothrow_move_constructible_v<T> &&
    alignof(storage_t) % alignof(T) == 0;

template <typename T>
static T* get_pointer(storage_t& buf) noexcept {
  if constexpr (is_small<T>) {
    return reinterpret_cast<T*>(&buf);
  } else {
    return *reinterpret_cast<T**>(&buf);
  }
}

template <typename T>
static T const* get_pointer(storage_t const& buf) noexcept {
  if constexpr (is_small<T>) {
    return reinterpret_cast<T const*>(&buf);
  } else {
    return *reinterpret_cast<T* const*>(&buf);
  }
}

template <typename R, typename... Args>
struct type_descriptor {
  void (*copy)(storage_t const& src, storage_t& dst);
  void (*move)(storage_t& src, storage_t& dst);
  void (*destroy)(storage_t& src);
  R (*invoke)(storage_t const& src, Args... args);

  static type_descriptor<R, Args...> const* get_empty_func_descriptor() {
    constexpr static type_descriptor<R, Args...> empty = {
        [](storage_t const& src, storage_t& dst) {},
        [](storage_t& src, storage_t& dst) {},
        [](storage_t& src) {},
        [](storage_t const& src, Args... args) -> R {
          throw bad_function_call{};
        }};
    return &empty;
  }

  template <typename T>
  static type_descriptor<R, Args...> const* get_func_descriptor() {
    constexpr static type_descriptor<R, Args...> result = {
        [](storage_t const& src, storage_t& dst) {
          if constexpr (is_small<T>) {
            new (&dst) T(*get_pointer<T>(src));
          } else {
            *reinterpret_cast<T**>(&dst) = new T(*get_pointer<T>(src));
          }
        },
        [](storage_t& src, storage_t& dst) {
          if constexpr (is_small<T>) {
            new (&dst) T(std::move(*get_pointer<T>(src)));
            get_pointer<T>(src)->~T();
          } else {
            *reinterpret_cast<T**>(&dst) = *reinterpret_cast<T* const*>(&src);
          }
        },
        [](storage_t& src) {
          if constexpr (is_small<T>) {
            get_pointer<T>(src)->~T();
          } else {
            delete get_pointer<T>(src);
          }
        },
        [](storage_t const& src, Args... args) -> R {
          return (*get_pointer<T>(src))(std::forward<Args>(args)...);
        }};
    return &result;
  }
};

template <typename F>
struct function;

template <typename R, typename... Args>
struct function<R(Args...)> {
  function() noexcept
      : desc(type_descriptor<R, Args...>::get_empty_func_descriptor()) {}

  function(function const& other) : desc(other.desc) {
    desc->copy(other.buf, buf);
  }

  function(function&& other) noexcept : desc(other.desc) {
    desc->move(other.buf, buf);
    other.desc = type_descriptor<R, Args...>::get_empty_func_descriptor();
  }

  template <typename T>
  function(T val)
      : desc(type_descriptor<R, Args...>::template get_func_descriptor<T>()) {
    if constexpr (is_small<T>) {
      new (&buf) T(std::move(val));
    } else {
      *reinterpret_cast<T**>(&buf) = new T(std::move(val));
    }
  }

  function& operator=(function const& rhs) {
    if (this == &rhs) {
      return *this;
    }

    function(rhs).swap(*this);
    return *this;
  }

  function& operator=(function&& rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }

    desc->destroy(buf);
    desc = rhs.desc;
    desc->move(rhs.buf, buf);
    rhs.desc = type_descriptor<R, Args...>::get_empty_func_descriptor();
    return *this;
  }

  ~function() {
    desc->destroy(buf);
  }

  explicit operator bool() const noexcept {
    return desc != type_descriptor<R, Args...>::get_empty_func_descriptor();
  }

  R operator()(Args... args) const {
    if (!*this == true) {
      throw bad_function_call();
    }

    return desc->invoke(buf, std::forward<Args>(args)...);
  }

  template <typename T>
  T* target() noexcept {
    if (desc ==
            type_descriptor<R, Args...>::template get_func_descriptor<T>())
      return get_pointer<T>(buf);
    return nullptr;
  }

  template <typename T>
  T const* target() const noexcept {
    if (desc ==
            type_descriptor<R, Args...>::template get_func_descriptor<T>())
      return get_pointer<T>(buf);
    return nullptr;
  }

private:

  void swap(function& other) noexcept {
    auto tmp = std::move(*this);
    *this = std::move(other);
    other = std::move(tmp);
  }

  type_descriptor<R, Args...> const* desc;
  storage_t buf;
};
