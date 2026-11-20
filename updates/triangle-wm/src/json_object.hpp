#pragma once
#include "common.hpp"
#include <utility>
inline void add_fields(boayo::J &) {}
template<class Value,class... Rest>
inline void add_fields(boayo::J &result,const char *key,Value &&value,Rest&&... rest){
 result.emplace(key,std::forward<Value>(value));
 add_fields(result,std::forward<Rest>(rest)...);
}
// Construct object members directly, without temporary JSON [key,value] arrays.
template<class... Fields> inline boayo::J json_object(Fields&&... fields){
 auto result=boayo::J::object();add_fields(result,std::forward<Fields>(fields)...);return result;
}
