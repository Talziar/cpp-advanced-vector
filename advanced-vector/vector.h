#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity)), capacity_(capacity) {
    }

    RawMemory(const RawMemory &) = delete;

    RawMemory(RawMemory &&other) noexcept : buffer_(std::exchange(other.buffer_, nullptr)), capacity_(std::exchange(other.capacity_, 0)) {}

    RawMemory &operator=(const RawMemory &rhs) = delete;

    RawMemory &operator=(RawMemory &&other) noexcept {
        if (this != &other) {
            Deallocate(buffer_);
            buffer_ = std::exchange(other.buffer_, nullptr);
            capacity_ = std::exchange(other.capacity_, 0);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T *operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T *operator+(size_t offset) const noexcept {
        return const_cast<RawMemory &>(*this) + offset;
    }

    const T &operator[](size_t index) const noexcept {
        return const_cast<RawMemory &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory &other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T *GetAddress() const noexcept {
        return buffer_;
    }

    T *GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n) {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T *buf) noexcept {
        operator delete(buf);
    }

    T *buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T *;
    using const_iterator = const T *;

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector &other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector &&other) noexcept : data_(std::exchange(other.data_, RawMemory<T>())), size_(std::exchange(other.size_, 0ull)) {}

    Vector &operator=(const Vector &rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if (rhs.size_ < size_) {
                    std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                } else {
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector &operator=(Vector &&rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    const T &operator[](size_t index) const noexcept {
        return const_cast<Vector &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Swap(Vector &other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        } else if (new_size > size_) {
            Reserve(new_size);

            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    void PushBack(const T &value) {
        EmplaceBack(value);
    }

    void PushBack(T &&value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T &EmplaceBack(Args &&...args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    iterator Insert(const_iterator pos, const T &value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T &&value) {
        return Emplace(pos, std::move(value));
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args &&...args) {
        size_t index = pos - cbegin();
        assert(index <= size_);

        if (size_ < data_.Capacity()) {
            if (pos == cend()) {
                std::construct_at(end(), std::forward<Args>(args)...);
                size_++;
                return end() - 1;
            }

            T tmp_copy(std::forward<Args>(args)...);

            std::construct_at(end(), std::move(*(end() - 1)));
            std::move_backward(begin() + index, end() - 1, end());

            data_[index] = std::move(tmp_copy);

            ++size_;
            return begin() + index;
        } else {
            RawMemory<T> new_data(data_.Capacity() == 0 ? 1 : data_.Capacity() * 2);

            std::construct_at(new_data.GetAddress() + index, std::forward<Args>(args)...);

            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(begin(), index, new_data.GetAddress());
                std::uninitialized_move_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
            } else {
                std::uninitialized_copy_n(begin(), index, new_data.GetAddress());
                std::uninitialized_copy_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
            }

            std::destroy_n(begin(), size_);
            data_.Swap(new_data);

            ++size_;
            return begin() + index;
        }
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t index = pos - cbegin();
        assert(index < size_);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(begin() + index + 1, end(), begin() + index);
        } else {
            std::copy(begin() + index + 1, end(), begin() + index);
        }

        std::destroy_at(end() - 1);

        --size_;
        return begin() + index;
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
