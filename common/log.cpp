#include "common/log.hpp"
#include "common/config.hpp"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
namespace rokidvr {
std::string narrow(const std::wstring& s){if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
void log(const std::string& text){static std::mutex m;std::lock_guard lock(m);std::ofstream f(data_directory()+L"\\logs\\rokidvr.log",std::ios::app);SYSTEMTIME t{};GetLocalTime(&t);f<<std::setfill('0')<<std::setw(4)<<t.wYear<<'-'<<std::setw(2)<<t.wMonth<<'-'<<std::setw(2)<<t.wDay<<' '<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<'.'<<std::setw(3)<<t.wMilliseconds<<" ["<<GetCurrentProcessId()<<"] "<<text<<'\n';OutputDebugStringA(("RokidVR: "+text+"\n").c_str());}
}

