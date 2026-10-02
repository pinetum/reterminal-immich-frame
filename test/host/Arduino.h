#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>

using std::min; using std::max;

#define PROGMEM
#define RTC_DATA_ATTR
#define SERIAL_8N1 0
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(int v) { char b[32]; snprintf(b,32,"%d",v); s=b; }
  String(unsigned v) { char b[32]; snprintf(b,32,"%u",v); s=b; }
  String(long v) { char b[32]; snprintf(b,32,"%ld",v); s=b; }
  String(unsigned long v) { char b[32]; snprintf(b,32,"%lu",v); s=b; }
  String(float v, int d=2) { char b[32]; snprintf(b,32,"%.*f",d,v); s=b; }
  String(double v, int d=2) { char b[32]; snprintf(b,32,"%.*f",d,v); s=b; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  bool isEmpty() const { return s.empty(); }
  bool endsWith(const String& x) const { return s.size()>=x.s.size() && s.compare(s.size()-x.s.size(),x.s.size(),x.s)==0; }
  bool startsWith(const String& x) const { return s.rfind(x.s,0)==0; }
  void trim() {}
  void remove(size_t i) { if(i<s.size()) s.erase(i); }
  void remove(size_t i,size_t n) { if(i<s.size()) s.erase(i,n); }
  void replace(const String&a,const String&b){ size_t p=0; while((p=s.find(a.s,p))!=std::string::npos){s.replace(p,a.s.size(),b.s);p+=b.s.size();} }
  void concat(const char* c,size_t n){ s.append(c,n); }
  int indexOf(char c,int from=0) const { auto p=s.find(c,from); return p==std::string::npos?-1:(int)p; }
  String substring(int a) const { return String(s.substr(a)); }
  String substring(int a,int b) const { return String(s.substr(a,b-a)); }
  long toInt() const { return atol(s.c_str()); }
  char operator[](size_t i) const { return s[i]; }
  String operator+(const String& o) const { return String(s+o.s); }
  String& operator+=(const String& o){ s+=o.s; return *this; }
  String& operator+=(char c){ s+=c; return *this; }
  bool operator==(const String& o) const { return s==o.s; }
  bool operator!=(const String& o) const { return s!=o.s; }
};
inline String operator+(const char* a, const String& b){ return String(a)+b; }

struct SerialT {
  void begin(unsigned long,int=0,int=-1,int=-1){}
  template<class...A> void printf(const char*,A...){}
  void println(const char*){} void println(){} void print(const char*){} void print(char){}
  void flush(){}
};
extern SerialT Serial1;

inline unsigned long millis(){ return 0; }
inline void delay(unsigned long){}
inline void* ps_malloc(size_t n){ return malloc(n); }
inline void* ps_realloc(void*p,size_t n){ return realloc(p,n); }
inline void pinMode(int,int){}
inline int digitalRead(int){ return 1; }
inline void digitalWrite(int,int){}
inline uint32_t esp_random(){ return 1; }

struct ESPClass {
  size_t getFreePsram(){ return 8u*1024*1024; }
  size_t getPsramSize(){ return 8u*1024*1024; }
  size_t getFreeHeap(){ return 200*1024; }
};
extern ESPClass ESP;
