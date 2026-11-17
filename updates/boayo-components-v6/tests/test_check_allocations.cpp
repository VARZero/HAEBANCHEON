#include "../software/boayo/native/src/common.hpp"
#include <atomic>
#include <cstdlib>
#include <cstdio>
static std::atomic<unsigned> allocations{0};
void *operator new(std::size_t n){++allocations;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
int main(){
  auto before=allocations.load();
  for(unsigned i=0;i<4220;++i)boayo::require(true,"incomplete scene");
  if(allocations.load()!=before)return 1;
  try{boayo::require(false,"incomplete scene");return 2;}
  catch(const std::runtime_error &e){if(std::string(e.what())!="incomplete scene")return 3;}
  std::puts("PASS_CHECK_ALLOCATIONS: 4220 successful checks allocate zero; failure still throws exact message");
}
