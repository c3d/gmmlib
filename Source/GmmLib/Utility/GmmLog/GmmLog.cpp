/*==============================================================================
Copyright(c) 2017 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files(the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and / or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
============================================================================*/

#include "GmmLog.h"

#if GMM_LOG_AVAILABLE
#include "Internal/Common/GmmLibInc.h"

#if _WIN32
#include <process.h>
#include <memory>
#else
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#if __QNX__
#include <limits.h>
#else
#include <linux/limits.h>
#endif
#endif

/// Logger instance shared by all of GmmLib within a process
GmmLib::Logger& GmmLoggerPerProc = GmmLib::Logger::CreateGmmLogSingleton();

#if _WIN32
namespace spdlog
{
    namespace sinks
    {
        /////////////////////////////////////////////////////////////////////////////////////
        /// class defines a sink which prints the messages to the debugger
        /////////////////////////////////////////////////////////////////////////////////////
        class Debugger: public sink
        {
            void log(const details::log_msg &msg) override
            {
                OutputDebugString(msg.formatted.str().c_str());
            }

            void flush()
            {

            }
        };
    }
}
#endif

/////////////////////////////////////////////////////////////////////////////////////
/// Initializes Gmm Logger
///
/// @return     true if initialized successfully. false otherwise
/////////////////////////////////////////////////////////////////////////////////////
bool GmmLib::Logger::GmmLogInit()
{
    std::string     LogFilePath;
    std::string     ProcPath;
    std::string     ProcName;
    int             Pid = 0;

    // Get logging method
#if _WIN32
    DWORD regkeyVal = 0;
    if (Utility::GmmUMDReadRegistryFullPath(GMM_LOG_REG_KEY_SUB_PATH, GMM_LOG_TO_FILE, &regkeyVal))
    {
        LogMethod = regkeyVal ? ToFile : ToOSLog;
    }

    if (Utility::GmmUMDReadRegistryFullPath(GMM_LOG_REG_KEY_SUB_PATH, GMM_LOG_LEVEL_REGKEY, &regkeyVal))
    {
        switch(static_cast<GmmLogLevel>(regkeyVal))
        {
            case Off:   LogLevel = spdlog::level::off;      break;
            case Trace: LogLevel = spdlog::level::trace;    break;
            case Info:  LogLevel = spdlog::level::info;     break;
            case Error: LogLevel = spdlog::level::err;      break;
        }
    }

#endif
    try
    {
        if(LogMethod == ToFile)
        {
            // Get process name
            #if _WIN32
                TCHAR ProcPathTChar[MAX_PATH];
                GetModuleFileName(NULL, ProcPathTChar, MAX_PATH);
                ProcPath = std::string(ProcPathTChar);

                size_t PosOfLastSlash = ProcPath.find_last_of("\\") + 1;
                size_t PosOfLastDot = ProcPath.find_last_of(".");

                if (PosOfLastDot <= PosOfLastSlash || PosOfLastDot >= ProcPath.length() || PosOfLastSlash >= ProcPath.length())
                {
                    ProcName = GMM_UNKNOWN_PROCESS;
                }
                else
                {
                    ProcName = ProcPath.substr(PosOfLastSlash, PosOfLastDot - PosOfLastSlash);
                }
            #else
                ProcPath = "Unknown_Proc_Path";
                ProcName = GMM_UNKNOWN_PROCESS;

                std::ifstream file;
                file.open("/proc/self/cmdline");
                if(file.is_open())
                {
                    // Get process name
                    getline(file, ProcPath);

                    size_t PosOfLastSlash = ProcPath.find_last_of("/") + 1;
                    if (PosOfLastSlash >= ProcPath.length())
                    {
                        ProcName = GMM_UNKNOWN_PROCESS;
                    }
                    else
                    {
                        // "length-1" to remove null character
                        ProcName = ProcPath.substr(PosOfLastSlash, ProcPath.length()-1);
                    }

                    file.close();
                }

            #endif

            // Get process ID
            #if _WIN32
                Pid = _getpid();
            #else
                Pid = getpid();
            #endif
            std::string PidStr = std::to_string(Pid);

            // TODO: Multiple GmmLib instance can be running in the same process. In that case, the file name will be
            // the same for two instances. Figure out a way to differentiate between the two instances.
            LogFilePath = std::string(GMM_LOG_FILENAME) + "_" + ProcName + "_" + PidStr;

            // Create logger
            SpdLogger = spdlog::rotating_logger_mt(GMM_LOGGER_NAME,
                LogFilePath,
                GMM_LOG_FILE_SIZE,
                GMM_ROTATE_FILE_NUMBER);

            // Log process path
            SpdLogger->set_pattern("Process path: %v");
            SpdLogger->info(ProcPath.c_str());
        }
        else
        {
            #if defined(_WIN32)
                // Log to debugger
                auto debugger_sink = std::make_shared<spdlog::sinks::Debugger>();
                SpdLogger = std::make_shared<spdlog::logger>(GMM_LOGGER_NAME, debugger_sink);
            #elif defined(__ANDROID__)
                // Log to logcat
                SpdLogger = spdlog::android_logger(GMM_LOGGER_NAME, GMM_LOG_TAG);
            #elif defined(__linux__)
                // Log to syslog
                SpdLogger = spdlog::syslog_logger(GMM_LOGGER_NAME, GMM_LOG_TAG, 1 /*Log Pid*/);
            #else
                __GMM_ASSERT(0);
                return false;
            #endif
        }
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        __GMM_ASSERT(0);
        return false;
    }

    // Set log level
    SpdLogger->set_level(LogLevel);
    // Set log pattern
    SpdLogger->set_pattern("[%T.%e] [Thread %t] [%l] %v");    // [Time] [Thread id] [Log Level] [Text to Log]

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
/// Gmm Logger constructor
/////////////////////////////////////////////////////////////////////////////////////
GmmLib::Logger::Logger() :
                LogMethod(ToOSLog),
                LogLevel(spdlog::level::err)
{
    if(!GmmLogInit())
    {
        spdlog::set_level(spdlog::level::off);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
/// Gmm Logger Destructor
/////////////////////////////////////////////////////////////////////////////////////
GmmLib::Logger::~Logger()
{
    if (SpdLogger)
    {
        SpdLogger->flush();
    }
}

#endif //#if GMM_LOG_AVAILABLE
