#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

template <typename T, size_t SMALL_SIZE>
class socow_vector {
public:
  using value_type = T;

  using reference = T&;
  using const_reference = const T&;

  using pointer = T*;
  using const_pointer = const T*;

  using iterator = pointer;
  using const_iterator = const_pointer;

private:
  struct dynamic_storage {
    size_t _capacity;
    size_t _references;
    T _data[0];

    dynamic_storage(size_t capacity) : _capacity(capacity), _references(1) {}

    dynamic_storage(const dynamic_storage& other) = default;

    void inc_references() noexcept {
      ++_references;
    }

    void dec_references() noexcept {
      assert(references() > 0);
      --_references;
    }

    size_t capacity() const noexcept {
      return _capacity;
    }

    size_t references() const noexcept {
      return _references;
    }
  };

private:
  size_t _size;
  bool _is_small;

  union {
    T _static_data[SMALL_SIZE];
    dynamic_storage* _dynamic_data;
  };

private:
  bool is_small() const noexcept {
    return _is_small;
  }

  void dec_references() {
    if (is_small()) {
      std::destroy_n(data(), size());
      return;
    }
    dec_references(_dynamic_data, size());
  }

  static void dec_references(dynamic_storage* data, size_t length) {
    data->dec_references();
    if (data->references() == 0) {
      std::destroy_n(data->_data, length);
      operator delete(data);
    }
  }

  static dynamic_storage* get_new_empty_storage(size_t capacity) {
    auto* data_pointer = operator new(sizeof(dynamic_storage) + sizeof(T) * capacity);
    auto* new_dynamic_data = new (data_pointer) dynamic_storage(capacity);
    return new_dynamic_data;
  }

  dynamic_storage* get_copied_storage(const_pointer from, size_t size, size_t capacity) {
    assert(capacity >= size);
    auto* new_dynamic_data = get_new_empty_storage(capacity);
    try {
      std::uninitialized_copy_n(from, size, new_dynamic_data->_data);
    } catch (...) {
      operator delete(new_dynamic_data);
      throw;
    }

    return new_dynamic_data;
  }

  dynamic_storage* get_copied_storage(size_t capacity) {
    assert(capacity >= size());
    return get_copied_storage(std::as_const(*this).data(), size(), capacity);
  }

  void copy_on_write(size_t capacity) {
    auto* new_dynamic_data = get_copied_storage(capacity);
    dec_references();
    _is_small = false;
    _dynamic_data = new_dynamic_data;
  }

  bool copied() const noexcept {
    if (is_small()) {
      return false;
    }
    return _dynamic_data->references() > 1;
  }

  void check_cow() {
    if (copied()) {
      copy_on_write(capacity());
    }
  }

  socow_vector(const socow_vector& other, size_t size, size_t capacity) : _size(size), _is_small(false) {
    assert(capacity > SMALL_SIZE);
    assert(capacity > size);
    _dynamic_data = get_copied_storage(other.data(), size, capacity);
  }

public:
  socow_vector() noexcept : _size(0), _is_small(true), _dynamic_data(nullptr) {}

  socow_vector(const socow_vector& other) : _size(0), _is_small(true) {
    *this = other;
  }

  socow_vector& operator=(const socow_vector& other) {
    if (this == &other) {
      return *this;
    }

    if (is_small() && other.is_small()) {
      size_t common_len = std::min(size(), other.size());
      socow_vector tmp;
      tmp.reserve(common_len);
      for (size_t i = 0; i < common_len; ++i) {
        tmp.push_back(other[i]);
      }
      if (size() < other.size()) {
        std::uninitialized_copy_n(other.data() + size(), other.size() - size(), data() + size());
      } else if (other.size() < size()) {
        std::destroy_n(data() + other.size(), size() - other.size());
      }
      _size = other.size();
      std::swap_ranges(tmp.begin(), tmp.end(), data());
    } else if (!is_small() && other.is_small()) {
      dynamic_storage* _data_ptr = _dynamic_data;
      _dynamic_data = nullptr;
      try {
        std::uninitialized_copy_n(other._static_data, other.size(), _static_data);
      } catch (...) {
        _dynamic_data = _data_ptr;
        throw;
      }
      dec_references(_data_ptr, size());
    } else {
      dec_references();
      _dynamic_data = other._dynamic_data;
      _dynamic_data->inc_references();
    }

    _is_small = other.is_small();
    _size = other.size();

    return *this;
  }

  ~socow_vector() noexcept {
    dec_references();
  }

  reference operator[](size_t index) {
    assert(index < size());
    return data()[index];
  }

  const_reference operator[](size_t index) const {
    assert(index < size());
    return data()[index];
  }

  pointer data() {
    if (is_small()) {
      return _static_data;
    }
    check_cow();
    return _dynamic_data->_data;
  }

  const_pointer data() const noexcept {
    if (is_small()) {
      return _static_data;
    }
    return _dynamic_data->_data;
  }

  size_t size() const noexcept {
    return _size;
  }

  reference front() {
    assert(size() > 0);
    return (*this)[0];
  }

  const_reference front() const {
    assert(size() > 0);
    return (*this)[0];
  }

  reference back() {
    assert(size() > 0);
    return (*this)[size() - 1];
  }

  const_reference back() const {
    assert(size() > 0);
    return (*this)[size() - 1];
  }

  void push_back(const T& value) {
    if (size() == capacity() || copied()) {
      socow_vector new_vector(*this, size(), capacity() * 2);
      new_vector.push_back(value);
      *this = new_vector;
      return;
    }

    new (data() + size()) T(value);
    ++_size;
  }

  void pop_back() {
    assert(size() > 0);
    if (copied()) {
      socow_vector new_vector(*this, size() - 1, capacity());
      swap(new_vector);
      return;
    }

    data()[size() - 1].~T();
    --_size;
  }

  bool empty() const noexcept {
    return size() == 0;
  }

  size_t capacity() const noexcept {
    return is_small() ? SMALL_SIZE : _dynamic_data->capacity();
  }

private:
  void shrink_big_to_small() {
    dynamic_storage* _data_ptr = _dynamic_data;
    _dynamic_data = nullptr;
    try {
      std::uninitialized_copy_n(_data_ptr->_data, size(), _static_data);
    } catch (...) {
      _dynamic_data = _data_ptr;
      throw;
    }
    dec_references(_data_ptr, size());
    _is_small = true;
  }

public:
  void reserve(size_t new_capacity) {
    if (size() > new_capacity) {
      return;
    }
    if (is_small() && new_capacity <= SMALL_SIZE) {
      return;
    }
    if (!is_small() && _dynamic_data->references() > 1 && new_capacity <= SMALL_SIZE) {
      shrink_big_to_small();
      return;
    }
    if (is_small() || _dynamic_data->references() > 1 ||
        (_dynamic_data->references() == 1 && new_capacity > capacity())) {
      copy_on_write(new_capacity);
    }
  }

  void shrink_to_fit() {
    if (size() == capacity() || is_small()) {
      return;
    }
    if (size() <= SMALL_SIZE) {
      shrink_big_to_small();
      return;
    }
    copy_on_write(size());
  }

  void clear() {
    if (is_small() || _dynamic_data->references() == 1) {
      size_t counter = size();
      for (size_t i = 0; i < counter; ++i) {
        pop_back();
      }
      return;
    }
    dec_references();
    _is_small = true;
    _size = 0;
  }

  void swap(socow_vector& other) {
    if (this == &other) {
      return;
    }
    if ((!is_small() && other.is_small()) || (is_small() && other.is_small() && other.size() < size())) {
      other.swap(*this);
      return;
    }

    if (is_small() && other.is_small()) {
      size_t common_len = size();
      size_t diff = other.size() - size();
      std::uninitialized_copy_n(other.data() + common_len, diff, data() + common_len);
      try {
        std::swap_ranges(begin(), begin() + common_len, other.begin());
      } catch (std::exception& e) {
        std::destroy_n(data() + common_len, diff);
        throw;
      }
      std::destroy_n(other.data() + common_len, diff);
    } else if (!is_small() && !other.is_small()) {
      std::swap(_dynamic_data, other._dynamic_data);
    } else {
      dynamic_storage* tmp = other._dynamic_data;
      other._dynamic_data = nullptr;
      try {
        std::uninitialized_copy_n(_static_data, size(), other._static_data);
      } catch (...) {
        other._dynamic_data = tmp;
        throw;
      }
      std::destroy_n(data(), size());
      _dynamic_data = tmp;
    }
    std::swap(_size, other._size);
    std::swap(_is_small, other._is_small);
  }

  iterator begin() noexcept {
    return data();
  }

  iterator end() noexcept {
    return data() + size();
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator end() const noexcept {
    return data() + size();
  }

  iterator insert(const_iterator pos, const T& value) {
    ptrdiff_t diff = pos - std::as_const(*this).data();
    if (size() == capacity() || copied()) {
      socow_vector new_vector;
      new_vector.reserve(copied() ? capacity() + 1 : 2 * capacity());
      for (size_t i = 0; i < diff; ++i) {
        new_vector.push_back(std::as_const(*this)[i]);
      }
      new_vector.push_back(value);
      for (size_t i = diff; i < size(); ++i) {
        new_vector.push_back(std::as_const(*this)[i]);
      }
      *this = new_vector;
    } else {
      push_back(value);
      for (size_t i = size() - 1; i > diff; --i) {
        std::swap((*this)[i], (*this)[i - 1]);
      }
    }
    return data() + diff;
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    ptrdiff_t range = last - first;
    ptrdiff_t start = first - std::as_const(*this).data();
    if (first == last) {
      return data() + start;
    }
    if (copied()) {
      socow_vector new_vector;
      new_vector.reserve(capacity() - range);
      for (size_t i = 0; i < start; ++i) {
        new_vector.push_back(std::as_const(*this)[i]);
      }
      for (size_t i = start + range; i < size(); ++i) {
        new_vector.push_back(std::as_const(*this)[i]);
      }
      swap(new_vector);
      return data() + start;
    }

    for (size_t i = start; i < size() - range; i++) {
      std::swap((*this)[i], (*this)[i + range]);
    }
    for (size_t i = 0; i < range; ++i) {
      pop_back();
    }
    return data() + start;
  }
};
