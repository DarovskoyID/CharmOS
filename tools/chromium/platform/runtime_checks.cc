#include "build/build_config.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <locale>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <errno.h>
#include <fenv.h>
#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <charm/sys.hpp>
#include <fcntl.h>
#include <sys/stat.h>

static unsigned failures;
#define CHECK(c) do { if(!(c)) { printf("cxx runtime: FAILED line %d\n",__LINE__); ++failures; } } while(0)
#define REQUIRE(c) do { if(!(c)) { printf("cxx runtime: FAILED required line %d\n",__LINE__); return 1; } } while(0)
#define REPORT(message) do { printf("cxx runtime: " message " %s\n", failures==group_start ? "ok" : "FAILED"); group_start=failures; } while(0)
static unsigned handler_calls;
static void out_of_memory() { ++handler_calls; std::set_new_handler(nullptr); }
extern "C" int main() {
  unsigned group_start=0;
  static_assert(BUILDFLAG(IS_CHARMOS) && !BUILDFLAG(IS_LINUX));
  static_assert(!_LIBCPP_HAS_FILESYSTEM && _LIBCPP_HAS_LOCALIZATION && _LIBCPP_HAS_WIDE_CHARACTERS);
  static_assert(sizeof(fenv_t)==32 && sizeof(fexcept_t)==2);
  fenv_t original;
  CHECK(fegetenv(&original)==0 && fesetround(FE_TONEAREST)==0);
  char* end=nullptr;
  CHECK(strtod("0x1.8p+1tail",&end)==3.0 && !strcmp(end,"tail"));
  CHECK(strtof("-0",nullptr)==0.0f && std::signbit(strtof("-0",nullptr)));
  CHECK(strtold("1.0000000000000000001084202172485504434",nullptr)>1.0L);
  CHECK(std::isinf(strtod("infinity",nullptr)) && std::isnan(strtod("NaN",nullptr)));
  errno=0; CHECK(std::isinf(strtod("1e9999",nullptr)) && errno==ERANGE);
  errno=0; CHECK(strtod("1e-9999",nullptr)==0 && errno==ERANGE);
  CHECK(fesetround(FE_TONEAREST)==0 && strtod("0x1.00000000000008p0",nullptr)==1.0);
  CHECK(fesetround(FE_UPWARD)==0 && strtod("0x1.00000000000008p0",nullptr)==0x1.0000000000001p0);
  CHECK(fesetround(FE_DOWNWARD)==0 && strtod("0x1.00000000000008p0",nullptr)==1.0);
  CHECK(fesetround(FE_TONEAREST)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
  CHECK(feraiseexcept(FE_DIVBYZERO|FE_INEXACT)==0);
  CHECK((fetestexcept(FE_ALL_EXCEPT)&(FE_DIVBYZERO|FE_INEXACT))==(FE_DIVBYZERO|FE_INEXACT));
  CHECK(feclearexcept(FE_DIVBYZERO)==0 && !(fetestexcept(FE_ALL_EXCEPT)&FE_DIVBYZERO));
  fexcept_t flags;
  CHECK(fegetexceptflag(&flags,FE_ALL_EXCEPT)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
  CHECK(fesetexceptflag(&flags,FE_INEXACT)==0 && (fetestexcept(FE_ALL_EXCEPT)&FE_INEXACT));
  CHECK(fesetenv(&original)==0);
  volatile double input=0.5;
  CHECK(std::fabs(std::sin(input)-0.479425538604203)<1e-15);
  CHECK(std::fabs(std::cos(input)-0.8775825618903728)<1e-15);
  CHECK(std::fabs(std::log(std::exp(input))-input)<1e-14);
  CHECK(std::sqrt(81.0)==9.0 && std::pow(3.0,4.0)==81.0);
  errno=0; volatile double negative=-1;
  double (*volatile root)(double)=&::sqrt;
  CHECK(std::isnan(root(negative)) && errno==EDOM);
  REPORT("parsing, range, rounding, fenv and numeric functions");

  std::string text="Привет";
  for(unsigned i=0;i<100;++i) text+=" / Chromium";
  CHECK(text.find("Chromium")!=std::string::npos && std::stoi("123")==123 && std::stod("2.5")==2.5);
  CHECK(std::to_wstring(123)==L"123");
  auto wide=std::to_wstring(1.25);
  CHECK(wcstod(wide.c_str(),nullptr)==1.25);
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output<<std::fixed<<std::setprecision(2)<<1234.125;
  CHECK(output.str()=="1234.12");
  std::istringstream incoming("17 2.5");
  int integer=0; double real=0; incoming>>integer>>real;
  CHECK(integer==17 && real==2.5 && !incoming.fail());
  char bytes[80];
  CHECK(snprintf(bytes,sizeof bytes,"%.17g / %La",1.25,1.5L)>0 && !strncmp(bytes,"1.25 / ",7));
  CHECK(strtold(bytes+7,nullptr)==1.5L);
  long double scanned=0;
  CHECK(sscanf("0x1.8p+1","%Lf",&scanned)==1 && scanned==3.0L);
  char* allocated=nullptr;
  CHECK(asprintf(&allocated,"%ls / %.2f",L"Привет",1.25)>0 && !strcmp(allocated,"Привет / 1.25"));
  free(allocated);
  CHECK(std::set_new_handler(&out_of_memory)==nullptr);
  volatile size_t impossible=static_cast<size_t>(-1);
  CHECK(::operator new(impossible,std::nothrow)==nullptr && handler_calls==1);
  CHECK(std::get_new_handler()==nullptr);
  REPORT("strings, locale, streams, formatting and new-handler");

  CHECK(uselocale(nullptr)==LC_GLOBAL_LOCALE);
  locale_t selected=newlocale(LC_ALL_MASK,"C.UTF-8",nullptr);
  CHECK(selected && uselocale(selected)==LC_GLOBAL_LOCALE);
  CHECK(!strcmp(nl_langinfo_l(CODESET,selected),"UTF-8"));
  errno=0; CHECK(!newlocale(LC_ALL_MASK,"ru_RU.UTF-8",nullptr) && errno==ENOENT);
  CHECK(uselocale(nullptr)==selected);
  CHECK(fesetround(FE_DOWNWARD)==0);
  std::atomic<unsigned> status{0};
  std::thread worker([&] {
    if(uselocale(nullptr)!=LC_GLOBAL_LOCALE || fegetround()!=FE_DOWNWARD) { status=1; return; }
    if(fesetround(FE_UPWARD)) { status=2; return; }
    for(unsigned i=0;i<100;++i) { std::this_thread::yield(); if(fegetround()!=FE_UPWARD) { status=3; return; } }
    status=4;
  });
  bool stable=true;
  for(unsigned i=0;i<100;++i) { std::this_thread::yield(); stable&=fegetround()==FE_DOWNWARD; }
  CHECK(stable);
  worker.join(); CHECK(status==4);
  CHECK(fesetenv(&original)==0 && uselocale(LC_GLOBAL_LOCALE)==selected);
  freelocale(selected);
  std::mutex mutex; std::condition_variable condition; bool ready=false;
  std::thread signal([&] { std::lock_guard<std::mutex> lock(mutex); ready=true; condition.notify_one(); });
  { std::unique_lock<std::mutex> lock(mutex); CHECK(condition.wait_for(lock,std::chrono::seconds(2),[&]{return ready;})); }
  signal.join();
  auto before=std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(std::chrono::steady_clock::now()>before);
  std::random_device entropy;
  unsigned random=entropy(); bool different=false;
  for(unsigned i=0;i<8;++i) different|=entropy()!=random;
  CHECK(different);
  REPORT("C++ threads, fenv inheritance, locale isolation, clocks and entropy");

  FILE* file=fopen("/runtime-wide.txt","w+"); REQUIRE(file); CHECK(fwide(file,0)==0);
  CHECK(fputwc(L'Я',file)==L'Я' && fputwc(L'\U0001f600',file)==L'\U0001f600' && fputwc(L'!',file)==L'!');
  CHECK(fflush(file)==0 && fseeko(file,0,SEEK_SET)==0);
  CHECK(fgetwc(file)==L'Я'); fpos_t position; REQUIRE(fgetpos(file,&position)==0);
  CHECK(fgetwc(file)==L'\U0001f600' && ungetwc(L'\U0001f600',file)==L'\U0001f600');
  CHECK(fgetwc(file)==L'\U0001f600' && fgetwc(file)==L'!' && fgetwc(file)==WEOF && feof(file));
  CHECK(fsetpos(file,&position)==0 && !feof(file) && fgetwc(file)==L'\U0001f600');
  CHECK(fclose(file)==0);
  file=fopen("/runtime-wide.txt","r"); REQUIRE(file);
  char raw[16]{}; CHECK(fread(raw,1,sizeof raw,file)==7 && !strcmp(raw,"Я😀!"));
  CHECK(fclose(file)==0 && remove("/runtime-wide.txt")==0);
  time_t epoch=1609459200; tm date; REQUIRE(gmtime_r(&epoch,&date));
  CHECK(strftime(bytes,sizeof bytes,"%F %T %G-W%V-%u %Z",&date)>0 && !strcmp(bytes,"2021-01-01 00:00:00 2020-W53-5 UTC"));
  REPORT("wide file I/O, positions, UTF-8 bytes and UTC formatting");
  for (time_t sample : {time_t{-62135596800LL}, time_t{-1}, time_t{0},
                        time_t{951782400}, time_t{253402300799LL}}) {
    tm civil; REQUIRE(localtime_r(&sample, &civil));
    CHECK(civil.tm_isdst == 0 && mktime(&civil) == sample);
  }
  tm rollover{}; rollover.tm_year=120; rollover.tm_mon=12; rollover.tm_mday=0;
  CHECK(mktime(&rollover)==1609372800 && rollover.tm_year==120 && rollover.tm_mon==11 && rollover.tm_mday==31);
  CHECK(difftime(INT64_MAX,INT64_MAX-1)==1.0 && difftime(INT64_MIN,INT64_MIN+1)==-1.0);
  CHECK(timezone==0 && daylight==0 && !strcmp(tzname[0],"UTC"));
  sigset_t all, signal_original, current, worker_mask=0;
  CHECK(sigfillset(&all)==0 && pthread_sigmask(SIG_BLOCK,nullptr,&signal_original)==0);
  errno=EDOM;
  CHECK(pthread_sigmask(999,&all,nullptr)==EINVAL && errno==EDOM);
  std::thread mask_worker([&] {
    sigset_t previous;
    if(pthread_sigmask(SIG_BLOCK,&all,&previous)!=0) return;
    if(pthread_sigmask(SIG_BLOCK,nullptr,&worker_mask)!=0) worker_mask=0;
    (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);
  });
  mask_worker.join();
  CHECK(worker_mask==all && pthread_sigmask(SIG_BLOCK,nullptr,&current)==0 && current==signal_original);
  REPORT("UTC normalization, numeric time differences and per-thread signal masks");
  {
    pid_t process_id=getpid(), main_id=gettid();
    CHECK(process_id>0 && main_id>0);
    CHECK(charm::runtime_query(0).value==static_cast<uint64_t>(process_id));
    CHECK(charm::runtime_query(1).value==static_cast<uint64_t>(main_id));
    auto bad_query=charm::runtime_query(UINT64_MAX);
    CHECK(bad_query.status==charm::Status::InvalidArgs && bad_query.value==0);
    CHECK(charm::runtime_query(4).ok() && charm::runtime_query(4).value>0);
    auto cpu_ns=[](clockid_t clock) -> uint64_t {
      timespec sample{};
      if(clock_gettime(clock,&sample)!=0) return UINT64_MAX;
      return static_cast<uint64_t>(sample.tv_sec)*1000000000ull + static_cast<uint64_t>(sample.tv_nsec);
    };
    uint64_t proc_cpu_before=cpu_ns(CLOCK_PROCESS_CPUTIME_ID);
    pid_t ids[4]{};
    uint64_t elapsed[4]{}, work[4]{};
    std::thread busy[4];
    for(unsigned i=0;i<4;++i) busy[i]=std::thread([&,i] {
      ids[i]=getpid()==process_id ? gettid() : 0;
      uint64_t begin=cpu_ns(CLOCK_THREAD_CPUTIME_ID);
      uint64_t until=charm::now_ns()+10000000;
      uint64_t sum=i+1;
      while(charm::now_ns()<until) sum=sum*6364136223846793005ull+1;
      work[i]=sum;
      elapsed[i]=cpu_ns(CLOCK_THREAD_CPUTIME_ID)-begin;
    });
    for(auto& t:busy) t.join();
    uint64_t after=cpu_ns(CLOCK_PROCESS_CPUTIME_ID), total=0;
    for(unsigned i=0;i<4;++i) {
      CHECK(ids[i]>0 && ids[i]!=main_id && elapsed[i]>0 && work[i]!=0);
      total+=elapsed[i];
      for(unsigned j=0;j<i;++j) CHECK(ids[i]!=ids[j]);
    }
    CHECK(proc_cpu_before!=UINT64_MAX && after>=proc_cpu_before && after-proc_cpu_before>=total);
    uint64_t wall_begin=charm::now_ns(), cpu_begin=cpu_ns(CLOCK_THREAD_CPUTIME_ID);
    CHECK(usleep(100000)==0);
    uint64_t cpu_end=cpu_ns(CLOCK_THREAD_CPUTIME_ID), wall_end=charm::now_ns();
    CHECK(cpu_end>=cpu_begin && (cpu_end-cpu_begin)*2<wall_end-wall_begin);
    timespec boot{};
    CHECK(clock_gettime(CLOCK_BOOTTIME,&boot)==0);
    errno=0; CHECK(clock_gettime(CLOCK_MONOTONIC_RAW,&boot)==-1 && errno==EINVAL);
    REPORT("kernel process/thread IDs and CPU accounting excluding sleep");
  }
  {
    int fd_file=open("/runtime-fd.txt",O_CREAT|O_TRUNC|O_RDWR|O_CLOEXEC,0600);
    REQUIRE(fd_file>=0);
    CHECK(fcntl(fd_file,F_GETFD)==FD_CLOEXEC);
    CHECK(write(fd_file,"abcdef",6)==6);
    int fd_peer=dup(fd_file); REQUIRE(fd_peer>=0);
    CHECK(fcntl(fd_peer,F_GETFD)==0 && lseek(fd_peer,0,SEEK_SET)==0);
    char fd_text[8]{};
    CHECK(read(fd_file,fd_text,2)==2 && !memcmp(fd_text,"ab",2));
    CHECK(read(fd_peer,fd_text,2)==2 && !memcmp(fd_text,"cd",2));
    CHECK(pread(fd_peer,fd_text,1,0)==1 && fd_text[0]=='a' && lseek(fd_file,0,SEEK_CUR)==4);
    CHECK(close(fd_file)==0 && read(fd_peer,fd_text,2)==2 && !memcmp(fd_text,"ef",2));
    CHECK(fcntl(fd_peer,F_SETFD,FD_CLOEXEC)==0 && dup2(fd_peer,fd_peer)==fd_peer);
    CHECK(fcntl(fd_peer,F_GETFD)==FD_CLOEXEC);
    int fd_copy=fcntl(fd_peer,F_DUPFD,100); REQUIRE(fd_copy>=100);
    int fd_clo=fcntl(fd_peer,F_DUPFD_CLOEXEC,120); REQUIRE(fd_clo>=120);
    CHECK(fcntl(fd_copy,F_GETFD)==0 && fcntl(fd_clo,F_GETFD)==FD_CLOEXEC);
    CHECK(fcntl(fd_peer,F_SETFL,O_APPEND|O_NONBLOCK|O_RDONLY)==0);
    CHECK(fcntl(fd_copy,F_GETFL)==(O_RDWR|O_APPEND|O_NONBLOCK));
    CHECK(lseek(fd_copy,0,SEEK_SET)==0 && write(fd_clo,"!",1)==1 && lseek(fd_peer,0,SEEK_CUR)==7);
    CHECK(fcntl(fd_peer,F_SETFL,O_RDWR)==0 && close(fd_copy)==0 && close(fd_clo)==0);
    CHECK(lseek(fd_peer,INT64_MAX,SEEK_SET)==INT64_MAX);
    errno=0; CHECK(lseek(fd_peer,1,SEEK_CUR)==-1 && errno==EOVERFLOW);
    errno=0; CHECK(lseek(fd_peer,INT64_MIN,SEEK_CUR)==-1 && errno==EINVAL);
    CHECK(lseek(fd_peer,0,SEEK_END)==7);
    errno=0; CHECK(fcntl(fd_peer,F_DUPFD,-1)==-1 && errno==EINVAL);
    flock fd_lock_request{};
    errno=0; CHECK(fcntl(fd_peer,F_SETLK,&fd_lock_request)==-1 && errno==ENOTSUP);
    int fd_target=open("/runtime-fd.txt",O_RDONLY|O_CLOEXEC); REQUIRE(fd_target>=0);
    CHECK(dup2(fd_peer,fd_target)==fd_target && fcntl(fd_target,F_GETFD)==0);
    CHECK(close(fd_target)==0 && pread(fd_peer,fd_text,7,0)==7 && !memcmp(fd_text,"abcdef!",7));
    CHECK(close(fd_peer)==0 && unlink("/runtime-fd.txt")==0);

    int fd_shared=open("/runtime-shared-fd.txt",O_CREAT|O_TRUNC|O_RDWR,0600); REQUIRE(fd_shared>=0);
    int fd_writers[4];
    for(auto& descriptor:fd_writers) { descriptor=dup(fd_shared); REQUIRE(descriptor>=0); }
    std::atomic<unsigned> fd_ready{0}, fd_errors{0};
    std::atomic<bool> fd_start{false};
    std::thread fd_workers[4];
    for(unsigned i=0;i<4;++i) fd_workers[i]=std::thread([&,i] {
      char record[32]; memset(record,static_cast<int>('A'+i),sizeof record);
      fd_ready.fetch_add(1);
      while(!fd_start.load()) std::this_thread::yield();
      for(unsigned j=0;j<32;++j)
        if(write(fd_writers[i],record,sizeof record)!=sizeof record) fd_errors.fetch_add(1);
    });
    while(fd_ready.load()!=4) std::this_thread::yield();
    fd_start.store(true);
    for(auto& t:fd_workers) t.join();
    char fd_contents[4096];
    CHECK(fd_errors.load()==0 && pread(fd_shared,fd_contents,sizeof fd_contents,0)==sizeof fd_contents);
    size_t fd_counts[4]{};
    for(char byte:fd_contents) {
      if(byte<'A' || byte>'D') { CHECK(false); break; }
      ++fd_counts[static_cast<unsigned>(byte-'A')];
    }
    for(auto count:fd_counts) CHECK(count==1024);
    CHECK(lseek(fd_shared,0,SEEK_CUR)==4096);
    for(auto descriptor:fd_writers) CHECK(close(descriptor)==0);
    CHECK(close(fd_shared)==0 && unlink("/runtime-shared-fd.txt")==0);
    REPORT("shared file descriptions, descriptor flags and concurrent offsets");
  }
  return failures ? 1 : 0;
}
