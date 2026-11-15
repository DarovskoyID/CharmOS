#include "components/crash/core/app/crashpad.h"

#include "base/logging.h"

namespace crash_reporter {

bool InitializeCrashpad(bool initial_client, const std::string& process_type) {
  return false;
}

crashpad::CrashpadClient& GetCrashpadClient() {
  LOG(FATAL) << "CrashpadClient is unavailable on CharmOS";
  __builtin_unreachable();
}

void DestroyCrashpadClient() {}

void SetUploadConsent(bool consent) {}

void GetReports(std::vector<Report>* reports) {
  reports->clear();
}

void RequestSingleCrashUpload(const std::string& local_id) {}

void DumpWithoutCrashing() {}

std::optional<base::FilePath> GetCrashpadDatabasePath() {
  return std::nullopt;
}

void ClearReportsBetween(const base::Time& begin, const base::Time& end) {}

void GetReportsImpl(std::vector<Report>* reports) {
  reports->clear();
}

void RequestSingleCrashUploadImpl(const std::string& local_id) {}

base::FilePath::StringType::const_pointer GetCrashpadDatabasePathImpl() {
  return nullptr;
}

void ClearReportsBetweenImpl(time_t begin, time_t end) {}

namespace internal {

bool PlatformCrashpadInitialization(
    bool initial_client,
    bool browser_process,
    bool embedded_handler,
    const std::string& user_data_dir,
    const base::FilePath& exe_path,
    const std::vector<std::string>& initial_arguments,
    const std::vector<base::FilePath>& attachments,
    base::FilePath* database_path) {
  return false;
}

crashpad::CrashReportDatabase* GetCrashReportDatabase() {
  return nullptr;
}

void SetCrashReportDatabaseForTesting(crashpad::CrashReportDatabase* database,
                                      base::FilePath* database_path) {}

}  // namespace internal
}  // namespace crash_reporter
