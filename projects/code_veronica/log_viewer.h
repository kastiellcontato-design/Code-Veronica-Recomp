/* log_viewer.h -- the game's log, for the launcher's Advanced page: where it
 * is, a live viewer window, and copies of it to send along with a report.
 *
 * The log is this process's stdout and stderr: run-log.txt beside the exe when
 * it is started directly (main.cpp), or whatever file a launch script points
 * them at. It is found from the stdout handle itself, so both work.
 */
#pragma once
#include <windows.h>
#include <string>

/* The log file in use, or empty (stdout is a console or nothing). */
std::wstring log_file_path();
/* The log goes to a console window instead (started from a terminal). */
bool log_to_console();
/* Push what the game has printed into the file. */
void log_flush();
/* Size of the log in bytes (0 when there is none). */
unsigned long long log_size();
/* The whole log on the clipboard; its size in bytes, or 0 on failure. */
unsigned long long log_copy_to_clipboard(HWND owner);
/* A copy of the log at `dest`. */
bool log_save_copy(const std::wstring& dest);

struct LogViewerText {   /* the viewer's words, in the launcher's language */
    const wchar_t* title;
    const wchar_t* copy_all;
    const wchar_t* save;
    const wchar_t* folder;
    const wchar_t* follow;
    const wchar_t* cut;          /* the line shown where the start is left out */
    const wchar_t* none;         /* no log file */
    const wchar_t* console;      /* the log goes to a console window */
    const wchar_t* copied_fmt;   /* "%.1f MB" */
    const wchar_t* saved;
};
/* Open the viewer (or bring it forward). `save_as` asks for a path to save to. */
void log_viewer_open(HWND owner, const LogViewerText& text, std::wstring (*save_as)(HWND owner));
