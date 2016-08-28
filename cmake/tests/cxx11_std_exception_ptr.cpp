////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2016 Agustin Berge
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include <exception>

int main()
{
    std::exception_ptr e;

    try {
        throw 42;
    } catch (...) {
        e = std::current_exception();
    }

    try {
        std::rethrow_exception(e);
    } catch (...) {}
}
