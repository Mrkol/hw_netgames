#pragma once

#include <array>
#include <cassert>
#include <span>


template<size_t N>
class Buffer
{
public:
    bool can_produce() const { return end < data_.size(); }
    bool can_consume() const { return start < end; }
    
    template<std::invocable<char*, size_t> F>
    void produce(F f)
    {
        end += f(data_.data() + end, data_.size() - end);
    }

    template<std::invocable<char*, size_t> F>
    void consume(F f)
    {
        start += f(data_.data() + end, data_.size() - end);
        if (start == end)
        {
            start = end = 0;
        }
    }

    

private:
    std::array<char, N> data_;
    size_t start{0};
    size_t end{0};
};
