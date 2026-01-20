#include "Result.h"

template<class T>
bool Result<T>::ok() const noexcept
{
    return value.has_value();
}
