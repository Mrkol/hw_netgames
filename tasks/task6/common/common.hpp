#pragma once


template<class T>
using UniquePtr = std::unique_ptr<T, void(*)(T*)>;

#define UNUSED(x) (void) (x);

template <class F>
struct Defer
{
  F f;
  ~Defer() { f(); }
};

template<class F>
Defer(F) -> Defer<F>;
