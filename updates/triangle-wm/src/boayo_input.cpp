#include "ipc.hpp"
#include "key_transport.hpp"
#include <linux/input.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <glob.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>
#include <csignal>
#include <iostream>
#include <chrono>
#include <algorithm>
using namespace boayo;
static volatile sig_atomic_t stopped=0;
static void stop_input(int){stopped=1;}
static uint64_t micros(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static std::string linux_key(unsigned key){switch(key){case KEY_HOME:return "Home";case KEY_LEFT:return "ArrowLeft";case KEY_RIGHT:return "ArrowRight";case KEY_UP:return "ArrowUp";case KEY_DOWN:return "ArrowDown";case KEY_ENTER:case KEY_KPENTER:return "Enter";default:return {};}}
struct Key {std::string name,source;uint32_t sequence=0;uint64_t remote=0;bool activate=true;};
struct Delivery {
 std::mutex mutex;std::condition_variable wake;std::deque<Key> queue;bool done=false;std::atomic<int> active{-1};uint64_t acknowledged=0;std::thread worker;
 Delivery():worker([this]{run();}){}
 bool push(Key k){std::lock_guard<std::mutex> lock(mutex);if(queue.size()>=512)return false;queue.push_back(std::move(k));wake.notify_one();return true;}
 uint64_t ack(){std::lock_guard<std::mutex> lock(mutex);return acknowledged;}
 void run(){
  std::unique_ptr<Client> client;
  while(true){Key key;{std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return done||!queue.empty();});if(done)break;key=queue.front();}
   try{
    if(key.activate){
     if(!client){client=std::make_unique<Client>("boayo-input",socket_path()+".control");active=client->sock.fd;timeval timeout{3,0};setsockopt(active,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(active,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));}
     auto result=client->call("input_nav",{{"key",key.name},{"source",key.source},{"sequence",key.sequence}});
     if(!result.value("accepted",false))std::cerr<<"KEY_REJECTED "<<key.name<<" "<<result.value("error",std::string())<<'\n';
    }
    {std::lock_guard<std::mutex> lock(mutex);if(key.remote)acknowledged=key.remote;queue.pop_front();}
   }catch(const std::exception&e){active=-1;client.reset();std::cerr<<"WM_RETRY "<<e.what()<<'\n';std::unique_lock<std::mutex> lock(mutex);if(wake.wait_for(lock,std::chrono::milliseconds(250),[&]{return done;}))break;}
  }active=-1;
 }
 ~Delivery(){ {std::lock_guard<std::mutex> lock(mutex);done=true;}wake.notify_all();int fd=active;if(fd>=0)shutdown(fd,SHUT_RDWR);if(worker.joinable())worker.join();}
};
struct Source {
 std::string path;bool event=false,standard=false,dropped=false,saved=false;int fd=-1,old_flags=0;termios original{};key_transport::SerialParser parser;
 Source(std::string p,bool e=false,bool s=false):path(std::move(p)),event(e),standard(s){}
 Source(const Source&)=delete;Source&operator=(const Source&)=delete;
 Source(Source&&s)noexcept:path(std::move(s.path)),event(s.event),standard(s.standard),dropped(s.dropped),saved(s.saved),fd(s.fd),old_flags(s.old_flags),original(s.original),parser(std::move(s.parser)){s.fd=-1;s.saved=false;}
 ~Source(){close_source();}
 void close_source(){if(fd>=0){if(saved)tcsetattr(fd,TCSANOW,&original);if(standard)fcntl(fd,F_SETFL,old_flags);else close(fd);}fd=-1;saved=false;parser={};dropped=false;}
 bool open_source(){
  fd=standard?STDIN_FILENO:open(path.c_str(),O_RDONLY|O_NONBLOCK|O_NOCTTY|O_CLOEXEC);if(fd<0)return false;
  if(standard)old_flags=fcntl(fd,F_GETFL);
  if(!event){
   if(isatty(fd)){if(tcgetattr(fd,&original)){close_source();return false;}saved=true;termios raw=original;cfmakeraw(&raw);raw.c_cflag|=CLOCAL|CREAD;
    if(!standard){cfsetispeed(&raw,B115200);cfsetospeed(&raw,B115200);raw.c_cflag&=~(PARENB|CSTOPB|CSIZE|CRTSCTS);raw.c_cflag|=CS8;}
    if(tcsetattr(fd,TCSANOW,&raw)){close_source();return false;}}
   if(standard)fcntl(fd,F_SETFL,old_flags|O_NONBLOCK);
  }
  std::cerr<<"INPUT_OPEN "<<path<<'\n';return true;
 }
};
int main(int argc,char**argv){try{
 signal(SIGPIPE,SIG_IGN);signal(SIGINT,stop_input);signal(SIGTERM,stop_input);signal(SIGHUP,stop_input);
 std::vector<Source> sources;std::string spi_path,one_key;bool automatic=false;unsigned speed=1000000;
 for(int i=1;i<argc;i++){std::string a=argv[i];
  if((a=="--tty"||a=="--event")&&i+1<argc)sources.push_back(Source{argv[++i],a=="--event"});
  else if(a=="--stdin")sources.push_back(Source{"stdin",false,true});
  else if(a=="--spi"&&i+1<argc)spi_path=argv[++i];else if(a=="--auto")automatic=true;
  else if(a=="--key"&&i+1<argc)one_key=argv[++i];else throw std::runtime_error("usage: boayo-input [--stdin] [--tty DEVICE] [--event DEVICE] [--auto] [--spi /dev/spidev0.0] [--key Home|ArrowLeft|ArrowRight|ArrowUp|ArrowDown|Enter]");
 }
 if(!one_key.empty()){bool valid=false;for(unsigned k=1;k<=6;k++)valid|=one_key==key_transport::name(k);require(valid,"unsupported key");Client client("boayo-input",socket_path()+".control");auto result=client.call("input_nav",{{"key",one_key}});std::cout<<result.dump()<<std::endl;return result.value("accepted",false)?0:1;}
 if(argc==1)sources.push_back(Source{"stdin",false,true});
 std::set<std::string> paths;for(auto&s:sources)require(paths.insert(s.path).second,"duplicate input source");
 Delivery delivery;uint32_t sequence=0;const std::string identity="tty:"+std::to_string(getpid())+":"+std::to_string(micros());
 int spi=-1;uint64_t scan=0,inflight=0;auto emit=[&](const std::string&key){if(!++sequence)++sequence;if(!delivery.push(Key{key,identity,sequence}))std::cerr<<"INPUT_OVERFLOW\n";};
 while(!stopped){
  uint64_t now=micros();if(now>=scan){scan=now+2000000;
   if(automatic){glob_t entries{};if(glob("/dev/input/event*",0,nullptr,&entries)==0)for(size_t i=0;i<entries.gl_pathc;i++){std::string path=entries.gl_pathv[i];if(paths.insert(path).second)sources.push_back(Source{path,true});}globfree(&entries);}
   for(auto&s:sources)if(s.fd<0)s.open_source();
   if(spi<0&&!spi_path.empty()){spi=open(spi_path.c_str(),O_RDWR|O_CLOEXEC);if(spi>=0){uint8_t mode=SPI_MODE_1,bits=8;if(ioctl(spi,SPI_IOC_WR_MODE,&mode)||ioctl(spi,SPI_IOC_WR_BITS_PER_WORD,&bits)||ioctl(spi,SPI_IOC_WR_MAX_SPEED_HZ,&speed)){close(spi);spi=-1;}else std::cerr<<"INPUT_OPEN "<<spi_path<<'\n';}}
  }
  std::vector<pollfd> fds;for(auto&s:sources)fds.push_back({s.fd,POLLIN,0});int rc=poll(fds.data(),fds.size(),20);if(rc<0&&errno!=EINTR)throw std::runtime_error("input poll");
  for(size_t i=0;i<sources.size()&&!stopped;i++){
   auto&s=sources[i];if(s.fd<0)continue;
   if(fds[i].revents&POLLIN){
    if(s.event){input_event events[32];ssize_t n=read(s.fd,events,sizeof(events));if(n>0&&size_t(n)%sizeof(input_event)==0)for(size_t j=0;j<size_t(n)/sizeof(input_event);j++){auto&e=events[j];if(e.type==EV_SYN&&e.code==SYN_DROPPED){s.dropped=true;continue;}if(s.dropped){if(e.type==EV_SYN&&e.code==SYN_REPORT)s.dropped=false;continue;}auto key=linux_key(e.code);if(e.type==EV_KEY&&(e.value==1||(e.value==2&&key!="Home"&&key!="Enter"))&&!key.empty())emit(key);}else if(n==0)s.close_source();
    }else {uint8_t bytes[256];ssize_t n=read(s.fd,bytes,sizeof(bytes));if(n>0){if(s.standard&&std::find(bytes,bytes+n,3)!=bytes+n){stopped=1;break;}for(auto&key:s.parser.feed(bytes,size_t(n),micros()))emit(key);}else if(n==0){if(s.standard)stopped=1;s.close_source();}}
   }
   if(fds[i].revents&(POLLERR|POLLHUP|POLLNVAL)){if(s.standard)stopped=1;s.close_source();}
  }
  if(spi>=0&&!stopped){
   uint64_t ack=delivery.ack();auto tx=key_transport::acknowledge(uint32_t(ack>>32),uint32_t(ack));key_transport::Frame rx{};
   spi_ioc_transfer x{};x.tx_buf=reinterpret_cast<uintptr_t>(tx.data());x.rx_buf=reinterpret_cast<uintptr_t>(rx.data());x.len=rx.size();x.speed_hz=speed;x.bits_per_word=8;
   if(ioctl(spi,SPI_IOC_MESSAGE(1),&x)<0){close(spi);spi=-1;continue;}
   key_transport::Event event;if(!key_transport::decode(rx,event)||!event.code)continue;
   uint64_t token=(uint64_t(event.session)<<32)|event.sequence;if(token==ack||token==inflight)continue;
   if(inflight!=ack&&inflight&&uint32_t(inflight>>32)==event.session)continue;
   bool activate=event.value==1||(event.value==2&&event.code!=1&&event.code!=6);
   if(delivery.push(Key{key_transport::name(event.code),"esp32:"+std::to_string(event.session),event.sequence,token,activate}))inflight=token;
  }
 }
 for(auto&s:sources)s.close_source();if(spi>=0)close(spi);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
