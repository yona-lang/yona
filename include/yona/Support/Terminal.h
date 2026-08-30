#ifndef YONA_SUPPORT_TERMINAL_H
#define YONA_SUPPORT_TERMINAL_H

#include <cstddef>
#include <cstdio>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VC_EXTRALEAN
#include <Windows.h>
#else
#include <sys/ioctl.h>
#endif

namespace yona::terminal {
#if defined(_WIN32)
namespace detail {
inline std::pair<std::size_t, std::size_t>
getTerminalSizeFromHandle(HANDLE StandardOutput) {
  CONSOLE_SCREEN_BUFFER_INFO ScreenBufferInfo{};
  if (StandardOutput == nullptr || StandardOutput == INVALID_HANDLE_VALUE ||
      !GetConsoleScreenBufferInfo(StandardOutput, &ScreenBufferInfo)) {
    return {0, 0};
  }
  return {static_cast<std::size_t>(ScreenBufferInfo.srWindow.Right -
                                   ScreenBufferInfo.srWindow.Left + 1),
          static_cast<std::size_t>(ScreenBufferInfo.srWindow.Bottom -
                                   ScreenBufferInfo.srWindow.Top + 1)};
}
} // namespace detail
#endif

/// Query the process standard-output terminal.
///
/// Returns `{0, 0}` when the standard output handle is not a terminal or the
/// platform query fails. The function owns no resources. Serialize calls with
/// code that redirects or reconfigures the same process handle.
inline std::pair<std::size_t, std::size_t> getTerminalSize() {
#if defined(_WIN32)
  return detail::getTerminalSizeFromHandle(GetStdHandle(STD_OUTPUT_HANDLE));
#else
  winsize WindowSize{};
  if (ioctl(fileno(stdout), TIOCGWINSZ, &WindowSize) != 0)
    return {0, 0};
  return {WindowSize.ws_col, WindowSize.ws_row};
#endif
}

/// Clear the process standard-output terminal.
///
/// The operation owns no resources and reports no I/O failure. Concurrent
/// output may interleave, so callers are responsible for stream-level
/// synchronization.
inline void clearScreen() {
#if defined(_WIN32)
  HANDLE StandardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO ScreenBufferInfo;
  DWORD CellCount;
  DWORD WrittenCount;
  COORD HomeCoordinates = {0, 0};

  if (StandardOutput == INVALID_HANDLE_VALUE)
    return;

  if (!GetConsoleScreenBufferInfo(StandardOutput, &ScreenBufferInfo))
    return;
  CellCount = ScreenBufferInfo.dwSize.X * ScreenBufferInfo.dwSize.Y;

  if (!FillConsoleOutputCharacter(StandardOutput, (TCHAR)' ', CellCount,
                                  HomeCoordinates, &WrittenCount))
    return;
  if (!FillConsoleOutputAttribute(StandardOutput, ScreenBufferInfo.wAttributes,
                                  CellCount, HomeCoordinates, &WrittenCount))
    return;
  SetConsoleCursorPosition(StandardOutput, HomeCoordinates);
#else
  // ANSI escape code to clear screen and move cursor to home
  std::fputs("\033[2J\033[H", stdout);
  std::fflush(stdout);
#endif
}
} // namespace yona::terminal

#endif /* YONA_SUPPORT_TERMINAL_H */
