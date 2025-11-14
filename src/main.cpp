// Repository: Retrovue-air
// Component: FFmpeg CLI Wrapper
// Purpose: Thin wrapper around ffmpeg CLI for streaming MP4 to HTTP MPEG-TS
// Copyright (c) 2025 RetroVue

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

namespace
{

  volatile sig_atomic_t g_shutdown_requested = 0;
  pid_t g_child_pid = 0;

  void signal_handler(int sig)
  {
    (void)sig;
    g_shutdown_requested = 1;
  }

  std::string get_iso8601_timestamp()
  {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return oss.str();
  }

  void emit_json_event(const std::string &json_str)
  {
    std::cout << json_str << std::endl;
    std::cout.flush();
  }

  std::string json_escape(const std::string &str)
  {
    std::ostringstream oss;
    for (char c : str)
    {
      if (c == '"')
      {
        oss << "\\\"";
      }
      else if (c == '\\')
      {
        oss << "\\\\";
      }
      else if (c == '\n')
      {
        oss << "\\n";
      }
      else if (c == '\r')
      {
        oss << "\\r";
      }
      else if (c == '\t')
      {
        oss << "\\t";
      }
      else if (static_cast<unsigned char>(c) < 0x20)
      {
        oss << "\\u" << std::hex << std::setfill('0') << std::setw(4)
            << static_cast<int>(c);
      }
      else
      {
        oss << c;
      }
    }
    return oss.str();
  }

  struct Config
  {
    std::string input_path;
    int port = 0;
    std::string channel_id;
    std::string ffmpeg_path = "ffmpeg";
  };

  Config parse_args(int argc, char **argv)
  {
    Config config;

    for (int i = 1; i < argc; ++i)
    {
      std::string arg = argv[i];

      if (arg == "--input" && i + 1 < argc)
      {
        config.input_path = argv[++i];
      }
      else if (arg == "--port" && i + 1 < argc)
      {
        config.port = std::stoi(argv[++i]);
      }
      else if (arg == "--channel-id" && i + 1 < argc)
      {
        config.channel_id = argv[++i];
      }
      else if (arg == "--ffmpeg" && i + 1 < argc)
      {
        config.ffmpeg_path = argv[++i];
      }
      else if (arg == "--help" || arg == "-h")
      {
        std::cerr << "Usage: " << argv[0] << " --input <path> --port <int> --channel-id <string> [--ffmpeg <path>]\n";
        std::exit(1);
      }
    }

    if (config.input_path.empty() || config.port == 0 || config.channel_id.empty())
    {
      std::cerr << "Error: --input, --port, and --channel-id are required\n";
      std::exit(1);
    }

    return config;
  }

  std::vector<std::string> build_ffmpeg_args(const Config &config)
  {
    std::vector<std::string> args;

    args.push_back(config.ffmpeg_path);
    args.push_back("-v");
    args.push_back("warning");
    args.push_back("-hide_banner");
    args.push_back("-nostats");
    args.push_back("-re");
    args.push_back("-stream_loop");
    args.push_back("-1");
    args.push_back("-i");
    args.push_back(config.input_path);
    args.push_back("-c");
    args.push_back("copy");
    args.push_back("-f");
    args.push_back("mpegts");
    args.push_back("-muxdelay");
    args.push_back("0");
    args.push_back("-muxpreload");
    args.push_back("0");
    args.push_back("-flush_packets");
    args.push_back("1");
    args.push_back("-metadata");
    args.push_back("service_provider=Retrovue");
    args.push_back("-metadata");
    args.push_back("service_name=" + config.channel_id);
    args.push_back("-");

    return args;
  }

  int run_ffmpeg_wrapper(const Config &config)
  {
    // Setup signal handlers
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Build ffmpeg command
    std::vector<std::string> arg_strings = build_ffmpeg_args(config);
    std::vector<char *> exec_args;
    for (auto &s : arg_strings)
    {
      exec_args.push_back(const_cast<char *>(s.c_str()));
    }
    exec_args.push_back(nullptr);

    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
    {
      std::cerr << "Error: Failed to create pipe\n";
      return 1;
    }

    // Fork
    pid_t pid = fork();
    if (pid < 0)
    {
      std::cerr << "Error: Failed to fork\n";
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
      return 1;
    }

    if (pid == 0)
    {
      // Child: redirect stdout and stderr to pipes
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(stdout_pipe[1]);
      close(stderr_pipe[1]);

      // Execute ffmpeg
      execvp(exec_args[0], exec_args.data());

      // If we get here, execvp failed
      std::cerr << "Error: Failed to execute ffmpeg\n";
      std::exit(1);
    }

    // Parent
    g_child_pid = pid;
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Make pipes non-blocking
    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(stderr_pipe[0], F_GETFL, 0);
    fcntl(stderr_pipe[0], F_SETFL, flags | O_NONBLOCK);

    auto start_time = std::chrono::steady_clock::now();
    bool ready_emitted = false;
    std::string stderr_buffer;
    int status = 0;

    // Main loop
    while (true)
    {
      // Check if child exited
      pid_t wait_result = waitpid(pid, &status, WNOHANG);

      if (wait_result == pid)
      {
        // Child exited - read any remaining stdout and stderr before closing pipes
        // This ensures we capture all output even if FFmpeg exits quickly

        // Read any remaining stdout (MPEG-TS data) - just discard it
        char stdout_buf[4096];
        while (true)
        {
          ssize_t n = read(stdout_pipe[0], stdout_buf, sizeof(stdout_buf));
          if (n > 0)
          {
            // Discard stdout data
          }
          else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
          {
            break;
          }
          else
          {
            break;
          }
        }

        // Read any remaining stderr (error messages)
        while (true)
        {
          char buf[4096];
          ssize_t n = read(stderr_pipe[0], buf, sizeof(buf) - 1);
          if (n > 0)
          {
            buf[n] = '\0';
            stderr_buffer += buf;

            // Process complete lines
            size_t pos;
            while ((pos = stderr_buffer.find('\n')) != std::string::npos)
            {
              std::string line = stderr_buffer.substr(0, pos);
              stderr_buffer.erase(0, pos + 1);

              if (!line.empty())
              {
                // Emit ffmpeg_stderr event
                std::ostringstream event;
                event << "{\"evt\":\"ffmpeg_stderr\",\"line\":\"" << json_escape(line) << "\"}";
                emit_json_event(event.str());
              }
            }
          }
          else if (n == 0)
          {
            // EOF - no more data
            break;
          }
          else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
          {
            // Error or would block - stop reading
            break;
          }
          else
          {
            // Would block - no more data available
            break;
          }
        }

        // Emit child_exit event after reading all stderr
        std::ostringstream event;
        event << "{\"evt\":\"child_exit\",\"code\":";
        if (WIFEXITED(status))
        {
          event << WEXITSTATUS(status);
        }
        else
        {
          event << "null";
        }
        event << ",\"signal\":";
        if (WIFSIGNALED(status))
        {
          event << WTERMSIG(status);
        }
        else
        {
          event << "null";
        }
        event << "}";
        emit_json_event(event.str());

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        return (exit_code == 0) ? 0 : 1;
      }

      // Read stdout (MPEG-TS data) - read and discard for now
      // TODO: Stream this to HTTP server
      char stdout_buf[4096];
      ssize_t stdout_n = read(stdout_pipe[0], stdout_buf, sizeof(stdout_buf));
      if (stdout_n > 0)
      {
        // Data available - FFmpeg is streaming
        // For now, we just discard it. Later this should be streamed to HTTP clients.
        // This prevents FFmpeg from blocking when stdout buffer fills up.
      }
      else if (stdout_n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
      {
        // Error reading stdout
        break;
      }

      // Read stderr
      char buf[4096];
      ssize_t n = read(stderr_pipe[0], buf, sizeof(buf) - 1);
      if (n > 0)
      {
        buf[n] = '\0';
        stderr_buffer += buf;

        // Process complete lines
        size_t pos;
        while ((pos = stderr_buffer.find('\n')) != std::string::npos)
        {
          std::string line = stderr_buffer.substr(0, pos);
          stderr_buffer.erase(0, pos + 1);

          if (!line.empty())
          {
            // Emit ffmpeg_stderr event
            std::ostringstream event;
            event << "{\"evt\":\"ffmpeg_stderr\",\"line\":\"" << json_escape(line) << "\"}";
            emit_json_event(event.str());

            // No longer checking for "Listening on" since we're not using TCP server
          }
        }
      }
      else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
      {
        // Error reading
        break;
      }

      // Check for ready condition
      // Since FFmpeg writes to stdout, emit ready after a short delay when FFmpeg is running
      if (!ready_emitted)
      {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Emit ready after 500ms if FFmpeg is still running
        if (elapsed_milliseconds >= 500)
        {
          ready_emitted = true;
          std::ostringstream event;
          event << "{\"evt\":\"ready\",\"port\":" << config.port
                << ",\"channel_id\":\"" << json_escape(config.channel_id)
                << "\",\"timestamp\":\"" << get_iso8601_timestamp() << "\"}";
          emit_json_event(event.str());
        }
      }

      // Emit heartbeat every second
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
      static auto last_heartbeat = elapsed;

      if (elapsed - last_heartbeat >= std::chrono::seconds(1))
      {
        double uptime_s = elapsed.count() / 1000.0;
        std::ostringstream event;
        event << "{\"evt\":\"heartbeat\",\"uptime_s\":" << std::fixed << std::setprecision(1) << uptime_s
              << ",\"child_pid\":" << pid << "}";
        emit_json_event(event.str());
        last_heartbeat = elapsed;
      }

      // Check for shutdown request
      if (g_shutdown_requested)
      {
        // Send SIGTERM to child
        kill(pid, SIGTERM);

        // Wait up to 3 seconds
        for (int i = 0; i < 30; ++i)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          pid_t wait_result = waitpid(pid, &status, WNOHANG);
          if (wait_result == pid)
          {
            break;
          }
        }

        // If still alive, send SIGKILL
        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result != pid)
        {
          kill(pid, SIGKILL);
          waitpid(pid, &status, 0);
        }

        // Emit child_exit event
        std::ostringstream event;
        event << "{\"evt\":\"child_exit\",\"code\":";
        if (WIFEXITED(status))
        {
          event << WEXITSTATUS(status);
        }
        else
        {
          event << "null";
        }
        event << ",\"signal\":";
        if (WIFSIGNALED(status))
        {
          event << WTERMSIG(status);
        }
        else
        {
          event << "null";
        }
        event << "}";
        emit_json_event(event.str());

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        return 0;
      }

      // Small sleep to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    return 1;
  }

} // namespace

int main(int argc, char **argv)
{
  try
  {
    Config config = parse_args(argc, argv);
    return run_ffmpeg_wrapper(config);
  }
  catch (const std::exception &e)
  {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
