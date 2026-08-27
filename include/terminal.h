#pragma once
#pragma once

#define ANSI_COLOR_RESET "\033[0m"

// Regular Colors
#define ANSI_COLOR_BLACK "\033[0;30m"
#define ANSI_COLOR_RED "\033[0;31m"
#define ANSI_COLOR_GREEN "\033[0;32m"
#define ANSI_COLOR_YELLOW "\033[0;33m"
#define ANSI_COLOR_BLUE "\033[0;34m"
#define ANSI_COLOR_MAGENTA "\033[0;35m"
#define ANSI_COLOR_CYAN "\033[0;36m"
#define ANSI_COLOR_WHITE "\033[0;37m"

// Bold Colors
#define ANSI_COLOR_BOLD_BLACK "\033[1;30m"
#define ANSI_COLOR_BOLD_RED "\033[1;31m"
#define ANSI_COLOR_BOLD_GREEN "\033[1;32m"
#define ANSI_COLOR_BOLD_YELLOW "\033[1;33m"
#define ANSI_COLOR_BOLD_BLUE "\033[1;34m"
#define ANSI_COLOR_BOLD_MAGENTA "\033[1;35m"
#define ANSI_COLOR_BOLD_CYAN "\033[1;36m"
#define ANSI_COLOR_BOLD_WHITE "\033[1;37m"

// Bright Colors (for compatibility)
#define ANSI_COLOR_BRIGHT_RED ANSI_COLOR_BOLD_RED

#define FULL_BLOCK '='

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
inline std::pair<size_t, size_t> get_terminal_size() {
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  return std::make_pair((size_t)(csbi.srWindow.Right - csbi.srWindow.Left + 1), (size_t)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1));
#else
  winsize w{};
  ioctl(fileno(stdout), TIOCGWINSZ, &w);
  return std::make_pair(w.ws_col, w.ws_row);
#endif
}

inline void clear_screen() {
#if defined(_WIN32)
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD cellCount, count;
    COORD homeCoords = { 0, 0 };

    if (hStdOut == INVALID_HANDLE_VALUE) return;

    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    if (!FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count)) return;
    if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count)) return;
    SetConsoleCursorPosition(hStdOut, homeCoords);
#else
    // ANSI escape code to clear screen and move cursor to home
    std::fputs("\033[2J\033[H", stdout);
    std::fflush(stdout);
#endif
}
} // namespace yona::terminal
