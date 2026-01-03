# Project Build Environment

## Compiler Version
- *g++.exe (Rev8, Built by MSYS2 project) 15.2.0*

## OpenSSL Version
- *OpenSSL 3.6.0 1 Oct 2025 (Library: OpenSSL 3.6.0 1 Oct 2025)*

## Compile command
```
> g++ -std=c++17 -I"C:/msys64/mingw64/include" -L"C:/msys64/mingw64/lib" -o gen_queries gen_queries.cpp -lcrypto -lssl
 ```

## Execute command
```
> ./gen_queries 1024 20 
```