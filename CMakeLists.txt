cmake_minimum_required(VERSION 3.5)

project(AmzBooks)

add_subdirectory(AmzBooksLib)
add_subdirectory(AmzBooks)
add_subdirectory(AmzBooksAutoTests)


add_dependencies(AmzBooks
AmzBooksLib
)
add_dependencies(TestOrders
AmzBooksLib
)

