#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include <stdio.h>
#include <string>
#include <string.h>

int TestNativeMessagePump();
int TestNativeSharedMemoryAndProcesses();
int TestNativeSharedMemoryChild();
int TestNativeFilesAndAccounting();
int TestNativeIpc();
int TestNativeIpcChild();

extern "C" int main(int argc,char** argv) {
  if(argc==2 && !strcmp(argv[1],"native-child")) return 37;
  if(!base::CommandLine::Init(argc,argv)) return 1;
  if(base::CommandLine::ForCurrentProcess()->HasSwitch("native-memory"))
    return TestNativeSharedMemoryChild();
  if(base::CommandLine::ForCurrentProcess()->HasSwitch("native-channel"))
    return TestNativeIpcChild();
  auto json = base::JSONReader::ReadDict(
      "{\"title\":\"Привет / \\uD83D\\uDE00\",\"version\":1,\"version\":2}",
      base::JSON_PARSE_RFC);
  if (!json || !json->FindString("title") ||
      *json->FindString("title") != "Привет / 😀" ||
      json->FindInt("version") != 2) return 7;
  if (base::JSONReader::Read("[1,]", base::JSON_PARSE_RFC)) return 8;
  if (!base::JSONReader::Read("[1,]", base::JSON_ALLOW_TRAILING_COMMAS)) return 9;
  auto error = base::JSONReader::ReadAndReturnValueWithError(
      "{\n\"a\":?}", base::JSON_PARSE_RFC);
  if (error.has_value() || error.error().line != 2 ||
      error.error().message.empty()) return 10;
  if(base::StringPrintf("%s / %.2f","CharmOS",1.25)!="CharmOS / 1.25") return 2;
  base::FilePath path(FILE_PATH_LITERAL("/chromium-base.txt"));
  if(!base::WriteFile(path,"Привет / Chromium")) return 3;
  std::string contents;
  if(!base::ReadFileToString(path,&contents) || contents!="Привет / Chromium") return 4;
  if(!base::DeleteFile(path)) return 5;
  base::SingleThreadTaskExecutor executor;
  base::RunLoop loop;
  auto before=base::TimeTicks::Now();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,loop.QuitClosure(),base::Milliseconds(2));
  loop.Run();
  if(base::TimeTicks::Now()<=before) return 6;
  if (int result=TestNativeMessagePump()) return result;
  if (int result=TestNativeSharedMemoryAndProcesses()) return result;
  if (int result=TestNativeFilesAndAccounting()) return result;
  if (int result=TestNativeIpc()) return result;
  printf("base runtime: native strings, files, monotonic clock and task loop ok\n");
  return 0;
}
