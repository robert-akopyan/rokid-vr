#include <Windows.h>
#include <iostream>

using Factory=void*(*)(const char*,int*);
int wmain(int argc,wchar_t**argv){if(argc!=2)return 2;HMODULE dll=LoadLibraryW(argv[1]);if(!dll){std::cerr<<"LoadLibrary failed: "<<GetLastError()<<'\n';return 1;}auto factory=reinterpret_cast<Factory>(GetProcAddress(dll,"HmdDriverFactory"));if(!factory){std::cerr<<"HmdDriverFactory missing\n";FreeLibrary(dll);return 1;}int code=0;void* provider=factory("IServerTrackedDeviceProvider_004",&code);void* invalid=factory("invalid_interface",&code);const bool ok=provider!=nullptr&&invalid==nullptr;FreeLibrary(dll);std::cout<<(ok?"driver factory smoke passed":"driver factory smoke failed")<<'\n';return ok?0:1;}
